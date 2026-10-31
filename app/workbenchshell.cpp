#include "workbenchshell.h"

#include <QAction>
#include <QActionGroup>
#include <QDockWidget>
#include <QFrame>
#include <QGridLayout>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QPushButton>
#include <QSettings>
#include <QShortcut>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyle>
#include <QTabBar>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <functional>

namespace {
constexpr auto activityKey = "workbench/activity";
constexpr auto inspectorVisibleKey = "workbench/inspector-visible";
constexpr auto bottomVisibleKey = "workbench/bottom-visible";
constexpr auto bottomTabKey = "workbench/bottom-tab";
constexpr auto contentSplitterKey = "workbench/content-splitter";

QWidget *sidebarPage(const QString &title, const QString &description, QWidget *parent)
{
    auto *page = new QWidget(parent);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(4);
    auto *heading = new QLabel(title, page);
    heading->setObjectName(QStringLiteral("WorkbenchSidebarHeading"));
    QFont font = heading->font(); font.setBold(true); heading->setFont(font);
    layout->addWidget(heading);
    if (!description.isEmpty()) {
        auto *detail = new QLabel(description, page);
        detail->setWordWrap(true);
        detail->setObjectName(QStringLiteral("WorkbenchSidebarDescription"));
        layout->addWidget(detail);
    }
    return page;
}

QPushButton *sidebarButton(QWidget *page, const QString &text, const QString &name)
{
    auto *button = new QPushButton(text, page);
    button->setObjectName(name);
    button->setFlat(true);
    button->setCheckable(true);
    button->setAutoExclusive(true);
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    if (auto *layout = qobject_cast<QVBoxLayout *>(page->layout())) layout->addWidget(button);
    return button;
}
}

