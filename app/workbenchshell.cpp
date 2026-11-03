#include "workbenchshell.h"

#include <QAbstractItemView>
#include <QAction>
#include <QBoxLayout>
#include <QDockWidget>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QSettings>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStyle>
#include <QTabBar>
#include <QTabWidget>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <functional>

namespace {
constexpr auto inspectorKey = "workbench/inspector-shown";
constexpr auto splitterKey = "workbench/tree-splitter";
// New keys leave the previous always-visible panel's saved state untouched. First use of
// this layout is collapsed; an old Watch/Traps selection cannot select another page by index.
constexpr auto panelVisibleKey = "workbench/panel-visible";
constexpr auto panelTabKey = "workbench/panel-tab";
constexpr auto panelHeightKey = "workbench/panel-height";
constexpr auto mibPanelTabKey = "workbench/mib-panel-tab";
}

WorkbenchShell::WorkbenchShell(QMainWindow *window, QWidget *,
    QGridLayout *centralLayout, QWidget *header, QWidget *treePane, QTabWidget *workspaces,
    const Documents &pages, QWidget *inspector, const BottomPages &bottom, QObject *parent,
    QBoxLayout *connectionRow)
    : QObject(parent), mainWindow(window), workspaceTabs(workspaces), workbenchPages(pages)
{
    workspaceTabs->setObjectName(QStringLiteral("WorkbenchDocuments"));
    workspaceTabs->setDocumentMode(true);
    workspaceTabs->setTabsClosable(false);
    workspaceTabs->setMovable(false);
    while (workspaceTabs->count()) workspaceTabs->removeTab(0);

    queryPage = new QWidget(workspaceTabs);
    queryPage->setObjectName(QStringLiteral("WorkbenchQueryWorkspace"));
    auto *queryLayout = new QVBoxLayout(queryPage);
    queryLayout->setContentsMargins(0, 0, 0, 0);
    contentSplitter = new QSplitter(Qt::Horizontal, queryPage);
    contentSplitter->setObjectName(QStringLiteral("WorkbenchTreeSplitter"));
    contentSplitter->setChildrenCollapsible(false);
    treePane->setMinimumWidth(180);
    contentSplitter->addWidget(treePane);
    queryTabs = new QTabWidget(contentSplitter);
    queryTabs->setObjectName(QStringLiteral("WorkbenchQueryDocuments"));
    queryTabs->setDocumentMode(true);
    queryTabs->setTabsClosable(true);
    if (pages.results) {
        const int i = queryTabs->addTab(pages.results, tr("Results"));
        const auto side = static_cast<QTabBar::ButtonPosition>(queryTabs->style()->styleHint(
            QStyle::SH_TabBar_CloseButtonPosition, nullptr, queryTabs->tabBar()));
        queryTabs->tabBar()->setTabButton(i, side, nullptr);
    }
    if (pages.search) { pages.search->setParent(queryTabs); pages.search->hide(); }
    if (pages.watch) {
        const int i = queryTabs->addTab(pages.watch, tr("Watch"));
        const auto side = static_cast<QTabBar::ButtonPosition>(queryTabs->style()->styleHint(
            QStyle::SH_TabBar_CloseButtonPosition, nullptr, queryTabs->tabBar()));
        queryTabs->tabBar()->setTabButton(i, side, nullptr);
    }
    queryTabs->tabBar()->setVisible(queryTabs->count() > 1);
    connect(queryTabs, &QTabWidget::tabCloseRequested, this,
            [this](int i) { closeDocument(queryTabs->widget(i)); });
    contentSplitter->addWidget(queryTabs);
    contentSplitter->setStretchFactor(0, 0);
    contentSplitter->setStretchFactor(1, 1);
    contentSplitter->setSizes({300, 900});
    queryLayout->addWidget(contentSplitter);

    const QList<std::pair<QWidget *, QString>> top{{queryPage, tr("Query")},
        {pages.library, tr("MIBs")}, {pages.traps, tr("Traps")}};
    for (const auto &[page, title] : top) {
        if (!page) continue;
        workspaceTabs->addTab(page, title);
        workspaceTitles.insert(page, title);
    }
    workspaceTabs->tabBar()->show();

    centralLayout->removeWidget(header);
    headerBar = new QToolBar(tr("Connection"), window);
    headerBar->setObjectName(QStringLiteral("WorkbenchHeaderBar"));
    headerBar->setMovable(false);
    headerBar->setFloatable(false);
    headerBar->toggleViewAction()->setVisible(false);
    headerBar->setContentsMargins(0, 0, 0, 0);
    headerBar->layout()->setContentsMargins(0, 0, 0, 0);
    headerBar->addWidget(header);
    window->addToolBar(Qt::TopToolBarArea, headerBar);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(0);
    centralLayout->removeWidget(workspaceTabs);
    centralLayout->addWidget(workspaceTabs, 1, 0);
    centralLayout->setRowStretch(1, 1);
    window->setCorner(Qt::BottomRightCorner, Qt::BottomDockWidgetArea);

    inspectorPanel = new QDockWidget(tr("Inspector"), window);
    inspectorPanel->setObjectName(QStringLiteral("WorkbenchInspectorDock"));
    inspectorPanel->setAllowedAreas(Qt::RightDockWidgetArea);
    inspectorPanel->setWidget(inspector);
    inspectorPanel->setMinimumWidth(240);
    window->addDockWidget(Qt::RightDockWidgetArea, inspectorPanel);

    problemPage = bottom.problems;
    bottomTabs = new QTabWidget(window);
    bottomTabs->setObjectName(QStringLiteral("WorkbenchBottomTabs"));
    bottomTabs->setDocumentMode(true);
    const QList<std::pair<QWidget *, QString>> lower{{bottom.output, tr("Output")},
        {bottom.log, tr("Log")}, {bottom.problems, tr("Problems")}};
    const QStringList ids{QStringLiteral("output"), QStringLiteral("log"), QStringLiteral("problems")};
    for (qsizetype i = 0; i < lower.size(); ++i) {
        const auto &[page, title] = lower[i];
        if (!page) continue;
        bottomTabs->addTab(page, title);
        bottomTitles.insert(page, title);
        bottomIds.insert(page, ids[i]);
    }
    bottomPanel = new QDockWidget(tr("Panel"), window);
    bottomPanel->setObjectName(QStringLiteral("WorkbenchBottomDock"));
    bottomPanel->setAllowedAreas(Qt::BottomDockWidgetArea);
    bottomPanel->setFeatures(QDockWidget::NoDockWidgetFeatures);
    bottomPanel->setWidget(bottomTabs);
    bottomPanel->setTitleBarWidget(new QWidget(bottomPanel));
    bottomPanel->setMinimumHeight(130);
    window->addDockWidget(Qt::BottomDockWidgetArea, bottomPanel);

    auto *controls = new QWidget(header);
    controls->setObjectName(QStringLiteral("WorkbenchPanelControls"));
    auto *controlsLayout = new QHBoxLayout(controls);
    controlsLayout->setContentsMargins(0, 0, 0, 0);
    activityStatus = new QLabel(controls);
    activityStatus->setObjectName(QStringLiteral("WorkbenchActivityStatus"));
    activityStatus->setAccessibleName(tr("Activity status"));
    controlsLayout->addWidget(activityStatus);
    panelButton = new QToolButton(controls);
    panelButton->setObjectName(QStringLiteral("WorkbenchPanelToggle"));
    panelButton->setCheckable(true);
    panelButton->setToolTip(tr("Show or hide Output, Log and MIB Problems. Operations keep running while hidden."));
    controlsLayout->addWidget(panelButton);
    if (connectionRow) connectionRow->addWidget(controls);
    else headerBar->addWidget(controls);
    connect(panelButton, &QToolButton::toggled, this, &WorkbenchShell::setPanelExpanded);
    connect(bottomPanel, &QDockWidget::visibilityChanged, this, &WorkbenchShell::updatePanelControls);

    const auto focusAction = [this, window](const QString &name, const QString &title,
        const QString &shortcut, const std::function<void()> &run) {
        auto *action = new QAction(title, window);
        action->setObjectName(name);
        action->setShortcut(QKeySequence(shortcut));
        window->addAction(action);
        connect(action, &QAction::triggered, this, run);
    };
    focusAction("WorkbenchFocusTree", tr("Focus MIB Tree"), "Ctrl+0", [this, treePane]() {
        workspaceTabs->setCurrentWidget(queryPage);
        if (auto *tree = treePane->findChild<QAbstractItemView *>()) tree->setFocus(Qt::ShortcutFocusReason);
    });
    focusAction("WorkbenchFocusDocument", tr("Focus Work Area"), "Ctrl+1", [this]() {
        if (auto *page = workspaceTabs->currentWidget()) page->setFocus(Qt::ShortcutFocusReason);
    });
    focusAction("WorkbenchFocusInspector", tr("Focus Inspector"), "Ctrl+Alt+2", [this]() {
        workspaceTabs->setCurrentWidget(queryPage);
        inspectorPanel->show();
        inspectorPanel->widget()->setFocus(Qt::ShortcutFocusReason);
    });
    focusAction("WorkbenchFocusBottom", tr("Focus Panel"), "Ctrl+`", [this]() {
        setPanelExpanded(true);
        if (auto *page = bottomTabs->currentWidget()) page->setFocus(Qt::ShortcutFocusReason);
    });
    focusAction("WorkbenchGlobalSearch", tr("Search MIBs"), "Ctrl+Shift+F", [this]() {
        workspaceTabs->setCurrentWidget(queryPage);
        if (auto *filter = mainWindow->findChild<QLineEdit *>(QStringLiteral("CollectionSearch")))
            filter->setFocus(Qt::ShortcutFocusReason);
    });
    connect(bottomTabs, &QTabWidget::currentChanged, this, [this]() {
        pendingPanelId.clear();
        const QString id = bottomIds.value(bottomTabs->currentWidget());
        if (workspaceTabs->currentWidget() == workbenchPages.library) mibPanelId = id;
        else sharedPanelId = id;
    });
    buildViewMenu(window);
    connect(workspaceTabs, &QTabWidget::currentChanged, this, &WorkbenchShell::updateWorkspace);
    if (bottom.problems) setBottomPageVisible(bottom.problems, false);
    setActivityCounts(0, 0);
    setPanelExpanded(false);
}

