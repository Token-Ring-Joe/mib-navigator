#ifndef TRAPVIEW_H
#define TRAPVIEW_H

#include <QObject>
#include <QPointer>
#include <functional>

class QLabel;
class QTreeWidget;
class QTextEdit;
class QPushButton;
class QAction;
class TrapService;
struct TrapRecord;

// Presentation only. The service owns typed history; the active immutable MIB
// environment owns interpretation. Selection is a stable record ID, never a row.
class TrapView final : public QObject
{
    Q_OBJECT
public:
    TrapView(TrapService *service, QTreeWidget *log, QTreeWidget *content,
             QTextEdit *info, std::function<bool()> expandBindings, QObject *parent = nullptr);
    void refreshEnvironment();
private:
    const TrapRecord *selectedRecord() const;
    void appendRecord();
    void showSelected();
    void clearView();
    QPointer<TrapService> service;
    QPointer<QTreeWidget> log;
    QPointer<QTreeWidget> content;
    QPointer<QTextEdit> info;
    QPointer<QLabel> historyHint;
    QPointer<QPushButton> clearButton;
    QPointer<QAction> copySource;
    std::function<bool()> expandBindings;
};
#endif
