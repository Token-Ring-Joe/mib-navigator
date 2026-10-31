#include "workbenchshell.h"
#include "startupcoordinator.h"
#include "mainwindowlayout.h"

#include <QApplication>
#include <QDockWidget>
#include <QGridLayout>
#include <QMainWindow>
#include <QSettings>
#include <utility>
#include <QStackedWidget>
#include <QSplitter>
#include <QTabBar>
#include <QTabWidget>
#include <QTemporaryDir>
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
    auto *root = new QWidget(&window);
    auto *layout = new QGridLayout(root);
    auto *context = new QWidget(root);
    auto *documents = new QTabWidget(root);
    WorkbenchShell::Pages pages;
    pages.tree = new QWidget(documents); documents->addTab(pages.tree, "Tree");
    pages.library = new QWidget(documents); documents->addTab(pages.library, "Library");
    pages.editor = new QWidget(documents); documents->addTab(pages.editor, "Editor");
    pages.events = new QWidget(documents); documents->addTab(pages.events, "Traps");
    layout->addWidget(context, 0, 0);
    layout->addWidget(documents, 1, 0);
    window.setCentralWidget(root);

    auto *inspector = new QWidget;
    auto *watch = new QWidget;
    auto *search = new QWidget;
    auto *requestLog = new QWidget;
    auto *problems = new QWidget;
    auto *diagnostics = new QWidget;
    auto *raw = new QWidget;
    WorkbenchShell shell(&window, root, layout, context, documents, pages,
                         inspector, watch, search, requestLog, problems, diagnostics, raw, &window);

    check(documents->tabBar()->isHidden(), "old primary tab navigation remains visible");
    QTabBar *durableTabs = window.findChild<QTabBar *>("WorkbenchOpenDocuments");
    check(durableTabs && durableTabs->isHidden(), "empty document strip should not consume space");
    check(shell.sidebarStack()->count() == 3, "three contextual sidebars (MIBs, Queries, Events) not created");
    check(window.findChild<QSplitter *>("WorkbenchContentSplitter") != nullptr,
          "resizable sidebar/document boundary not created");
    for (WorkbenchActivity offered : {WorkbenchActivity::Mibs, WorkbenchActivity::Queries,
                                      WorkbenchActivity::Events})
        check(shell.activityAction(offered) != nullptr, "activity action missing");

    shell.setActivity(WorkbenchActivity::Queries);
    check(shell.activity() == WorkbenchActivity::Queries && shell.sidebarStack()->currentIndex() == 1,
          "Queries activity did not route its sidebar");
    check(documents->currentWidget() == pages.tree,
          "Queries activity did not preserve ad-hoc Tree operation workflow");
    shell.setActivity(WorkbenchActivity::Events);
    check(documents->currentWidget() == pages.events,
          "Events activity did not route to capture workspace");

    int requestedMode = -1;
    QObject::connect(&shell, &WorkbenchShell::mibLibraryModeRequested,
                     [&requestedMode](int mode) { requestedMode = mode; });
    shell.setActivity(WorkbenchActivity::Mibs);
    shell.showMibLibrary(1);
    check(documents->currentWidget() == pages.library && requestedMode == 1,
          "Profiles mode did not route through MIB Library");
    shell.showMibTree();
    check(documents->currentWidget() == pages.tree,
          "MIB Tree mode did not restore expert workflow");
    shell.openDocument(pages.editor);
    check(documents->currentWidget() == pages.editor && durableTabs &&
          durableTabs->count() == 1 && !durableTabs->isHidden(),
          "Editor did not open as a durable central document");
    shell.openDocument(pages.editor);
    check(durableTabs->count() == 1, "Editor document was duplicated");

    shell.bottomDock()->hide();
    shell.inspectorDock()->show();
    shell.savePresentationState();
    shell.bottomDock()->show();
    shell.inspectorDock()->hide();
    shell.setActivity(WorkbenchActivity::Queries);
    shell.restorePresentationState();
    check(shell.activity() == WorkbenchActivity::Mibs,
          "selected activity presentation state did not restore");
    check(shell.bottomDock()->isHidden() && !shell.inspectorDock()->isHidden(),
          "panel visibility presentation state did not restore");
    check(!QSettings().value("workbench/content-splitter").toByteArray().isEmpty(),
          "sidebar splitter presentation state was not persisted");

    // Settings saved by older builds: 1 was the retired Devices activity; 2 and 3 keep their meaning.
    for (const auto &[stored, expected] : {std::pair{1, WorkbenchActivity::Mibs},
                                           std::pair{2, WorkbenchActivity::Queries},
                                           std::pair{3, WorkbenchActivity::Events},
                                           std::pair{99, WorkbenchActivity::Mibs}}) {
        QSettings().setValue("workbench/activity", stored);
        shell.setActivity(WorkbenchActivity::Mibs);
        shell.restorePresentationState();
        check(shell.activity() == expected, "an activity saved by an older build did not restore correctly");
    }

    return failures ? 1 : 0;
}
