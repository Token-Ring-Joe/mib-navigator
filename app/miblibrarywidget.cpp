#include "miblibrarywidget.h"

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
#include <QVBoxLayout>

namespace {
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
    workspaceTabs = new QTabWidget(this);
    workspaceTabs->setObjectName("MibLibraryTabs");
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
        tr("Organize product MIBs as Vendor\\Product folders. Standards and Unassigned do not create automatic profiles."),
        inventoryPage);
    organizationHint->setWordWrap(true);
    layout->addWidget(organizationHint);
    auto *filters = new QHBoxLayout;
    search = new QLineEdit(this); search->setObjectName("MibLibrarySearch");
    search->setPlaceholderText(tr("Search modules"));
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
    refreshButton = new QPushButton(tr("Refresh Catalog"), this);
    refreshButton->setObjectName("MibLibraryRefreshCatalog");
    auto *checkUpdates = new QPushButton(tr("Check for Updates"), this);
    checkUpdates->setObjectName("MibLibraryCheckForUpdates");
    checkUpdates->hide();
    cancelButton = new QPushButton(tr("Cancel"), this);
    cancelButton->setObjectName("MibLibraryCancel"); cancelButton->hide();
    filters->addWidget(search, 1); filters->addWidget(sourceFilter); filters->addWidget(localRefresh);
    filters->addWidget(checkDependencies);
    filters->addWidget(refreshButton); filters->addWidget(checkUpdates); filters->addWidget(cancelButton);
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
    downloadButton = new QPushButton(tr("Download to Library"), this);
    downloadButton->setObjectName("MibLibraryDownload");
    installDependenciesButton = new QPushButton(tr("Download and Add to Active Profile"), this);
    installDependenciesButton->setObjectName("MibLibraryInstallWithDependencies");
    selection->addWidget(selectedCount); selection->addStretch();
    auto *importButton = new QPushButton(tr("Import Files..."), inventoryPage);
    importButton->setObjectName("MibLibraryImportFiles");
    selection->addWidget(importButton);
    selection->addWidget(downloadButton); selection->addWidget(installDependenciesButton);
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
    status = new QLabel(tr("Ready"), this); status->setWordWrap(true); layout->addWidget(status);

    auto *profilePage = new QWidget(workspaceTabs);
    auto *profileLayout = new QVBoxLayout(profilePage);
    auto *profileActions = new QHBoxLayout;
    profileCombo = new QComboBox(profilePage); profileCombo->setObjectName("MibProfileEditorSelector");
    auto *newProfile = new QPushButton(tr("New Profile"), profilePage);
    auto *duplicateProfileButton = new QPushButton(tr("Duplicate"), profilePage);
    auto *addFilesButton = new QPushButton(tr("Add Files..."), profilePage);
    auto *addFolderButton = new QPushButton(tr("Add Folder Snapshot..."), profilePage);
    renameProfileButton = new QPushButton(tr("Rename"), profilePage);
    deleteProfileButton = new QPushButton(tr("Delete"), profilePage);
    auto *activeProfileLabel = new QLabel(tr("Active MIB Profile:"), profilePage);
    activeProfileLabel->setObjectName("MibProfileActiveLabel");
    profileActions->addWidget(activeProfileLabel); profileActions->addWidget(profileCombo, 1);
    for (QPushButton *button : {newProfile, duplicateProfileButton, addFilesButton,
                                addFolderButton, renameProfileButton, deleteProfileButton})
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
    availableLayout->addWidget(new QLabel(tr("Available MIBs"), availablePane));
    availableSearch = new QLineEdit(availablePane); availableSearch->setObjectName("MibProfileAvailableFilter");
    availableSearch->setPlaceholderText(tr("Filter available"));
    availableList = new QListWidget(availablePane); availableList->setObjectName("MibProfileAvailableList");
    availableList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    availableLayout->addWidget(availableSearch); availableLayout->addWidget(availableList);
    auto *movePane = new QWidget(profileSplitter); auto *moveLayout = new QVBoxLayout(movePane);
    moveLayout->addStretch(); addMemberButton = new QPushButton(tr("Add >"), movePane);
    removeMemberButton = new QPushButton(tr("< Remove"), movePane);
    moveLayout->addWidget(addMemberButton); moveLayout->addWidget(removeMemberButton); moveLayout->addStretch();
    auto *memberPane = new QWidget(profileSplitter); auto *memberLayout = new QVBoxLayout(memberPane);
    memberLayout->addWidget(new QLabel(tr("Profile files — File · Declared identities · Why · Status"), memberPane));
    memberSearch = new QLineEdit(memberPane); memberSearch->setObjectName("MibProfileMemberFilter");
    memberSearch->setPlaceholderText(tr("Filter profile"));
    memberList = new QListWidget(memberPane); memberList->setObjectName("MibProfileMemberList");
    memberList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    includeStandards = new QCheckBox(tr("Include standard SNMP / MIB-II base"), memberPane);
    includeStandards->setObjectName("MibProfileIncludeStandards");
    includeStandards->hide();
    memberLayout->addWidget(memberSearch); memberLayout->addWidget(memberList); memberLayout->addWidget(includeStandards);
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
    profileLayout->addWidget(profileSplitter);
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
            !migrationError.isEmpty()) status->setText(migrationError);
        refresh();
    });
    connect(checkDependencies, &QPushButton::clicked, this, [this, checkDependencies]() {
        if (!callbacks.checkDependencies) return;
        checkDependencies->setEnabled(false);
        status->setText(tr("Checking library dependencies..."));
        QString error;
        const bool succeeded = callbacks.checkDependencies(&error);
        checkDependencies->setEnabled(true);
        if (!succeeded) status->setText(error);
        else { refresh(); status->setText(tr("Library dependency check complete")); }
    });
    connect(browseRoot, &QPushButton::clicked, this, &MibLibraryWidget::browseLibraryRoot);
    connect(openRoot, &QPushButton::clicked, this, &MibLibraryWidget::openLibraryRoot);
    connect(importButton, &QPushButton::clicked, this, &MibLibraryWidget::importFiles);
    connect(openProfileFolderButton, &QPushButton::clicked, this, &MibLibraryWidget::openProfileFolder);
    connect(cancelButton, &QPushButton::clicked, transport, &MibDownloadTransport::cancel);
    connect(downloadButton, &QPushButton::clicked, this, [this]() { downloadSelected(false); });
    connect(checkUpdates, &QPushButton::clicked, this, [this]() {
        status->setText(tr("No reliably comparable installed revisions are available"));
    });
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
    connect(addFilesButton, &QPushButton::clicked, this, &MibLibraryWidget::addProfileFiles);
    connect(addFolderButton, &QPushButton::clicked, this, &MibLibraryWidget::addProfileFolder);
    connect(removeMemberButton, &QPushButton::clicked, this, &MibLibraryWidget::removeProfileMembers);
    connect(includeStandards, &QCheckBox::toggled, this, [this]() { saveCurrentProfile(); });
    connect(availableSearch, &QLineEdit::textChanged, this, [this](const QString &text) {
        for (int i=0;i<availableList->count();++i) availableList->item(i)->setHidden(!availableList->item(i)->text().contains(text, Qt::CaseInsensitive));
    });
    connect(memberSearch, &QLineEdit::textChanged, this, [this](const QString &text) {
        for (int i=0;i<memberList->count();++i) memberList->item(i)->setHidden(!memberList->item(i)->text().contains(text, Qt::CaseInsensitive));
    });
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
            status->setText(tr("%1 failed\nStatus: Failed\nReason: %2")
                            .arg(module, reason));
            DiagnosticLogger::log("MIB", tr("Download failed module=%1 initial=%2 http=%3 final=%4 reason=%5 redirects=%6")
                .arg(module, result.initialUrl.toString(QUrl::FullyEncoded))
                .arg(result.httpStatus)
                .arg(result.finalUrl.toString(QUrl::FullyEncoded), reason,
                     result.redirectTrace.join(QStringLiteral(" | "))));
            const QString visibleFailure = status->text();
            downloadQueue.clear();
            setOperationActive(false); refresh();
            status->setText(visibleFailure); return;
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
    status->setText(tr("Catalog loadingâ€¦"));
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
    libraryEmptyState->setVisible(records.isEmpty());
    status->setText(tr("%n modules in library", nullptr, records.size()));
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
    status->setText(tr("Refreshing authoritative IANA catalog…"));
    DiagnosticLogger::log("MIB", status->text());
    transport->get(IanaMibSourceProvider::indexUrl());
}

