#include "appearancecolors.h"
#include "miblibrarywidget.h"
#include <QPointer>

#include "diagnosticlogger.h"
#include "mibdownloadtransport.h"
#include "mibcollection.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QElapsedTimer>
#include <QInputDialog>
#include <QLabel>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QDesktopServices>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStyle>
#include <QTableWidget>
#include <QTabWidget>
#include <QTextEdit>
#include <QTimer>
#include <QPointer>
#include <QThreadPool>
#include <QTreeWidget>
#include <QDirIterator>
#include <QVBoxLayout>

namespace {
// Replacing many cells while ResizeToContents/Stretch is live repeatedly asks
// the views to lay out partially populated models. Restore sizing and painting
// once the synchronous publication is complete, including on early returns.
class ItemViewPublication
{
public:
    ItemViewPublication(QAbstractItemView *view, QHeaderView *header)
        : view(view), header(header), updatesEnabled(view->updatesEnabled())
    {
        view->setUpdatesEnabled(false);
        for (int column = 0; column < header->count(); ++column) {
            modes.append(header->sectionResizeMode(column));
            header->setSectionResizeMode(column, QHeaderView::Fixed);
        }
    }
    ~ItemViewPublication()
    {
        if (header)
            for (int column = 0; column < qMin(header->count(), int(modes.size())); ++column)
                header->setSectionResizeMode(column, modes[column]);
        if (view) view->setUpdatesEnabled(updatesEnabled);
    }
private:
    QPointer<QAbstractItemView> view;
    QPointer<QHeaderView> header;
    QList<QHeaderView::ResizeMode> modes;
    bool updatesEnabled;
};

class CheckableHeaderView : public QHeaderView
{
public:
    explicit CheckableHeaderView(QWidget *parent) : QHeaderView(Qt::Horizontal, parent)
    {
        indicator = new QCheckBox(viewport());
        indicator->setObjectName(QStringLiteral("MibLibraryHeaderSelectAll"));
        indicator->setTristate(true);
        indicator->setFocusPolicy(Qt::NoFocus);
        connect(indicator, &QCheckBox::clicked, this, [this]() {
            if (clicked) clicked();
        });
        connect(this, &QHeaderView::sectionResized, this,
                [this](int, int, int) { positionIndicator(); });
        connect(this, &QHeaderView::geometriesChanged, this,
                [this]() { positionIndicator(); });
    }
    Qt::CheckState checkState() const { return state; }
    void setCheckState(Qt::CheckState value)
    {
        setProperty("checkState", static_cast<int>(value));
        if (state != value) {
            state = value;
            const QSignalBlocker blocker(indicator);
            indicator->setCheckState(value);
        }
    }
    std::function<void()> clicked;
protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QHeaderView::resizeEvent(event);
        positionIndicator();
    }
private:
    void positionIndicator()
    {
        const QSize size = indicator->sizeHint();
        indicator->setGeometry(sectionViewportPosition(0) + 4,
            (viewport()->height() - size.height()) / 2, size.width(), size.height());
        indicator->raise();
    }
    QCheckBox *indicator = nullptr;
    Qt::CheckState state = Qt::Unchecked;
};

void appendNode(QTreeWidgetItem *parent, const MibDependencyNode &node)
{
    auto *item = new QTreeWidgetItem(parent,
        {node.moduleName, node.cycle ? QStringLiteral("Cycle") : MibLibraryStatusText(node.status)});
    for (const MibDependencyNode &child : node.dependencies) appendNode(item, child);
}

QString normalizedLineEndings(QString text)
{
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    return text;
}

QString reflowMibDisplayText(const QString &source)
{
    QStringList paragraphs;
    QStringList paragraphLines;
    const auto finishParagraph = [&]() {
        if (!paragraphLines.isEmpty()) {
            paragraphs.append(paragraphLines.join(QLatin1Char(' ')));
            paragraphLines.clear();
        }
    };
    for (const QString &line : normalizedLineEndings(source).split(
             QLatin1Char('\n'), Qt::KeepEmptyParts)) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty()) finishParagraph();
        else paragraphLines.append(trimmed);
    }
    finishParagraph();
    return paragraphs.join(QStringLiteral("\n\n")).trimmed();
}

QString normalizeContactDisplayText(const QString &source)
{
    QStringList lines;
    bool pendingBlankLine = false;
    for (QString line : normalizedLineEndings(source).split(
             QLatin1Char('\n'), Qt::KeepEmptyParts)) {
        while (line.endsWith(QLatin1Char(' ')) || line.endsWith(QLatin1Char('\t')))
            line.chop(1);
        if (line.trimmed().isEmpty()) {
            pendingBlankLine = !lines.isEmpty();
            continue;
        }
        if (pendingBlankLine) lines.append(QString());
        lines.append(line);
        pendingBlankLine = false;
    }
    return lines.join(QLatin1Char('\n'));
}

QString inventoryStatusText(MibLibraryStatus status)
{
    switch (status) {
    case MibLibraryStatus::Standards:
    case MibLibraryStatus::Installed: return QObject::tr("Ready");
    case MibLibraryStatus::Available: return QObject::tr("Available");
    case MibLibraryStatus::Downloading: return QObject::tr("Downloading");
    case MibLibraryStatus::Failed: return QObject::tr("Failed");
    case MibLibraryStatus::Invalid: return QObject::tr("Invalid");
    case MibLibraryStatus::Conflict: return QObject::tr("Conflict");
    case MibLibraryStatus::Unresolved: return QObject::tr("Missing dependency");
    }
    return QObject::tr("Unknown");
}
}

