#ifndef THEMEDTEXTEDIT_H
#define THEMEDTEXTEDIT_H
#include "appearancecolors.h"
#include <QTextEdit>

// Map legacy presentation HTML before insertion. Editing a live document from
// contentsChange forces repeated layout and makes large Walks unresponsive.
class ThemedTextEdit final : public QTextEdit
{
    Q_OBJECT
public:
    explicit ThemedTextEdit(QWidget *parent = nullptr) : QTextEdit(parent)
    { setProperty("appearance.html-adapted", true); }
public slots:
    void append(const QString &html)
    { QTextEdit::append(AppearanceColors::legacyHtmlForPalette(html, qApp->palette())); }
    void setHtml(const QString &html)
    { QTextEdit::setHtml(AppearanceColors::legacyHtmlForPalette(html, qApp->palette())); }
};
#endif
