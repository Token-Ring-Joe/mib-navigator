#include "appearance.h"
#include "appearancecolors.h"
#include "mibhighlighter.h"

#include <QApplication>
#include <QAction>
#include <QActionGroup>
#include <QAbstractButton>
#include <QEvent>
#include <QFile>
#include <QIconEngine>
#include <QLabel>
#include <QMainWindow>
#include <QMenu>
#include <QPainter>
#include <QSettings>
#include <QStyleFactory>
#include <QStyle>
#include <memory>
#include <QStyleHints>
#include <QSvgRenderer>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextEdit>
#include <QTextFragment>
#include <QTimer>
#include <QToolButton>
#include <QPixmapCache>

namespace {
constexpr int originalInk = QTextFormat::UserProperty + 300;
QString modeName(Appearance::Mode mode)
{
    return mode == Appearance::Mode::Dark ? QStringLiteral("dark") :
           mode == Appearance::Mode::Light ? QStringLiteral("light") : QStringLiteral("system");
}
class ActionIcon final : public QIconEngine
{
public:
    explicit ActionIcon(QString name) : glyph(std::move(name)) {}
    QIconEngine *clone() const override { return new ActionIcon(glyph); }
    QString key() const override { return QStringLiteral("MibNavigatorLineIcon"); }
    void paint(QPainter *painter, const QRect &rect, QIcon::Mode mode, QIcon::State) override
    {
        QFile file(QStringLiteral(":/appearance/") + glyph);
        if (!file.open(QIODevice::ReadOnly)) return;
        QByteArray svg = file.readAll();
        const QColor ink = mode == QIcon::Selected ? qApp->palette().color(QPalette::HighlightedText)
            : qApp->palette().color(mode == QIcon::Disabled ? QPalette::Disabled : QPalette::Active,
                                    QPalette::ButtonText);
        svg.replace("currentColor", ink.name().toLatin1());
        QSvgRenderer renderer(svg);
        renderer.render(painter, rect);
    }
    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override
    { return scaledPixmap(size, mode, state, 1); }
    QPixmap scaledPixmap(const QSize &size, QIcon::Mode mode, QIcon::State state, qreal scale) override
    {
        QPixmap result(size * scale); result.setDevicePixelRatio(scale); result.fill(Qt::transparent);
        QPainter painter(&result); paint(&painter, QRect(QPoint(), size), mode, state); return result;
    }
private:
    QString glyph;
};
bool legacyInk(QColor color)
{
    return color == QColor(Qt::black) || color == QColor(Qt::red) || color == QColor(Qt::blue) ||
           color == QColor(Qt::green) || color == QColor("#009000") || color == QColor(Qt::darkGreen);
}
QColor readableInk(const QPalette &palette, QColor original)
{
    using namespace AppearanceColors;
    if (original == QColor(Qt::red)) return foreground(palette, Error);
    if (original == QColor(Qt::blue)) return palette.color(QPalette::Link);
    if (original == QColor(Qt::black)) return palette.color(QPalette::Text);
    return foreground(palette, Success);
}
}

QPalette Appearance::paletteFor(bool dark)
{
    const std::unique_ptr<QStyle> fusion(QStyleFactory::create(QStringLiteral("Fusion")));
    QPalette p = fusion->standardPalette();
    p.setColor(QPalette::Window, QColor(dark ? "#252b34" : "#f6f7f9"));
    p.setColor(QPalette::Base, QColor(dark ? "#1b2028" : "#ffffff"));
    p.setColor(QPalette::AlternateBase, QColor(dark ? "#242c37" : "#edf1f6"));
    p.setColor(QPalette::Button, QColor(dark ? "#303846" : "#eef1f5"));
    for (auto role : {QPalette::Text, QPalette::WindowText, QPalette::ButtonText}) {
        p.setColor(role, QColor(dark ? "#e5e7eb" : "#202124"));
        p.setColor(QPalette::Disabled, role, QColor(dark ? "#929caa" : "#687180"));
    }
    p.setColor(QPalette::Highlight, QColor(dark ? "#36669c" : "#2265ae"));
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::Link, QColor(dark ? "#87bfff" : "#0057b8"));
    p.setColor(QPalette::LinkVisited, QColor(dark ? "#c6adff" : "#64429c"));
    p.setColor(QPalette::ToolTipBase, p.color(QPalette::Base));
    p.setColor(QPalette::ToolTipText, p.color(QPalette::Text));
    p.setColor(QPalette::PlaceholderText, p.color(QPalette::Disabled, QPalette::Text));
    p.setColor(QPalette::Light, QColor(dark ? "#566172" : "#ffffff"));
    p.setColor(QPalette::Midlight, QColor(dark ? "#3d4756" : "#e1e5eb"));
    p.setColor(QPalette::Mid, QColor(dark ? "#4a5565" : "#b9c2cf"));
    p.setColor(QPalette::Dark, QColor(dark ? "#11161d" : "#8c98a9"));
    p.setColor(QPalette::Shadow, QColor(dark ? "#080b10" : "#536174"));
    p.setColor(QPalette::Accent, p.color(QPalette::Highlight));
    return p;
}

