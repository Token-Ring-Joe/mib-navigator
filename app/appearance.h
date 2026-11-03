#ifndef APPEARANCE_H
#define APPEARANCE_H

#include <QObject>
#include <QPalette>
#include <QPointer>
#include <QIcon>
#include <QStringList>

class QMainWindow;
class QTextEdit;

class Appearance final : public QObject
{
    Q_OBJECT
public:
    enum class Mode { System, Light, Dark };
    explicit Appearance(QObject *parent = nullptr);
    void attach(QMainWindow *window);
    void setMode(Mode mode);
    Mode mode() const { return selected; }
    static QPalette paletteFor(bool dark);
    static QIcon icon(const QString &name);
    static QStringList iconNames();
protected:
    bool eventFilter(QObject *object, QEvent *event) override;
private:
    void apply();
    void refreshWindow();
    void recolorText(QTextEdit *view, int position = 0, int added = -1);
    Mode selected = Mode::System;
    QPalette nativePalette, appliedPalette, previousPalette;
    QPointer<QMainWindow> root;
    bool applying = false, formatting = false, pending = false;
};
#endif
