#ifndef WORKBENCHSHELL_H
#define WORKBENCHSHELL_H

#include <QHash>
#include <QList>
#include <QObject>

class QBoxLayout;
class QDockWidget;
class QGridLayout;
class QLabel;
class QMainWindow;
class QMenu;
class QSplitter;
class QTabWidget;
class QToolBar;
class QToolButton;
class QWidget;

// Permanent workspaces own the existing feature widgets. The shared bottom panel is
// presentation only: hiding it or changing workspaces never changes operation lifetimes.
class WorkbenchShell final : public QObject
{
    Q_OBJECT
public:
    struct Documents {
        QWidget *results = nullptr;
        QWidget *library = nullptr; // contains Library | Profiles | Editor
        QWidget *editor = nullptr;
        QWidget *search = nullptr;
        QWidget *traps = nullptr;
        QWidget *watch = nullptr; // permanent local Query tab
    };
    struct BottomPages {
        QWidget *output = nullptr;
        QWidget *log = nullptr;
        QWidget *problems = nullptr;
    };

    WorkbenchShell(QMainWindow *window, QWidget *centralRoot, QGridLayout *centralLayout,
                   QWidget *header, QWidget *treePane, QTabWidget *workspaces,
                   const Documents &pages, QWidget *inspector, const BottomPages &bottom,
                   QObject *parent = nullptr, QBoxLayout *connectionRow = nullptr);

    void openDocument(QWidget *document);
    void closeDocument(QWidget *document); // only the optional Search page can close
    void showResults();
    void setResultsNote(const QString &note);
    void showBottomPage(QWidget *page); // explicit user navigation opens the shared panel
    void setBottomPageNote(QWidget *page, const QString &note);
    void setBottomPageVisible(QWidget *page, bool visible);
    void showMibLibrary(int mode); // 0 Library, 1 Profiles, 2 Editor
    void setPanelExpanded(bool expanded);
    void setActivityCounts(int running, int problems);
    void setQueryControls(QWidget *controls);
    bool isFullPage() const { return outsideQuery; }
    void restorePresentationState();
    void savePresentationState() const;

    QDockWidget *inspectorDock() const { return inspectorPanel; }
    QDockWidget *bottomDock() const { return bottomPanel; }
    QSplitter *treeSplitter() const { return contentSplitter; }
    QMenu *viewMenu() const { return view; }
    QToolBar *header() const { return headerBar; }

signals:
    void mibLibraryModeRequested(int mode);

private:
    void buildViewMenu(QMainWindow *window);
    void updateWorkspace();
    void updatePanelControls();
    void updateBottomPages();

    QMainWindow *mainWindow;
    QTabWidget *workspaceTabs;
    QTabWidget *queryTabs;
    QWidget *queryPage;
    Documents workbenchPages;
    QHash<QWidget *, QString> workspaceTitles;
    QHash<QWidget *, QString> bottomTitles;
    QHash<QWidget *, QString> bottomIds;
    QSplitter *contentSplitter;
    QDockWidget *inspectorPanel;
    QDockWidget *bottomPanel;
    QTabWidget *bottomTabs;
    QToolBar *headerBar;
    QToolButton *panelButton;
    QLabel *activityStatus;
    QMenu *view = nullptr;
    bool outsideQuery = false;
    bool inspectorShown = true;
    int panelHeight = 300;
    QWidget *problemPage = nullptr; // owned by bottomTabs
    bool problemsAvailable = false;
    QString sharedPanelId = QStringLiteral("output");
    QString mibPanelId = QStringLiteral("output");
    QString pendingPanelId; // wait for the initial MIB report before restoring Problems
};

#endif