void WorkbenchShell::buildViewMenu(QMainWindow *window)
{
    auto *bar = window->menuBar();
    view = new QMenu(tr("&View"), bar);
    view->setObjectName(QStringLiteral("WorkbenchViewMenu"));
    const auto top = bar->actions();
    bar->insertMenu(top.size() > 1 ? top[1] : nullptr, view);
    const auto add = [this](const QString &name, const QString &title, const std::function<void()> &run) {
        auto *action = view->addAction(title);
        action->setObjectName(name);
        connect(action, &QAction::triggered, this, run);
        return action;
    };
    add("ViewResults", tr("Results"), [this]() { showResults(); });
    add("ViewMibLibrary", tr("MIB Library"), [this]() { showMibLibrary(0); });
    add("ViewMibProfiles", tr("MIB Profiles"), [this]() { showMibLibrary(1); });
    add("ViewMibEditor", tr("MIB Editor"), [this]() { openDocument(workbenchPages.editor); });
    if (workbenchPages.traps)
        add("ViewTraps", tr("Traps"), [this]() { openDocument(workbenchPages.traps); });
    if (workbenchPages.watch)
        add("ViewWatch", tr("Watch"), [this]() { openDocument(workbenchPages.watch); });
    view->addSeparator();
    auto *inspectorAction = inspectorPanel->toggleViewAction();
    inspectorAction->setObjectName("WorkbenchToggleInspector");
    inspectorAction->setText(tr("Inspector"));
    inspectorAction->setShortcut(QKeySequence("Ctrl+Alt+I"));
    view->addAction(inspectorAction);
    auto *panelAction = add("WorkbenchToggleBottom", tr("Show panel"), [this]() {
        setPanelExpanded(bottomPanel->isHidden());
    });
    panelAction->setShortcut(QKeySequence("Ctrl+J"));
    view->addSeparator();
    for (int i = 0; i < bottomTabs->count(); ++i) {
        auto *page = bottomTabs->widget(i);
        add(QStringLiteral("ViewBottom%1").arg(bottomTitles.value(page)), bottomTitles.value(page),
            [this, page]() { showBottomPage(page); });
    }
}