MibLibraryWidget::MibLibraryWidget(const QStringList &paths,
    QWidget *parent, MibDownloadTransport *providedTransport, Callbacks providedCallbacks)
    : QWidget(parent), callbacks(std::move(providedCallbacks)), bundledPaths(paths),
      library(),
      transport(providedTransport ? providedTransport : new QtMibDownloadTransport(this))
      , profiles(MibProfileRepository(QDir(MibCollection::legacyManagedRoot())
                     .filePath("profiles-v1.json")))
{
    setObjectName("MibLibraryWorkspace");
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    workspaceTabs = new QTabWidget(this);
    workspaceTabs->setObjectName("MibLibraryTabs");
    connect(workspaceTabs, &QTabWidget::currentChanged, this, &MibLibraryWidget::workspaceModeChanged);
    outer->addWidget(workspaceTabs);
    auto *inventoryPage = new QWidget(workspaceTabs);
    auto *layout = new QVBoxLayout(inventoryPage);
    workspaceTabs->addTab(inventoryPage, tr("Library"));
    auto *location = new QHBoxLayout;
    location->addWidget(new QLabel(tr("MIB Library Location:"), inventoryPage));
    libraryRootEdit = new QLineEdit(library.rootPath(), inventoryPage);
    libraryRootEdit->setObjectName("MibLibraryRoot"); libraryRootEdit->setReadOnly(true);
    auto *browseRoot = new QPushButton(tr("Browse..."), inventoryPage);
    browseRoot->setObjectName("MibLibraryBrowseRoot");
    auto *openRoot = new QPushButton(tr("Open MIB Folder"), inventoryPage);
    openRoot->setObjectName("MibLibraryOpenRoot");
    location->addWidget(libraryRootEdit, 1); location->addWidget(browseRoot); location->addWidget(openRoot);
    layout->addLayout(location);
    auto *organizationHint = new QLabel(
        tr("Organize product MIBs in Vendor / Product folders. Standards and Unassigned do not create automatic profiles."),
        inventoryPage);
    organizationHint->setWordWrap(true);
    layout->addWidget(organizationHint);
    auto *filters = new QHBoxLayout;
    search = new QLineEdit(this); search->setObjectName("MibLibrarySearch");
    search->setPlaceholderText(tr("Search modules"));
    search->setAccessibleName(tr("Search Library modules"));
    inventoryPage->setProperty("workbenchFocusTarget", QStringLiteral("MibLibrarySearch"));
    sourceFilter = new QComboBox(this); sourceFilter->addItem(tr("All sources"));
    statusFilter = new QComboBox(this); statusFilter->setObjectName("MibLibraryStatusFilter");
    statusFilter->addItem(tr("All states"));
    for (const QString &state : {tr("Ready"), tr("Available"), tr("Missing dependency"),
                                 tr("Conflict"), tr("Invalid"), tr("Failed")})
        statusFilter->addItem(state);
    auto *localRefresh = new QPushButton(tr("Refresh"), this);
    localRefresh->setObjectName("MibLibraryRefresh");
    auto *checkDependencies = new QPushButton(tr("Check Dependencies"), this);
    checkDependencies->setObjectName("MibLibraryCheckDependencies");
    checkDependencies->setToolTip(tr("Check dependencies across the entire Library. Checked rows control the dependency preview."));
    refreshButton = new QPushButton(tr("Refresh Catalog"), this);
    refreshButton->setObjectName("MibLibraryRefreshCatalog");
    cancelButton = new QPushButton(tr("Cancel"), this);
    cancelButton->setObjectName("MibLibraryCancel"); cancelButton->hide();
    // Every filter control must be in the layout: an unplaced child is drawn at the widget's
    // top-left corner, over the Library / Profiles tab names.
    filters->addWidget(search, 1); filters->addWidget(sourceFilter); filters->addWidget(statusFilter);
    filters->addWidget(localRefresh);
    filters->addWidget(checkDependencies);
    auto *getIana = new QPushButton(tr("Get MIBs from IANA…"), inventoryPage);
    getIana->setObjectName("MibLibraryGetIana");
    filters->addWidget(getIana);
    layout->addLayout(filters);
    table = new QTableWidget(this); table->setObjectName("MibLibraryTable");
    table->setColumnCount(6); table->setHorizontalHeaderLabels(
        {QString(), tr("Module"), tr("Status"), tr("Active Profile"), tr("Revision"), tr("Origin")});
    auto *checkHeader = new CheckableHeaderView(table);
    checkHeader->setObjectName("MibLibraryCheckableHeader");
    checkHeader->setProperty("indicatorAlignment", QStringLiteral("left"));
    checkHeader->setToolTip(tr("Select all filtered modules"));
    table->setHorizontalHeader(checkHeader);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    auto *selection = new QHBoxLayout;
    selectedCount = new QLabel(tr("0 selected for download"), this);
    selectedCount->setObjectName("MibLibrarySelectedCount");
    selectedCount->setToolTip(tr("Checked modules appear on the Dependencies preview tab. Check Dependencies validates the entire Library."));
    downloadButton = new QPushButton(tr("Download to Library"), this);
    downloadButton->setObjectName("MibLibraryDownload");
    installDependenciesButton = new QPushButton(tr("Download and Add to Active Profile"), this);
    installDependenciesButton->setObjectName("MibLibraryInstallWithDependencies");
    selection->addWidget(selectedCount); selection->addStretch();
    auto *importButton = new QPushButton(tr("Import Files..."), inventoryPage);
    importButton->setObjectName("MibLibraryImportFiles");
    selection->addWidget(importButton);

    // Catalog browsing and download selection are a separate projection of the
    // shared catalog, not another Library/index or active-profile selector.
    ianaDialog = new QDialog(this);
    ianaDialog->setObjectName("IanaMibDialog");
    ianaDialog->setWindowTitle(tr("Get MIBs from IANA")); ianaDialog->resize(880, 620);
    auto *ianaLayout = new QVBoxLayout(ianaDialog);
    auto *ianaHint = new QLabel(tr("Choose IANA modules to download. Required modules are included when a provider is available. Downloads preserve different-content files already in your Library."), ianaDialog);
    ianaHint->setWordWrap(true); ianaLayout->addWidget(ianaHint);
    auto *ianaTools = new QHBoxLayout;
    ianaSearch = new QLineEdit(ianaDialog); ianaSearch->setObjectName("IanaCatalogSearch");
    ianaSearch->setPlaceholderText(tr("Filter IANA modules…")); ianaSearch->setClearButtonEnabled(true);
    ianaSearch->setAccessibleName(tr("Filter IANA modules"));
    ianaTools->addWidget(ianaSearch, 1);
    for (auto *control : {refreshButton, cancelButton}) ianaTools->addWidget(control);
    ianaLayout->addLayout(ianaTools);
    ianaTable = new QTableWidget(0, 2, ianaDialog); ianaTable->setObjectName("IanaCatalogTable");
    ianaTable->setHorizontalHeaderLabels({tr("Module"), tr("Library state")});
    ianaTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ianaTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    ianaTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
    ianaTable->setAlternatingRowColors(true); ianaTable->verticalHeader()->hide();
    ianaTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    ianaTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    ianaLayout->addWidget(ianaTable, 1);
    ianaSelectedCount = new QLabel(ianaDialog); ianaSelectedCount->setObjectName("IanaSelectedCount");
    auto *ianaActions = new QHBoxLayout;
    ianaActions->addWidget(ianaSelectedCount, 1);
    ianaActions->addWidget(downloadButton); ianaActions->addWidget(installDependenciesButton);
    ianaLayout->addLayout(ianaActions);
    ianaStatus = new QLabel(ianaDialog); ianaStatus->setObjectName("IanaStatus");
    ianaStatus->setWordWrap(true); ianaLayout->addWidget(ianaStatus);
    auto *ianaClose = new QDialogButtonBox(QDialogButtonBox::Close, ianaDialog);
    connect(ianaClose, &QDialogButtonBox::rejected, ianaDialog, &QDialog::reject);
    ianaLayout->addWidget(ianaClose);
    connect(getIana, &QPushButton::clicked, this, [this] {
        refreshIanaCatalog(); ianaDialog->show(); ianaDialog->raise(); ianaDialog->activateWindow();
    });
    connect(ianaSearch, &QLineEdit::textChanged, this, [this] { filterIanaCatalog(); });
    connect(ianaTable, &QTableWidget::itemSelectionChanged, this, [this] { updateIanaActions(); });
    auto *tablePane = new QWidget(inventoryPage);
    tablePane->setObjectName("MibLibraryInventoryTablePane");
    auto *tableLayout = new QVBoxLayout(tablePane);
    tableLayout->setContentsMargins(0, 0, 0, 0);
    tableLayout->addWidget(table, 1);
    libraryEmptyState = new QLabel(
        tr("No MIB modules found. Import MIB files or refresh the configured Library folder."), tablePane);
    libraryEmptyState->setObjectName("MibLibraryEmptyState");
    libraryEmptyState->setWordWrap(true); libraryEmptyState->hide();
    tableLayout->addWidget(libraryEmptyState);
    tableLayout->addLayout(selection);
    detailsTabs = new QTabWidget(this);
    detailsTabs->setObjectName("MibLibraryDetailsTabs");
    auto *infoPage = new QWidget(detailsTabs);
    auto *infoLayout = new QVBoxLayout(infoPage);
    const auto addFields = [this, infoPage](QFormLayout *form,
                                            const QList<QPair<QString, QString>> &fields) {
        for (const auto &field : fields) {
            auto *valueLabel = new QLabel(infoPage);
            valueLabel->setTextFormat(Qt::PlainText);
            valueLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
            valueLabel->setObjectName(QStringLiteral("MibLibraryInfo_%1").arg(field.first));
            valueLabel->setWordWrap(true); infoValues.insert(field.first, valueLabel);
            form->addRow(field.second + QLatin1Char(':'), valueLabel);
            infoLabels.insert(field.first, form->labelForField(valueLabel));
        }
    };
    auto *moduleGroup = new QGroupBox(tr("Module Information"), infoPage);
    moduleGroup->setObjectName("MibLibraryModuleInformation");
    auto *moduleForm = new QFormLayout(moduleGroup);
    addFields(moduleForm, {{"module", tr("Module")}, {"moduleOid", tr("Module OID")},
        {"lastUpdated", tr("Last Updated")}, {"organization", tr("Organization")},
        {"description", tr("Description")}, {"contact", tr("Contact Information")},
        {"reference", tr("Reference")}, {"revisionHistory", tr("Revision History")}});
    QLabel *oldContact = infoValues.take("contact");
    QWidget *contactLabel = infoLabels.value("contact");
    moduleForm->removeWidget(oldContact); oldContact->deleteLater();
    contactInfo = new QTextEdit(infoPage);
    contactInfo->setObjectName("MibLibraryInfo_contact");
    contactInfo->setReadOnly(true);
    contactInfo->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    contactInfo->setLineWrapMode(QTextEdit::WidgetWidth);
    contactInfo->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    contactInfo->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    contactInfo->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    const int contactHeight = contactInfo->fontMetrics().lineSpacing() * 10 +
        contactInfo->frameWidth() * 2 + 8;
    contactInfo->setMaximumHeight(contactHeight);
    contactInfo->setMinimumHeight(contactInfo->fontMetrics().lineSpacing() * 3);
    moduleForm->addRow(contactLabel, contactInfo);
    auto *fileGroup = new QGroupBox(tr("File Information"), infoPage);
    fileGroup->setObjectName("MibLibraryFileInformation");
    auto *fileForm = new QFormLayout(fileGroup);
    addFields(fileForm, {{"origin", tr("Origin")}, {"revision", tr("Catalog revision")},
        {"filename", tr("Filename")}, {"path", tr("Local path")},
        {"provider", tr("Source / provider")}, {"url", tr("Source URL")},
        {"timestamp", tr("Installed / downloaded")}, {"sha", tr("SHA-256")},
        {"state", tr("State")}});
    moduleGroup->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    fileGroup->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    auto *moduleScroll = new QScrollArea(infoPage);
    moduleScroll->setObjectName("MibLibraryModuleInformationScroll");
    moduleScroll->setWidgetResizable(true);
    moduleScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    moduleScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    moduleScroll->setWidget(moduleGroup);
    auto *fileScroll = new QScrollArea(infoPage);
    fileScroll->setObjectName("MibLibraryFileInformationScroll");
    fileScroll->setWidgetResizable(true);
    fileScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    fileScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    fileScroll->setWidget(fileGroup);
    auto *informationSplitter = new QSplitter(Qt::Horizontal, infoPage);
    informationSplitter->setObjectName("MibLibraryInformationSplitter");
    informationSplitter->setChildrenCollapsible(false);
    informationSplitter->addWidget(moduleScroll);
    informationSplitter->addWidget(fileScroll);
    informationSplitter->setStretchFactor(0, 13);
    informationSplitter->setStretchFactor(1, 7);
    QTimer::singleShot(0, informationSplitter, [informationSplitter]() {
        const int width = informationSplitter->width();
        if (width > 0) informationSplitter->setSizes({width * 13, width * 7});
    });
    infoLayout->addWidget(informationSplitter, 1);
    openEditorButton = new QPushButton(tr("Open in Editor"), infoPage);
    openEditorButton->setObjectName("MibLibraryOpenInEditor");
    infoLayout->addWidget(openEditorButton, 0, Qt::AlignLeft); infoLayout->addStretch();
    dependencies = new QTreeWidget(this); dependencies->setObjectName("MibDependencyTree");
    dependencies->setHeaderLabels({tr("Dependency"), tr("Status")});
    auto *dependencyHeader = dependencies->header();
    dependencyHeader->setObjectName("MibDependencyHeader");
    dependencyHeader->setStretchLastSection(false);
    dependencyHeader->setMinimumSectionSize(120);
    dependencyHeader->setSectionResizeMode(0, QHeaderView::Stretch);
    dependencyHeader->setSectionResizeMode(1, QHeaderView::Interactive);
    const QString dependencyHeaderKey = QStringLiteral("mib-library/dependencies-status-width");
    const QVariant savedDependencyWidth = QSettings().value(dependencyHeaderKey);
    const int restoredDependencyWidth = savedDependencyWidth.toInt();
    connect(dependencyHeader, &QHeaderView::sectionResized, this,
            [dependencyHeaderKey](int logicalIndex, int, int newSize) {
        if (logicalIndex == 1) QSettings().setValue(dependencyHeaderKey, newSize);
    });
    detailsTabs->addTab(infoPage, tr("Info")); detailsTabs->addTab(dependencies, tr("Dependencies"));
    const auto applyDependencyWidth = [this, dependencyHeader, restoredDependencyWidth]() {
        if (dependencyHeader->property("statusWidthApplied").toBool()) return;
        const int width = dependencies->viewport()->width();
        if (width > 0) {
            dependencyHeader->setProperty("statusWidthApplied", true);
            dependencyHeader->resizeSection(
                1, restoredDependencyWidth >= 120 ? restoredDependencyWidth : qMax(120, width / 4));
        }
    };
    connect(detailsTabs, &QTabWidget::currentChanged, this,
            [applyDependencyWidth](int index) {
        if (index == 1) QTimer::singleShot(0, applyDependencyWidth);
    });
    auto *inventorySplitter = new QSplitter(Qt::Vertical, inventoryPage);
    inventorySplitter->setObjectName("MibLibraryInventoryDetailsSplitter");
    inventorySplitter->setChildrenCollapsible(false);
    tablePane->setMinimumHeight(140);
    detailsTabs->setMinimumHeight(160);
    inventorySplitter->addWidget(tablePane);
    inventorySplitter->addWidget(detailsTabs);
    inventorySplitter->setStretchFactor(0, 11);
    inventorySplitter->setStretchFactor(1, 9);
    const QString inventorySplitterKey = QStringLiteral("mib-library/inventory-details-splitter");
    const QByteArray inventorySplitterState = QSettings().value(inventorySplitterKey).toByteArray();
    const bool inventorySplitterRestored = !inventorySplitterState.isEmpty() &&
        inventorySplitter->restoreState(inventorySplitterState);
    connect(inventorySplitter, &QSplitter::splitterMoved, this,
            [inventorySplitter, inventorySplitterKey]() {
        QSettings().setValue(inventorySplitterKey, inventorySplitter->saveState());
    });
    if (!inventorySplitterRestored) {
        QTimer::singleShot(0, inventorySplitter, [inventorySplitter]() {
            const int height = inventorySplitter->height();
            if (height > 0) inventorySplitter->setSizes({height * 11, height * 9});
        });
    }
    layout->addWidget(inventorySplitter, 1);
    status = new QLabel(tr("Ready"), this); status->setObjectName("MibLibraryStatus");
    status->setWordWrap(true); layout->addWidget(status);

    auto *profilePage = new QWidget(workspaceTabs);
    auto *profileLayout = new QVBoxLayout(profilePage);
    auto *profileActions = new QHBoxLayout;
    profileCombo = new QComboBox(profilePage); profileCombo->setObjectName("MibProfileEditorSelector");
    profileCombo->setToolTip(tr("Choose a profile to inspect or edit. Query's active MIB Profile is selected in the connection bar."));
    auto *newProfile = new QPushButton(tr("New Profile"), profilePage);
    auto *duplicateProfileButton = new QPushButton(tr("Duplicate"), profilePage);
    renameProfileButton = new QPushButton(tr("Rename"), profilePage);
    deleteProfileButton = new QPushButton(tr("Delete"), profilePage);
    auto *activeProfileLabel = new QLabel(tr("Edit profile"), profilePage);
    activeProfileLabel->setObjectName("MibProfileEditorLabel");
    activeProfileLabel->setBuddy(profileCombo);
    profileCombo->setAccessibleName(tr("Edit profile"));
    profilePage->setProperty("workbenchFocusTarget", QStringLiteral("MibProfileEditorSelector"));
    profileActions->addWidget(activeProfileLabel); profileActions->addWidget(profileCombo, 1);
    for (QPushButton *button : {newProfile, duplicateProfileButton, renameProfileButton, deleteProfileButton})
        profileActions->addWidget(button);
    profileLayout->addLayout(profileActions);
    auto *profileDetails = new QHBoxLayout;
    profileSource = new QLabel(profilePage); profileSource->setObjectName("MibProfileSource");
    profileSource->setTextInteractionFlags(Qt::TextSelectableByMouse);
    openProfileFolderButton = new QPushButton(tr("Open Folder"), profilePage);
    openProfileFolderButton->setObjectName("MibProfileOpenFolder");
    profileDetails->addWidget(profileSource, 1); profileDetails->addWidget(openProfileFolderButton);
    profileLayout->addLayout(profileDetails);
    profileGuidance = new QLabel(profilePage);
    profileGuidance->setObjectName("MibProfileGuidance");
    profileGuidance->setWordWrap(true); profileLayout->addWidget(profileGuidance);
    auto *profileSplitter = new QSplitter(profilePage);
    auto *availablePane = new QWidget(profileSplitter); auto *availableLayout = new QVBoxLayout(availablePane);
    availableLayout->addWidget(new QLabel(tr("MIB Library folders"), availablePane));
    availableSearch = new QLineEdit(availablePane); availableSearch->setObjectName("MibProfileAvailableFilter");
    availableSearch->setPlaceholderText(tr("Filter folders, files and modules"));
    availableTree = new QTreeWidget(availablePane); availableTree->setObjectName("MibProfileAvailableTree");
    availableTree->setColumnCount(2);
    availableTree->setHeaderLabels({tr("Folder or file"), tr("In this profile")});
    availableTree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    availableTree->setToolTip(tr("Select folders or files and press Add. A folder is followed: MIBs added "
                                 "to it or removed from it later change the profile."));
    availableTree->header()->setStretchLastSection(false);
    availableTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    availableTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    availableLayout->addWidget(availableSearch); availableLayout->addWidget(availableTree);
    auto *movePane = new QWidget(profileSplitter); auto *moveLayout = new QVBoxLayout(movePane);
    moveLayout->addStretch(); addMemberButton = new QPushButton(tr("Add >"), movePane);
    removeMemberButton = new QPushButton(tr("< Remove"), movePane);
    moveLayout->addWidget(addMemberButton); moveLayout->addWidget(removeMemberButton); moveLayout->addStretch();
    auto *memberPane = new QWidget(profileSplitter); auto *memberLayout = new QVBoxLayout(memberPane);
    memberLayout->addWidget(new QLabel(tr("In this profile"), memberPane));
    memberSearch = new QLineEdit(memberPane); memberSearch->setObjectName("MibProfileMemberFilter");
    memberSearch->setPlaceholderText(tr("Filter profile"));
    memberTree = new QTreeWidget(memberPane); memberTree->setObjectName("MibProfileMemberTree");
    memberTree->setItemDelegate(new AppearanceColors::ItemDelegate(memberTree));
    memberTree->setColumnCount(3);
    memberTree->setHeaderLabels({tr("Folder or file"), tr("Modules"), tr("Status")});
    memberTree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    memberTree->setToolTip(tr("Remove takes a followed folder or a single file out of the profile. On a file "
                              "inside a followed folder it leaves just that file out (Excluded)."));
    includeStandards = new QCheckBox(tr("Include standard SNMP / MIB-II base"), memberPane);
    includeStandards->setObjectName("MibProfileIncludeStandards");
    includeStandards->hide();
    memberTree->header()->setStretchLastSection(false);
    memberTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    memberTree->header()->setSectionResizeMode(1, QHeaderView::Interactive);
    memberTree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    memberTree->header()->resizeSection(1, 160);
    memberLayout->addWidget(memberSearch); memberLayout->addWidget(memberTree, 2); memberLayout->addWidget(includeStandards);
    profileEmptyState = new QLabel(memberPane);
    profileEmptyState->setObjectName("MibProfileEmptyState");
    profileEmptyState->setWordWrap(true); profileEmptyState->hide();
    memberLayout->addWidget(profileEmptyState);
    auto *requiredLabel = new QLabel(tr("Required Dependencies"), memberPane);
    memberLayout->addWidget(requiredLabel);
    requiredList = new QListWidget(memberPane); requiredList->setObjectName("MibProfileRequiredDependencies");
    requiredList->setSelectionMode(QAbstractItemView::NoSelection);
    memberLayout->addWidget(requiredList);
    dependencyCheckSummary = new QLabel(memberPane);
    dependencyCheckSummary->setObjectName("MibProfileDependencySummary");
    dependencyCheckSummary->setWordWrap(true); memberLayout->addWidget(dependencyCheckSummary);
    profileSplitter->addWidget(availablePane); profileSplitter->addWidget(movePane); profileSplitter->addWidget(memberPane);
    profileSplitter->setStretchFactor(0, 1); profileSplitter->setStretchFactor(2, 1);
    profileLayout->addWidget(profileSplitter, 1);   // the folder lists get the page's spare height
    workspaceTabs->addTab(profilePage, tr("Profiles"));
    connect(search, &QLineEdit::textChanged, this, &MibLibraryWidget::applyFilter);
    connect(sourceFilter, &QComboBox::currentTextChanged, this, &MibLibraryWidget::applyFilter);
    connect(statusFilter, &QComboBox::currentTextChanged, this, &MibLibraryWidget::applyFilter);
    checkHeader->clicked = [this, checkHeader]() {
        setFilteredSelection(checkHeader->checkState() != Qt::Checked);
    };
    connect(refreshButton, &QPushButton::clicked, this, &MibLibraryWidget::refreshCatalog);
    connect(statusFilter, &QComboBox::currentIndexChanged, this, &MibLibraryWidget::applyFilter);
    connect(localRefresh, &QPushButton::clicked, this, [this]() {
        if (callbacks.collectionChanged) callbacks.collectionChanged();
        QString migrationError;
        if (callbacks.retryLegacyProfiles &&
            !callbacks.retryLegacyProfiles(profiles, &migrationError) &&
            !migrationError.isEmpty()) setStatusText(migrationError);
        refresh();
    });
    connect(checkDependencies, &QPushButton::clicked, this, [this, checkDependencies, localRefresh, browseRoot, importButton]() {
        if (!callbacks.checkDependencies || dependencyCheckRunning ||
            catalogRefreshInProgress || !downloadQueue.isEmpty()) return;
        dependencyCheckRunning = true;
        emit activityChanged();
        checkDependencies->setEnabled(false);
        localRefresh->setEnabled(false);
        browseRoot->setEnabled(false);
        importButton->setEnabled(false);
        refreshButton->setEnabled(false);
        updateSelectionUi();
        checkDependencies->setText(tr("Checking…"));
        setStatusText(tr("Checking dependencies across the entire Library… You can keep browsing."));
        QPointer<MibLibraryWidget> guard(this);
        const MibLibraryService librarySnapshot = library;
        const QStringList pathsSnapshot = bundledPaths;
        const MibCatalog catalogSnapshot = catalog;
        auto prepared = std::make_shared<QList<MibLibraryRecord>>();
        callbacks.checkDependencies(
            [prepared, librarySnapshot, pathsSnapshot, catalogSnapshot](const QList<MibModuleRecord> &localRecords) {
                *prepared = librarySnapshot.inventory(pathsSnapshot, catalogSnapshot, localRecords);
            }, [guard, prepared, checkDependencies, localRefresh, browseRoot, importButton](const QString &error) {
            if (!guard) return;
            guard->dependencyCheckRunning = false;
            emit guard->activityChanged();
            checkDependencies->setEnabled(true);
            checkDependencies->setText(tr("Check Dependencies"));
            localRefresh->setEnabled(true);
            browseRoot->setEnabled(true);
            importButton->setEnabled(true);
            guard->refreshButton->setEnabled(true);
            guard->updateSelectionUi();
            guard->refreshImpl(true, prepared.get());
            guard->setStatusText(error.isEmpty() ? tr("Library dependency check complete") : error);
        });
    });
    connect(browseRoot, &QPushButton::clicked, this, &MibLibraryWidget::browseLibraryRoot);
    connect(openRoot, &QPushButton::clicked, this, &MibLibraryWidget::openLibraryRoot);
    connect(importButton, &QPushButton::clicked, this, &MibLibraryWidget::importFiles);
    connect(openProfileFolderButton, &QPushButton::clicked, this, &MibLibraryWidget::openProfileFolder);
    connect(cancelButton, &QPushButton::clicked, transport, &MibDownloadTransport::cancel);
    connect(downloadButton, &QPushButton::clicked, this, [this]() { downloadSelected(false); });
    connect(installDependenciesButton, &QPushButton::clicked, this, [this]() { downloadSelected(true); });
    connect(table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *item) {
        if (initialPublishing) return;
        if (item && item->column() == 0) { updateSelectionUi(); resolveSelected(); }
    });
    connect(openEditorButton, &QPushButton::clicked, this, &MibLibraryWidget::openCurrentModule);
    connect(table, &QTableWidget::cellDoubleClicked, this, [this](int, int) { openCurrentModule(); });
    connect(profileCombo, &QComboBox::currentIndexChanged, this, &MibLibraryWidget::profileChanged);
    connect(newProfile, &QPushButton::clicked, this, &MibLibraryWidget::createProfile);
    connect(duplicateProfileButton, &QPushButton::clicked, this, &MibLibraryWidget::duplicateProfile);
    connect(renameProfileButton, &QPushButton::clicked, this, &MibLibraryWidget::renameProfile);
    connect(deleteProfileButton, &QPushButton::clicked, this, &MibLibraryWidget::deleteProfile);
    connect(addMemberButton, &QPushButton::clicked, this, &MibLibraryWidget::addProfileMembers);
    connect(removeMemberButton, &QPushButton::clicked, this, &MibLibraryWidget::removeProfileMembers);
    connect(includeStandards, &QCheckBox::toggled, this, [this]() { saveCurrentProfile(); });
    // A filter shows matching items and the folders that lead to them.
    const auto filterTree = [](QTreeWidget *tree, const QString &text) {
        std::function<bool(QTreeWidgetItem *)> apply = [&](QTreeWidgetItem *item) {
            bool childMatches = false;
            for (int i = 0; i < item->childCount(); ++i) childMatches = apply(item->child(i)) || childMatches;
            bool matches = text.isEmpty();
            for (int column = 0; !matches && column < tree->columnCount(); ++column)
                matches = item->text(column).contains(text, Qt::CaseInsensitive);
            item->setHidden(!matches && !childMatches);
            if (!text.isEmpty() && childMatches) item->setExpanded(true);
            return matches || childMatches;
        };
        for (int i = 0; i < tree->topLevelItemCount(); ++i) apply(tree->topLevelItem(i));
    };
    connect(availableSearch, &QLineEdit::textChanged, this,
            [this, filterTree](const QString &text) { filterTree(availableTree, text); });
    connect(memberSearch, &QLineEdit::textChanged, this,
            [this, filterTree](const QString &text) { filterTree(memberTree, text); });
    connect(table, &QTableWidget::currentCellChanged, this,
            [this](int row, int, int, int) {
        Q_UNUSED(row); showCurrentInfo();
    });
    connect(transport, &MibDownloadTransport::finished, this, [this](const MibDownloadResult &result) {
        if (catalogRefreshInProgress) {
            catalogRefreshInProgress = false;
            setOperationActive(false);
            QString error; MibCatalog refreshed;
            if (!result.error.isEmpty() ||
                !IanaMibSourceProvider::parseIndex(result.content, &refreshed, &error)) {
                sourceStatusText = tr("IANA refresh failed — using cached catalog: %1")
                    .arg(result.error.isEmpty() ? error : result.error);
                refresh(); DiagnosticLogger::log("MIB", sourceStatusText); return;
            }
            MibCatalogCacheInfo info{QDateTime::currentDateTimeUtc(),
                IanaMibSourceProvider::id(), IanaMibSourceProvider::indexUrl().toString()};
            if (!MibCatalogCache::save(catalogCachePath(), refreshed, info, &error)) {
                sourceStatusText = tr("IANA refresh could not be cached — using prior catalog: %1").arg(error);
                refresh(); DiagnosticLogger::log("MIB", sourceStatusText); return;
            }
            catalog = refreshed;
            sourceStatusText = tr("IANA current — refreshed %1 — %n modules", nullptr,
                                  catalog.entries().size())
                .arg(info.refreshedAt.toLocalTime().toString(Qt::ISODate));
            refresh();
            setIanaStatusText(sourceStatusText);
            return;
        }
        const QString module = downloadQueue.isEmpty() ? QString() : downloadQueue.takeFirst();
        const MibCatalogEntry *found = catalog.find(module); QString error;
        MibCatalogEntry entry = found ? *found : MibCatalogEntry{};
        const auto scanned = MibImportScanner::scan(result.content);
        entry.imports = scanned.imports;
        if (!scanned.revision.isEmpty()) entry.revision = scanned.revision;
        if (!found || !result.error.isEmpty() ||
            !library.install(entry, result.content, bundledPaths, nullptr, &error,
                callbacks.validate)) {
            const QString reason = result.error.isEmpty() ? error : result.error;
            sessionState.fail(module, reason);
            setIanaStatusText(tr("%1 failed\nStatus: Failed\nReason: %2")
                            .arg(module, reason));
            DiagnosticLogger::log("MIB", tr("Download failed module=%1 initial=%2 http=%3 final=%4 reason=%5 redirects=%6")
                .arg(module, result.initialUrl.toString(QUrl::FullyEncoded))
                .arg(result.httpStatus)
                .arg(result.finalUrl.toString(QUrl::FullyEncoded), reason,
                     result.redirectTrace.join(QStringLiteral(" | "))));
            const QString visibleFailure = status->text();
            downloadQueue.clear();
            setOperationActive(false); refresh();
            setIanaStatusText(visibleFailure); return;
        }
        sessionState.succeed(module);
        ++completedDownloads;
        catalog.upsert(entry);
        DiagnosticLogger::log("MIB", tr("Installed module=%1 source=%2 initial=%3 final=%4 http=%5 redirects=%6")
            .arg(module, entry.sourceName,
                 result.initialUrl.toString(QUrl::FullyEncoded),
                 result.finalUrl.toString(QUrl::FullyEncoded))
            .arg(result.httpStatus)
            .arg(result.redirectTrace.join(QStringLiteral(" | "))));
        refresh();
        QMap<QString, MibLibraryStatus> known;
        for (const auto &record : records) known.insert(record.moduleName, record.status);
        downloadQueue = MibDependencyResolver().resolve(requestedTargets, known, catalog).orderedDownloads;
        downloadNext();
    });
    loadCachedCatalog();
    reloadProfiles();
    setStatusText(tr("Catalog loading…"));
    setOperationActive(true);
}