WorkbenchShell::WorkbenchShell(QMainWindow *window, QWidget *centralRoot,
                               QGridLayout *centralLayout, QWidget *contextStrip,
                               QTabWidget *documents, const Pages &pages,
                               QWidget *devicePane, QWidget *inspector, QWidget *results,
                               QWidget *problems, QWidget *diagnostics, QWidget *raw,
                               QObject *parent)
    : QObject(parent), mainWindow(window), documentTabs(documents), workbenchPages(pages)
{
    documentTabs->setObjectName(QStringLiteral("WorkbenchDocuments"));
    documentTabs->tabBar()->hide();
    documentTabs->setDocumentMode(true);

    auto *rail = new QFrame(centralRoot);
    rail->setObjectName(QStringLiteral("WorkbenchActivityRail"));
    rail->setFrameShape(QFrame::NoFrame);
    rail->setFixedWidth(58);
    auto *railLayout = new QVBoxLayout(rail);
    railLayout->setContentsMargins(4, 4, 4, 4);
    railLayout->setSpacing(3);
    auto *group = new QActionGroup(this);
    group->setExclusive(true);
    addActivityAction(WorkbenchActivity::Mibs, tr("MIBs"), QKeySequence(QStringLiteral("Alt+1")));
    addActivityAction(WorkbenchActivity::Devices, tr("Devices"), QKeySequence(QStringLiteral("Alt+2")));
    addActivityAction(WorkbenchActivity::Queries, tr("Queries"), QKeySequence(QStringLiteral("Alt+3")));
    addActivityAction(WorkbenchActivity::Events, tr("Events"), QKeySequence(QStringLiteral("Alt+4")));
    const QList<QStyle::StandardPixmap> icons = {QStyle::SP_FileDialogContentsView,
        QStyle::SP_ComputerIcon, QStyle::SP_ArrowRight, QStyle::SP_MessageBoxInformation};
    for (int i = 0; i < activities.size(); ++i) {
        QAction *action = activities.at(i);
        action->setActionGroup(group);
        action->setIcon(window->style()->standardIcon(icons.at(i)));
        window->addAction(action);
        auto *button = new QToolButton(rail);
        button->setDefaultAction(action);
        button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        button->setIconSize(QSize(20, 20));
        button->setAccessibleName(action->text());
        railLayout->addWidget(button);
    }
    railLayout->addStretch();

    sidebars = new QStackedWidget(centralRoot);
    sidebars->setObjectName(QStringLiteral("WorkbenchSidebar"));
    sidebars->setMinimumWidth(180);
    sidebars->setMaximumWidth(360);
    sidebars->addWidget(makeMibSidebar());
    devicePane->setParent(sidebars);
    sidebars->addWidget(devicePane);
    sidebars->addWidget(makeQueriesSidebar());
    sidebars->addWidget(makeEventsSidebar());

    centralLayout->removeWidget(contextStrip);
    centralLayout->removeWidget(documentTabs);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setHorizontalSpacing(0);
    centralLayout->setVerticalSpacing(0);
    centralLayout->addWidget(contextStrip, 0, 0, 1, 3);
    centralLayout->addWidget(rail, 1, 0);
    auto *documentArea = new QWidget(centralRoot);
    documentArea->setObjectName(QStringLiteral("WorkbenchDocumentArea"));
    auto *documentLayout = new QVBoxLayout(documentArea);
    documentLayout->setContentsMargins(0, 0, 0, 0);
    documentLayout->setSpacing(0);
    openDocumentTabs = new QTabBar(documentArea);
    openDocumentTabs->setObjectName(QStringLiteral("WorkbenchOpenDocuments"));
    openDocumentTabs->setDocumentMode(true);
    openDocumentTabs->setTabsClosable(true);
    openDocumentTabs->setExpanding(false);
    openDocumentTabs->hide();
    documentLayout->addWidget(openDocumentTabs);
    documentLayout->addWidget(documentTabs, 1);
    connect(openDocumentTabs, &QTabBar::currentChanged, this, [this](int index) {
        if (index >= 0 && index < openedDocuments.size()) {
            const int page = documentTabs->indexOf(openedDocuments.at(index));
            if (page >= 0) documentTabs->setCurrentIndex(page);
        }
    });
    connect(openDocumentTabs, &QTabBar::tabCloseRequested, this, [this](int index) {
        if (index < 0 || index >= openedDocuments.size()) return;
        openedDocuments.removeAt(index);
        openDocumentTabs->removeTab(index);
        openDocumentTabs->setVisible(!openedDocuments.isEmpty());
        openDocument(defaultDocument(currentActivity));
    });
    contentSplitter = new QSplitter(Qt::Horizontal, centralRoot);
    contentSplitter->setObjectName(QStringLiteral("WorkbenchContentSplitter"));
    contentSplitter->setChildrenCollapsible(false);
    contentSplitter->addWidget(sidebars);
    contentSplitter->addWidget(documentArea);
    contentSplitter->setStretchFactor(0, 0);
    contentSplitter->setStretchFactor(1, 1);
    contentSplitter->setSizes({280, 900});
    centralLayout->addWidget(contentSplitter, 1, 1, 1, 2);
    centralLayout->setColumnStretch(1, 1);
    centralLayout->setRowStretch(1, 1);

    inspectorPanel = new QDockWidget(tr("Inspector"), window);
    inspectorPanel->setObjectName(QStringLiteral("WorkbenchInspectorDock"));
    inspectorPanel->setAllowedAreas(Qt::RightDockWidgetArea);
    inspectorPanel->setWidget(inspector);
    window->addDockWidget(Qt::RightDockWidgetArea, inspectorPanel);
    inspectorPanel->setMinimumWidth(240);

    bottomTabs = new QTabWidget(window);
    bottomTabs->setObjectName(QStringLiteral("WorkbenchBottomTabs"));
    bottomTabs->setDocumentMode(true);
    bottomTabs->addTab(results, tr("Results"));
    bottomTabs->addTab(problems, tr("Problems"));
    bottomTabs->addTab(diagnostics, tr("Diagnostics"));
    bottomTabs->addTab(raw, tr("Raw"));
    bottomPanel = new QDockWidget(tr("Output"), window);
    bottomPanel->setObjectName(QStringLiteral("WorkbenchBottomDock"));
    bottomPanel->setAllowedAreas(Qt::BottomDockWidgetArea);
    bottomPanel->setWidget(bottomTabs);
    window->addDockWidget(Qt::BottomDockWidgetArea, bottomPanel);
    bottomPanel->setMinimumHeight(130);

    auto *toggleInspector = new QAction(tr("Toggle Inspector"), window);
    toggleInspector->setObjectName(QStringLiteral("WorkbenchToggleInspector"));
    toggleInspector->setShortcut(QKeySequence(QStringLiteral("Ctrl+Alt+I")));
    connect(toggleInspector, &QAction::triggered, inspectorPanel, [this]() {
        const bool show = inspectorPanel->isHidden();
        if (show) {
            setActivity(WorkbenchActivity::Mibs);
            showMibTree();
        }
        inspectorPanel->setVisible(show);
    });
    window->addAction(toggleInspector);
    auto *toggleBottom = new QAction(tr("Toggle Bottom Panel"), window);
    toggleBottom->setObjectName(QStringLiteral("WorkbenchToggleBottom"));
    toggleBottom->setShortcut(QKeySequence(QStringLiteral("Ctrl+J")));
    connect(toggleBottom, &QAction::triggered, bottomPanel, [this]() {
        bottomPanel->setVisible(!bottomPanel->isVisible());
    });
    window->addAction(toggleBottom);
    const auto focusAction = [this, window](const QString &name, const QString &text,
                                            const QKeySequence &shortcut,
                                            const std::function<void()> &focus) {
        auto *action = new QAction(text, window);
        action->setObjectName(name);
        action->setShortcut(shortcut);
        connect(action, &QAction::triggered, this, focus);
        window->addAction(action);
    };
    focusAction(QStringLiteral("WorkbenchFocusSidebar"), tr("Focus Sidebar"),
                QKeySequence(QStringLiteral("Ctrl+0")), [this]() {
        if (QWidget *page = sidebars->currentWidget()) {
            const auto candidates = page->findChildren<QWidget *>();
            for (QWidget *candidate : candidates) {
                if (candidate->focusPolicy() != Qt::NoFocus && candidate->isEnabled()) {
                    candidate->setFocus(Qt::ShortcutFocusReason);
                    break;
                }
            }
        }
    });
    focusAction(QStringLiteral("WorkbenchFocusDocument"), tr("Focus Work Area"),
                QKeySequence(QStringLiteral("Ctrl+1")), [this]() {
        if (QWidget *page = documentTabs->currentWidget()) page->setFocus(Qt::ShortcutFocusReason);
    });
    focusAction(QStringLiteral("WorkbenchFocusInspector"), tr("Focus Inspector"),
                QKeySequence(QStringLiteral("Ctrl+Alt+2")), [this]() {
        setActivity(WorkbenchActivity::Mibs);
        showMibTree();
        inspectorPanel->show();
        if (inspectorPanel->widget()) inspectorPanel->widget()->setFocus(Qt::ShortcutFocusReason);
    });
    focusAction(QStringLiteral("WorkbenchFocusBottom"), tr("Focus Bottom Panel"),
                QKeySequence(QStringLiteral("Ctrl+`")), [this]() {
        bottomPanel->show();
        if (QWidget *page = bottomTabs->currentWidget()) page->setFocus(Qt::ShortcutFocusReason);
    });
    focusAction(QStringLiteral("WorkbenchGlobalSearch"), tr("Search MIBs"),
                QKeySequence(QStringLiteral("Ctrl+Shift+F")), [this]() {
        setActivity(WorkbenchActivity::Mibs);
        if (auto *filter = mainWindow->findChild<QLineEdit *>(QStringLiteral("MibTreeFilter")))
            filter->setFocus(Qt::ShortcutFocusReason);
    });

    window->setStyleSheet(window->styleSheet() + QStringLiteral(R"(
        QFrame#WorkbenchActivityRail { background: palette(midlight); border-right: 1px solid palette(mid); }
        QFrame#WorkbenchActivityRail QToolButton { border: 0; padding: 5px 1px; }
        QFrame#WorkbenchActivityRail QToolButton:checked { background: palette(highlight); color: palette(highlighted-text); border-left: 3px solid palette(highlight); }
        QStackedWidget#WorkbenchSidebar { border-right: 1px solid palette(mid); background: palette(base); }
        QLabel#WorkbenchSidebarHeading { padding: 2px 0 5px 0; }
        QLabel#WorkbenchSidebarDescription { color: palette(text); font-style: italic; padding-bottom: 5px; }
        QStackedWidget#WorkbenchSidebar QPushButton { text-align: left; padding: 5px 7px; border: 0; }
        QStackedWidget#WorkbenchSidebar QPushButton:checked { background: palette(highlight); color: palette(highlighted-text); }
    )"));
}

void WorkbenchShell::addActivityAction(WorkbenchActivity activity, const QString &text,
                                       const QKeySequence &shortcut)
{
    auto *action = new QAction(text, mainWindow);
    action->setObjectName(QStringLiteral("WorkbenchActivity%1").arg(text));
    action->setCheckable(true);
    action->setShortcut(shortcut);
    action->setToolTip(tr("Switch to %1 (%2)").arg(text, shortcut.toString()));
    connect(action, &QAction::triggered, this, [this, activity]() { setActivity(activity); });
    activities.append(action);
}

QWidget *WorkbenchShell::makeMibSidebar()
{
    QWidget *page = sidebarPage(tr("MIBs"), tr("Browse MIB knowledge and manage its runtime context."), sidebars);
    auto *tree = sidebarButton(page, tr("OID Tree"), QStringLiteral("MibModeTree"));
    auto *modules = sidebarButton(page, tr("Modules"), QStringLiteral("MibModeModules"));
    auto *catalog = sidebarButton(page, tr("Catalog"), QStringLiteral("MibModeCatalog"));
    auto *profiles = sidebarButton(page, tr("Profiles"), QStringLiteral("MibModeProfiles"));
    tree->setChecked(true);
    connect(tree, &QPushButton::clicked, this, &WorkbenchShell::showMibTree);
    connect(modules, &QPushButton::clicked, this, [this]() { showMibLibrary(0); });
    connect(catalog, &QPushButton::clicked, this, [this]() { showMibLibrary(0); });
    connect(profiles, &QPushButton::clicked, this, [this]() { showMibLibrary(1); });
    qobject_cast<QVBoxLayout *>(page->layout())->addStretch();
    return page;
}

QWidget *WorkbenchShell::makeQueriesSidebar()
{
    QWidget *page = sidebarPage(tr("Queries"),
        tr("Run an ad-hoc SNMP operation from the MIB Tree. Saved Queries arrive in a later phase."), sidebars);
    auto *operations = sidebarButton(page, tr("Ad-hoc Operations"), QStringLiteral("QueryModeAdHoc"));
    operations->setChecked(true);
    connect(operations, &QPushButton::clicked, this, [this]() { openDocument(workbenchPages.tree); });
    qobject_cast<QVBoxLayout *>(page->layout())->addStretch();
    return page;
}

QWidget *WorkbenchShell::makeEventsSidebar()
{
    QWidget *page = sidebarPage(tr("SNMP Events"),
        tr("Trap and Inform capture. Receiver settings remain in Preferences."), sidebars);
    auto *capture = sidebarButton(page, tr("Capture Console"), QStringLiteral("EventModeCapture"));
    capture->setChecked(true);
    connect(capture, &QPushButton::clicked, this, [this]() { openDocument(workbenchPages.events); });
    qobject_cast<QVBoxLayout *>(page->layout())->addStretch();
    return page;
}

QWidget *WorkbenchShell::defaultDocument(WorkbenchActivity value) const
{
    switch (value) {
    case WorkbenchActivity::Mibs: return workbenchPages.tree;
    case WorkbenchActivity::Devices: return workbenchPages.tree;
    case WorkbenchActivity::Queries: return workbenchPages.tree;
    case WorkbenchActivity::Events: return workbenchPages.events;
    }
    return workbenchPages.tree;
}

void WorkbenchShell::setActivity(WorkbenchActivity value)
{
    currentActivity = value;
    sidebars->setCurrentIndex(static_cast<int>(value));
    if (QAction *action = activityAction(value)) action->setChecked(true);
    openDocument(defaultDocument(value));
}

void WorkbenchShell::openDocument(QWidget *document)
{
    const int index = documentTabs->indexOf(document);
    if (index < 0) return;
    documentTabs->setCurrentIndex(index);
    inspectorPanel->setVisible(currentActivity == WorkbenchActivity::Mibs &&
                               document == workbenchPages.tree);
    const bool durable = document == workbenchPages.editor;
    if (!durable) {
        openDocumentTabs->setCurrentIndex(-1);
        return;
    }
    int tab = openedDocuments.indexOf(document);
    if (tab < 0) {
        openedDocuments.append(document);
        tab = openDocumentTabs->addTab(tr("MIB Editor"));
    }
    openDocumentTabs->show();
    openDocumentTabs->setCurrentIndex(tab);
}

void WorkbenchShell::showMibTree() { openDocument(workbenchPages.tree); }

void WorkbenchShell::showMibLibrary(int mode)
{
    openDocument(workbenchPages.library);
    emit mibLibraryModeRequested(mode);
}

QAction *WorkbenchShell::activityAction(WorkbenchActivity value) const
{
    const int index = static_cast<int>(value);
    return index >= 0 && index < activities.size() ? activities.at(index) : nullptr;
}

void WorkbenchShell::restorePresentationState()
{
    QSettings settings;
    const int stored = settings.value(activityKey, 0).toInt();
    const auto restored = stored >= 0 && stored <= 3
        ? static_cast<WorkbenchActivity>(stored) : WorkbenchActivity::Mibs;
    setActivity(restored);
    const bool inspectorRequested = settings.value(inspectorVisibleKey,
        restored == WorkbenchActivity::Mibs).toBool();
    inspectorPanel->setVisible(inspectorRequested && restored == WorkbenchActivity::Mibs &&
                               documentTabs->currentWidget() == workbenchPages.tree);
    bottomPanel->setVisible(settings.value(bottomVisibleKey, true).toBool());
    bottomTabs->setCurrentIndex(qBound(0, settings.value(bottomTabKey, 0).toInt(),
                                       bottomTabs->count() - 1));
    const QByteArray splitterState = settings.value(contentSplitterKey).toByteArray();
    if (!splitterState.isEmpty()) contentSplitter->restoreState(splitterState);
}

void WorkbenchShell::savePresentationState() const
{
    QSettings settings;
    settings.setValue(activityKey, static_cast<int>(currentActivity));
    // Explicit hidden state is stable even when the parent window is closing;
    // isVisible() would incorrectly persist false after the window is hidden.
    settings.setValue(inspectorVisibleKey, !inspectorPanel->isHidden());
    settings.setValue(bottomVisibleKey, !bottomPanel->isHidden());
    settings.setValue(bottomTabKey, bottomTabs->currentIndex());
    settings.setValue(contentSplitterKey, contentSplitter->saveState());
}