void MibLibraryWidget::refresh()
{
    refreshImpl(true);
}

void MibLibraryWidget::refreshImpl(bool prepareProfile)
{
    QElapsedTimer totalTimer; totalTimer.start();
    QElapsedTimer phaseTimer; phaseTimer.start();
    ++counters.automaticProfileScans;
    profiles.migrateLegacyFolderProfiles();
    const qint64 automaticProfilesMs = phaseTimer.restart();
    const QStringList previouslySelected = selectedModules();
    const QSet<QString> selectedBefore(previouslySelected.cbegin(), previouslySelected.cend());
    const QString currentModule = table->currentRow() >= 0
        ? table->item(table->currentRow(), 1)->text() : QString();
    const int verticalPosition = table->verticalScrollBar()->value();
    const QSignalBlocker tableBlocker(table);
    const QList<MibModuleRecord> localRecords = callbacks.localInventory
        ? callbacks.localInventory() : QList<MibModuleRecord>{};
    const qint64 localProjectionMs = phaseTimer.restart();
    ++counters.inventoryBuilds;
    records = library.inventory(bundledPaths, catalog, localRecords);
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
    table->setUpdatesEnabled(false);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
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
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table->setUpdatesEnabled(true);
    const QString oldSource = sourceFilter->currentText();
    sourceFilter->clear(); sourceFilter->addItems(sources);
    sourceFilter->setCurrentText(oldSource); applyFilter();
    libraryEmptyState->setVisible(records.isEmpty());
    const qint64 filterMs = phaseTimer.restart();
    if (!currentModule.isEmpty()) {
        for (int row = 0; row < table->rowCount(); ++row)
            if (table->item(row, 1)->text() == currentModule) {
                table->setCurrentCell(row, 1); break;
            }
    }
    table->verticalScrollBar()->setValue(verticalPosition);
    status->setText(sourceStatusText.isEmpty()
        ? tr("%n modules in library", nullptr, records.size())
        : tr("%n modules in library — %1", nullptr, records.size()).arg(sourceStatusText));
    ++counters.profilePopulations;
    if (prepareProfile) {
        refreshProfiles(profileCombo && profileCombo->currentIndex() >= 0
            ? profileCombo->currentData().toString() : QString());
    } else {
        QString id = QSettings().value("mib-library/current-profile",
            MibProfileDefinitions::standardsId()).toString();
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
    const qint64 detailsMs = phaseTimer.elapsed();
    DiagnosticLogger::log("MIB", tr("Library refresh total-ms=%1 automatic-profiles-ms=%2 local-projection-ms=%3 inventory-ms=%4 table-model-ms=%5 filter-sort-ms=%6 profiles-ms=%7 details-layout-ms=%8 records=%9")
        .arg(totalTimer.elapsed()).arg(automaticProfilesMs).arg(localProjectionMs)
        .arg(inventoryMs).arg(tableBuildMs).arg(filterMs).arg(profilesMs)
        .arg(detailsMs).arg(records.size()));
}

MibProfileRecord MibLibraryWidget::currentProfile() const
{
    const MibProfileRecord *profile = profileCombo
        ? profiles.find(profileCombo->currentData().toString()) : nullptr;
    return profile ? *profile : MibProfileRecord{};
}

void MibLibraryWidget::reloadProfiles()
{
    QString error;
    profiles.reload(&error);
    QString id = QSettings().value("mib-library/current-profile",
        MibProfileDefinitions::standardsId()).toString();
    id = MibProfileDefinitions::validCurrentId(id, profiles.profiles());
    const QSignalBlocker blocker(profileCombo);
    profileCombo->clear();
    for (const MibProfileRecord &profile : profiles.profiles())
        profileCombo->addItem(profile.name, profile.id);
    profileCombo->setCurrentIndex(qMax(0, profileCombo->findData(id)));
    if (!error.isEmpty()) status->setText(error);
}

void MibLibraryWidget::publishPreparedProfile(const QString &id,
                                              const MibEffectivePlan &plan)
{
    const int index = profileCombo->findData(id);
    if (index < 0) return;
    {
        const QSignalBlocker blocker(profileCombo);
        profileCombo->setCurrentIndex(index);
    }
    QSettings().setValue("mib-library/current-profile", id);
    const MibProfileRecord *profile = profiles.find(id);
    if (!profile) return;
    const bool editable = profile->type == MibProfileType::Custom;
    renameProfileButton->setEnabled(editable);
    deleteProfileButton->setEnabled(editable);
    addMemberButton->setEnabled(editable);
    removeMemberButton->setEnabled(editable);
    profileSource->setText(tr("%n exact root file(s)", nullptr, profile->roots.size()));
    profileGuidance->setText(tr("Profile roots are a saved snapshot of exact files. Dependencies are derived when the Profile is activated."));
    refreshProfileLists(&plan);
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
    for (int row = 0; row < table->rowCount(); ++row) {
        const bool matchSearch = table->item(row, 1)->text().contains(search->text(), Qt::CaseInsensitive) ||
            table->item(row, 4)->text().contains(search->text(), Qt::CaseInsensitive) ||
            table->item(row, 5)->text().contains(search->text(), Qt::CaseInsensitive);
        const bool matchSource = sourceFilter->currentIndex() <= 0 ||
            table->item(row, 5)->text() == sourceFilter->currentText();
        const bool matchStatus = statusFilter->currentIndex() <= 0 ||
            table->item(row, 2)->text() == statusFilter->currentText();
        table->setRowHidden(row, !(matchSearch && matchSource && matchStatus));
    }
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
    selectedCount->setText(tr("%n selected for download", nullptr, totalSelected));
    const bool enabled = totalSelected > 0 && downloadQueue.isEmpty();
    downloadButton->setEnabled(enabled);
    const MibProfileRecord *profile = profileCombo
        ? profiles.find(profileCombo->currentData().toString()) : nullptr;
    const bool editableProfile = profile && profile->type == MibProfileType::Custom;
    installDependenciesButton->setEnabled(enabled && editableProfile);
    installDependenciesButton->setToolTip(editableProfile ? QString() :
        tr("Choose an editable Profile to add downloaded MIB files."));
}

void MibLibraryWidget::setOperationActive(bool active)
{
    cancelButton->setVisible(active);
    refreshButton->setEnabled(!active);
    if (active) { downloadButton->setEnabled(false); installDependenciesButton->setEnabled(false); }
    else updateSelectionUi();
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
    status->setText(plan.unresolved.isEmpty() ? tr("Dependency plan resolved") :
        tr("Unresolved: %1").arg(plan.unresolved.join(", ")));
    DiagnosticLogger::log("MIB", status->text());
}

void MibLibraryWidget::downloadSelected(bool loadAfter)
{
    requestedTargets = selectedModules();
    completedDownloads = 0;
    for (const QString &module : requestedTargets) sessionState.begin(module);
    QMap<QString, MibLibraryStatus> known;
    for (const auto &record : records) known.insert(record.moduleName, record.status);
    const MibDependencyPlan plan = MibDependencyResolver().resolve(requestedTargets, known, catalog);
    downloadQueue = plan.orderedDownloads; loadAfterDownload = loadAfter;
    if (downloadQueue.isEmpty()) { resolveSelected(); return; }
    setOperationActive(true); downloadNext();
}

void MibLibraryWidget::downloadNext()
{
    if (downloadQueue.isEmpty()) {
        setOperationActive(false); refresh();
        const QString outcome = callbacks.downloadsCompleted
            ? callbacks.downloadsCompleted(requestedTargets, loadAfterDownload, completedDownloads)
            : QString();
        status->setText(outcome.isEmpty()
            ? tr("Downloaded %n MIB file(s) to the Library.", nullptr, completedDownloads)
            : outcome);
        return;
    }
    const MibCatalogEntry *entry = catalog.find(downloadQueue.first());
    if (!entry) { status->setText(tr("No catalog entry for %1").arg(downloadQueue.first())); downloadQueue.clear(); setOperationActive(false); return; }
    status->setText(tr("Downloading %1 from %2").arg(entry->moduleName, entry->sourceName));
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

void MibLibraryWidget::selectProfile(const QString &id)
{
    const int index = profileCombo->findData(id);
    if (index >= 0 && index != profileCombo->currentIndex()) profileCombo->setCurrentIndex(index);
    else if (index >= 0) profileChanged();
}

void MibLibraryWidget::profileChanged()
{
    const QString id = profileCombo->currentData().toString();
    const MibProfileRecord *profile = profiles.find(id);
    if (!profile) return;
    const MibProfileRecord selectedProfile = *profile;
    QSettings().setValue("mib-library/current-profile", id);
    const bool editable = selectedProfile.type == MibProfileType::Custom;
    renameProfileButton->setEnabled(editable); deleteProfileButton->setEnabled(editable);
    addMemberButton->setEnabled(editable); removeMemberButton->setEnabled(editable);
    includeStandards->hide();
    profileSource->setText(tr("%n exact root file(s)", nullptr, selectedProfile.roots.size()));
    profileGuidance->setText(tr("Profile roots are a saved snapshot of exact files. Dependencies are derived when the Profile is activated."));
    openProfileFolderButton->hide();
    const MibEffectivePlan plan = planFor(selectedProfile);
    refreshProfileLists(&plan);
    for (int row = 0; row < table->rowCount(); ++row) {
        const QString module = table->item(row, 1)->text();
        table->item(row, 3)->setText(plan.effectiveModules.contains(module)
            ? tr("Included") : tr("Not included"));
    }
    updateSelectionUi();
    emit profileSelectionChanged(id, plan);
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
    status->setText(tr("Imported %1 file(s) to the Library; %2 identical file(s) already existed; %3 conflict(s) were preserved. Active Profile membership was not changed.")
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
    QStringList selectedAvailable, selectedMembers;
    for (QListWidgetItem *item : availableList->selectedItems()) selectedAvailable.append(item->text());
    for (QListWidgetItem *item : memberList->selectedItems()) selectedMembers.append(item->text());
    const int availableScroll = availableList->verticalScrollBar()->value();
    const int memberScroll = memberList->verticalScrollBar()->value();
    const QSignalBlocker blocker(includeStandards);
    includeStandards->setChecked(false);
    memberList->clear();
    if (!profile->roots.isEmpty()) {
        for (const auto &member : profile->roots) {
            MibProfileMemberState state = QFileInfo::exists(member.canonicalPath)
                ? MibProfileMemberState::Changed : MibProfileMemberState::Missing;
            for (const QString &identity : member.identities) {
                const MibEffectivePlanMember *resolved = effective.member(identity);
                if (resolved && resolved->provider.sha256 == member.sha256 &&
                    QFileInfo(resolved->provider.canonicalPath).absoluteFilePath().compare(
                        QFileInfo(member.canonicalPath).absoluteFilePath(), Qt::CaseInsensitive) == 0) {
                    state = MibProfileMemberState::Current;
                    break;
                }
            }
            const QString stateText = state == MibProfileMemberState::Current ? tr("OK")
                : state == MibProfileMemberState::Missing ? tr("Missing")
                : state == MibProfileMemberState::Changed ? tr("Changed") : tr("Catalog stale");
            const QString why = member.reason == MibProfileMemberReason::Dependency
                ? tr("Dependency") : tr("Added");
            auto *item = new QListWidgetItem(tr("%1 · %2 · %3 · %4").arg(
                QFileInfo(member.canonicalPath).fileName(),
                member.identities.join(QStringLiteral(", ")), why, stateText), memberList);
            item->setData(Qt::UserRole, member.canonicalPath);
            item->setToolTip(QDir::toNativeSeparators(member.canonicalPath));
            if (state != MibProfileMemberState::Current)
                item->setToolTip(item->toolTip() + (state == MibProfileMemberState::Missing
                    ? tr("\nProfile file is missing") : tr("\nProfile file content changed")));
        }
    }
    profileEmptyState->setVisible(memberList->count() == 0);
    profileEmptyState->setText(tr("This Profile is empty. Add files or take a one-time folder snapshot."));
    QStringList available = availableModuleNames();
    for (const QString &member : profile->explicitModules) available.removeAll(member);
    availableList->clear(); availableList->addItems(available);
    for (int row = 0; row < availableList->count(); ++row) {
        QListWidgetItem *item = availableList->item(row);
        item->setHidden(!item->text().contains(availableSearch->text(), Qt::CaseInsensitive));
        item->setSelected(selectedAvailable.contains(item->text()));
    }
    for (int row = 0; row < memberList->count(); ++row) {
        QListWidgetItem *item = memberList->item(row);
        item->setHidden(!item->text().contains(memberSearch->text(), Qt::CaseInsensitive));
        item->setSelected(selectedMembers.contains(item->text()));
        if (const MibEffectivePlanMember *member = effective.member(item->text())) {
            QStringList providers;
            for (const MibIndexedProvider &alternative : member->alternatives)
                providers.append(tr("%1 [%2]").arg(alternative.canonicalPath,
                    alternative.sha256.left(12)));
            const QString selected = member->provider.canonicalPath.isEmpty()
                ? tr("No provider selected")
                : tr("Selected: %1 [%2]").arg(member->provider.canonicalPath,
                    member->provider.sha256.left(12));
            item->setToolTip(selected + tr("\nReason: %1").arg(providerReasonText(member->providerReason)) +
                (providers.isEmpty() ? QString() :
                tr("\nProviders:\n%1").arg(providers.join('\n'))));
        }
    }
    availableList->verticalScrollBar()->setValue(availableScroll);
    memberList->verticalScrollBar()->setValue(memberScroll);
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
        profileSummary += tr(" · %1 conflicts").arg(effective.ambiguousModules.size());
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
    if (id.isEmpty()) { status->setText(error); return; }
    refreshProfiles(id); emit profilesChanged();
}

void MibLibraryWidget::duplicateProfile()
{
    const QString current = profileCombo->currentData().toString();
    bool ok = false; const QString name = QInputDialog::getText(this, tr("Duplicate MIB Profile"), tr("Name:"), QLineEdit::Normal, profileCombo->currentText() + tr(" Copy"), &ok);
    if (!ok) return; QString error; const QString id = profiles.duplicate(current, name, &error);
    if (id.isEmpty()) { status->setText(error); return; }
    refreshProfiles(id); emit profilesChanged();
}

void MibLibraryWidget::renameProfile()
{
    bool ok = false; const QString name = QInputDialog::getText(this, tr("Rename MIB Profile"), tr("Name:"), QLineEdit::Normal, profileCombo->currentText(), &ok);
    if (!ok) return; const QString id = profileCombo->currentData().toString(); QString error;
    if (!profiles.rename(id, name, &error)) { status->setText(error); return; }
    refreshProfiles(id); emit profilesChanged();
}

void MibLibraryWidget::deleteProfile()
{
    const QString id = profileCombo->currentData().toString(); QString error;
    if (!profiles.remove(id, &error)) { status->setText(error); return; }
    refreshProfiles(); emit profilesChanged();
}

void MibLibraryWidget::saveCurrentProfile()
{
    const MibProfileRecord *current = profiles.find(profileCombo->currentData().toString());
    if (!current || current->type != MibProfileType::Custom) return;
    MibProfileRecord changed = *current;
    QString error; if (!profiles.update(changed, &error)) status->setText(error);
    emit profilesChanged(); profileChanged();
}

void MibLibraryWidget::addProfileMembers()
{
    const MibProfileRecord *current = profiles.find(profileCombo->currentData().toString());
    if (!current || current->type != MibProfileType::Custom) return;
    QStringList files;
    for (QListWidgetItem *item : availableList->selectedItems()) {
        const auto record = std::find_if(records.cbegin(), records.cend(), [item](const auto &candidate) {
            return candidate.moduleName == item->text() && !candidate.localPath.isEmpty();
        });
        if (record != records.cend()) files.append(record->localPath);
    }
    QString error;
    if (!profiles.addFiles(current->id, files, MibProfileMemberReason::Added, &error))
        status->setText(error.isEmpty() ? tr("Select Catalog entries with local files") : error);
    emit profilesChanged(); profileChanged();
}

void MibLibraryWidget::addProfileFiles()
{
    const MibProfileRecord *current = profiles.find(profileCombo->currentData().toString());
    if (!current || current->type != MibProfileType::Custom) return;
    const QStringList files = QFileDialog::getOpenFileNames(this, tr("Add MIB Files to Profile"),
        library.rootPath(), tr("MIB files (*.mib *.my *.smiv1 *.smiv2 *.txt *-MIB);;All files (*)"));
    if (files.isEmpty()) return;
    QString error;
    if (!profiles.addFiles(current->id, files, MibProfileMemberReason::Added, &error))
        status->setText(error);
    emit profilesChanged(); profileChanged();
}

void MibLibraryWidget::addProfileFolder()
{
    const MibProfileRecord *current = profiles.find(profileCombo->currentData().toString());
    if (!current || current->type != MibProfileType::Custom) return;
    const QString folder = QFileDialog::getExistingDirectory(this,
        tr("Add Folder Snapshot to Profile"), library.rootPath());
    if (folder.isEmpty()) return;
    QString error;
    if (!profiles.addFolder(current->id, folder, &error)) status->setText(error);
    else status->setText(tr("Added a recursive snapshot of eligible files. Later folder changes will not alter this Profile."));
    emit profilesChanged(); profileChanged();
}

void MibLibraryWidget::removeProfileMembers()
{
    const MibProfileRecord *current = profiles.find(profileCombo->currentData().toString());
    if (!current || current->type != MibProfileType::Custom) return;
    MibProfileRecord changed = *current;
    for (QListWidgetItem *item : memberList->selectedItems()) {
        const QString path = item->data(Qt::UserRole).toString();
        if (!path.isEmpty()) changed.roots.erase(std::remove_if(changed.roots.begin(), changed.roots.end(),
            [&path](const auto &member) {
#ifdef Q_OS_WIN
                return member.canonicalPath.compare(path, Qt::CaseInsensitive) == 0;
#else
                return member.canonicalPath == path;
#endif
            }), changed.roots.end());
        else changed.explicitModules.removeAll(item->text());
    }
    changed.explicitModules = MibProfileMemberIdentities(changed.roots);
    QString error; if (!profiles.update(changed, &error)) status->setText(error);
    emit profilesChanged(); profileChanged();
}