void WorkbenchShell::openDocument(QWidget *document)
{
    if (!document) return;
    if (document == workbenchPages.editor) { showMibLibrary(2); return; }
    if (document == workbenchPages.results) { showResults(); return; }
    if (document == workbenchPages.watch) {
        workspaceTabs->setCurrentWidget(queryPage);
        queryTabs->setCurrentWidget(document);
        return;
    }
    if (document == workbenchPages.search) {
        if (queryTabs->indexOf(document) < 0) queryTabs->addTab(document, tr("Search"));
        queryTabs->tabBar()->show();
        workspaceTabs->setCurrentWidget(queryPage);
        queryTabs->setCurrentWidget(document);
    } else if (workspaceTitles.contains(document)) workspaceTabs->setCurrentWidget(document);
}

void WorkbenchShell::closeDocument(QWidget *document)
{
    if (!document || document != workbenchPages.search) return;
    const int i = queryTabs->indexOf(document);
    if (i >= 0) queryTabs->removeTab(i);
    queryTabs->tabBar()->setVisible(queryTabs->count() > 1);
}

void WorkbenchShell::showResults()
{
    workspaceTabs->setCurrentWidget(queryPage);
    if (workbenchPages.results) queryTabs->setCurrentWidget(workbenchPages.results);
}