Appearance::Appearance(QObject *parent) : QObject(parent), nativePalette(qApp->palette())
{
    qApp->setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    qApp->installEventFilter(this);
    const QString saved = QSettings().value("ui/theme", QStringLiteral("system")).toString();
    selected = saved == "dark" ? Mode::Dark : saved == "light" ? Mode::Light : Mode::System;
    connect(qApp->styleHints(), &QStyleHints::colorSchemeChanged, this, [this]() { apply(); });
    qApp->styleHints()->setColorScheme(selected == Mode::System ? Qt::ColorScheme::Unknown :
        selected == Mode::Dark ? Qt::ColorScheme::Dark : Qt::ColorScheme::Light);
    apply();
}

void Appearance::setMode(Mode mode)
{
    selected = mode;
    QSettings().setValue("ui/theme", modeName(mode));
    qApp->styleHints()->setColorScheme(mode == Mode::System ? Qt::ColorScheme::Unknown :
        mode == Mode::Dark ? Qt::ColorScheme::Dark : Qt::ColorScheme::Light);
    apply();
}

bool Appearance::eventFilter(QObject *object, QEvent *event)
{
    if (event->type() == QEvent::ApplicationPaletteChange && !applying &&
        qApp->palette() != appliedPalette) {
        nativePalette = qApp->palette();
        if (!pending) {
            pending = true;
            QTimer::singleShot(0, this, [this]() { pending = false; apply(); });
        }
    }
    return QObject::eventFilter(object, event);
}

void Appearance::apply()
{
    if (applying) return;
    applying = true;
    const auto scheme = qApp->styleHints()->colorScheme();
    const bool dark = selected == Mode::Dark || (selected == Mode::System &&
        (scheme == Qt::ColorScheme::Dark || (scheme == Qt::ColorScheme::Unknown &&
                                           AppearanceColors::dark(nativePalette))));
    previousPalette = appliedPalette;
    appliedPalette = paletteFor(dark);
    qApp->setPalette(appliedPalette);
    QPixmapCache::clear();
    refreshWindow();
    applying = false;
}

QStringList Appearance::iconNames()
{
    return {"get", "get-next", "get-bulk", "walk", "table", "set", "stop", "new", "open", "save",
            "save-as", "search", "replace", "verify", "expand", "collapse", "exit"};
}
QIcon Appearance::icon(const QString &name) { return QIcon(new ActionIcon(name)); }

void Appearance::attach(QMainWindow *window)
{
    root = window;
    if (auto *view = window->findChild<QMenu *>("WorkbenchViewMenu")) {
        auto *menu = view->addMenu(tr("Appearance")); menu->setObjectName("AppearanceMenu");
        auto *group = new QActionGroup(menu); group->setExclusive(true);
        const QList<QPair<Mode, QString>> choices{{Mode::System, tr("Follow System")},
            {Mode::Light, tr("Light")}, {Mode::Dark, tr("Dark")}};
        for (const auto &choice : choices) {
            const Mode mode = choice.first;
            auto *action = menu->addAction(choice.second); action->setObjectName("Appearance" + modeName(mode));
            action->setCheckable(true); action->setData(modeName(mode)); group->addAction(action);
            connect(action, &QAction::triggered, this, [this, mode]() { setMode(mode); });
        }
    }
    for (auto *text : window->findChildren<QTextEdit *>()) {
        if (!text->isReadOnly() || text->property("appearance.html-adapted").toBool()) continue;
        const QPointer<QTextEdit> guarded(text);
        connect(text->document(), &QTextDocument::contentsChange, this,
            [this, guarded](int position, int, int added) {
                if (guarded && !formatting && added > 0) recolorText(guarded, position, added);
            });
    }
    refreshWindow();
}

