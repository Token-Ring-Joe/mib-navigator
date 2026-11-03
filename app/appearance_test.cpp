#include "appearance.h"
#include "appearancecolors.h"
#include "themedtextedit.h"
#include "mibhighlighter.h"
#include <QApplication>
#include <QAction>
#include <QLabel>
#include <QMainWindow>
#include <QMenuBar>
#include <QMenu>
#include <QSettings>
#include <QStyle>
#include <QStyleHints>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTextEdit>
#include <QTextCursor>
#include <QToolButton>
#include <QVBoxLayout>
#include <cmath>
#include <cstdio>
namespace {
int failures = 0;
void check(bool ok, const char *what) { if (!ok) { ++failures; std::fprintf(stderr,"FAIL: %s\n",what); } }
double luminance(QColor c)
{
    const auto channel=[](double v) { return v<=0.04045 ? v/12.92 : std::pow((v+0.055)/1.055,2.4); };
    return .2126*channel(c.redF())+.7152*channel(c.greenF())+.0722*channel(c.blueF());
}
double contrast(QColor a,QColor b)
{ const auto x=luminance(a),y=luminance(b);return (qMax(x,y)+.05)/(qMin(x,y)+.05); }
QColor inkAt(QTextEdit *text,int position)
{ QTextCursor c(text->document());c.setPosition(position);c.movePosition(QTextCursor::Right,QTextCursor::KeepAnchor);return c.charFormat().foreground().color(); }
bool pixels(const QPixmap &p)
{ const auto i=p.toImage();for(int y=0;y<i.height();++y)for(int x=0;x<i.width();++x)if(qAlpha(i.pixel(x,y)))return true;return false; }
}
int main(int argc,char **argv)
{
    QApplication app(argc,argv);
    QTemporaryDir tmp;
    QCoreApplication::setOrganizationName("MibNavigatorAppearanceTest");
    QCoreApplication::setApplicationName("Appearance");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,tmp.path());
    QSettings().setValue("ui/theme","future-theme");
    for(bool dark:{false,true}) {
        const auto p=Appearance::paletteFor(dark);
        check(contrast(p.color(QPalette::Text),p.color(QPalette::Base))>=4.5,"body text contrast");
        check(contrast(p.color(QPalette::ButtonText),p.color(QPalette::Button))>=4.5,"button text contrast");
        check(contrast(p.color(QPalette::HighlightedText),p.color(QPalette::Highlight))>=4.5,"selected text contrast");
        check(contrast(p.color(QPalette::Link),p.color(QPalette::Base))>=4.5,"link contrast");
        for(auto tone:{AppearanceColors::Success,AppearanceColors::Warning,AppearanceColors::Error})
            check(contrast(AppearanceColors::foreground(p,tone),AppearanceColors::background(p,tone))>=4.5,"semantic status contrast");
    }
    {
        Appearance appearance;
        check(appearance.mode()==Appearance::Mode::System && QSettings().value("ui/theme")=="future-theme",
              "unknown persisted theme falls back safely without rewriting settings");
        check(app.style()->objectName().compare("fusion",Qt::CaseInsensitive)==0,"Fusion is the application style");
        QMainWindow window;
        window.setStyleSheet("QToolButton { padding: 3px 7px; } QHeaderView::section { padding: 4px; }");
        auto *view=window.menuBar()->addMenu("View");view->setObjectName("WorkbenchViewMenu");
        auto *central=new QWidget(&window);window.setCentralWidget(central);
        auto *layout=new QVBoxLayout(central);
        auto *badge=new AppearanceColors::SemanticLabel("Environment: Error",AppearanceColors::SemanticLabel::EnvironmentBadge,central);badge->setObjectName("CurrentEnvironmentStatus");badge->setProperty("status","error");layout->addWidget(badge);
        auto *hint=new AppearanceColors::SemanticLabel("Select a credential",AppearanceColors::SemanticLabel::ConnectionWarning,central);hint->setObjectName("ConnectionHint");layout->addWidget(hint);
        auto *button=new QToolButton(central);button->setProperty("appearanceIcon","get");layout->addWidget(button);
        auto *action=new QAction(&window);action->setObjectName("fileNewAction");
        auto *output=new QTextEdit(central);output->setReadOnly(true);layout->addWidget(output);
        output->setHtml("<b><font color=red>red</font></b> <font color=blue>blue</font> <font color=black>black</font>");
        auto *stream = new ThemedTextEdit(central); stream->setReadOnly(true); layout->addWidget(stream);
        const QString legacy = "<font color='red'>error</font> <font color=blue>link</font> color=red";
        stream->append(legacy);
        check(legacy.contains("color='red'"), "HTML adaptation leaves producer data unchanged");
        const QString original=output->toPlainText();
        output->document()->setModified(false);
        QTextCursor selection(output->document());selection.setPosition(0);selection.setPosition(3,QTextCursor::KeepAnchor);output->setTextCursor(selection);
        auto *editor=new QTextEdit(central);layout->addWidget(editor);
        editor->setPlainText("MODULE-IDENTITY -- comment\nDESCRIPTION \"hello\"");
        editor->moveCursor(QTextCursor::End);editor->insertPlainText("\n-- unsaved edit");
        MibHighlighter syntax(editor->document());
        const QString source=editor->toPlainText();const bool undo=editor->document()->isUndoAvailable();
        auto *table=new QTableWidget(1,1,central);layout->addWidget(table);
        auto *item=new QTableWidgetItem("unchanged value");item->setData(AppearanceColors::ToneRole,AppearanceColors::Warning);table->setItem(0,0,item);
        auto *delegate=new AppearanceColors::ItemDelegate(table);table->setItemDelegate(delegate);
        appearance.attach(&window);window.resize(900,800);window.show();app.processEvents();
        QImage lightIcon,darkIcon;QColor previous;
        for(auto mode:{Appearance::Mode::Light,Appearance::Mode::Dark,Appearance::Mode::Light}) {
            appearance.setMode(mode);app.processEvents();
            const bool dark=mode==Appearance::Mode::Dark;
            check(inkAt(stream,0)==AppearanceColors::foreground(app.palette(),AppearanceColors::Error),
                  "pre-adapted output follows subsequent theme changes");
            stream->clear();
            check(QMetaObject::invokeMethod(stream,"append",Q_ARG(QString,legacy)), "stream append remains a Qt slot");
            check(inkAt(stream,0)==AppearanceColors::foreground(app.palette(),AppearanceColors::Error) &&
                  inkAt(stream,6)==app.palette().color(QPalette::Link) && stream->toPlainText()=="error link color=red",
                  "stream adapts quoted and unquoted font colours without changing literal data");
            check(AppearanceColors::dark(app.palette())==dark,"explicit theme applies immediately");
            check((central->palette().color(QPalette::Window).lightness()<128)==dark,
                  "existing workbench container receives the chosen palette");
            check(AppearanceColors::dark(output->palette())==dark && AppearanceColors::dark(editor->palette())==dark &&
                  AppearanceColors::dark(table->palette())==dark && AppearanceColors::dark(button->palette())==dark,
                  "existing styled controls receive the chosen palette");
            check(output->toPlainText()==original && editor->toPlainText()==source && editor->document()->isUndoAvailable()==undo,
                  "theme changes preserve output, source and editor undo state");
            check(output->textCursor().selectedText()=="red" && !output->document()->isModified(),
                  "theme formatting preserves read-only selection and modified state");
            check(inkAt(output,0)==AppearanceColors::foreground(app.palette(),AppearanceColors::Error),"existing red text follows theme");
            QTextCursor bold(output->document());bold.setPosition(0);bold.movePosition(QTextCursor::Right,QTextCursor::KeepAnchor);
            check(bold.charFormat().fontWeight()==QFont::Bold,"rich text retains bold formatting");
            check(AppearanceColors::dark(hint->palette())==dark && AppearanceColors::dark(badge->palette())==dark,
                  "semantic labels discard their previous stylesheet palette snapshots");
            const auto badgeImage=badge->grab().toImage();bool hasBadgeColour=false;
            for(int y=0;y<badgeImage.height();++y)for(int x=0;x<badgeImage.width();++x)
                hasBadgeColour=hasBadgeColour || badgeImage.pixelColor(x,y)==AppearanceColors::background(app.palette(),AppearanceColors::Error);
            check(hasBadgeColour,"badge paints its current semantic theme colour");
            QStyleOptionViewItem opt;opt.palette=app.palette();delegate->initStyleOption(&opt,table->model()->index(0,0));
            check(opt.backgroundBrush.color()==AppearanceColors::background(app.palette(),AppearanceColors::Warning),"existing table warnings repaint from semantic roles");
            if(previous.isValid())check(opt.backgroundBrush.color()!=previous,"palette switches change existing warning tint");
            previous=opt.backgroundBrush.color();
            item->setData(AppearanceColors::MutedRole,true);
            QStyleOptionViewItem muted;muted.palette=app.palette();delegate->initStyleOption(&muted,table->model()->index(0,0));
            check(muted.palette.color(QPalette::Text)==app.palette().color(QPalette::Disabled,QPalette::Text),
                  "existing paused/excluded rows use the current muted text colour");
            item->setData(AppearanceColors::MutedRole,false);
            check(table->item(0,0)==item && item->text()=="unchanged value","theme does not rebuild result data");
            for(const auto &name:Appearance::iconNames()) {
                const auto icon=Appearance::icon(name);
                check(pixels(icon.pixmap(QSize(24,24))),"SVG action icon renders visible pixels");
                const auto hi=icon.pixmap(QSize(24,24),2.0);
                check(hi.width()>=48 && hi.devicePixelRatio()>=2 && pixels(hi),"SVG action icon renders at high DPI");
                check(pixels(icon.pixmap(QSize(24,24),QIcon::Disabled)),"disabled SVG action icon renders");
                check(pixels(icon.pixmap(QSize(24,24),QIcon::Selected)),"selected SVG action icon renders");
            }
            if(dark)darkIcon=button->icon().pixmap(24).toImage();else lightIcon=button->icon().pixmap(24).toImage();
            check(!action->icon().isNull(),"Editor/menu action receives the line icon set");
        }
        check(lightIcon!=darkIcon,"action icons change colour with the palette");
        editor->undo();check(editor->toPlainText()!=source,"unsaved Editor change can still be undone");
        editor->redo();check(editor->toPlainText()==source,"Editor redo survives theme switching");
        appearance.setMode(Appearance::Mode::Dark);
        output->append("<font color=red>new error</font>");app.processEvents();
        check(inkAt(output,output->toPlainText().indexOf("new error"))==AppearanceColors::foreground(app.palette(),AppearanceColors::Error),
              "new rich-text output uses the active theme");
        auto *light=window.findChild<QAction *>("Appearancelight");if(light)light->trigger();
        check(light && light->isChecked() && appearance.mode()==Appearance::Mode::Light && QSettings().value("ui/theme")=="light",
              "View Appearance selection applies and persists the theme");
        appearance.setMode(Appearance::Mode::System);app.processEvents();
        check(QSettings().value("ui/theme")=="system","Follow System is persisted independently of the effective palette");
        if(app.styleHints()->colorScheme()==Qt::ColorScheme::Unknown) {
            app.setPalette(Appearance::paletteFor(true));QEvent change(QEvent::ApplicationPaletteChange);app.sendEvent(&app,&change);app.processEvents();
            check(AppearanceColors::dark(app.palette()),"Follow System reacts to native palette changes when the scheme is unavailable");
        }
        appearance.setMode(Appearance::Mode::Dark);
    }
    { Appearance restarted;check(restarted.mode()==Appearance::Mode::Dark && AppearanceColors::dark(app.palette()),"saved theme survives restart"); }
    return failures?1:0;
}