void MibLibraryWidget::beginInitialRefresh(quint64 generation)
{
    if (initialReady) { emit initialRefreshFinished(0, 0); return; }
    initialGeneration = generation;
    const QList<MibModuleRecord> localRecords = callbacks.localInventory
        ? callbacks.localInventory() : QList<MibModuleRecord>{};
    const MibLibraryService librarySnapshot = library;
    const QStringList pathsSnapshot = bundledPaths;
    const MibCatalog catalogSnapshot = catalog;
    const QPointer<MibLibraryWidget> context(this);
    QThreadPool::globalInstance()->start(
        [context, generation, librarySnapshot, pathsSnapshot, catalogSnapshot,
         localRecords]() mutable {
        QElapsedTimer timer;
        timer.start();
        MibCollection(librarySnapshot.rootPath()).initialize(pathsSnapshot);
        QList<MibLibraryRecord> prepared = librarySnapshot.inventory(
            pathsSnapshot, catalogSnapshot, localRecords);
        const qint64 elapsed = timer.elapsed();
        QMetaObject::invokeMethod(context,
            [context, generation, prepared = std::move(prepared), elapsed]() mutable {
            if (!context || context->initialGeneration != generation) return;
            context->records = prepared;
            context->pendingInitialRecords = std::move(prepared);
            context->pendingInitialSources = {context->tr("All origins")};
            context->pendingInitialRow = 0;
            context->initialPreparationMs = elapsed;
            context->initialPublicationTimer.start();
            context->initialPublishing = true;
            context->table->setUpdatesEnabled(false);
            context->table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
            context->table->setRowCount(context->pendingInitialRecords.size());
            context->publishInitialChunk(generation);
        }, Qt::QueuedConnection);
    });
}