void WorkbenchShell::setResultsNote(const QString &note)
{
    const int i = queryTabs->indexOf(workbenchPages.results);
    if (i >= 0) queryTabs->setTabText(i, note.isEmpty() ? tr("Results")
        : tr("Results (%1)").arg(note));
}

void WorkbenchShell::showMibLibrary(int mode)
{
    if (!workbenchPages.library) return;
    workspaceTabs->setCurrentWidget(workbenchPages.library);
    emit mibLibraryModeRequested(mode);
}

void WorkbenchShell::showBottomPage(QWidget *page)
{
    if (!bottomTitles.contains(page)) return;
    pendingPanelId.clear();
    if (page == problemPage) {
        if (!workbenchPages.library) return;
        problemsAvailable = true; // explicit navigation also permits inspecting an empty report
        mibPanelId = QStringLiteral("problems");
        workspaceTabs->setCurrentWidget(workbenchPages.library);
        updateBottomPages();
    } else {
        setBottomPageVisible(page, true);
        bottomTabs->setCurrentWidget(page);
        if (workspaceTabs->currentWidget() == workbenchPages.library) mibPanelId = bottomIds.value(page);
        else sharedPanelId = bottomIds.value(page);
    }
    setPanelExpanded(true);
}

void WorkbenchShell::setBottomPageVisible(QWidget *page, bool visible)
{
    const int i = bottomTabs->indexOf(page);
    if (i < 0) return;
    if (page != problemPage) { bottomTabs->setTabVisible(i, visible); return; }
    problemsAvailable = visible;
    if (!visible && mibPanelId == QStringLiteral("problems")) mibPanelId = QStringLiteral("output");
    // Consume the initial report once, including an empty report. Later background
    // reports never select Problems or change the current workspace/panel visibility.
    if (pendingPanelId == QStringLiteral("problems")) {
        pendingPanelId.clear();
        if (visible) mibPanelId = QStringLiteral("problems");
    }
    updateBottomPages();
}

void WorkbenchShell::updateBottomPages()
{
    const bool inMibs = workspaceTabs->currentWidget() == workbenchPages.library;
    // Qt selects a different tab when hiding the current one. That is presentation,
    // not user navigation, and must not overwrite either workspace's remembered ID.
    const QSignalBlocker blocked(bottomTabs);
    const int problemsIndex = bottomTabs->indexOf(problemPage);
    if (problemsIndex >= 0) bottomTabs->setTabVisible(problemsIndex, inMibs && problemsAvailable);
    const QString wanted = inMibs ? mibPanelId : sharedPanelId;
    int fallback = -1;
    for (int i = 0; i < bottomTabs->count(); ++i) {
        if (!bottomTabs->isTabVisible(i)) continue;
        if (fallback < 0) fallback = i;
        if (bottomIds.value(bottomTabs->widget(i)) == wanted) {
            bottomTabs->setCurrentIndex(i);
            return;
        }
    }
    if (fallback >= 0) bottomTabs->setCurrentIndex(fallback);
}

void WorkbenchShell::setBottomPageNote(QWidget *page, const QString &note)
{
    const int i = bottomTabs->indexOf(page);
    if (i >= 0) bottomTabs->setTabText(i, note.isEmpty() ? bottomTitles.value(page)
        : QStringLiteral("%1 (%2)").arg(bottomTitles.value(page), note));
}

