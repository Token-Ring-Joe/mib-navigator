#ifndef WATCHPANEL_H
#define WATCHPANEL_H

#include <QWidget>
#include <functional>

#include "watchservice.h"

class QPushButton;
class QSpinBox;
class QTableWidget;

// The Watch tab: one row per watched object with its latest value, change since the previous
// poll, rate per second, and the device and credential the row was created with.
class WatchPanel : public QWidget
{
    Q_OBJECT
public:
    // credentialLabel turns a saved credential reference into the name to show.
    WatchPanel(WatchService *service, std::function<QString(const QString &)> credentialLabel,
               QWidget *parent = nullptr);
    int intervalSeconds() const;   // the interval new watches start with

private:
    int rowOf(quint64 id) const;
    void fill(int row, const WatchRow &data);
    void selectionChanged();
    void editInterval(quint64 id);
    quint64 selectedId() const;

    WatchService *service;
    std::function<QString(const QString &)> credentialLabel;
    QTableWidget *table;
    QSpinBox *interval;
    QPushButton *pauseButton;
    QPushButton *editIntervalButton;
    QPushButton *removeButton;
    QPushButton *clearButton;
};

#endif
