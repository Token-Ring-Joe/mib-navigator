#ifndef MAINWINDOWLAYOUT_H
#define MAINWINDOWLAYOUT_H

#include <QByteArray>
#include <QList>
#include <QRect>

class QDockWidget;
class QMainWindow;

void RestoreMainWindowStateWithRequiredDevicesDock(
    QMainWindow *window, QDockWidget *devicesDock, const QByteArray &state);
QRect SafeRestoredWindowGeometry(const QRect &requested,
                                 const QList<QRect> &availableScreens,
                                 const QSize &fallbackSize = QSize(1100, 760));
void EnsureAccessibleWindowGeometry(QMainWindow *window);

#endif