void Appearance::refreshWindow()
{
    if (!root) return;
    const auto p = qApp->palette();
    // Stylesheets retain resolved palettes on both controls and their containers.
    // Unpolish first, then replace those palette snapshots before restoring styles.
    // Semantic item colours are painted from roles and are not changed here.
    const QString sheet = root->styleSheet();
    root->setStyleSheet({});
    root->setPalette(p);
    for (auto *widget : root->findChildren<QWidget *>()) widget->setPalette(p);
    root->setStyleSheet(sheet);
    const QHash<QString, QString> actionNames{{"fileNewAction","new"}, {"fileOpenAction","open"},
        {"fileSaveAction","save"}, {"fileSaveAsAction","save-as"}, {"fileExitAction","exit"},
        {"actionVerifyMIB","verify"}, {"actionFind","search"}, {"actionFindNext","get-next"},
        {"actionReplace","replace"}, {"ViewResults","table"}};
    for (auto *action : root->findChildren<QAction *>()) {
        QString glyph = action->property("appearanceIcon").toString();
        if (glyph.isEmpty()) glyph = actionNames.value(action->objectName());
        if (!glyph.isEmpty()) action->setIcon(icon(glyph));
        if (action->objectName().startsWith("Appearance") && action->isCheckable())
            action->setChecked(action->data().toString() == modeName(selected));
    }
    for (auto *button : root->findChildren<QAbstractButton *>()) {
        const QString glyph = button->property("appearanceIcon").toString();
        if (!glyph.isEmpty()) button->setIcon(icon(glyph));
    }
    for (auto *view : root->findChildren<QTextEdit *>()) if (view->isReadOnly()) recolorText(view);
    for (auto *highlighter : root->findChildren<MibHighlighter *>()) highlighter->applyPalette(p);
    root->update();
}

void Appearance::recolorText(QTextEdit *view, int position, int added)
{
    if (formatting) return;
    formatting = true;
    auto *document = view->document();
    const bool modified = document->isModified();
    const int end = added < 0 ? document->characterCount() : position + added;
    struct Run { int start, length; QColor original; };
    QList<Run> runs;
    for (QTextBlock block = document->findBlock(position); block.isValid() && block.position() < end; block = block.next()) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const auto fragment = it.fragment();
            if (!fragment.isValid() || fragment.position() >= end || fragment.position()+fragment.length() <= position) continue;
            const auto format = fragment.charFormat();
            const QColor current = format.foreground().color();
            QColor original = legacyInk(current) && format.foreground().style() != Qt::NoBrush
                ? current : format.property(originalInk).value<QColor>();
            if (!original.isValid() && format.foreground().style() != Qt::NoBrush) {
                if (current == AppearanceColors::foreground(previousPalette, AppearanceColors::Error)) original = Qt::red;
                else if (current == previousPalette.color(QPalette::Link)) original = Qt::blue;
                else if (current == previousPalette.color(QPalette::Text)) original = Qt::black;
                else if (current == AppearanceColors::foreground(previousPalette, AppearanceColors::Success)) original = Qt::green;
            }
            if (original.isValid() && legacyInk(original))
                runs.append({fragment.position(), fragment.length(), original});
        }
    }
    QTextCursor edit(document);
    edit.beginEditBlock();
    for (const auto &run : runs) {
        QTextCursor cursor(document); cursor.setPosition(run.start);
        cursor.setPosition(run.start+run.length, QTextCursor::KeepAnchor);
        QTextCharFormat format; format.setForeground(readableInk(qApp->palette(), run.original));
        format.setProperty(originalInk, run.original); cursor.mergeCharFormat(format);
    }
    edit.endEditBlock();
    document->setModified(modified);
    formatting = false;
}