void MibLibraryWidget::publishRecord(int row, const MibLibraryRecord &record,
                                     const QSet<QString> &selectedBefore)
{
    auto *checkItem = new QTableWidgetItem;
    checkItem->setCheckState(selectedBefore.contains(record.moduleName)
        ? Qt::Checked : Qt::Unchecked);
    checkItem->setFlags((checkItem->flags() | Qt::ItemIsUserCheckable) & ~Qt::ItemIsEditable);
    auto *moduleItem = new QTableWidgetItem(record.moduleName);
    const QString failure = sessionState.failureReason(record.moduleName);
    if (!failure.isEmpty()) {
        moduleItem->setIcon(style()->standardIcon(QStyle::SP_MessageBoxCritical));
        moduleItem->setToolTip(tr("Failed: %1").arg(failure));
    }
    const MibLibraryStatus displayedStatus = sessionState.status(record.moduleName, record.status);
    auto *statusItem = new QTableWidgetItem(inventoryStatusText(displayedStatus));
    auto *activeItem = new QTableWidgetItem(tr("Loading"));
    if (displayedStatus == MibLibraryStatus::Unresolved ||
        displayedStatus == MibLibraryStatus::Conflict ||
        displayedStatus == MibLibraryStatus::Invalid ||
        displayedStatus == MibLibraryStatus::Failed)
        statusItem->setIcon(style()->standardIcon(QStyle::SP_MessageBoxWarning));
    auto *revisionItem = new QTableWidgetItem(record.revision);
    auto *originItem = new QTableWidgetItem(originText(record));
    for (QTableWidgetItem *item : {moduleItem, statusItem, activeItem, revisionItem, originItem})
        item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    table->setItem(row, 0, checkItem);
    table->setItem(row, 1, moduleItem);
    table->setItem(row, 2, statusItem);
    table->setItem(row, 3, activeItem);
    table->setItem(row, 4, revisionItem);
    table->setItem(row, 5, originItem);
}

void MibLibraryWidget::publishInitialChunk(quint64 generation)
{
    if (initialGeneration != generation) return;
    const int end = qMin(pendingInitialRow + 24, pendingInitialRecords.size());
    for (; pendingInitialRow < end; ++pendingInitialRow) {
        const MibLibraryRecord &record = pendingInitialRecords[pendingInitialRow];
        publishRecord(pendingInitialRow, record);
        const QString origin = originText(record);
        if (!pendingInitialSources.contains(origin)) pendingInitialSources.append(origin);
    }
    if (pendingInitialRow < pendingInitialRecords.size()) {
        QTimer::singleShot(0, this, [this, generation]() { publishInitialChunk(generation); });
        return;
    }
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table->setUpdatesEnabled(true);
    initialPublishing = false;
    sourceFilter->clear();
    sourceFilter->addItems(pendingInitialSources);
    applyFilter();
    setStatusText(tr("%n modules in library", nullptr, records.size()));
    setOperationActive(false);
    pendingInitialRecords.clear();
    pendingInitialSources.clear();
    initialReady = true;
    ++counters.inventoryBuilds;
    ++counters.profilePopulations;
    showCurrentInfo();
    updateSelectionUi();
    emit initialRefreshFinished(initialPreparationMs, initialPublicationTimer.elapsed());
}

void MibLibraryWidget::setEditorPage(QWidget *page)
{
    if (page && workspaceTabs->indexOf(page) < 0) workspaceTabs->addTab(page, tr("Editor"));
}

void MibLibraryWidget::showWorkspaceMode(int mode)
{
    workspaceTabs->setCurrentIndex(qBound(0, mode, workspaceTabs->count() - 1));
    activate();
}

void MibLibraryWidget::loadCachedCatalog()
{
    QString error; MibCatalog parsed; MibCatalogCacheInfo info;
    if (MibCatalogCache::load(catalogCachePath(), &parsed, &info, &error)) {
        catalog = parsed;
        if (info.refreshedAt.isValid()) sourceStatusText = tr("IANA cached — refreshed %1")
            .arg(info.refreshedAt.toLocalTime().toString(Qt::ISODate));
    }
}

QString MibLibraryWidget::catalogCachePath() const
{
    return QDir(MibCollection::legacyManagedRoot()).filePath("cache/catalog-v1.json");
}

void MibLibraryWidget::refreshCatalog()
{
    if (catalogRefreshInProgress) return;
    catalogRefreshInProgress = true;
    setOperationActive(true);
    setIanaStatusText(tr("Refreshing authoritative IANA catalog…"));
    DiagnosticLogger::log("MIB", status->text());
    transport->get(IanaMibSourceProvider::indexUrl());
}

void MibLibraryWidget::refresh()
{
    if (dependencyCheckRunning) return; // Completion publishes the worker-prepared inventory once.
    refreshImpl(true);
}

void MibLibraryWidget::refreshImpl(bool prepareProfile, const QList<MibLibraryRecord> *preparedInventory)
{
    QElapsedTimer totalTimer; totalTimer.start();
    QElapsedTimer phaseTimer; phaseTimer.start();
    ++counters.automaticProfileScans;
    const bool automaticChanged = applyAutomaticProfiles();
    profiles.migrateLegacyFolderProfiles();
    const qint64 automaticProfilesMs = phaseTimer.restart();
    const QStringList previouslySelected = selectedModules();
    const QSet<QString> selectedBefore(previouslySelected.cbegin(), previouslySelected.cend());
    const QString currentModule = table->currentRow() >= 0
        ? table->item(table->currentRow(), 1)->text() : QString();
    const int verticalPosition = table->verticalScrollBar()->value();
    const QSignalBlocker tableBlocker(table);
    const ItemViewPublication publication(table, table->horizontalHeader());
    const QList<MibModuleRecord> localRecords = callbacks.localInventory
        ? callbacks.localInventory() : QList<MibModuleRecord>{};
    const qint64 localProjectionMs = phaseTimer.restart();
    ++counters.inventoryBuilds;
    records = preparedInventory ? *preparedInventory : library.inventory(bundledPaths, catalog, localRecords);
    const qint64 inventoryMs = phaseTimer.restart();
    const MibProfileRecord *activeProfile = profiles.find(QSettings().value(
        "mib-library/current-profile", MibProfileDefinitions::standardsId()).toString());
    QSet<QString> activeModules;
    if (activeProfile) {
        const MibEffectivePlan activePlan = planFor(*activeProfile);
        const QStringList effective = callbacks.effectivePlan
            ? activePlan.effectiveModules : activeProfile->explicitModules;
        activeModules = QSet<QString>(effective.cbegin(), effective.cend());
    }
    table->setRowCount(records.size());
    QStringList sources{tr("All origins")};
    for (int row = 0; row < records.size(); ++row) {
        const auto &record = records[row];
        auto *checkItem = new QTableWidgetItem;
        checkItem->setCheckState(selectedBefore.contains(record.moduleName)
            ? Qt::Checked : Qt::Unchecked);
        checkItem->setFlags((checkItem->flags() | Qt::ItemIsUserCheckable) & ~Qt::ItemIsEditable);
        auto *moduleItem = new QTableWidgetItem(record.moduleName);
        const QString failure = sessionState.failureReason(record.moduleName);
        if (!failure.isEmpty()) {
            moduleItem->setIcon(style()->standardIcon(QStyle::SP_MessageBoxCritical));
            moduleItem->setToolTip(tr("Failed: %1").arg(failure));
        }
        const MibLibraryStatus displayedStatus = sessionState.status(record.moduleName, record.status);
        auto *statusItem = new QTableWidgetItem(inventoryStatusText(displayedStatus));
        const bool inActiveProfile = activeProfile && activeModules.contains(record.moduleName);
        auto *activeItem = new QTableWidgetItem(inActiveProfile ? tr("Included") : tr("Not included"));
        if (displayedStatus == MibLibraryStatus::Unresolved ||
            displayedStatus == MibLibraryStatus::Conflict ||
            displayedStatus == MibLibraryStatus::Invalid ||
            displayedStatus == MibLibraryStatus::Failed) {
            statusItem->setIcon(style()->standardIcon(QStyle::SP_MessageBoxWarning));
            statusItem->setToolTip(displayedStatus == MibLibraryStatus::Unresolved
                ? tr("A required dependency is missing from the MIB Library")
                : inventoryStatusText(displayedStatus));
        }
        auto *revisionItem = new QTableWidgetItem(record.revision);
        auto *originItem = new QTableWidgetItem(originText(record));
        for (QTableWidgetItem *item : {moduleItem, statusItem, activeItem, revisionItem, originItem})
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        table->setItem(row, 0, checkItem);
        table->setItem(row, 1, moduleItem);
        table->setItem(row, 2, statusItem);
        table->setItem(row, 3, activeItem);
        table->setItem(row, 4, revisionItem);
        table->setItem(row, 5, originItem);
        if (!sources.contains(originText(record))) sources.append(originText(record));
    }
    const qint64 tableBuildMs = phaseTimer.restart();
    const QString oldSource = sourceFilter->currentText();
    sourceFilter->clear(); sourceFilter->addItems(sources);
    sourceFilter->setCurrentText(oldSource); applyFilter();
    const qint64 filterMs = phaseTimer.restart();
    if (!currentModule.isEmpty()) {
        for (int row = 0; row < table->rowCount(); ++row)
            if (table->item(row, 1)->text() == currentModule) {
                table->setCurrentCell(row, 1); break;
            }
    }
    table->verticalScrollBar()->setValue(verticalPosition);
    setStatusText(sourceStatusText.isEmpty()
        ? tr("%n modules in library", nullptr, records.size())
        : tr("%n modules in library — %1", nullptr, records.size()).arg(sourceStatusText));
    ++counters.profilePopulations;
    if (automaticChanged) emit profilesChanged();
    if (prepareProfile) {
        refreshProfiles(profileCombo && profileCombo->currentIndex() >= 0
            ? profileCombo->currentData().toString() : QString());
        refreshActiveProfile();
    } else {
        QString id = profileCombo->currentData().toString();
        if (id.isEmpty()) id = currentProfile().id;
        id = MibProfileDefinitions::validCurrentId(id, profiles.profiles());
        const QSignalBlocker blocker(profileCombo);
        profileCombo->clear();
        for (const MibProfileRecord &profile : profiles.profiles())
            profileCombo->addItem(profile.name, profile.id);
        profileCombo->setCurrentIndex(qMax(0, profileCombo->findData(id)));
    }
    const qint64 profilesMs = phaseTimer.restart();
    showCurrentInfo();
    updateSelectionUi();
    refreshIanaCatalog();
    const qint64 detailsMs = phaseTimer.elapsed();
    DiagnosticLogger::log("MIB", tr("Library refresh total-ms=%1 automatic-profiles-ms=%2 local-projection-ms=%3 inventory-ms=%4 table-model-ms=%5 filter-sort-ms=%6 profiles-ms=%7 details-layout-ms=%8 records=%9")
        .arg(totalTimer.elapsed()).arg(automaticProfilesMs).arg(localProjectionMs)
        .arg(inventoryMs).arg(tableBuildMs).arg(filterMs).arg(profilesMs)
        .arg(detailsMs).arg(records.size()));
}

