#include "mainwindowlayout.h"

#include <QDockWidget>
#include <QMainWindow>
#include <QGuiApplication>
#include <QScreen>

void RestoreMainWindowStateWithRequiredDevicesDock(
    QMainWindow *window, QDockWidget *devicesDock, const QByteArray &state)
{
    if (!state.isEmpty())
        window->restoreState(state);

    // The Devices pane is primary navigation. Older state, or state written by
    // an earlier preview build, may restore it hidden or floating.
    devicesDock->setFloating(false);
    window->addDockWidget(Qt::LeftDockWidgetArea, devicesDock);
    devicesDock->show();
}

QRect SafeRestoredWindowGeometry(const QRect &requested,
                                 const QList<QRect> &availableScreens,
                                 const QSize &fallbackSize)
{
    if (availableScreens.isEmpty()) return requested;
    QSize size = requested.size();
    if (size.width() < 320 || size.height() < 240) size = fallbackSize;
    int bestArea = -1;
    QRect target = availableScreens.first();
    for (const QRect &screen : availableScreens) {
        const QRect intersection = QRect(requested.topLeft(), size).intersected(screen);
        const int area = intersection.width() * intersection.height();
        if (area > bestArea) { bestArea = area; target = screen; }
    }
    size.setWidth(qMin(size.width(), target.width()));
    size.setHeight(qMin(size.height(), target.height()));
    QRect repaired(requested.topLeft(), size);
    const QRect visible = repaired.intersected(target);
    const bool usefulWidth = visible.width() >= qMin((repaired.width() * 2) / 3, 960);
    const bool titleVisible = repaired.top() >= target.top() - 8 &&
                              repaired.top() <= target.bottom() - 32;
    if (usefulWidth && titleVisible && repaired.bottom() <= target.bottom()) return repaired;
    repaired.moveLeft(qBound(target.left(), repaired.left(), target.right() - repaired.width() + 1));
    repaired.moveTop(qBound(target.top(), repaired.top(), target.bottom() - repaired.height() + 1));
    return repaired;
}

void EnsureAccessibleWindowGeometry(QMainWindow *window)
{
    QList<QRect> screens;
    for (QScreen *screen : QGuiApplication::screens()) screens.append(screen->availableGeometry());
    window->setGeometry(SafeRestoredWindowGeometry(window->geometry(), screens));
}