void WorkbenchShell::setFullPageHiddenWidgets(const QList<QWidget *> &widgets)
{
    queryOnlyWidgets = widgets;
    for (auto *widget : queryOnlyWidgets) widget->setVisible(!outsideQuery);
}

void WorkbenchShell::updateWorkspace()
{
    updateBottomPages();
    const bool wanted = workspaceTabs->currentWidget() != queryPage;
    if (wanted == outsideQuery) return;
    if (wanted) inspectorShown = !inspectorPanel->isHidden();
    outsideQuery = wanted;
    inspectorPanel->setVisible(!wanted && inspectorShown);
    inspectorPanel->toggleViewAction()->setEnabled(!wanted);
    for (auto *widget : queryOnlyWidgets) widget->setVisible(!wanted);
}

void WorkbenchShell::setPanelExpanded(bool expanded)
{
    if (!expanded && !bottomPanel->isHidden()) panelHeight = bottomPanel->height();
    bottomPanel->setVisible(expanded);
    if (expanded) mainWindow->resizeDocks({bottomPanel},
        {qBound(130, panelHeight, qMax(130, mainWindow->height() - 200))}, Qt::Vertical);
    updatePanelControls();
}

void WorkbenchShell::updatePanelControls()
{
    const bool expanded = !bottomPanel->isHidden();
    const QSignalBlocker blocked(panelButton);
    panelButton->setChecked(expanded);
    const QString text = expanded ? tr("Hide panel") : tr("Show panel");
    panelButton->setText(text);
    if (auto *action = mainWindow->findChild<QAction *>("WorkbenchToggleBottom")) action->setText(text);
}

void WorkbenchShell::setActivityCounts(int running, int problems)
{
    running = qMax(0, running); problems = qMax(0, problems);
    QStringList parts;
    if (running) parts << tr("%1 running").arg(running);
    if (problems) parts << (problems == 1 ? tr("1 problem") : tr("%1 problems").arg(problems));
    activityStatus->setText(parts.isEmpty() ? tr("Idle") : parts.join(QStringLiteral(" · ")));
    activityStatus->setToolTip(tr("Running SNMP requests and MIB environment builds. Inspect Log for failed requests and MIBs > Problems for MIB errors or warnings."));
}

void WorkbenchShell::restorePresentationState()
{
    QSettings settings;
    showResults();
    headerBar->show();
    mainWindow->setCorner(Qt::BottomRightCorner, Qt::BottomDockWidgetArea);
    inspectorShown = settings.value(inspectorKey, true).toBool();
    inspectorPanel->setVisible(inspectorShown);
    const auto state = settings.value(splitterKey).toByteArray();
    if (!state.isEmpty()) contentSplitter->restoreState(state);
    panelHeight = qBound(130, settings.value(panelHeightKey, 300).toInt(), 10000);
    pendingPanelId.clear();
    // Keep old settings until normal save. The former shared Problems selection
    // becomes a MIB-only preference; Query starts on its own Output/Log selection.
    const auto validId = [](QString id) {
        if (id == QStringLiteral("results")) id = QStringLiteral("output");
        return id == QStringLiteral("output") || id == QStringLiteral("log") ||
               id == QStringLiteral("problems") ? id : QStringLiteral("output");
    };
    const QString legacy = validId(settings.value(panelTabKey, QStringLiteral("output")).toString());
    sharedPanelId = legacy == QStringLiteral("problems") ? QStringLiteral("output") : legacy;
    mibPanelId = validId(settings.value(mibPanelTabKey, legacy).toString());
    if (mibPanelId == QStringLiteral("problems") && !problemsAvailable) {
        pendingPanelId = mibPanelId;
        mibPanelId = QStringLiteral("output");
    }
    updateBottomPages();
    setPanelExpanded(settings.value(panelVisibleKey, false).toBool());
}

void WorkbenchShell::savePresentationState() const
{
    QSettings settings;
    settings.setValue(inspectorKey, outsideQuery ? inspectorShown : !inspectorPanel->isHidden());
    settings.setValue(splitterKey, contentSplitter->saveState());
    settings.setValue(panelVisibleKey, !bottomPanel->isHidden());
    settings.setValue(panelHeightKey, bottomPanel->isHidden() ? panelHeight : bottomPanel->height());
    settings.setValue(panelTabKey, sharedPanelId);
    settings.setValue(mibPanelTabKey, pendingPanelId.isEmpty() ? mibPanelId : pendingPanelId);
}
