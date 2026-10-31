#ifndef WORKBENCHSHELL_H
#define WORKBENCHSHELL_H

#include <QObject>
#include <QList>

class QAction;
class QDockWidget;
class QGridLayout;
class QKeySequence;
class QMainWindow;
class QStackedWidget;
class QSplitter;
class QTabBar;
class QTabWidget;
class QWidget;

// The numeric values are persisted in settings ("workbench/activity"); do not renumber.
// 1 was the retired Devices activity and is no longer offered.
enum class WorkbenchActivity { Mibs = 0, Queries = 2, Events = 3 };

class WorkbenchShell final : public QObject
{
    Q_OBJECT
public:
    struct Pages {
        QWidget *tree = nullptr;
        QWidget *library = nullptr;
        QWidget *editor = nullptr;
        QWidget *events = nullptr;
    };

    WorkbenchShell(QMainWindow *window, QWidget *centralRoot, QGridLayout *centralLayout,
                   QWidget *contextStrip, QTabWidget *documents, const Pages &pages,
                   QWidget *inspector,
                   QWidget *problems, QWidget *diagnostics, QWidget *raw,
                   QObject *parent = nullptr);

    WorkbenchActivity activity() const { return currentActivity; }
    void setActivity(WorkbenchActivity activity);
    void openDocument(QWidget *document);
    void showMibTree();
    void showMibLibrary(int mode);
    void restorePresentationState();
    void savePresentationState() const;

    QDockWidget *inspectorDock() const { return inspectorPanel; }
    QDockWidget *bottomDock() const { return bottomPanel; }
    QStackedWidget *sidebarStack() const { return sidebars; }
    QAction *activityAction(WorkbenchActivity activity) const;

signals:
    void mibLibraryModeRequested(int mode);

private:
    QWidget *makeMibSidebar();
    QWidget *makeQueriesSidebar();
    QWidget *makeEventsSidebar();
    void addActivityAction(WorkbenchActivity activity, const QString &text,
                           const QKeySequence &shortcut);
    QWidget *defaultDocument(WorkbenchActivity activity) const;
    static int sidebarIndex(WorkbenchActivity activity);

    QMainWindow *mainWindow;
    QTabWidget *documentTabs;
    Pages workbenchPages;
    QStackedWidget *sidebars;
    QSplitter *contentSplitter;
    QDockWidget *inspectorPanel;
    QDockWidget *bottomPanel;
    QTabWidget *bottomTabs;
    QTabBar *openDocumentTabs;
    QList<QWidget *> openedDocuments;
    QList<QAction *> activities;
    WorkbenchActivity currentActivity = WorkbenchActivity::Mibs;
};

#endif