MibProfileRecord MibLibraryWidget::currentProfile() const
{
    const QString id = MibProfileDefinitions::validCurrentId(QSettings().value(
        "mib-library/current-profile", MibProfileDefinitions::standardsId()).toString(), profiles.profiles());
    const auto *profile = profiles.find(id);
    return profile ? *profile : MibProfileRecord{};
}

void MibLibraryWidget::refreshActiveProfile()
{
    const auto profile = currentProfile();
    if (!profile.id.isEmpty()) publishPreparedProfile(profile.id, planFor(profile));
}

bool MibLibraryWidget::applyAutomaticProfiles()
{
    return callbacks.automaticProfiles ? profiles.setAutomaticProfiles(callbacks.automaticProfiles()) : false;
}

void MibLibraryWidget::reloadProfiles()
{
    const QString editingId = profileCombo->currentData().toString();
    QString error;
    profiles.reload(&error);
    applyAutomaticProfiles();
    QString id = editingId.isEmpty() ? currentProfile().id : editingId;
    id = MibProfileDefinitions::validCurrentId(id, profiles.profiles());
    const QSignalBlocker blocker(profileCombo);
    profileCombo->clear();
    for (const MibProfileRecord &profile : profiles.profiles())
        profileCombo->addItem(profile.name, profile.id);
    profileCombo->setCurrentIndex(qMax(0, profileCombo->findData(id)));
    if (!error.isEmpty()) setStatusText(error);
}

void MibLibraryWidget::publishPreparedProfile(const QString &id,
                                              const MibEffectivePlan &plan)
{
    const int index = profileCombo->findData(id);
    if (index < 0) return;
    QSettings().setValue("mib-library/current-profile", id);
    const MibProfileRecord *profile = profiles.find(id);
    if (!profile) return;
    if (profileCombo->currentData().toString() == id) {
        const bool editable = profile->type == MibProfileType::Custom;
        renameProfileButton->setEnabled(editable); deleteProfileButton->setEnabled(editable);
        addMemberButton->setEnabled(editable); removeMemberButton->setEnabled(editable);
        profileSource->setText(profileSummaryText(*profile));
        profileGuidance->setText(profileGuidanceText(*profile));
        refreshProfileLists(&plan);
    }
    for (int row = 0; row < table->rowCount(); ++row) {
        const QString module = table->item(row, 1)->text();
        table->item(row, 3)->setText(plan.effectiveModules.contains(module)
            ? tr("Included") : tr("Not included"));
    }
    updateSelectionUi();
    emit profileSelectionChanged(id, plan);
}

void MibLibraryWidget::activate()
{
    QElapsedTimer timer; timer.start();
    ++counters.activations;
    // All Library/Profile models are maintained by construction, explicit
    // Refresh, downloads, collection changes, and profile edits. Visibility
    // changes must never reconcile storage or reconstruct the parser runtime.
    DiagnosticLogger::log("MIB", tr("MIBs tab activation elapsed-ms=%1 cached-records=%2")
        .arg(timer.elapsed()).arg(records.size()));
}

void MibLibraryWidget::applyFilter()
{
    int visible = 0;
    for (int row = 0; row < table->rowCount(); ++row) {
        const bool matchSearch = table->item(row, 1)->text().contains(search->text(), Qt::CaseInsensitive) ||
            table->item(row, 4)->text().contains(search->text(), Qt::CaseInsensitive) ||
            table->item(row, 5)->text().contains(search->text(), Qt::CaseInsensitive);
        const bool matchSource = sourceFilter->currentIndex() <= 0 ||
            table->item(row, 5)->text() == sourceFilter->currentText();
        const bool matchStatus = statusFilter->currentIndex() <= 0 ||
            table->item(row, 2)->text() == statusFilter->currentText();
        const bool match = matchSearch && matchSource && matchStatus;
        table->setRowHidden(row, !match);
        visible += match;
    }
    libraryEmptyState->setText(records.isEmpty()
        ? tr("No MIB modules found. Import MIB files or refresh the configured Library folder.")
        : tr("No modules match these filters. Clear Search modules and choose All origins / All states."));
    libraryEmptyState->setVisible(visible == 0);
    updateSelectionUi();
}
void MibLibraryWidget::setFilteredSelection(bool selected)
{
    const QSignalBlocker blocker(table);
    for (int row = 0; row < table->rowCount(); ++row)
        if (!table->isRowHidden(row))
            table->item(row, 0)->setCheckState(selected ? Qt::Checked : Qt::Unchecked);
    updateSelectionUi(); resolveSelected();
}

void MibLibraryWidget::updateSelectionUi()
{
    int visible = 0, visibleSelected = 0, totalSelected = 0;
    for (int row = 0; row < table->rowCount(); ++row) {
        const bool selected = table->item(row, 0)->checkState() == Qt::Checked;
        totalSelected += selected ? 1 : 0;
        if (!table->isRowHidden(row)) { ++visible; visibleSelected += selected ? 1 : 0; }
    }
    auto *header = static_cast<CheckableHeaderView *>(table->horizontalHeader());
    if (header) header->setCheckState(visibleSelected == 0 ? Qt::Unchecked
        : visibleSelected == visible ? Qt::Checked : Qt::PartiallyChecked);
    selectedCount->setText(tr("Selected: %n", nullptr, totalSelected));
    updateIanaActions();
}

QStringList MibLibraryWidget::selectedIanaModules() const
{
    QStringList selected;
    if (ianaTable) for (auto *item : ianaTable->selectedItems())
        if (item->column() == 0) selected.append(item->text());
    selected.removeDuplicates();
    return selected;
}

void MibLibraryWidget::refreshIanaCatalog()
{
    if (!ianaTable) return;
    const auto selection = selectedIanaModules();
    const QSet<QString> selected(selection.cbegin(), selection.cend());
    const QSignalBlocker blocker(ianaTable);
    ianaTable->setSortingEnabled(false); ianaTable->setRowCount(0);
    QMap<QString, QString> states;
    for (const auto &record : records) states.insert(record.moduleName, inventoryStatusText(record.status));
    for (const auto &entry : catalog.entries()) {
        if (entry.sourceId != IanaMibSourceProvider::id()) continue;
        const int row = ianaTable->rowCount(); ianaTable->insertRow(row);
        auto *module = new QTableWidgetItem(entry.moduleName); module->setToolTip(entry.moduleName);
        ianaTable->setItem(row, 0, module);
        ianaTable->setItem(row, 1, new QTableWidgetItem(states.value(entry.moduleName, tr("Available"))));
        if (selected.contains(entry.moduleName))
            for (int column = 0; column < ianaTable->columnCount(); ++column) ianaTable->item(row, column)->setSelected(true);
    }
    ianaTable->setSortingEnabled(true);
    filterIanaCatalog(); updateIanaActions();
    if (ianaStatus->text().isEmpty()) ianaStatus->setText(ianaTable->rowCount()
        ? tr("Select modules to download. Refresh Catalog checks IANA for the latest module list.")
        : tr("No cached IANA catalog. Click Refresh Catalog to retrieve the module list."));
}

void MibLibraryWidget::filterIanaCatalog()
{
    for (int row = 0; row < ianaTable->rowCount(); ++row)
        ianaTable->setRowHidden(row, !ianaTable->item(row, 0)->text().contains(ianaSearch->text(), Qt::CaseInsensitive));
}

void MibLibraryWidget::updateIanaActions()
{
    if (!ianaTable) return;
    const int selected = selectedIanaModules().size();
    ianaSelectedCount->setText(tr("Selected: %n", nullptr, selected));
    const bool enabled = selected && downloadQueue.isEmpty() && !catalogRefreshInProgress && !dependencyCheckRunning;
    downloadButton->setEnabled(enabled);
    const auto active = currentProfile();
    const bool editable = !active.id.isEmpty() && active.type == MibProfileType::Custom;
    installDependenciesButton->setEnabled(enabled && editable);
    installDependenciesButton->setToolTip(editable
        ? tr("Add the downloaded files to '%1'. This destination is captured when the download starts.").arg(active.name)
        : tr("Choose a Custom MIB Profile in the connection bar to add downloaded files to it."));
}

void MibLibraryWidget::setStatusText(const QString &text)
{
    status->setText(text);
}

void MibLibraryWidget::setIanaStatusText(const QString &text)
{
    setStatusText(text);
    ianaStatus->setText(text);
}

void MibLibraryWidget::setOperationActive(bool active)
{
    cancelButton->setVisible(active);
    refreshButton->setEnabled(!active);
    emit activityChanged();
    if (active) { downloadButton->setEnabled(false); installDependenciesButton->setEnabled(false); }
    else updateSelectionUi();
    for (const auto &name : {"MibLibraryBrowseRoot", "MibLibraryRefresh", "MibLibraryImportFiles"})
        if (auto *button = findChild<QPushButton *>(name)) button->setEnabled(!active && !dependencyCheckRunning);
}

QStringList MibLibraryWidget::selectedModules() const
{
    QStringList result;
    for (int row = 0; row < table->rowCount(); ++row)
        if (table->item(row, 0)->checkState() == Qt::Checked)
            result.append(table->item(row, 1)->text());
    return result;
}

void MibLibraryWidget::resolveSelected()
{
    QMap<QString, MibLibraryStatus> known;
    MibCatalog working = catalog; QList<MibCatalogEntry> entries = catalog.entries();
    for (const MibLibraryRecord &record : records) {
        known.insert(record.moduleName, record.status);
        if (!working.find(record.moduleName) && !record.localPath.isEmpty()) {
            QFile file(record.localPath); if (!file.open(QIODevice::ReadOnly)) continue;
            const auto scan = MibImportScanner::scan(file.readAll());
            MibCatalogEntry entry; entry.moduleName = record.moduleName; entry.imports = scan.imports;
            entries.append(entry);
        }
    }
    working.setEntries(entries);
    QStringList targets = selectedModules();
    if (targets.isEmpty() && table->currentRow() >= 0)
        targets.append(table->item(table->currentRow(), 1)->text());
    const MibDependencyPlan plan = MibDependencyResolver().resolve(targets, known, working);
    dependencies->clear();
    for (const MibDependencyNode &root : plan.roots) appendNode(dependencies->invisibleRootItem(), root);
    dependencies->expandAll();
    setStatusText(plan.unresolved.isEmpty() ? tr("Dependency plan resolved") :
        tr("Unresolved: %1").arg(plan.unresolved.join(", ")));
    DiagnosticLogger::log("MIB", status->text());
}

void MibLibraryWidget::downloadSelected(bool loadAfter)
{
    if (catalogRefreshInProgress || !downloadQueue.isEmpty() || dependencyCheckRunning) return;
    requestedTargets = selectedIanaModules();
    if (requestedTargets.isEmpty()) return;
    downloadProfileId = loadAfter ? currentProfile().id : QString();
    completedDownloads = 0;
    for (const QString &module : requestedTargets) sessionState.begin(module);
    QMap<QString, MibLibraryStatus> known;
    for (const auto &record : records) known.insert(record.moduleName, record.status);
    const MibDependencyPlan plan = MibDependencyResolver().resolve(requestedTargets, known, catalog);
    downloadQueue = plan.orderedDownloads; loadAfterDownload = loadAfter;
    if (downloadQueue.isEmpty()) { downloadNext(); return; }
    setOperationActive(true); downloadNext();
}

