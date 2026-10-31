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

enum class WorkbenchActivity { Mibs = 0, Devices, Queries, Events };

class WorkbenchShell final : public QObject
{
    Q_OBJECT
public:
    struct Pages {
        QWidget *tree = nullptr;
        QWidget *library = nullptr;
        QWidget *editor = nullptr;
        QWidget *discovery = nullptr;
        QWidget *events = nullptr;
        QWidget *graphs = nullptr;
    };

    WorkbenchShell(QMainWindow *window, QWidget *centralRoot, QGridLayout *centralLayout,
                   QWidget *contextStrip, QTabWidget *documents, const Pages &pages,
                   QWidget *devicePane, QWidget *inspector, QWidget *results,
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
