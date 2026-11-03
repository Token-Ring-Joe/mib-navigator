#ifndef APPEARANCECOLORS_H
#define APPEARANCECOLORS_H

#include <QPalette>
#include <QRegularExpression>
#include <QApplication>
#include <QLabel>
#include <QPainter>
#include <QEvent>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QStyleOptionViewItem>

namespace AppearanceColors {
enum Tone { None, Success, Warning, Error };
constexpr int ToneRole = Qt::UserRole + 1000;
constexpr int MutedRole = Qt::UserRole + 1001;
constexpr int ForegroundToneRole = Qt::UserRole + 1002;
inline bool dark(const QPalette &p) { return p.color(QPalette::Base).lightness() < 128; }
inline QColor foreground(const QPalette &p, Tone tone)
{
    if (tone == Success) return QColor(dark(p) ? "#91d9b2" : "#17643c");
    if (tone == Warning) return QColor(dark(p) ? "#ffd28a" : "#805000");
    if (tone == Error) return QColor(dark(p) ? "#ff9aa5" : "#aa2537");
    return p.color(QPalette::Text);
}
// Adapt only legacy font attributes; plain values and the producer's HTML stay intact.
inline QString legacyHtmlForPalette(const QString &html, const QPalette &palette)
{
    static const QRegularExpression colours(
        QStringLiteral("(<font\\b[^>]*?\\bcolor\\s*=\\s*)([\"']?)(red|blue|black|green|#009000|darkgreen)\\2(?=[\\s>])"),
        QRegularExpression::CaseInsensitiveOption);
    QString result;
    qsizetype offset = 0;
    auto matches = colours.globalMatch(html);
    while (matches.hasNext()) {
        const auto match = matches.next();
        const QString original = match.captured(3).toLower();
        const QColor ink = original == "red" ? foreground(palette, Error) :
            original == "blue" ? palette.color(QPalette::Link) :
            original == "black" ? palette.color(QPalette::Text) : foreground(palette, Success);
        result += html.mid(offset, match.capturedStart(3) - offset);
        result += ink.name();
        offset = match.capturedEnd(3);
    }
    if (!offset) return html;
    result += html.mid(offset);
    return result;
}
inline QColor background(const QPalette &p, Tone tone)
{
    const QColor base = p.color(QPalette::Base), ink = foreground(p, tone);
    return QColor((base.red()*5+ink.red())/6, (base.green()*5+ink.green())/6,
                  (base.blue()*5+ink.blue())/6);
}
// These labels have plain text and stable semantic properties. Read colours at
// paint time instead of letting a colour stylesheet cache resolved palettes.
class SemanticLabel final : public QLabel
{
public:
    enum Kind { EnvironmentBadge, ConnectionWarning, RequestStatus };
    SemanticLabel(const QString &text, Kind kind, QWidget *parent = nullptr)
        : QLabel(text, parent), kind(kind)
    {
        setTextFormat(Qt::PlainText);
        if (kind != ConnectionWarning) { QFont f = font(); f.setBold(true); setFont(f); }
    }
    QSize sizeHint() const override
    { return QLabel::sizeHint() + (kind == EnvironmentBadge ? QSize(14,4) : QSize(0,0)); }
    QSize minimumSizeHint() const override
    { return QLabel::minimumSizeHint() + (kind == EnvironmentBadge ? QSize(14,4) : QSize(0,0)); }
protected:
    bool event(QEvent *event) override
    {
        if (event->type() == QEvent::DynamicPropertyChange) update();
        return QLabel::event(event);
    }
    void paintEvent(QPaintEvent *) override
    {
        const QPalette current = qApp->palette();
        Tone tone = None;
        if (kind == ConnectionWarning) tone = Warning;
        else if (kind == RequestStatus && property("failed").toBool()) tone = Error;
        else if (kind == EnvironmentBadge) {
            const QString status = property("status").toString();
            tone = status == "ready" ? Success : status == "error" ? Error :
                status == "warning" || status == "building" ? Warning : None;
        }
        QPainter painter(this);
        QRect area = contentsRect();
        if (kind == EnvironmentBadge) {
            painter.setPen(Qt::NoPen); painter.setBrush(background(current,tone));
            painter.setRenderHint(QPainter::Antialiasing);
            painter.drawRoundedRect(rect().adjusted(0,0,-1,-1),3,3);
            area.adjust(7,2,-7,-2);
        }
        QPalette textPalette = current;
        textPalette.setColor(QPalette::WindowText,foreground(current,tone));
        const int flags = int(alignment()) | (wordWrap() ? Qt::TextWordWrap : Qt::TextSingleLine);
        style()->drawItemText(&painter,area,flags,textPalette,isEnabled(),text(),QPalette::WindowText);
    }
private:
    Kind kind;
};

// Semantic roles remain stable; painting reads the current palette, including rows
// already displayed when the theme changes. No model rebuild or cached colour state.
class ItemDelegate final : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const override
    {
        QStyledItemDelegate::initStyleOption(option, index);
        const auto tone = static_cast<Tone>(index.data(ToneRole).toInt());
        if (tone != None) option->backgroundBrush = background(option->palette, tone);
        const auto textTone = static_cast<Tone>(index.data(ForegroundToneRole).toInt());
        if (textTone != None) option->palette.setColor(QPalette::Text, foreground(option->palette, textTone));
        if (index.data(MutedRole).toBool())
            option->palette.setColor(QPalette::Text, option->palette.color(QPalette::Disabled, QPalette::Text));
    }
};
}
#endif