void MibLibraryWidget::downloadNext()
{
    if (downloadQueue.isEmpty()) {
        setOperationActive(false); refresh();
        const QString outcome = callbacks.downloadsCompleted
            ? callbacks.downloadsCompleted(requestedTargets, loadAfterDownload, completedDownloads, downloadProfileId)
            : QString();
        setIanaStatusText(outcome.isEmpty()
            ? tr("Downloaded %n MIB file(s) to the Library.", nullptr, completedDownloads)
            : outcome);
        return;
    }
    const MibCatalogEntry *entry = catalog.find(downloadQueue.first());
    if (!entry) { setIanaStatusText(tr("No catalog entry for %1").arg(downloadQueue.first())); downloadQueue.clear(); setOperationActive(false); return; }
    setIanaStatusText(tr("Downloading %1 from %2").arg(entry->moduleName, entry->sourceName));
    DiagnosticLogger::log("MIB", tr("Downloading module=%1 initial=%2 source=%3")
        .arg(entry->moduleName, QUrl(entry->url).toString(QUrl::FullyEncoded), entry->sourceName));
    transport->get(QUrl(entry->url));
}

QString MibLibraryWidget::originText(const MibLibraryRecord &record) const
{
    return MibLibraryOriginText(record);
}

QStringList MibLibraryWidget::availableModuleNames() const
{
    QStringList result;
    for (const MibLibraryRecord &record : records)
        if (!record.localPath.isEmpty() && record.status != MibLibraryStatus::Invalid &&
            record.status != MibLibraryStatus::Conflict) result.append(record.moduleName);
    result.removeDuplicates(); result.sort(Qt::CaseInsensitive); return result;
}

MibCatalog MibLibraryWidget::dependencyCatalog() const
{
    MibCatalog result = catalog;
    QList<MibCatalogEntry> entries = result.entries();
    for (const MibLibraryRecord &record : records) {
        if (record.localPath.isEmpty()) continue;
        QFile file(record.localPath); if (!file.open(QIODevice::ReadOnly)) continue;
        const auto scan = MibImportScanner::scan(file.readAll());
        MibCatalogEntry entry;
        if (const MibCatalogEntry *existing = result.find(record.moduleName)) entry = *existing;
        entry.moduleName = record.moduleName; entry.imports = scan.imports;
        bool replaced = false;
        for (MibCatalogEntry &candidate : entries) if (candidate.moduleName == entry.moduleName) {
            candidate = entry; replaced = true; break;
        }
        if (!replaced) entries.append(entry);
    }
    result.setEntries(entries); return result;
}

void MibLibraryWidget::showCurrentInfo()
{
    const int row = table->currentRow();
    const bool valid = row >= 0 && row < records.size();
    openEditorButton->setEnabled(valid && !records[row].localPath.isEmpty());
    if (!valid) { for (QLabel *label : infoValues) label->clear(); return; }
    const MibLibraryRecord &record = records[row];
    const MibModuleRecord metadata = callbacks.metadata
        ? callbacks.metadata(record.moduleName, record.localPath) : MibModuleRecord{};
    const QString unavailable = QStringLiteral("—");
    infoValues["module"]->setText(metadata.name.isEmpty() ? record.moduleName : metadata.name);
    infoValues["moduleOid"]->setText(metadata.rootOid.isEmpty() ? unavailable : metadata.rootOid);
    infoValues["lastUpdated"]->setText(metadata.lastRevision.isValid()
        ? metadata.lastRevision.toUTC().toString(QStringLiteral("yyyy-MM-dd HH:mm 'UTC'")) : unavailable);
    infoValues["organization"]->setText(metadata.organization.isEmpty()
        ? unavailable : reflowMibDisplayText(metadata.organization));
    infoValues["description"]->setText(metadata.description.isEmpty()
        ? unavailable : reflowMibDisplayText(metadata.description));
    contactInfo->setPlainText(metadata.contactInfo.isEmpty()
        ? unavailable : normalizeContactDisplayText(metadata.contactInfo));
    infoValues["reference"]->setText(metadata.reference.isEmpty()
        ? unavailable : reflowMibDisplayText(metadata.reference));
    QStringList history;
    for (const MibRevisionRecord &revision : metadata.revisions) {
        QString item = revision.date.toUTC().toString(QStringLiteral("yyyy-MM-dd"));
        if (!revision.description.isEmpty())
            item += QStringLiteral("\n") + reflowMibDisplayText(revision.description);
        history.append(item);
    }
    infoValues["revisionHistory"]->setText(history.isEmpty() ? unavailable : history.join(QStringLiteral("\n\n")));
    const QString reason = sessionState.failureReason(record.moduleName);
    const MibLibraryFileInfo fileInfo = MibLibraryFileInformation(record, reason);
    const auto setFileField = [this](const QString &key, const QString &value, bool visible = true) {
        infoValues[key]->setText(value);
        infoValues[key]->setVisible(visible);
        infoLabels[key]->setVisible(visible);
    };
    setFileField("origin", fileInfo.origin);
    setFileField("revision", fileInfo.revision);
    setFileField("filename", fileInfo.filename);
    setFileField("path", fileInfo.localPath);
    setFileField("provider", fileInfo.provider, fileInfo.showProvider);
    setFileField("url", fileInfo.sourceUrl, fileInfo.showSourceUrl);
    setFileField("timestamp", fileInfo.timestamp, fileInfo.showTimestamp);
    setFileField("sha", fileInfo.sha256, fileInfo.showSha256);
    setFileField("state", fileInfo.state, fileInfo.showState);
    QSignalBlocker blocker(table);
    table->selectRow(row);
    resolveSelected();
}

void MibLibraryWidget::openCurrentModule()
{
    const int row = table->currentRow();
    if (row >= 0 && row < records.size() && !records[row].localPath.isEmpty())
        emit openModuleRequested(records[row].localPath);
}

void MibLibraryWidget::refreshProfiles(const QString &selectId)
{
    QString id = selectId;
    if (id.isEmpty()) id = QSettings().value("mib-library/current-profile",
                                             MibProfileDefinitions::standardsId()).toString();
    id = MibProfileDefinitions::validCurrentId(id, profiles.profiles());
    const QSignalBlocker blocker(profileCombo);
    profileCombo->clear();
    for (const MibProfileRecord &profile : profiles.profiles())
        profileCombo->addItem(profile.name, profile.id);
    int index = profileCombo->findData(id);
    if (index < 0 && profileCombo->count() > 0) index = 0;
    profileCombo->setCurrentIndex(index);
    profileChanged();
}

QString MibLibraryWidget::profileSummaryText(const MibProfileRecord &profile) const
{
    if (profile.type == MibProfileType::Standards)
        return tr("%n built-in module(s)", nullptr, profile.explicitModules.size());
    if (profile.type == MibProfileType::Automatic)
        return tr("%n MIB file(s) from its folder", nullptr, profile.roots.size());
    return tr("%1 followed folder(s) · %2 single file(s)").arg(profile.folders.size()).arg(profile.roots.size());
}

QString MibLibraryWidget::profileGuidanceText(const MibProfileRecord &profile) const
{
    switch (profile.type) {
    case MibProfileType::Automatic:
        return tr("Automatic profile: built from the MIB files in the folder %1 (and shared files in %2). It follows "
                  "the folder: add, remove or change a file there and refresh the Library. It cannot be edited member "
                  "by member; to choose your own set, create a Custom profile.")
            .arg(profile.orderedScopes.value(0).id.mid(7), profile.orderedScopes.value(1).id.mid(7));
    case MibProfileType::Standards:
        return tr("Built-in profile with the standard SNMP and MIB-II modules. It cannot be edited; to choose your "
                  "own MIBs, create a Custom profile.");
    default:
        return tr("Custom profile. Select folders or MIB files on the left and press Add. A folder is followed: MIBs "
                  "added to it or removed from it change the profile at the next Library refresh. Remove on a file "
                  "inside a followed folder leaves just that file out. MIBs they import are found automatically.");
    }
}

void MibLibraryWidget::selectProfile(const QString &id)
{
    const auto *profile = profiles.find(id);
    if (profile) publishPreparedProfile(id, planFor(*profile));
}

void MibLibraryWidget::profileChanged()
{
    const QString id = profileCombo->currentData().toString();
    const MibProfileRecord *profile = profiles.find(id);
    if (!profile) return;
    const MibProfileRecord selectedProfile = *profile;
    const bool editable = selectedProfile.type == MibProfileType::Custom;
    renameProfileButton->setEnabled(editable); deleteProfileButton->setEnabled(editable);
    addMemberButton->setEnabled(editable); removeMemberButton->setEnabled(editable);
    includeStandards->hide();
    profileSource->setText(profileSummaryText(selectedProfile));
    profileGuidance->setText(profileGuidanceText(selectedProfile));
    openProfileFolderButton->hide();
    const MibEffectivePlan plan = planFor(selectedProfile);
    refreshProfileLists(&plan);
    updateSelectionUi();
}

MibEffectivePlan MibLibraryWidget::planFor(const MibProfileRecord &profile) const
{
    return callbacks.effectivePlan ? callbacks.effectivePlan(profile) : MibEffectivePlan{};
}

void MibLibraryWidget::browseLibraryRoot()
{
    const QString chosen = QFileDialog::getExistingDirectory(
        this, tr("Choose MIB Library Location"), library.rootPath());
    if (chosen.isEmpty()) return;
    QSettings settings;
    MibCollectionResult result;
    if (!MibCollection::setConfiguredRoot(settings, chosen, bundledPaths, &result)) {
        QMessageBox::warning(this, tr("MIB Library Location"), result.error);
        return;
    }
    library = MibLibraryService(chosen);
    libraryRootEdit->setText(library.rootPath());
    profiles.migrateLegacyFolderProfiles();
    if (callbacks.collectionChanged) callbacks.collectionChanged();
    QString migrationError;
    if (callbacks.retryLegacyProfiles)
        callbacks.retryLegacyProfiles(profiles, &migrationError);
    refresh(); refreshProfiles();
    if (!result.conflicts.isEmpty())
        QMessageBox::warning(this, tr("MIB Library conflicts"),
            tr("%n existing different-content file(s) were preserved and not overwritten.",
               nullptr, result.conflicts.size()));
}

void MibLibraryWidget::openLibraryRoot()
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(library.rootPath()));
}

void MibLibraryWidget::importFiles()
{
    const QStringList files = QFileDialog::getOpenFileNames(
        this, tr("Import MIB Files"), QString(), tr("MIB files (*.mib *.txt *-MIB);;All files (*)"));
    if (files.isEmpty()) return;
    for (const QString &file : files) {
        QString error;
        if (callbacks.validate && !callbacks.validate(file, &error)) {
            QMessageBox::warning(this, tr("Import MIB Files"),
                tr("No files were imported. %1 could not be validated: %2")
                    .arg(QFileInfo(file).fileName(), error));
            return;
        }
    }
    const MibCollectionResult result = MibCollection(library.rootPath()).importFiles(files);
    if (!result.success) {
        QMessageBox::warning(this, tr("Import MIB Files"), result.error);
        return;
    }
    if (callbacks.collectionChanged) callbacks.collectionChanged();
    QString migrationError;
    if (callbacks.retryLegacyProfiles)
        callbacks.retryLegacyProfiles(profiles, &migrationError);
    refresh();
    setStatusText(tr("Imported %1 file(s) to the Library; %2 identical file(s) already existed; %3 conflict(s) were preserved. Active Profile membership was not changed.")
        .arg(result.importedCopied).arg(result.identicalSkipped).arg(result.conflicts.size()));
}

void MibLibraryWidget::openProfileFolder()
{
    const MibProfileRecord *profile = profiles.find(profileCombo->currentData().toString());
    if (profile && profile->type == MibProfileType::Folder)
        QDesktopServices::openUrl(QUrl::fromLocalFile(profile->directory));
}

