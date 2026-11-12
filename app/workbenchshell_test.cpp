#include "workbenchshell.h"
#include "startupcoordinator.h"
#include "mainwindowlayout.h"

#include <QAction>
#include <QApplication>
#include <QDockWidget>
#include <QGridLayout>
#include <QMainWindow>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QKeyEvent>
#include <QToolButton>
#include <QMenu>
#include <QMenuBar>
#include <QPointer>
#include <QSettings>
#include <QSplitter>
#include <QStyle>
#include <QTabBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QToolBar>
#include <QTreeView>
#include <QVBoxLayout>
#include <QWidget>
#include <algorithm>
#include <cstdio>

namespace {
int failures = 0;
void check(bool condition, const char *message)
{
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); ++failures; }
}
}

int main(int argc, char **argv)
{
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("MibNavigatorWorkbenchTest"));
    QCoreApplication::setApplicationName(QStringLiteral("Shell"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QTemporaryDir settingsRoot;
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsRoot.path());

    QMainWindow window;
    StartupCoordinator coordinator;
    const quint64 firstStartup = coordinator.begin();
    coordinator.advance(firstStartup, StartupState::ShellReady);
    check(coordinator.state() == StartupState::ShellReady,
          "startup coordinator did not advance current generation");
    const quint64 secondStartup = coordinator.begin();
    coordinator.advance(firstStartup, StartupState::Ready);
    check(coordinator.state() == StartupState::ShellStarting &&
          !coordinator.accepts(firstStartup),
          "startup coordinator accepted a stale generation");
    coordinator.cancel();
    coordinator.advance(secondStartup, StartupState::Ready);
    check(coordinator.state() == StartupState::Cancelling &&
          !coordinator.accepts(secondStartup),
          "startup coordinator accepted publication after cancellation");
    const QList<QRect> screens{{-2560, 0, 2560, 1392}, {0, 0, 2194, 1186},
                               {2560, 0, 1920, 1032}};
    const QRect valid(-2200, 80, 1400, 900);
    check(SafeRestoredWindowGeometry(valid, screens) == valid,
          "valid multi-monitor geometry was changed");
    check(SafeRestoredWindowGeometry({3847, -6, 1920, 1010}, screens) ==
              QRect(2560, 0, 1920, 1010),
          "partly inaccessible real-user geometry was not repaired on its monitor");
    const QRect tiny = SafeRestoredWindowGeometry({9000, 9000, 20, 20}, screens);
    check(tiny.width() >= 320 && tiny.height() >= 240 &&
          std::any_of(screens.cbegin(), screens.cend(), [&tiny](const QRect &screen) {
              return screen.contains(tiny.topLeft());
          }), "tiny/off-screen geometry was not repaired");


    const QRect bottomLost = SafeRestoredWindowGeometry({100, 1100, 1100, 760}, screens);
    check(bottomLost.bottom() <= screens[1].bottom(),
          "restored window keeps its bottom controls on screen");

    QMenu *fileMenu = window.menuBar()->addMenu("&File");
    QMenu *editMenu = window.menuBar()->addMenu("&Edit");
    QMenu *viewMenu = window.menuBar()->addMenu("&View");
    viewMenu->setObjectName("WorkbenchViewMenu");
    QMenu *mibMenu = window.menuBar()->addMenu("&MIB");
    QMenu *snmpMenu = window.menuBar()->addMenu("&SNMP");
    QMenu *settingsMenu = window.menuBar()->addMenu("Se&ttings");
    QMenu *helpMenu = window.menuBar()->addMenu("&Help");
    auto *root = new QWidget(&window);
    auto *layout = new QGridLayout(root);
    auto *header = new QWidget(root);
    auto *documents = new QTabWidget(root);
    auto *oldTreeTab = new QWidget(documents); documents->addTab(oldTreeTab, "Tree");
    WorkbenchShell::Documents pages;
    pages.library = new QWidget(documents); documents->addTab(pages.library, "Library");
    pages.editor = new QWidget(documents); documents->addTab(pages.editor, "Editor");
    pages.traps = new QWidget(documents); documents->addTab(pages.traps, "Traps");
    pages.watch = new QWidget;
    pages.macros = new QWidget;
    auto *editorContent = new QLineEdit(pages.editor);
    auto *mibTabs = new QTabWidget(pages.library);
    (new QVBoxLayout(pages.library))->addWidget(mibTabs);
    mibTabs->addTab(new QWidget, "Library");
    mibTabs->addTab(new QWidget, "Profiles");
    mibTabs->addTab(pages.editor, "Editor");
    pages.results = new QWidget;
    auto *resultContent = new QLineEdit(pages.results);
    resultContent->setObjectName("resultContent"); pages.results->setProperty("workbenchFocusTarget", QStringLiteral("resultContent"));
    auto *trapContent = new QLineEdit(pages.traps);
    trapContent->setObjectName("trapContent"); pages.traps->setProperty("workbenchFocusTarget", QStringLiteral("trapContent"));
    auto *watchContent = new QLineEdit(pages.watch);
    watchContent->setObjectName("watchContent"); pages.watch->setProperty("workbenchFocusTarget", QStringLiteral("watchContent"));
    editorContent->setObjectName("editorContent"); pages.editor->setProperty("workbenchFocusTarget", QStringLiteral("editorContent"));
    pages.log = new QWidget;
    auto *logContent = new QLineEdit(pages.log);
    logContent->setObjectName("logContent"); pages.log->setProperty("workbenchFocusTarget", QStringLiteral("logContent"));
    pages.search = new QWidget(&window);
    auto *treePane = new QWidget(oldTreeTab);
    auto *tree = new QTreeView(treePane);
    (new QVBoxLayout(treePane))->addWidget(tree);
    layout->addWidget(header, 0, 0);
    layout->addWidget(documents, 1, 0);
    window.setCentralWidget(root);
    WorkbenchShell::BottomPages bottom;
    bottom.output = new QWidget;
    bottom.problems = new QWidget;
    auto *inspector = new QWidget;
    auto *inspectorContent = new QLineEdit(inspector); inspectorContent->setObjectName("inspectorContent"); inspector->setProperty("workbenchFocusTarget", QStringLiteral("inspectorContent"));
    auto *outputContent = new QLineEdit(bottom.output); outputContent->setObjectName("outputContent"); bottom.output->setProperty("workbenchFocusTarget", QStringLiteral("outputContent"));
    auto *target = new QComboBox(header); target->setEditable(true); target->setObjectName("ConnectionTarget");
    WorkbenchShell shell(&window, root, layout, header, treePane, documents, pages,
                         inspector, bottom, &window);
    QObject::connect(&shell, &WorkbenchShell::mibLibraryModeRequested,
                     mibTabs, &QTabWidget::setCurrentIndex);
    shell.restorePresentationState();
    window.resize(1200, 850);
    window.show();
    QCoreApplication::processEvents();
    auto *query = window.findChild<QTabWidget *>("WorkbenchQueryDocuments");
    auto *bottomTabs = window.findChild<QTabWidget *>("WorkbenchBottomTabs");
    auto *button = window.findChild<QToolButton *>("WorkbenchPanelToggle");
    auto *status = window.findChild<QLabel *>("WorkbenchActivityStatus");
    check(documents->count() == 4 && documents->tabText(0) == "Query" &&
          documents->tabText(1) == "MIBs" && documents->tabText(2) == "Traps" &&
          documents->tabText(3) == "Log" && documents->widget(3) == pages.log && !documents->tabsClosable() &&
          !documents->tabBar()->isHidden(), "permanent top-level workspace tabs");
    check(query && query->count() == 3 && query->widget(0) == pages.results &&
          query->widget(1) == pages.watch && query->widget(2) == pages.macros && !query->tabBar()->isHidden(), "Query contains Results, Watch and Macros");
    check(shell.treeSplitter()->count() == 2 && shell.treeSplitter()->widget(0) == treePane &&
          shell.treeSplitter()->widget(1) == query, "tree and Results belong to Query");
    check(bottomTabs && bottomTabs->count() == 2 && bottomTabs->widget(0) == bottom.output &&
          bottomTabs->widget(1) == bottom.problems && !bottomTabs->isTabVisible(1) &&
          bottomTabs->indexOf(pages.log) < 0, "bottom panel holds Output and conditional Problems; Log is a workspace");
    check(shell.bottomDock()->isHidden() && button && !button->isChecked() &&
          button->text() == "Operations", "panel starts collapsed with a visible toggle");
    check(shell.header()->isAncestorOf(header) && !shell.header()->isMovable() &&
          !shell.header()->isFloatable() && !shell.header()->toggleViewAction()->isVisible() &&
          window.corner(Qt::BottomRightCorner) == Qt::BottomDockWidgetArea,
          "fixed header and full-width bottom panel");
    QPointer<QWidget> keptEditor(pages.editor), keptWatch(pages.watch), keptTraps(pages.traps);
    auto *queryRow = new QWidget(header);
    auto *search = new QLineEdit(queryRow); search->setObjectName("CollectionSearch");
    (new QHBoxLayout(queryRow))->addWidget(search);
    shell.setQueryControls(queryRow);
    shell.inspectorDock()->show();
    for (int repeat = 0; repeat < 10; ++repeat) {
        shell.openDocument(pages.editor);
        check(documents->currentWidget() == pages.library && mibTabs->currentWidget() == pages.editor &&
              shell.isFullPage() && !treePane->isVisibleTo(&window) &&
              shell.inspectorDock()->isHidden() && !queryRow->isVisibleTo(&window), "Editor is nested under MIBs");
        shell.openDocument(pages.traps);
        shell.openDocument(pages.watch);
        check(documents->currentIndex() == 0 && query->currentWidget() == pages.watch && documents->count() == 4 &&
              shell.bottomDock()->isHidden() && header->isVisibleTo(&window),
              "workspace navigation keeps header and collapsed panel state");
        shell.showResults();
        check(!shell.inspectorDock()->isHidden() && queryRow->isVisibleTo(&window) &&
              treePane->isVisibleTo(&window), "Query restores its Inspector and tools");
    }
    check(keptEditor && keptWatch && keptTraps && editorContent->parentWidget() == pages.editor,
          "workspace navigation preserves feature widgets and their state");
    shell.inspectorDock()->hide();
    shell.openDocument(pages.library);
    shell.showResults();
    check(shell.inspectorDock()->isHidden(), "explicit Inspector preference survives navigation");
    shell.openDocument(pages.search);
    check(documents->currentIndex() == 0 && query->currentWidget() == pages.search &&
          query->count() == 4 && !query->tabBar()->isHidden(), "Search opens locally in Query");
    QPointer<QWidget> keptSearch(pages.search);
    emit query->tabCloseRequested(query->indexOf(pages.search));
    check(keptSearch && query->count() == 3 && !query->tabBar()->isHidden(),
          "closing Search preserves Results and Watch");
    shell.closeDocument(pages.results);
    shell.closeDocument(pages.library);
    shell.closeDocument(pages.watch);
    shell.closeDocument(pages.macros);
    check(query->count() == 3 && documents->count() == 4, "permanent pages cannot close");
    if (auto *macrosAction = window.findChild<QAction *>("ViewMacros")) macrosAction->trigger();
    check(query->currentWidget() == pages.macros && documents->currentIndex() == 0, "View opens the existing Query-local Macros page");
    shell.openDocument(pages.search);
    check(query->currentWidget() == pages.search, "Search reopens");
    shell.showMibLibrary(1);
    check(documents->currentWidget() == pages.library && mibTabs->currentIndex() == 1,
          "Profiles is a peer of Library and Editor");
    shell.setBottomPageVisible(bottom.problems, true);
    shell.setBottomPageNote(bottom.problems, "2");
    shell.openDocument(pages.editor);
    editorContent->setFocus();
    QCoreApplication::processEvents();
    QWidget *focused = QApplication::focusWidget();
    shell.setActivityCounts(1, 2);
    check(status && status->text().contains("1 running") && status->text().contains("2 problems") &&
          shell.bottomDock()->isHidden() && documents->currentWidget() == pages.library &&
          bottomTabs->currentWidget() == bottom.output && QApplication::focusWidget() == focused,
          "background counts neither expand panel nor change workspace or selection");
    if (auto *viewProblems = window.findChild<QAction *>("ViewBottomProblems")) viewProblems->trigger();
    QCoreApplication::processEvents();
    check(documents->currentWidget() == pages.library && bottomTabs->currentWidget() == bottom.problems &&
          !shell.bottomDock()->isHidden() && button->isChecked() && button->text() == "Operations",
          "explicit Problems opens within MIBs");
    const int expandedHeight = shell.bottomDock()->height();
    for (QWidget *page : {pages.traps, pages.watch, pages.results, pages.editor}) {
        shell.openDocument(page);
        const bool inMibs = documents->currentWidget() == pages.library;
        check(!shell.bottomDock()->isHidden() && bottomTabs->isTabVisible(1) == inMibs &&
              bottomTabs->currentWidget() == (inMibs ? bottom.problems : bottom.output),
              "Problems is MIB-only and each workspace restores its own panel selection");
    }
    button->click();
    shell.setActivityCounts(0, 3);
    shell.setResultsNote("1 warning");
    check(query->tabText(0) == "Results (1 warning)", "warnings annotate the main Results tab");
    check(shell.bottomDock()->isHidden() && bottomTabs->currentWidget() == bottom.problems,
          "new results and errors leave a collapsed panel collapsed");
    shell.showResults();
    shell.showBottomPage(bottom.output);
    shell.setPanelExpanded(false);
    shell.savePresentationState();
    check(QSettings().value("workbench/panel-tab").toString() == "output" &&
          !QSettings().value("workbench/panel-visible").toBool() &&
          QSettings().value("workbench/panel-height").toInt() >= 130 && expandedHeight >= 130,
          "panel selection, collapsed state and useful height persist");
    shell.showBottomPage(bottom.output);
    shell.restorePresentationState();
    check(documents->currentIndex() == 0 && shell.bottomDock()->isHidden() &&
          bottomTabs->currentWidget() == bottom.output, "restore uses stable panel ID and starts on Query");
    shell.showBottomPage(bottom.output);
    shell.savePresentationState();
    check(QSettings().value("workbench/panel-tab").toString() == "output",
          "Output has its own stable panel ID");
    shell.setPanelExpanded(false);
    shell.openDocument(pages.watch);
    shell.restorePresentationState();
    check(!shell.bottomDock()->isHidden() && bottomTabs->currentWidget() == bottom.output &&
          documents->currentIndex() == 0, "expanded panel round trip");
    {
        QSettings settings;
        settings.remove("workbench/panel-visible");
        settings.setValue("workbench/bottom-visible", true);
        settings.setValue("workbench/bottom-tab-name", "Watch");
        settings.setValue("workbench/panel-tab", "retired");
        settings.setValue("workbench/panel-height", -100);
    }
    shell.restorePresentationState();
    check(shell.bottomDock()->isHidden() && QSettings().value("workbench/bottom-visible").toBool() &&
          QSettings().value("workbench/bottom-tab-name").toString() == "Watch",
          "old settings remain untouched and cannot override collapsed-first-use behavior");
    shell.showBottomPage(bottom.output);
    QCoreApplication::processEvents();
    check(shell.bottomDock()->height() >= 130, "invalid persisted height is bounded");
    shell.setBottomPageVisible(bottom.problems, false);
    shell.showBottomPage(bottom.problems);
    check(bottomTabs->isTabVisible(1), "explicit View Problems works even with no current problems");
    shell.setBottomPageVisible(bottom.problems, false);
    check(bottomTabs->currentWidget() != bottom.problems, "hidden Problems releases selection");
    {
        QSettings settings;
        settings.remove("workbench/mib-panel-tab");
        settings.setValue("workbench/panel-tab", "problems");
    }
    shell.restorePresentationState();
    check(QSettings().value("workbench/panel-tab").toString() == "problems",
          "restoration alone does not overwrite legacy Problems settings");
    shell.savePresentationState();
    check(QSettings().value("workbench/panel-tab").toString() == "output" &&
          QSettings().value("workbench/mib-panel-tab").toString() == "problems",
          "normal save migrates a pending Problems preference to the MIB-specific key");
    shell.setBottomPageVisible(bottom.problems, true);
    check(documents->currentIndex() == 0 && !bottomTabs->isTabVisible(1) &&
          bottomTabs->currentWidget() == bottom.output && shell.bottomDock()->isHidden(),
          "initial report cannot expose Problems in Query or expand the panel");
    shell.openDocument(pages.editor);
    check(bottomTabs->currentWidget() == bottom.problems && bottomTabs->isTabVisible(1) &&
          shell.bottomDock()->isHidden(), "entering MIBs restores its saved Problems selection");
    shell.showResults();
    shell.showBottomPage(bottom.output);
    shell.savePresentationState();
    shell.restorePresentationState();
    check(bottomTabs->currentWidget() == bottom.output && !bottomTabs->isTabVisible(1),
          "restart restores the Query panel page without exposing MIB Problems");
    shell.openDocument(pages.library);
    check(bottomTabs->currentWidget() == bottom.problems,
          "MIB Problems selection survives saving and restarting from Query");
    shell.setBottomPageVisible(bottom.problems, false);
    shell.restorePresentationState();
    shell.showBottomPage(bottom.output);
    shell.setBottomPageVisible(bottom.problems, true);
    shell.openDocument(pages.library);
    check(bottomTabs->currentWidget() != bottom.problems,
          "explicit panel navigation cancels deferred Problems restoration");
    shell.setBottomPageVisible(bottom.problems, false);
    shell.restorePresentationState();
    shell.setBottomPageVisible(bottom.problems, false); // initial report has no problems
    shell.setBottomPageVisible(bottom.problems, true);  // later background report
    shell.openDocument(pages.library);
    check(bottomTabs->currentWidget() != bottom.problems,
          "a later report cannot revive a consumed Problems preference");
    shell.openDocument(pages.traps);
    shell.setPanelExpanded(false);
    shell.setBottomPageVisible(bottom.problems, true);
    check(documents->currentWidget() == pages.traps && !bottomTabs->isTabVisible(1) &&
          shell.bottomDock()->isHidden(), "background reports cannot expose Problems in Traps");
    shell.showBottomPage(bottom.problems);
    check(documents->currentWidget() == pages.library && mibTabs->currentWidget() == pages.editor &&
          bottomTabs->currentWidget() == bottom.problems && !shell.bottomDock()->isHidden(),
          "explicit Problems from Traps selects MIBs without changing its local Editor tab");
    shell.setPanelExpanded(true);
    const auto top = window.menuBar()->actions();
    check(top == QList<QAction *>{fileMenu->menuAction(), editMenu->menuAction(),
              viewMenu->menuAction(), mibMenu->menuAction(), snmpMenu->menuAction(),
              settingsMenu->menuAction(), helpMenu->menuAction()} && shell.viewMenu() == viewMenu,
          "shell populates the existing View menu and preserves the seven-menu order");
    auto *toggle = window.findChild<QAction *>("WorkbenchToggleBottom");
    check(toggle && toggle->shortcut() == QKeySequence("Ctrl+J"), "panel toggle shortcut");
    if (toggle) toggle->trigger();
    check(shell.bottomDock()->isHidden(), "View toggle collapses panel");
    window.setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);
    shell.restorePresentationState();
    check(window.corner(Qt::BottomRightCorner) == Qt::BottomDockWidgetArea && !shell.header()->isHidden(),
          "restore repairs old dock corner state");
    QSettings().setValue("workbench/panel-tab", "results");
    shell.showBottomPage(bottom.output);
    shell.restorePresentationState();
    check(query->currentWidget() == pages.results && bottomTabs->currentWidget() == bottom.output &&
          QSettings().value("workbench/panel-tab").toString() == "results",
          "old Results preference restores to Output without rewriting stored settings");
    shell.openDocument(pages.watch);
    shell.setPanelExpanded(false);
    shell.showResults();
    check(query->currentWidget() == pages.results && shell.bottomDock()->isHidden(),
          "starting a query selects Results without expanding the bottom panel");
    if (auto *viewOutput = window.findChild<QAction *>("ViewBottomOutput")) viewOutput->trigger();
    check(bottomTabs->currentWidget() == bottom.output && !shell.bottomDock()->isHidden() &&
          query->currentWidget() == pages.results, "Output opens below Results");
    shell.setResultsNote({});
    check(query->tabText(0) == "Results", "clearing warnings restores Results title");
    window.activateWindow(); QCoreApplication::processEvents();
    auto *navigation = window.findChild<QMenu *>("WorkbenchKeyboardNavigation");
    check(navigation && navigation->actions().size() == 6,
          "View exposes every focus shortcut in Keyboard Navigation");
    const auto focus = [&](const char *name, QWidget *expected) {
        auto *action = window.findChild<QAction *>(name);
        check(action, "focus action exists");
        if (action) action->trigger();
        QCoreApplication::processEvents();
        if (QApplication::focusWidget() != expected)
            std::fprintf(stderr, "Focus mismatch: %s expected=%s actual=%s visible=%d enabled=%d\n", name,
                expected->metaObject()->className(), QApplication::focusWidget() ? QApplication::focusWidget()->metaObject()->className() : "null",
                expected->isVisible(), expected->isEnabled());
        check(QApplication::focusWidget() == expected, "focus action reaches usable visible content");
    };
    shell.showResults(); focus("WorkbenchFocusDocument", resultContent);
    shell.openDocument(pages.watch); focus("WorkbenchFocusDocument", watchContent);
    shell.openDocument(pages.editor); focus("WorkbenchFocusDocument", editorContent);
    shell.openDocument(pages.traps); focus("WorkbenchFocusDocument", trapContent);
    focus("WorkbenchFocusInspector", inspectorContent);
    shell.showBottomPage(bottom.output); focus("WorkbenchFocusBottom", outputContent);
    focus("WorkbenchFocusTree", tree);
    focus("WorkbenchGlobalSearch", search);
    focus("WorkbenchFocusTarget", target);
    // Exercise Qt shortcut dispatch as well as action wiring, from another workspace.
    shell.openDocument(pages.traps); target->setEditText("192.0.2.1");
    QKeyEvent targetKey(QEvent::KeyPress, Qt::Key_L, Qt::ControlModifier);
    QApplication::sendEvent(&window, &targetKey);
    check(QApplication::focusWidget() == target && target->lineEdit()->hasSelectedText(),
          "Ctrl+L focuses and selects the target without navigating workspaces");
    check(documents->currentWidget() == pages.traps, "target focus leaves the workspace unchanged");
    QKeyEvent workKey(QEvent::KeyPress, Qt::Key_1, Qt::ControlModifier);
    QApplication::sendEvent(&window, &workKey);
    check(QApplication::focusWidget() == trapContent, "Ctrl+1 reaches the active work area");
    // The Log is a top-level workspace: View > Log opens it, Ctrl+1 reaches it, and it is not
    // a bottom-panel page, so a retired "log" panel selection falls back to Output.
    if (auto *viewLog = window.findChild<QAction *>("ViewLog")) viewLog->trigger();
    check(documents->currentWidget() == pages.log && shell.isFullPage() && shell.inspectorDock()->isHidden() &&
          !bottomTabs->isTabVisible(1), "View > Log opens the Log workspace without Problems or the Inspector");
    QApplication::sendEvent(&window, &workKey);
    check(QApplication::focusWidget() == logContent, "Ctrl+1 reaches the Log workspace");
    QSettings().setValue("workbench/panel-tab", "log");
    QSettings().setValue("workbench/mib-panel-tab", "log");
    shell.restorePresentationState();
    check(documents->currentIndex() == 0 && bottomTabs->currentWidget() == bottom.output,
          "a retired Log panel selection restores to Output");
    shell.showResults();
    shell.showResults();
    shell.treeSplitter()->setSizes({250, 650});
    const QList<int> savedSizes = shell.treeSplitter()->sizes();
    shell.inspectorDock()->hide(); shell.showBottomPage(bottom.output);
    shell.setPanelExpanded(false); shell.savePresentationState();
    shell.treeSplitter()->setSizes({550, 350}); shell.inspectorDock()->show();
    shell.restorePresentationState();
    check(shell.treeSplitter()->sizes() == savedSizes && shell.inspectorDock()->isHidden() &&
          shell.bottomDock()->isHidden() && bottomTabs->currentWidget() == bottom.output,
          "splitter, Inspector and collapsed panel round-trip together");
    shell.treeSplitter()->setOrientation(Qt::Vertical);
    QSettings().setValue("workbench/tree-splitter", shell.treeSplitter()->saveState());
    shell.restorePresentationState();
    check(shell.treeSplitter()->orientation() == Qt::Horizontal && !shell.treeSplitter()->childrenCollapsible(),
          "stale vertical splitter state cannot replace the Query layout");
    QSettings().setValue("workbench/tree-splitter", QByteArray("invalid splitter state"));
    shell.restorePresentationState();
    check(shell.treeSplitter()->sizes().first() > 0 && shell.treeSplitter()->sizes().last() > 0,
          "damaged splitter state retains usable panes");
    return failures ? 1 : 0;
}
