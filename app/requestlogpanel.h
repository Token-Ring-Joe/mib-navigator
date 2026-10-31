#ifndef REQUESTLOGPANEL_H
#define REQUESTLOGPANEL_H

#include <QWidget>

class QPushButton;
class QTableWidget;
class RequestLog;

// The Log tab: one row per request, newest last, with how it ended.
class RequestLogPanel : public QWidget
{
    Q_OBJECT
public:
    explicit RequestLogPanel(RequestLog *log, QWidget *parent = nullptr);

private:
    int rowOf(quint64 id) const;
    void fill(int row, quint64 id);

    RequestLog *log;
    QTableWidget *table;
    QPushButton *copyButton;
    QPushButton *clearButton;
};

#endif