void MibLibraryWidget::refreshProfileLists(const MibEffectivePlan *providedPlan)
{
    if (!profileCombo || profileCombo->currentIndex() < 0) return;
    const MibProfileRecord *profile = profiles.find(profileCombo->currentData().toString());
    if (!profile) return;
    const MibEffectivePlan effective = providedPlan ? *providedPlan : planFor(*profile);
    const auto providerReasonText = [this](MibPlanProviderReason reason) {
        switch (reason) {
        case MibPlanProviderReason::ExplicitPin: return tr("explicit provider pin");
        case MibPlanProviderReason::AutomaticProfileFolder: return tr("Automatic profile product affinity");
        case MibPlanProviderReason::GlobalPrecedence: return tr("global provider precedence");
        case MibPlanProviderReason::EquivalentProviders: return tr("identical duplicate providers");
        case MibPlanProviderReason::SingleProvider: return tr("only provider");
        case MibPlanProviderReason::Ambiguous: return tr("unresolved provider conflict");
        case MibPlanProviderReason::InvalidPin: return tr("invalid provider pin");
        case MibPlanProviderReason::None: return tr("no provider decision");
        }
        return QString();
    };
    const QSignalBlocker blocker(includeStandards);
    includeStandards->setChecked(false);
    const QList<MibDependencyFileRecord> files = libraryFileRecords();
    fillAvailableTree(*profile, files);
    fillMemberTree(*profile, files, effective);
    requiredList->clear();
    for (const MibEffectivePlanMember &requirement : effective.members) {
        if (requirement.membershipReason == MibPlanMembershipReason::Explicit) continue;
        const bool missing = requirement.membershipReason == MibPlanMembershipReason::Missing;
        const bool ambiguous = requirement.membershipReason == MibPlanMembershipReason::Ambiguous;
        const bool pinFailure = requirement.membershipReason == MibPlanMembershipReason::PinFailure;
        const QString state = pinFailure ? tr("Invalid pin") : missing ? tr("Missing") : ambiguous ? tr("Ambiguous") : tr("Available");
        const QString reason = pinFailure ? tr("Pinned provider is missing or changed") :
            ambiguous ? tr("Provider conflict") : tr("Imported dependency");
        auto *item = new QListWidgetItem(tr("%1 — %2 — %3")
            .arg(requirement.identity, state, reason), requiredList);
        item->setData(Qt::UserRole, requirement.identity);
        item->setData(Qt::UserRole + 1, missing);
        QStringList providerDetails;
        for (const MibIndexedProvider &provider : requirement.alternatives)
            providerDetails.append(tr("%1 [%2]").arg(provider.canonicalPath, provider.sha256.left(12)));
        item->setToolTip(tr("Reason: %1\n%2").arg(providerReasonText(requirement.providerReason),
            providerDetails.join('\n')));
        if (missing || ambiguous || pinFailure)
            item->setIcon(style()->standardIcon(QStyle::SP_MessageBoxWarning));
    }
    const DependencySummary librarySummary = callbacks.libraryDependencySummary
        ? callbacks.libraryDependencySummary() : DependencySummary{};
    QString profileSummary = tr("%1 selected · %2 dependencies · %3 ready")
        .arg(effective.explicitModules.size()).arg(effective.dependencyModules.size())
        .arg(effective.effectiveModules.size());
    if (!effective.missingModules.isEmpty())
        profileSummary += tr(" · %1 missing").arg(effective.missingModules.size());
    if (!effective.ambiguousModules.isEmpty())
        profileSummary += tr(" · Conflicting definitions: %1").arg(effective.ambiguousModules.join(QStringLiteral(", ")));
    if (!effective.pinFailureModules.isEmpty())
        profileSummary += tr(" · %1 invalid pins").arg(effective.pinFailureModules.size());
    if (!effective.converged)
        profileSummary += tr(" · provider planning did not converge");
    if (librarySummary.stale) profileSummary += tr("\nMIB Library needs checking");
    dependencyCheckSummary->setText(profileSummary);
}

void MibLibraryWidget::createProfile()
{
    bool ok = false; const QString name = QInputDialog::getText(this, tr("New MIB Profile"), tr("Name:"), QLineEdit::Normal, {}, &ok);
    if (!ok) return; QString error; const QString id = profiles.create(name, &error);
    if (id.isEmpty()) { setStatusText(error); return; }
    refreshProfiles(id); emit profilesChanged();
}

void MibLibraryWidget::duplicateProfile()
{
    const QString current = profileCombo->currentData().toString();
    bool ok = false; const QString name = QInputDialog::getText(this, tr("Duplicate MIB Profile"), tr("Name:"), QLineEdit::Normal, profileCombo->currentText() + tr(" Copy"), &ok);
    if (!ok) return; QString error; const QString id = profiles.duplicate(current, name, &error);
    if (id.isEmpty()) { setStatusText(error); return; }
    refreshProfiles(id); emit profilesChanged();
}

void MibLibraryWidget::renameProfile()
{
    bool ok = false; const QString name = QInputDialog::getText(this, tr("Rename MIB Profile"), tr("Name:"), QLineEdit::Normal, profileCombo->currentText(), &ok);
    if (!ok) return; const QString id = profileCombo->currentData().toString(); QString error;
    if (!profiles.rename(id, name, &error)) { setStatusText(error); return; }
    refreshProfiles(id); emit profilesChanged();
    if (id == currentProfile().id) refreshActiveProfile();
}

void MibLibraryWidget::deleteProfile()
{
    const QString id = profileCombo->currentData().toString(); QString error;
    const bool wasActive = id == currentProfile().id;
    if (!profiles.remove(id, &error)) { setStatusText(error); return; }
    refreshProfiles(); emit profilesChanged();
    if (wasActive) refreshActiveProfile();
}

void MibLibraryWidget::saveCurrentProfile()
{
    const MibProfileRecord *current = profiles.find(profileCombo->currentData().toString());
    if (!current || current->type != MibProfileType::Custom) return;
    MibProfileRecord changed = *current;
    QString error; if (!profiles.update(changed, &error)) setStatusText(error);
    emit profilesChanged(); profileChanged();
    if (profileCombo->currentData().toString() == currentProfile().id) refreshActiveProfile();
}

namespace {
// What a row on the Profiles page stands for.
enum ProfileItemRole { PathRole = Qt::UserRole, KindRole, ExcludedRole, IdentitiesRole };
const QString kindFolder = QStringLiteral("folder");              // a Library folder (left)
const QString kindFile = QStringLiteral("file");                  // a Library file (left)
const QString kindFollowed = QStringLiteral("followed-folder");   // a folder the profile follows (right)
const QString kindFolderFile = QStringLiteral("folder-file");     // a file inside a followed folder (right)
const QString kindRoot = QStringLiteral("single-file");           // a file added on its own (right)
const QString kindModule = QStringLiteral("module");              // a built-in profile's module (right)

QString itemKey(const QTreeWidgetItem *item)
{
    return item->data(0, KindRole).toString() + QLatin1Char('|') + item->data(0, PathRole).toString();
}

// Remembers which rows were expanded and selected, so a rebuild keeps the user's place.
struct TreeState {
    QSet<QString> expanded, selected;
    explicit TreeState(QTreeWidget *tree) {
        std::function<void(QTreeWidgetItem *)> walk = [&](QTreeWidgetItem *item) {
            if (item->isExpanded()) expanded.insert(itemKey(item));
            if (item->isSelected()) selected.insert(itemKey(item));
            for (int i = 0; i < item->childCount(); ++i) walk(item->child(i));
        };
        for (int i = 0; i < tree->topLevelItemCount(); ++i) walk(tree->topLevelItem(i));
    }
    void apply(QTreeWidget *tree, bool expandTopLevel) const {
        std::function<void(QTreeWidgetItem *, bool)> walk = [&](QTreeWidgetItem *item, bool top) {
            const QString key = itemKey(item);
            item->setExpanded(expanded.contains(key) || (top && expandTopLevel && expanded.isEmpty()));
            item->setSelected(selected.contains(key));
            for (int i = 0; i < item->childCount(); ++i) walk(item->child(i), false);
        };
        for (int i = 0; i < tree->topLevelItemCount(); ++i) walk(tree->topLevelItem(i), true);
    }
};

bool isHiddenBelow(const QString &path, const QString &root)
{
    const QString relative = QDir(root).relativeFilePath(path);
    for (const QString &part : relative.split(QLatin1Char('/'), Qt::SkipEmptyParts))
        if (part.startsWith(QLatin1Char('.'))) return true;
    return false;
}
}

QList<MibDependencyFileRecord> MibLibraryWidget::libraryFileRecords() const
{
    if (callbacks.libraryFiles) return callbacks.libraryFiles();
    // Without the index (tests that only give a module inventory), the Library's own records.
    QMap<QString, MibDependencyFileRecord> byPath;
    for (const MibLibraryRecord &record : records) {
        if (record.localPath.isEmpty()) continue;
        const QString path = QFileInfo(record.localPath).canonicalFilePath();
        if (path.isEmpty()) continue;
        MibDependencyFileRecord &file = byPath[path];
        file.canonicalPath = path;
        file.filename = QFileInfo(path).fileName();
        file.sha256 = QStringLiteral("local");
        file.importsByModule.insert(record.moduleName, {});
    }
    return byPath.values();
}

void MibLibraryWidget::fillAvailableTree(const MibProfileRecord &profile,
                                         const QList<MibDependencyFileRecord> &files)
{
    const ItemViewPublication publication(availableTree, availableTree->header());
    const QIcon folderIcon = style()->standardIcon(QStyle::SP_DirIcon);
    const QIcon fileIcon = style()->standardIcon(QStyle::SP_FileIcon);
    const TreeState state(availableTree);
    const int scroll = availableTree->verticalScrollBar()->value();
    availableTree->clear();
    const QString libraryRoot = QFileInfo(library.rootPath()).canonicalFilePath();
    const auto followedFolder = [&profile](const QString &path) -> const MibProfileFolder * {
        for (const MibProfileFolder &folder : profile.folders)
            if (MibPathIsInsideFolder(path, folder.canonicalPath)) return &folder;
        return nullptr;
    };
    QHash<QString, QTreeWidgetItem *> folderItems;
    QTreeWidgetItem *outside = nullptr;
    std::function<QTreeWidgetItem *(const QString &)> folderItem = [&](const QString &path) -> QTreeWidgetItem * {
        if (!libraryRoot.isEmpty() && path == libraryRoot) return nullptr;   // the Library itself: top level
        if (QTreeWidgetItem *known = folderItems.value(path)) return known;
        const bool inside = !libraryRoot.isEmpty() && MibPathIsInsideFolder(path, libraryRoot);
        QTreeWidgetItem *parent = nullptr;
        if (inside) {
            parent = folderItem(QFileInfo(path).absolutePath());
        } else {
            if (!outside) {
                outside = new QTreeWidgetItem(availableTree, {tr("Outside the MIB Library")});
                outside->setToolTip(0, tr("MIB files the Library knows about that are not in its folder"));
                outside->setFlags(outside->flags() & ~Qt::ItemIsSelectable);
            }
            parent = outside;
        }
        auto *item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(availableTree);
        item->setText(0, inside ? QFileInfo(path).fileName() : QDir::toNativeSeparators(path));
        item->setIcon(0, folderIcon);
        item->setToolTip(0, QDir::toNativeSeparators(path));
        item->setData(0, PathRole, path);
        item->setData(0, KindRole, kindFolder);
        const bool followed = std::any_of(profile.folders.cbegin(), profile.folders.cend(),
            [&path](const MibProfileFolder &folder) {
                return MibPathIsInsideFolder(path, folder.canonicalPath) &&
                       MibPathIsInsideFolder(folder.canonicalPath, path);
            });
        item->setText(1, followed ? tr("Followed") : followedFolder(path) ? tr("Via folder") : QString());
        folderItems.insert(path, item);
        return item;
    };
    // Every Library folder appears, even an empty one (a folder can be followed before it has MIBs).
    if (!libraryRoot.isEmpty()) {
        QDirIterator folders(libraryRoot, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        while (folders.hasNext()) {
            const QString path = QFileInfo(folders.next()).canonicalFilePath();
            if (!path.isEmpty() && !isHiddenBelow(path, libraryRoot)) folderItem(path);
        }
    }
    for (const MibDependencyFileRecord &file : files) {
        if (file.sha256.isEmpty() || file.importsByModule.isEmpty()) continue;   // not a usable MIB file
        if (!libraryRoot.isEmpty() && MibPathIsInsideFolder(file.canonicalPath, libraryRoot) &&
            isHiddenBelow(file.canonicalPath, libraryRoot)) continue;
        QTreeWidgetItem *parent = folderItem(QFileInfo(file.canonicalPath).absolutePath());
        auto *item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(availableTree);
        const QStringList modules = file.importsByModule.keys();
        item->setText(0, tr("%1  (%2)").arg(QFileInfo(file.canonicalPath).fileName(),
                                            modules.join(QStringLiteral(", "))));
        item->setIcon(0, fileIcon);
        item->setToolTip(0, QDir::toNativeSeparators(file.canonicalPath));
        item->setData(0, PathRole, file.canonicalPath);
        item->setData(0, KindRole, kindFile);
        item->setData(0, IdentitiesRole, modules);
        QString membership;
        if (std::any_of(profile.roots.cbegin(), profile.roots.cend(), [&file](const MibProfileMember &root) {
                return MibPathIsInsideFolder(file.canonicalPath, root.canonicalPath); }))
            membership = tr("Added");
        else if (const MibProfileFolder *folder = followedFolder(file.canonicalPath))
            membership = std::any_of(folder->excludedFiles.cbegin(), folder->excludedFiles.cend(),
                [&file](const QString &excluded) { return MibPathIsInsideFolder(file.canonicalPath, excluded); })
                ? tr("Excluded") : tr("Via folder");
        item->setText(1, membership);
    }
    availableTree->sortItems(0, Qt::AscendingOrder);
    if (outside) {   // keep the outside group last
        availableTree->takeTopLevelItem(availableTree->indexOfTopLevelItem(outside));
        availableTree->addTopLevelItem(outside);
    }
    state.apply(availableTree, true);   // the first time, vendors open to show their products
    if (!availableSearch->text().isEmpty()) emit availableSearch->textChanged(availableSearch->text());
    availableTree->doItemsLayout(); // Publish geometry once before restoring the saved scroll position.
    availableTree->verticalScrollBar()->setValue(scroll);
}

void MibLibraryWidget::fillMemberTree(const MibProfileRecord &profile,
                                      const QList<MibDependencyFileRecord> &files,
                                      const MibEffectivePlan &effective)
{
    const ItemViewPublication publication(memberTree, memberTree->header());
    const TreeState state(memberTree);
    const int scroll = memberTree->verticalScrollBar()->value();
    memberTree->clear();
    const QString libraryRoot = QFileInfo(library.rootPath()).canonicalFilePath();
    const auto shownPath = [&libraryRoot](const QString &path) {
        return !libraryRoot.isEmpty() && MibPathIsInsideFolder(path, libraryRoot) && path != libraryRoot
            ? QDir::toNativeSeparators(QDir(libraryRoot).relativeFilePath(path))
            : QDir::toNativeSeparators(path);
    };
    const auto providerTip = [&effective](const QStringList &identities) {
        QStringList lines;
        for (const QString &identity : identities)
            if (const MibEffectivePlanMember *member = effective.member(identity))
                if (!member->provider.canonicalPath.isEmpty())
                    lines << tr("%1 loads from %2").arg(identity, QDir::toNativeSeparators(member->provider.canonicalPath));
        return lines.join(QLatin1Char('\n'));
    };
    for (const MibProfileFolder &folder : profile.folders) {
        auto *item = new QTreeWidgetItem(memberTree);
        item->setText(0, shownPath(folder.canonicalPath));
        item->setIcon(0, style()->standardIcon(QStyle::SP_DirIcon));
        item->setToolTip(0, QDir::toNativeSeparators(folder.canonicalPath));
        item->setData(0, PathRole, folder.canonicalPath);
        item->setData(0, KindRole, kindFollowed);
        if (!QFileInfo(folder.canonicalPath).isDir()) {
            item->setText(2, tr("Folder not found"));
            item->setIcon(2, style()->standardIcon(QStyle::SP_MessageBoxWarning));
            item->setToolTip(2, tr("The folder was renamed, moved or deleted. Restore it, or remove it from the profile."));
            continue;
        }
        int included = 0, excluded = 0;
        for (const MibProfileMember &member : MibProfileFolderMembers(folder, files, true)) {
            const bool isExcluded = std::any_of(folder.excludedFiles.cbegin(), folder.excludedFiles.cend(),
                [&member](const QString &path) { return MibPathIsInsideFolder(member.canonicalPath, path); });
            auto *child = new QTreeWidgetItem(item);
            child->setText(0, QDir::toNativeSeparators(QDir(folder.canonicalPath).relativeFilePath(member.canonicalPath)));
            child->setText(1, member.identities.join(QStringLiteral(", ")));
            child->setText(2, isExcluded ? tr("Excluded") : tr("Included"));
            child->setToolTip(0, QDir::toNativeSeparators(member.canonicalPath));
            child->setToolTip(1, providerTip(member.identities));
            child->setData(0, PathRole, member.canonicalPath);
            child->setData(0, KindRole, kindFolderFile);
            child->setData(0, ExcludedRole, isExcluded);
            child->setData(0, IdentitiesRole, member.identities);
            if (isExcluded) {
                for (int column = 0; column < 3; ++column) child->setData(column, AppearanceColors::MutedRole, true);
                ++excluded;
            } else {
                ++included;
            }
        }
        item->setText(2, excluded ? tr("Followed · %1 MIB file(s) · %2 excluded").arg(included).arg(excluded)
                                  : tr("Followed · %1 MIB file(s)").arg(included));
    }
    for (const MibProfileMember &member : profile.roots) {
        MibProfileMemberState memberState = QFileInfo::exists(member.canonicalPath)
            ? MibProfileMemberState::Changed : MibProfileMemberState::Missing;
        for (const QString &identity : member.identities) {
            const MibEffectivePlanMember *resolved = effective.member(identity);
            if (resolved && resolved->provider.sha256 == member.sha256 &&
                QFileInfo(resolved->provider.canonicalPath).absoluteFilePath().compare(
                    QFileInfo(member.canonicalPath).absoluteFilePath(), Qt::CaseInsensitive) == 0) {
                memberState = MibProfileMemberState::Current;
                break;
            }
        }
        const QString stateText = memberState == MibProfileMemberState::Current ? tr("OK")
            : memberState == MibProfileMemberState::Missing ? tr("Missing")
            : memberState == MibProfileMemberState::Changed ? tr("Changed") : tr("Catalog stale");
        auto *item = new QTreeWidgetItem(memberTree);
        item->setText(0, shownPath(member.canonicalPath));
        item->setIcon(0, style()->standardIcon(QStyle::SP_FileIcon));
        item->setText(1, member.identities.join(QStringLiteral(", ")));
        item->setText(2, member.reason == MibProfileMemberReason::Dependency
            ? tr("%1 · added as a dependency").arg(stateText) : stateText);
        item->setToolTip(0, QDir::toNativeSeparators(member.canonicalPath) +
            (memberState == MibProfileMemberState::Missing ? tr("\nProfile file is missing")
             : memberState == MibProfileMemberState::Current ? QString() : tr("\nProfile file content changed")));
        item->setToolTip(1, providerTip(member.identities));
        item->setData(0, PathRole, member.canonicalPath);
        item->setData(0, KindRole, kindRoot);
        item->setData(0, IdentitiesRole, member.identities);
    }
    if (profile.folders.isEmpty() && profile.roots.isEmpty()) {
        // Built-in (and older, module-list) profiles name modules rather than files.
        for (const QString &module : profile.explicitModules) {
            auto *item = new QTreeWidgetItem(memberTree, {module, module,
                profile.type == MibProfileType::Standards ? tr("Built in") : tr("Listed module")});
            item->setData(0, PathRole, module);
            item->setData(0, KindRole, kindModule);
            item->setToolTip(1, providerTip({module}));
        }
    }
    state.apply(memberTree, false);
    if (!memberSearch->text().isEmpty()) emit memberSearch->textChanged(memberSearch->text());
    memberTree->doItemsLayout(); // Publish geometry once before restoring the saved scroll position.
    memberTree->verticalScrollBar()->setValue(scroll);
    profileEmptyState->setVisible(memberTree->topLevelItemCount() == 0);
    profileEmptyState->setText(profile.type == MibProfileType::Automatic
        ? tr("This product folder contains no MIBs. Add MIB files to its folder, then refresh the Library.")
        : tr("This profile is empty. Select folders or MIB files on the left and press Add."));
}

void MibLibraryWidget::addProfileMembers()
{
    const MibProfileRecord *current = profiles.find(profileCombo->currentData().toString());
    if (!current || current->type != MibProfileType::Custom) {
        setStatusText(tr("This profile cannot be edited. Create a Custom profile (New Profile or Duplicate) to choose your own MIBs."));
        return;
    }
    const MibProfileRecord profile = *current;
    QStringList folders, singleFiles, takeBack;
    for (QTreeWidgetItem *item : availableTree->selectedItems()) {
        const QString path = item->data(0, PathRole).toString();
        const QString kind = item->data(0, KindRole).toString();
        if (kind == kindFolder) folders << path;
        else if (kind == kindFile) {
            const auto folder = std::find_if(profile.folders.cbegin(), profile.folders.cend(),
                [&path](const MibProfileFolder &followed) { return MibPathIsInsideFolder(path, followed.canonicalPath); });
            if (folder == profile.folders.cend()) singleFiles << path;
            else if (item->text(1) == tr("Excluded")) takeBack << path;   // back in from its followed folder
        }
    }
    // A file inside a folder that is being followed in the same step comes in with the folder.
    singleFiles.erase(std::remove_if(singleFiles.begin(), singleFiles.end(), [&folders](const QString &path) {
        return std::any_of(folders.cbegin(), folders.cend(),
                           [&path](const QString &folder) { return MibPathIsInsideFolder(path, folder); });
    }), singleFiles.end());
    if (folders.isEmpty() && singleFiles.isEmpty() && takeBack.isEmpty()) {
        setStatusText(tr("Select folders or MIB files in the MIB Library folders list first. Files already in "
                           "the profile through a followed folder are not added again."));
        return;
    }
    QString error;
    const bool done = (folders.isEmpty() || profiles.followFolders(profile.id, folders, &error)) &&
        (takeBack.isEmpty() || profiles.setFilesExcluded(profile.id, takeBack, false, &error)) &&
        (singleFiles.isEmpty() || profiles.addFiles(profile.id, singleFiles, MibProfileMemberReason::Added, &error));
    setStatusText(done ? tr("Profile updated: %1 folder(s) followed, %2 file(s) added, %3 file(s) taken back in.")
                               .arg(folders.size()).arg(singleFiles.size()).arg(takeBack.size())
                         : error);
    emit profilesChanged(); profileChanged();
    if (profileCombo->currentData().toString() == currentProfile().id) refreshActiveProfile();
}

void MibLibraryWidget::removeProfileMembers()
{
    const MibProfileRecord *current = profiles.find(profileCombo->currentData().toString());
    if (!current || current->type != MibProfileType::Custom) {
        setStatusText(tr("This profile cannot be edited. Create a Custom profile (New Profile or Duplicate) to choose your own MIBs."));
        return;
    }
    const QString id = current->id;
    QStringList folders, exclude, roots;
    for (QTreeWidgetItem *item : memberTree->selectedItems()) {
        const QString path = item->data(0, PathRole).toString();
        const QString kind = item->data(0, KindRole).toString();
        if (kind == kindFollowed) folders << path;
        else if (kind == kindFolderFile && !item->data(0, ExcludedRole).toBool()) exclude << path;
        else if (kind == kindRoot) roots << path;
    }
    if (folders.isEmpty() && exclude.isEmpty() && roots.isEmpty()) {
        setStatusText(tr("Select followed folders or files in the profile first."));
        return;
    }
    QString error;
    bool done = true;
    if (!roots.isEmpty()) {
        MibProfileRecord changed = *current;
        changed.roots.erase(std::remove_if(changed.roots.begin(), changed.roots.end(), [&roots](const auto &member) {
            return std::any_of(roots.cbegin(), roots.cend(), [&member](const QString &path) {
                return MibPathIsInsideFolder(member.canonicalPath, path) && MibPathIsInsideFolder(path, member.canonicalPath);
            });
        }), changed.roots.end());
        changed.explicitModules = MibProfileMemberIdentities(changed.roots);
        done = profiles.update(changed, &error);
    }
    done = done && (exclude.isEmpty() || profiles.setFilesExcluded(id, exclude, true, &error)) &&
        (folders.isEmpty() || profiles.unfollowFolders(id, folders, &error));
    setStatusText(done ? tr("Profile updated: %1 folder(s) no longer followed, %2 file(s) excluded, %3 file(s) removed.")
                               .arg(folders.size()).arg(exclude.size()).arg(roots.size())
                         : error);
    emit profilesChanged(); profileChanged();
    if (profileCombo->currentData().toString() == currentProfile().id) refreshActiveProfile();
}
