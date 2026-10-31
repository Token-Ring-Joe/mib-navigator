/*
    Copyright (C) 2004-2011 Martin Jolicoeur (snmpb1@gmail.com) 

    This file is part of the SnmpB project 
    (http://sourceforge.net/projects/snmpb)

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#include <functional>
#include <QtGui>
#include <qfileinfo.h>
#include <qdir.h>
#include <qmessagebox.h>
#include <QFileDialog>
#include <QFile>
#include <qdockwidget.h>
#include <QGridLayout>
#include <QComboBox>
#include <QLabel>
#include <QCheckBox>
#include <QPlainTextEdit>
#include <QCryptographicHash>
#include <QTableWidget>
#include <QHeaderView>
#include <QSplitter>
#include <QElapsedTimer>
#include <QHeaderView>
#include <QLineEdit>
#include <QTableWidget>
#include <QToolButton>
#include <qmenu.h>
#include "snmpb.h"
#include "mibmodule.h"
#include "agent.h"
#include "trap.h"
#if SNMPB_ENABLE_QWT
#include "graphmanager.h"
#endif
#include "logsnmpb.h"
#include "mibeditor.h"
#include "discovery.h"
#include "devicepane.h"
#include "mainwindowlayout.h"
#include "workbenchshell.h"
#include "startupcoordinator.h"
#include "productidentity.h"
#include "diagnosticlogger.h"
#include "miblibrarywidget.h"
#include "mibprofile.h"
#include "miblegacymigration.h"

#include "agentprofile.h"
#include "agentprofileservice.h"
#include "profilemetadataservice.h"
#include "profiletransfer.h"
#include "discoverydestination.h"
#include "mibcollection.h"
#include "usmprofile.h"
#include "communitycredentialservice.h"
#include "communitycredentialmanager.h"
#include "usmcredentialservice.h"
#include "preferences.h"
#include <QPointer>
#include <QThreadPool>

// These are needed to get the libraries version strings for the about box
#include "mibengine.h"
#include "tomcrypt.h"
#if SNMPB_ENABLE_QWT
#include "qwt.h"
#endif
#include "snmp_pp/config_snmp_pp.h"

#define SMI_CONFIG_FILE          "smi.conf"
#define BOOT_COUNTER_CONFIG_FILE "boot_counter.conf"
#define USM_USERS_CONFIG_FILE    "usm_users.conf"
#define AGENTS_CONFIG_FILE       "agents.conf"
#define LOG_CONFIG_FILE          "log.conf"
#define GRAPHS_CONFIG_FILE       "graphs.conf"
#define DEVICE_TREE_CONFIG_FILE  "device-tree.conf"
#define PROFILE_METADATA_CONFIG_FILE "profile-metadata.conf"
#define CREDENTIAL_IDENTITIES_CONFIG_FILE "credential-identities.conf"
#define COMMUNITY_CREDENTIALS_CONFIG_FILE "community-credentials.conf"
#define CREDENTIAL_BINDINGS_CONFIG_FILE "credential-bindings.conf"

#if SNMPB_ENABLE_QWT
#define SNMPB_QWT_ABOUT_LINE \
    "QWT [v%5] (<a href=http://qwt.sourceforge.net>http://qwt.sourceforge.net</a>)<br>"
#define SNMPB_QT_ABOUT_LINE \
    "QT [v%6] (<a href=http://qt.io>http://qt.io</a>)"
#else
#define SNMPB_QWT_ABOUT_LINE ""
#define SNMPB_QT_ABOUT_LINE \
    "QT [v%5] (<a href=http://qt.io>http://qt.io</a>)"
#endif


namespace {
// One description of the Environment, so the header badge, status bar, startup state and
// Problems tab never disagree. "Needs attention" means something the user may want to
// look at; the Environment is still usable.
struct EnvironmentPresentation {
    QString state;          // short word or phrase for the badge
    QString badge;          // style key: ready / warning / empty
    int problems = 0;       // errors + warnings (notes are not problems)
    bool needsAttention = false;
};

EnvironmentPresentation PresentEnvironment(const MibEnvironmentPtr &environment)
{
    EnvironmentPresentation result;
    if (!environment) {
        result.state = QObject::tr("Empty");
        result.badge = QStringLiteral("empty");
        return result;
    }
    const MibProblemSummary summary = MibSummarizeProblems(environment->problems());
    result.problems = summary.errors + summary.warnings;
    if (environment->loadedCount() == 0 && result.problems == 0) {
        result.state = QObject::tr("Empty");
        result.badge = QStringLiteral("empty");
        return result;
    }
    if (environment->loadedCount() == 0) {
        result.state = QObject::tr("Nothing loaded · %n problem(s)", nullptr, result.problems);
        result.badge = QStringLiteral("warning");
        result.needsAttention = true;
        return result;
    }
    if (result.problems > 0 || environment->status() == MibEnvironmentStatus::Partial) {
        if (result.problems == 0) result.problems = 1;
        result.state = QObject::tr("Loaded with %n problem(s)", nullptr, result.problems);
        result.badge = QStringLiteral("warning");
        result.needsAttention = true;
    } else {
        result.state = QObject::tr("Ready");
        result.badge = QStringLiteral("ready");
    }
    return result;
}
}

Snmpb::Snmpb(bool offline)
{
    QElapsedTimer settingsTimer; settingsTimer.start();
    DiagnosticLogger::log("Startup", "settings initialization begin");
    // First thing to do is to give up root privileges that allow permission to
    // bind on privileged ports (<1024). This is needed to bind on 
    // the RFC-defined trap port number 162 on UNIX machines.
    prefs = new Preferences(this);
    DiagnosticLogger::log("Startup", QStringLiteral("preferences load complete elapsed_ms=%1").arg(settingsTimer.elapsed()));
#ifndef WIN32 
    // Allows to bind on privileged ports only if it is the standard trap port...
    if (! (prefs->ShouldListenStdTrapPort4() || prefs->ShouldListenStdTrapPort6()))
    {
        setuid(getuid());
    }
#endif
    agent = new Agent(this, offline);
    DiagnosticLogger::log("Traps", "trap receiver construction complete");
    start_issuccess = agent->GetStartupResult(start_msg);    
#ifndef WIN32 
    // Drop root privileges
    if (setuid(getuid()) < 0)
    {
        printf("Unable to drop root privileges: %m\n");
    }
#endif
    // Note: beware as anything BEFORE this point is run as root on UNIX ... 

    CheckForConfigFiles();
    DiagnosticLogger::log("Startup", QStringLiteral("settings initialization complete elapsed_ms=%1").arg(settingsTimer.elapsed()));
}

Snmpb::~Snmpb()
{
    delete agent;
    agent = nullptr;
}

void Snmpb::BindToGUI(QMainWindow* mw)
{
    mainWindow = mw;
    startup = new StartupCoordinator(this);
    const quint64 startupGeneration = startup->begin();
    if (start_issuccess == false)
    {
        QMessageBox::critical(nullptr, tr("MIB Navigator"), start_msg, QMessageBox::Ok);

        // Desperate measures: delete the preferences file so 
        // at the next startup, the app might have a chance to start
        QFile(GetSmiConfigFile()).remove();
        QFile(QSettings().fileName()).remove(); // this one is probably overkill
        exit (-1);
    }
    else
    {
        if (start_msg != "")
            QMessageBox::warning(nullptr, tr("MIB Navigator"), start_msg, QMessageBox::Ok);
        agent->StartTrapTimer();
    }

    w.setupUi(mw);
    DiagnosticLogger::log("UI", "main window UI setup complete");
    // The Connections navigator is the visible authoritative selector. These
    // controls remain hidden temporarily as compatibility state for legacy
    // request consumers while selection is synchronized by stable profile ID.
    w.AgentProperties->hide();
    w.actionManageAgentProfiles->setVisible(false);
    prefs->RestoreWindowGeometry(*mw);
    EnsureAccessibleWindowGeometry(mw);
    static auto saver = std::bind(&Preferences::SaveWindowGeometry, std::ref(*mw));
    connect(QApplication::instance(), &QCoreApplication::aboutToQuit, saver);

    connect(&loader, SIGNAL ( LogError(QString) ),
            w.LogOutput, SLOT ( append (QString) ));

    // Creation order is VERY important here
    logsnmpb = new LogSnmpb(this);
    DiagnosticLogger::attachLogWidget(w.LogOutput);
    QSettings mibRootSettings;
    const bool migrationComplete = mibRootSettings.value(
        "mib-library/migration-v1-complete", false).toBool();
    MibCollectionResult collectionResult = MibCollection(
        MibCollection::configuredRoot(mibRootSettings)).initialize(
            prefs->DefaultMibPaths(), migrationComplete ? QString() : MibCollection::legacyManagedRoot());
    if (!collectionResult.success)
        DiagnosticLogger::log("MIB", tr("MIB collection initialization failed: %1")
                              .arg(collectionResult.error));
    else {
        mibRootSettings.setValue("mib-library/migration-v1-complete", true);
        DiagnosticLogger::log("MIB", tr("MIB collection initialized standards-copied=%1 imported-copied=%2 identical=%3 conflicts=%4")
            .arg(collectionResult.standardsCopied).arg(collectionResult.importedCopied)
            .arg(collectionResult.identicalSkipped).arg(collectionResult.conflicts.size()));
    }
    QElapsedTimer mibStartupTimer; mibStartupTimer.start();
    modules = new MibModule(this);
    DiagnosticLogger::log("Startup", QStringLiteral("MIB initialization complete elapsed_ms=%1").arg(mibStartupTimer.elapsed()));
    profileService = new AgentProfileService(GetAgentsConfigFile(), this);
    DiagnosticLogger::log("Connections", QStringLiteral("profiles loaded count=%1")
                          .arg(profileService->profiles().size()));
    communityCredentialService = new CommunityCredentialService(
        GetCommunityCredentialsConfigFile(), GetCredentialBindingsConfigFile(), this);
    profileMetadataService = new ProfileMetadataService(
        GetProfileMetadataConfigFile(), this);
    DiagnosticLogger::log("Connections", QStringLiteral("profile metadata loaded count=%1")
                          .arg(profileMetadataService->allMetadata().size()));
    devicePlacementService = new DeviceTreePlacementService(
        GetDeviceTreeConfigFile(), this);
    apm = new AgentProfileManager(this, profileService, profileMetadataService);
    QElapsedTimer phaseTimer; phaseTimer.start(); prefs->Init();
    DiagnosticLogger::log("Startup", QStringLiteral("settings UI load complete elapsed_ms=%1").arg(phaseTimer.elapsed()));
    trap = new Trap(this);
    agent->Init();
    upm = new USMProfileManager(this);
    connect(upm, &USMProfileManager::CredentialsChanged,
            apm, &AgentProfileManager::RefreshCredentialChoices);
#if SNMPB_ENABLE_QWT
    gm = new GraphManager(this);
#else
    w.TabW->setTabVisible(w.TabW->indexOf(w.GraphsTab), false);
#endif
    editor = new MibEditor(this);
    MibLibraryWidget::Callbacks mibLibraryCallbacks;
    mibLibraryCallbacks.validate = [this](const QString &path, QString *error) {
        return modules->ValidateModuleFile(path, error);
    };
    mibLibraryCallbacks.downloadsCompleted = [this](const QStringList &requested, bool load,
                                                     int downloaded) -> QString {
        modules->RescanPath(); QString indexError; modules->RefreshDependencyIndex(&indexError);
        if (!indexError.isEmpty()) DiagnosticLogger::log("MIB", indexError);
        if (!load) return tr("Downloaded %n MIB file(s) to the Library.", nullptr, downloaded);

        const QString activeProfileId = QSettings().value(
            "mib-library/current-profile", MibProfileDefinitions::standardsId()).toString();
        QStringList exactFiles;
        const MibRuntimeRequestNormalization normalized =
            MibNormalizeRuntimeRequests(requested, *modules->DependencyIndex());
        for (const QString &identity : normalized.identities) {
            const auto providers = modules->DependencyIndex()->providersFor(identity);
            if (providers.size() == 1) exactFiles.append(providers.first().canonicalPath);
        }
        exactFiles.removeDuplicates();
        QString profileError;
        const bool updated = mibLibrary->profileService()->addFiles(
            activeProfileId, exactFiles, MibProfileMemberReason::Added, &profileError);
        if (updated) {
            DiagnosticLogger::log("MIB", tr("Downloaded exact MIB files applied to Profile '%1': %2")
                .arg(activeProfileId, exactFiles.join(QStringLiteral(", "))));
            mibLibrary->selectProfile(activeProfileId);
            const MibProfileRecord *active = mibLibrary->profileService()->find(activeProfileId);
            return tr("Downloaded %1 MIB file(s) to the Library. Added %2 unambiguous exact file(s) to Profile '%3'.")
                .arg(downloaded).arg(exactFiles.size())
                .arg(active ? active->name : activeProfileId);
        } else {
            DiagnosticLogger::log("MIB", tr("Downloaded files were not added to the Profile: %1").arg(profileError));
            return tr("Downloaded %n MIB file(s) to the Library, but exact Profile membership was not changed: %1",
                      nullptr, downloaded).arg(profileError);
        }
    };
    mibLibraryCallbacks.metadata = [this](const QString &module, const QString &path) {
        return modules->ModuleMetadata(module, path);
    };
    mibLibraryCallbacks.localInventory = [this]() {
        return modules->AvailableModuleRecords();
    };
    const auto libraryDependencySummary = [this]() {
        MibLibraryWidget::DependencySummary summary;
        MibDependencyIndex *index = modules->DependencyIndex();
        const QStringList moduleNames = index->moduleNames();
        summary.stale = modules->DependencyIndexStale();
        summary.knownModules = moduleNames.size();
        QSet<QString> unresolved;
        QSet<QString> ambiguous;
        for (const QString &module : moduleNames) {
            const QStringList imports = index->imports(module);
            summary.relationships += imports.size();
            for (const QString &dependency : imports) {
                const MibProviderStatus providerStatus = index->provider(dependency).status;
                if (providerStatus == MibProviderStatus::Missing) unresolved.insert(dependency);
                else if (providerStatus == MibProviderStatus::Ambiguous) ambiguous.insert(dependency);
            }
        }
        summary.unresolved = unresolved.size();
        summary.ambiguous = ambiguous.size();
        for (const MibDependencyFileRecord &record : index->files())
            if (record.lastCheckedUtc > summary.lastCheckedUtc)
                summary.lastCheckedUtc = record.lastCheckedUtc;
        return summary;
    };
    mibLibraryCallbacks.libraryDependencySummary = libraryDependencySummary;
    mibLibraryCallbacks.collectionChanged = [this]() {
        modules->RescanPath();
        QString error;
        modules->RefreshDependencyIndex(&error);
        if (!error.isEmpty()) DiagnosticLogger::log("MIB", error);
    };
    mibLibraryCallbacks.retryLegacyProfiles = [this](MibProfileService &profiles,
                                                      QString *error) {
        if (!profiles.hasUnresolvedLegacyResidue() &&
            !profiles.requiresLegacyConversion()) return true;
        return profiles.migrateLegacyProfiles(*modules->DependencyIndex(), error);
    };
    mibLibraryCallbacks.checkDependencies = [this](QString *error) {
        modules->CheckProfileDependencies(QStringLiteral("mib-library"),
            modules->DependencyIndex()->moduleNames(), false, error);
        return !error || error->isEmpty();
    };
    mibLibraryCallbacks.effectivePlan = [this](const MibProfileRecord &profile) {
        return modules->BuildEffectivePlan(profile);
    };
    phaseTimer.restart();
    mibLibrary = new MibLibraryWidget(prefs->DefaultMibPaths(), w.TabW, nullptr,
                                      mibLibraryCallbacks);
    DiagnosticLogger::log("Startup", QStringLiteral("Inventory construction complete elapsed_ms=%1").arg(phaseTimer.elapsed()));
    const int legacyModulesTab = w.TabW->indexOf(w.ModulesTab);
    if (legacyModulesTab >= 0) w.TabW->removeTab(legacyModulesTab);
    const int mibsTab = w.TabW->insertTab(1, mibLibrary, tr("MIBs"));
    w.TabW->setTabToolTip(mibsTab,
        tr("Browse the MIB library and choose an exact-file Profile."));
    const auto refreshLibraryDependencyStatus = [this, libraryDependencySummary]() {
        const MibLibraryWidget::DependencySummary summary = libraryDependencySummary();
        if (summary.stale) w.MibLibraryDependencyState->setText(tr("Dependencies need checking"));
        else if (summary.unresolved > 0 || summary.ambiguous > 0)
            w.MibLibraryDependencyState->setText(tr("Dependencies: Has unresolved dependencies"));
        else w.MibLibraryDependencyState->setText(tr("Dependencies: Up to date"));
        QString text = tr("%1 modules · %2 relationships · %3 unresolved · %4 ambiguous")
            .arg(summary.knownModules).arg(summary.relationships)
            .arg(summary.unresolved).arg(summary.ambiguous);
        if (summary.lastCheckedUtc.isValid()) text += tr(" · Last checked: %1")
            .arg(summary.lastCheckedUtc.toLocalTime().toString(Qt::ISODate));
        w.MibLibraryDependencySummary->setText(text);
    };
    refreshLibraryDependencyStatus();
    connect(w.MibLibraryCheckDependencies, &QPushButton::clicked, this,
            [this, refreshLibraryDependencyStatus]() {
        QElapsedTimer dependencyTimer;
        dependencyTimer.start();
        w.MibLibraryCheckDependencies->setEnabled(false);
        w.MibLibraryDependencyState->setText(tr("Dependencies: Checking…"));
        QString error;
        const MibProfileDependencyCheck before = modules->CachedProfileDependencies(
            QStringLiteral("mib-library"), MibDependencyIndex::profileSignature(
                modules->DependencyIndex()->moduleNames(), false));
        MibProfileDependencyCheck after;
        if (error.isEmpty()) after = modules->CheckProfileDependencies(QStringLiteral("mib-library"),
            modules->DependencyIndex()->moduleNames(), false, &error);
        const bool semanticVerificationRan = before.checkedUtc != after.checkedUtc;
        QElapsedTimer restoreTimer;
        restoreTimer.start();
        if (semanticVerificationRan) modules->Refresh();
        const qint64 restoreMs = semanticVerificationRan ? restoreTimer.elapsed() : 0;
        w.MibLibraryCheckDependencies->setEnabled(true);
        if (!error.isEmpty()) {
            w.MibLibraryDependencyState->setText(error);
            return;
        }
        mibLibrary->refresh();
        refreshLibraryDependencyStatus();
        DiagnosticLogger::log("MIB", tr("Check Dependencies UI total-ms=%1 runtime-restore-ms=%2 semantic-ran=%3")
            .arg(dependencyTimer.elapsed()).arg(restoreMs)
            .arg(semanticVerificationRan ? QStringLiteral("yes") : QStringLiteral("no")));
    });
    connect(modules, &MibModule::inventoryChanged, mibLibrary, &MibLibraryWidget::refresh);
    connect(modules, &MibModule::inventoryChanged, this, refreshLibraryDependencyStatus);
    connect(editor, &MibEditor::FileSaved, mw, [this](const QString &path) {
        const QString saved = QFileInfo(path).canonicalFilePath();
        if (saved.isEmpty()) return;
        QString indexError;
        modules->RefreshDependencyIndex(&indexError);
        QString hash;
        if (QFile file(saved); file.open(QIODevice::ReadOnly))
            hash = QString::fromLatin1(QCryptographicHash::hash(file.readAll(),
                                                                QCryptographicHash::Sha256).toHex());
        const int updated = hash.isEmpty() ? 0 : mibLibrary->profileService()->refreshRootHash(saved, hash);
        const MibProfileRecord current = mibLibrary->currentProfile();
        bool involved = false;
        for (const MibProfileMember &root : current.roots)
            involved = involved || QFileInfo(root.canonicalPath).canonicalFilePath() == saved;
        for (const QString &scope : MibProfileScopePaths(current)) {
            QString prefix = QDir::cleanPath(QFileInfo(scope).canonicalFilePath());
            if (!prefix.isEmpty() && !prefix.endsWith('/')) prefix += '/';
            involved = involved || (!prefix.isEmpty() && saved.startsWith(prefix));
        }
        DiagnosticLogger::log("MIB", QStringLiteral(
            "Saved MIB %1: library index refreshed%2, %3 profile root hash(es) updated, active profile %4")
            .arg(saved, indexError.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(indexError))
            .arg(updated).arg(involved ? QStringLiteral("rebuilding") : QStringLiteral("unaffected")));
        if (involved && !current.id.isEmpty()) mibLibrary->selectProfile(current.id);
    });
    connect(mibLibrary, &MibLibraryWidget::openModuleRequested, mw,
            [this](const QString &path) {
        editor->MibFileOpen(path);
        ShowEditorDocument();
    });
    auto *browserProfile = new QComboBox(w.MIBTreeLayout);
    browserProfile->setObjectName(QStringLiteral("MibBrowserProfileSelector"));
    auto *browserProfileRow = new QWidget(w.MIBTreeLayout);
    auto *browserProfileLayout = new QHBoxLayout(browserProfileRow);
    browserProfileLayout->setContentsMargins(0, 0, 0, 4);
    browserProfileLayout->addWidget(new QLabel(tr("Active MIB Profile:"), browserProfileRow));
    browserProfileLayout->addWidget(browserProfile, 1);
    qobject_cast<QVBoxLayout *>(w.MIBTreeLayout->layout())->insertWidget(1, browserProfileRow);

    auto *queryTools = new QWidget(w.MIBTreeLayout);
    queryTools->setObjectName(QStringLiteral("QueryToolBar"));
    auto *queryToolsLayout = new QHBoxLayout(queryTools);
    queryToolsLayout->setContentsMargins(0, 3, 0, 3);
    queryToolsLayout->setSpacing(4);
    const auto addQueryButton = [queryTools, queryToolsLayout](const QString &text,
                                                               QStyle::StandardPixmap icon) {
        auto *button = new QToolButton(queryTools);
        button->setText(text);
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        button->setIcon(button->style()->standardIcon(icon));
        queryToolsLayout->addWidget(button);
        return button;
    };
    auto *getButton = addQueryButton(tr("Get"), QStyle::SP_ArrowRight);
    auto *getNextButton = addQueryButton(tr("Get Next"), QStyle::SP_ArrowForward);
    auto *getBulkButton = addQueryButton(tr("Get Bulk"), QStyle::SP_FileDialogListView);
    auto *walkButton = addQueryButton(tr("Walk"), QStyle::SP_ArrowDown);
    auto *tableButton = addQueryButton(tr("Table"), QStyle::SP_FileDialogDetailedView);
    auto *setButton = addQueryButton(tr("Set"), QStyle::SP_DialogApplyButton);
    auto *stopButton = addQueryButton(tr("Stop"), QStyle::SP_BrowserStop);
    const QList<QToolButton *> mibDependentActions{getButton, getNextButton, getBulkButton,
                                                   walkButton, tableButton, setButton};
    for (QToolButton *button : mibDependentActions) {
        button->setEnabled(false);
        button->setToolTip(tr("Available after the MIB Environment is ready"));
    }
    connect(getButton, &QToolButton::clicked, w.MIBTree, [this]() { w.MIBTree->QueryCurrent(0); });
    connect(getNextButton, &QToolButton::clicked, w.MIBTree, [this]() { w.MIBTree->QueryCurrent(1); });
    connect(getBulkButton, &QToolButton::clicked, w.MIBTree, [this]() { w.MIBTree->QueryCurrent(2); });
    connect(walkButton, &QToolButton::clicked, w.MIBTree, &MibModelView::WalkCurrent);
    connect(tableButton, &QToolButton::clicked, w.MIBTree, &MibModelView::QueryTableFromCurrent);
    connect(setButton, &QToolButton::clicked, w.MIBTree, &MibModelView::SetCurrent);
    connect(stopButton, &QToolButton::clicked, w.actionStop, &QAction::trigger);
    connect(w.MIBTree, &MibModelView::QueryTableAvailabilityChanged,
            tableButton, [this, tableButton](bool available) {
        tableButton->setEnabled(available && startup &&
            (startup->state() == StartupState::Ready ||
             startup->state() == StartupState::NeedsAttention));
    });
    connect(w.actionStop, &QAction::changed, stopButton,
            [stopButton, action=w.actionStop]() { stopButton->setEnabled(action->isEnabled()); });
    qobject_cast<QVBoxLayout *>(w.MIBTreeLayout->layout())->insertWidget(2, queryTools);

    auto *objectRow = new QWidget(w.MIBTreeLayout);
    auto *objectLayout = new QHBoxLayout(objectRow);
    objectLayout->setContentsMargins(0, 0, 0, 4);
    objectLayout->setSpacing(6);
    objectLayout->addWidget(new QLabel(tr("Object / OID:"), objectRow));
    auto *objectAddress = new QLineEdit(objectRow);
    objectAddress->setObjectName(QStringLiteral("QueryObjectAddress"));
    objectAddress->setPlaceholderText(tr("Numeric OID or current Tree object"));
    objectAddress->setClearButtonEnabled(true);
    objectLayout->addWidget(objectAddress, 2);
    objectLayout->addWidget(new QLabel(tr("Filter Tree:"), objectRow));
    auto *treeFilter = new QLineEdit(objectRow);
    treeFilter->setObjectName(QStringLiteral("MibTreeFilter"));
    treeFilter->setPlaceholderText(tr("Name, module, or OID prefix"));
    treeFilter->setClearButtonEnabled(true);
    objectLayout->addWidget(treeFilter, 1);
    connect(objectAddress, &QLineEdit::returnPressed, w.MIBTree,
            [this, objectAddress]() { w.MIBTree->SelectFromOid(objectAddress->text().trimmed()); });
    connect(w.MIBTree, &MibModelView::CurrentObjectChanged, objectAddress, &QLineEdit::setText);
    connect(treeFilter, &QLineEdit::textChanged, w.MIBTree, &MibModelView::SetFilterText);
    qobject_cast<QVBoxLayout *>(w.MIBTreeLayout->layout())->insertWidget(3, objectRow);

    auto *resultTable = new QTableWidget(w.QueryLayout);
    resultTable->setObjectName(QStringLiteral("StructuredQueryResults"));
    resultTable->setColumnCount(6);
    resultTable->setHorizontalHeaderLabels({tr("Object"), tr("Instance"), tr("OID"),
                                            tr("Syntax"), tr("Value"), tr("Time")});
    resultTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    resultTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    resultTable->setSortingEnabled(true);
    resultTable->setAlternatingRowColors(true);
    resultTable->verticalHeader()->setVisible(false);
    resultTable->horizontalHeader()->setStretchLastSection(true);
    for (int column = 0; column < 4; ++column)
        resultTable->horizontalHeader()->setSectionResizeMode(column, QHeaderView::ResizeToContents);
    qobject_cast<QVBoxLayout *>(w.QueryLayout->layout())->insertWidget(1, resultTable, 2);
    w.Query->setToolTip(tr("Raw protocol and diagnostic output"));
    const auto refreshMibProfiles = [this, browserProfile]() {
        const QString selected = QSettings().value("mib-library/current-profile",
            MibProfileDefinitions::standardsId()).toString();
        const QSignalBlocker blocker(browserProfile);
        browserProfile->clear();
        for (const MibProfileRecord &profile : mibLibrary->profileService()->profiles())
            browserProfile->addItem(profile.name, profile.id);
        int index = browserProfile->findData(selected);
        if (index < 0) index = browserProfile->findData(MibProfileDefinitions::standardsId());
        browserProfile->setCurrentIndex(index);
    };
    refreshMibProfiles();
    connect(mibLibrary, &MibLibraryWidget::profilesChanged, mw, refreshMibProfiles);
    connect(browserProfile, &QComboBox::currentIndexChanged, mw, [this, browserProfile]() {
        mibLibrary->selectProfile(browserProfile->currentData().toString());
    });
    connect(mibLibrary, &MibLibraryWidget::profileSelectionChanged, mw,
            [this, browserProfile](const QString &id, const MibEffectivePlan &plan) {
        { const QSignalBlocker blocker(browserProfile);
          const int index = browserProfile->findData(id); if (index >= 0) browserProfile->setCurrentIndex(index); }
        QString error;
        if (!modules->ApplyProfileRuntime(plan, &error)) {
            if (!error.isEmpty())
                DiagnosticLogger::log("MIB", tr("Unable to apply MIB profile: %1").arg(error));
            return;
        }
    });
    connect(modules,&MibModule::profileRuntimeBuildStarted,mw,[this,mw](const QString &name){
        mw->statusBar()->showMessage(tr("Building MIB environment… %1").arg(name));
    });
    connect(modules,&MibModule::profileRuntimeReady,mw,
        [this,mw,mibDependentActions](const QString &id,const MibEffectivePlan &plan,MibEnvironmentPtr environment,
               const QStringList &,bool cacheHit,bool){
            Q_UNUSED(id)
            const EnvironmentPresentation presentation = PresentEnvironment(environment);
            w.MIBTree->setVisibleModules(plan.effectiveModules);
            w.MIBTree->Populate();
            mw->statusBar()->showMessage(cacheHit?tr("MIB environment ready (cached)"):
                presentation.needsAttention
                    ? tr("MIB environment loaded with %n problem(s); see the Problems tab", nullptr, presentation.problems)
                    : tr("MIB environment ready"),5000);
            for (QToolButton *button : mibDependentActions) {
                button->setEnabled(true);
                button->setToolTip({});
            }
            if (startup) startup->advance(startup->generation(),
                presentation.needsAttention ? StartupState::NeedsAttention : StartupState::Ready,
                environment ? tr("Environment: %1 modules · %2").arg(environment->loadedCount())
                                  .arg(presentation.state) : tr("Environment: Empty"));
        });
    connect(modules,&MibModule::profileRuntimeFailed,mw,[this,mw](const QString &,const QString &error){
        mw->statusBar()->showMessage(tr("MIB environment build failed; previous environment remains active"),7000);
        DiagnosticLogger::log("MIB",tr("Unable to apply MIB profile: %1").arg(error));
        if (startup) startup->advance(startup->generation(), StartupState::NeedsAttention,
                                      tr("Environment: Needs attention"));
    });
    initializingMibProfile = false;
    DiagnosticLogger::log("Startup", QStringLiteral("MIB profile initialization deferred until after shell display"));
    discovery = new Discovery(this);

    auto *contextWidget = new QWidget(w.widget);
    contextWidget->setObjectName(QStringLiteral("CurrentDeviceContext"));
    auto *contextLayout = new QVBoxLayout(contextWidget);
    contextLayout->setContentsMargins(10, 6, 10, 6);
    contextLayout->setSpacing(1);
    auto *contextName = new QLabel(tr("No device selected"), contextWidget);
    contextName->setObjectName(QStringLiteral("CurrentDeviceName"));
    QFont contextFont = contextName->font(); contextFont.setBold(true);
    contextName->setFont(contextFont);
    auto *contextSummary = new QLabel(tr("Select a device from the sidebar"), contextWidget);
    contextSummary->setObjectName(QStringLiteral("CurrentDeviceSummary"));
    auto *contextState = new QWidget(contextWidget);
    auto *contextStateLayout = new QHBoxLayout(contextState);
    contextStateLayout->setContentsMargins(0, 2, 0, 0);
    contextStateLayout->setSpacing(12);
    auto *securityContext = new QLabel(tr("SNMP: No connection"), contextState);
    securityContext->setObjectName(QStringLiteral("CurrentSecurityContext"));
    auto *mibContext = new QLabel(tr("MIB Profile: %1").arg(browserProfile->currentText()), contextState);
    mibContext->setObjectName(QStringLiteral("CurrentMibProfile"));
    auto *environmentContext = new QLabel(tr("Environment: Building"), contextState);
    environmentContext->setObjectName(QStringLiteral("CurrentEnvironmentStatus"));
    environmentContext->setProperty("status", QStringLiteral("building"));
    contextStateLayout->addWidget(securityContext);
    contextStateLayout->addWidget(mibContext);
    contextStateLayout->addWidget(environmentContext);
    contextStateLayout->addStretch();
    contextLayout->addWidget(contextName);
    contextLayout->addWidget(contextSummary);
    contextLayout->addWidget(contextState);
    if (auto *centralLayout = qobject_cast<QGridLayout *>(w.widget->layout())) {
        centralLayout->removeWidget(w.TabW);
        centralLayout->setContentsMargins(8, 8, 8, 8);
        centralLayout->setSpacing(6);
        centralLayout->addWidget(contextWidget, 0, 0);
        centralLayout->addWidget(w.TabW, 1, 0);
        centralLayout->setRowStretch(1, 1);
    }

    devicesDock = new QDockWidget(tr("Devices"), mw);
    devicesDock->setObjectName("DevicesDock");
    devicesDock->setAllowedAreas(Qt::LeftDockWidgetArea);
    devicesDock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    auto *dockTitle = new QWidget(devicesDock);
    dockTitle->setFixedHeight(0);
    devicesDock->setTitleBarWidget(dockTitle);
    devicePane = new DevicePane(GetDeviceTreeConfigFile(),
                                apm->GetAgentProfileRecords(),
                                profileMetadataService->allMetadata(),
                                profileService, profileMetadataService,
                                communityCredentialService, UsmCredentials(), devicesDock);
    DiagnosticLogger::log("Connections", "Connections model and Device Details constructed");
    devicesDock->setWidget(devicePane);
    auto refreshCredentialHealth = [this]() {
        QHash<QString, QString> health;
        for (const AgentProfileRecord &profile : profileService->profiles())
        {
            QString text;
            if (profile.v3)
            {
                const UsmReferenceResult result = UsmCredentials()->validate(
                    profile, profileMetadataService->metadataForProfile(profile.profileId));
                switch (result.status) {
                case UsmReferenceStatus::Valid: text = tr("SNMPv3 credential available"); break;
                case UsmReferenceStatus::Missing: text = tr("SNMPv3 credential missing"); break;
                case UsmReferenceStatus::Ambiguous: text = tr("SNMPv3 security name ambiguous"); break;
                case UsmReferenceStatus::IncompatibleSecurityLevel:
                    text = tr("SNMPv3 security-level mismatch"); break;
                default: break;
                }
            }
            else if (profile.v1 || profile.v2)
                text = communityCredentialService->healthText(profile);
            health.insert(profile.profileId, text);
        }
        devicePane->setCredentialHealth(health);
    };
    refreshCredentialHealth();
    connect(communityCredentialService, &CommunityCredentialService::credentialsChanged,
            devicePane, refreshCredentialHealth);
    connect(communityCredentialService, &CommunityCredentialService::bindingsChanged,
            devicePane, refreshCredentialHealth);
    connect(profileService, &AgentProfileService::profilesChanged,
            devicePane, refreshCredentialHealth);
    connect(UsmCredentials(), &UsmCredentialService::credentialsChanged,
            devicePane, refreshCredentialHealth);
    devicesDock->setMinimumWidth(280);
    mw->addDockWidget(Qt::LeftDockWidgetArea, devicesDock);
    mw->resizeDocks({devicesDock}, {330}, Qt::Horizontal);

    connect(devicePane, &DevicePane::profileSelected,
            agent, &Agent::SelectProfileById);
    connect(devicePane, &DevicePane::currentContextChanged, mw,
            [contextName, contextSummary, securityContext](const QString &name, const QString &summary) {
        DiagnosticLogger::log("UI", QStringLiteral(
            "current-profile header update begin name=%1").arg(name));
        contextName->setText(name.isEmpty() ? QObject::tr("No device selected") : name);
        contextSummary->setText(summary.isEmpty() ?
            QObject::tr("Select a device from the sidebar") : summary);
        securityContext->setText(summary.isEmpty() ? QObject::tr("SNMP: No connection")
                                                    : QObject::tr("SNMP: %1").arg(summary));
        DiagnosticLogger::log("UI", "current-profile header update end");
    });
    connect(browserProfile, &QComboBox::currentTextChanged, mibContext,
            [mibContext](const QString &name) {
        mibContext->setText(QObject::tr("MIB Profile: %1").arg(name.isEmpty()
            ? QObject::tr("None") : name));
    });
    connect(modules, &MibModule::profileRuntimeBuildStarted, environmentContext,
            [environmentContext](const QString &) {
        environmentContext->setText(QObject::tr("Environment: Building"));
        environmentContext->setProperty("status", QStringLiteral("building"));
        environmentContext->style()->unpolish(environmentContext);
        environmentContext->style()->polish(environmentContext);
    });
    connect(modules, &MibModule::profileRuntimeReady, environmentContext,
            [environmentContext](const QString &, const MibEffectivePlan &, MibEnvironmentPtr environment,
                                 const QStringList &, bool, bool) {
        const EnvironmentPresentation presentation = PresentEnvironment(environment);
        environmentContext->setText(QObject::tr("Environment: %1").arg(presentation.state));
        environmentContext->setProperty("status", presentation.badge);
        environmentContext->style()->unpolish(environmentContext);
        environmentContext->style()->polish(environmentContext);
    });
    connect(modules, &MibModule::profileRuntimeFailed, environmentContext,
            [environmentContext](const QString &, const QString &) {
        environmentContext->setText(QObject::tr("Environment: Error"));
        environmentContext->setProperty("status", QStringLiteral("error"));
        environmentContext->style()->unpolish(environmentContext);
        environmentContext->style()->polish(environmentContext);
    });

    mw->setStyleSheet(mw->styleSheet() + QStringLiteral(R"(
        QWidget#CurrentDeviceContext { background: palette(alternate-base); border-bottom: 1px solid palette(mid); }
        QLabel#CurrentEnvironmentStatus { border-radius: 3px; padding: 2px 7px; font-weight: 600; }
        QLabel#CurrentEnvironmentStatus[status="ready"] { color: #237a3b; background: #dff2e4; }
        QLabel#CurrentEnvironmentStatus[status="building"] { color: #765600; background: #fff0bd; }
        QLabel#CurrentEnvironmentStatus[status="warning"] { color: #8a4b00; background: #ffe4c2; }
        QLabel#CurrentEnvironmentStatus[status="error"] { color: #9b2525; background: #f8d7da; }
        QWidget#QueryToolBar { border-bottom: 1px solid palette(mid); }
        QToolButton { padding: 3px 7px; }
        QHeaderView::section { padding: 4px 6px; font-weight: 600; }
        QTreeView::item, QTableView::item, QListView::item { min-height: 20px; }
    )"));
    connect(devicePane->detailsEditor(), &DeviceDetailsEditor::profileApplied,
            devicePane, [this](const QString &profileId) {
        DiagnosticLogger::log("Connections", QStringLiteral(
            "model refresh begin profile=%1").arg(profileId));
        devicePane->setProfiles(profileService->profiles());
        DiagnosticLogger::log("Connections", QStringLiteral(
            "tree reselection begin profile=%1").arg(profileId));
        agent->SelectProfileById(profileId);
        DiagnosticLogger::log("Connections", "tree reselection/model refresh end");
    });
    connect(devicePane, &DevicePane::editProfileRequested,
            apm, &AgentProfileManager::EditProfile);
    connect(devicePane, &DevicePane::duplicateProfileRequested,
            apm, [this](const QString &name) { apm->DuplicateProfile(name); });
    connect(devicePane, &DevicePane::newProfileRequested,
            apm, [this](const QString &) { apm->NewProfile(); });
    connect(devicePane, &DevicePane::deleteProfileRequested,
            apm, &AgentProfileManager::DeleteProfile);
    connect(apm, &AgentProfileManager::NewProfileCompleted,
            devicePane, &DevicePane::placeCreatedProfile);
    connect(apm, &AgentProfileManager::NewProfileCompleted, this,
            [this](const QString &profileId) {
        const AgentProfileRecord *profile = profileService->findById(profileId);
        if (!profile) return;
        ProfileMetadataRecord metadata =
            profileMetadataService->metadataForProfile(profileId);
        metadata.hasActiveProtocol = true;
        metadata.activeProtocol = profile->v1 ? 0 : (profile->v2 ? 1 : 2);
        metadata.hasRequestSettingsMode = true;
        metadata.requestSettingsMode = 1;
        profileMetadataService->update(metadata);
    });
    connect(devicePane, &DevicePane::organizationPersisted,
            apm, &AgentProfileManager::PersistProfiles);
    connect(devicePane, &DevicePane::organizationPersisted,
            discovery, &Discovery::RefreshDestinationFolders);
    connect(devicePlacementService, &DeviceTreePlacementService::placementChanged,
            devicePane, [this](const QString &) { devicePane->reloadTree(); });
    connect(apm, &AgentProfileManager::AgentProfileListChanged,
            devicePane, [this]() {
                devicePane->setProfiles(apm->GetAgentProfileRecords());
            });
    connect(apm, &AgentProfileManager::AgentProfileRenamed,
            devicePane, [this](const QString &profileId, const QString &,
                               const QString &newName) {
                devicePane->renameProfile(profileId, newName);
            });
    connect(apm, &AgentProfileManager::AgentProfileDuplicated,
            devicePane, &DevicePane::placeDuplicate);
    connect(profileService, &AgentProfileService::profileDuplicated,
            profileMetadataService, &ProfileMetadataService::copy);
    connect(profileService, &AgentProfileService::profileDuplicated,
            communityCredentialService, &CommunityCredentialService::copyBinding);
    connect(profileService, &AgentProfileService::profileDeleted,
            profileMetadataService, &ProfileMetadataService::remove);
    connect(profileService, &AgentProfileService::profileDeleted,
            communityCredentialService,
            &CommunityCredentialService::removeProfileBinding);
    connect(profileMetadataService, &ProfileMetadataService::metadataChanged,
            devicePane, [this](const QString &) {
                devicePane->setMetadata(profileMetadataService->allMetadata());
            });
    connect(devicePane, &DevicePane::loadPreferredMibsRequested, this,
            [this](const QString &profileId) {
        const int retainedCount = profileMetadataService->metadataForProfile(
            profileId).preferredMibs.size();
        DiagnosticLogger::log("MIB", tr("Connection preferred-MIB data is retained for compatibility but is not an activation authority (profile=%1 entries=%2). Select a MIB Profile to change MIB semantics.")
            .arg(profileId).arg(retainedCount));
    });
    connect(devicePane, &DevicePane::manageUsmCredentialsRequested, this, [this]() {
        if (upm) upm->ExecuteNewCredential();
    });
    connect(devicePane, &DevicePane::exportRequested, this,
            [this](int scope, const QString &id) {
        ProfileTransferDocument document;
        document.profiles = profileService->profiles();
        document.metadata = profileMetadataService->allMetadata();
        document.tree = devicePane->model()->state();
        if (scope == 1)
            document = ProfileTransfer::selectProfiles(document, {id});
        else if (scope == 2)
            document = ProfileTransfer::selectFolder(document, id);
        const QString fileName = QFileDialog::getSaveFileName(
            nullptr, tr("Export Device Profiles"), QString(), tr("JSON files (*.json)"));
        if (fileName.isEmpty()) return;
        QFile file(fileName);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
            file.write(ProfileTransfer::exportJson(document)) < 0)
            QMessageBox::warning(nullptr, tr("Export Failed"),
                                 tr("The profile export could not be written."));
    });
    connect(devicePane, &DevicePane::importRequested, this, [this]() {
        const QString fileName = QFileDialog::getOpenFileName(
            nullptr, tr("Import Device Profiles"), QString(), tr("JSON files (*.json)"));
        if (fileName.isEmpty()) return;
        QFile file(fileName);
        if (!file.open(QIODevice::ReadOnly))
        {
            QMessageBox::warning(nullptr, tr("Import Failed"),
                                 tr("The selected import file could not be read."));
            return;
        }
        ProfileImportPlan plan;
        QString detail;
        const ProfileTransferError error = ProfileTransfer::planImport(
            file.readAll(), profileService->profiles(), devicePane->model()->state(),
            &plan, &detail);
        if (error != ProfileTransferError::None)
        {
            QMessageBox::warning(nullptr, tr("Import Failed"),
                                 tr("The import file is invalid or unsupported. No profiles were imported."));
            return;
        }
        QString applyError;
        if (!ProfileImportStorage::apply(plan, GetAgentsConfigFile(),
                                         GetProfileMetadataConfigFile(),
                                         GetDeviceTreeConfigFile(),
                                         profileService->profiles(),
                                         profileMetadataService->allMetadata(),
                                         devicePane->model()->state(), &applyError))
        {
            QMessageBox::warning(nullptr, tr("Import Failed"),
                                 tr("The import could not be saved. Existing configuration was restored."));
            return;
        }
        profileService->reload();
        profileMetadataService->reload();
        devicePane->setProfiles(profileService->profiles());
        devicePane->setMetadata(profileMetadataService->allMetadata());
        devicePane->reloadTree();
    });

    // Rehost the established feature widgets in a workbench shell. This only
    // changes presentation ownership: request, Profile, Environment, parser,
    // trap, editor, and graph services continue to own their existing state.
    auto *resultsPage = new QWidget(mw);
    resultsPage->setObjectName(QStringLiteral("WorkbenchResultsPage"));
    auto *resultsLayout = new QVBoxLayout(resultsPage);
    resultsLayout->setContentsMargins(4, 4, 4, 4);
    auto *resultsHeader = new QHBoxLayout;
    resultsHeader->addWidget(new QLabel(tr("Current operation results"), resultsPage));
    resultsHeader->addStretch();
    w.QueryTable->setParent(resultsPage);
    resultsHeader->addWidget(w.QueryTable);
    resultsLayout->addLayout(resultsHeader);
    resultTable->setParent(resultsPage);
    resultsLayout->addWidget(resultTable, 1);

    auto *rawPage = new QWidget(mw);
    rawPage->setObjectName(QStringLiteral("WorkbenchRawPage"));
    auto *rawLayout = new QVBoxLayout(rawPage);
    rawLayout->setContentsMargins(4, 4, 4, 4);
    rawLayout->addWidget(new QLabel(tr("Raw SNMP and protocol output"), rawPage));
    w.Query->setParent(rawPage);
    rawLayout->addWidget(w.Query, 1);
    w.QueryLayout->hide();

    auto *problemsPage = new QWidget(mw);
    problemsPage->setObjectName(QStringLiteral("WorkbenchProblemsPage"));
    auto *problemsLayout = new QVBoxLayout(problemsPage);
    problemsLayout->setContentsMargins(8, 8, 8, 8);
    auto *problemState = new QLabel(tr("No MIB environment has been built yet."), problemsPage);
    problemState->setObjectName(QStringLiteral("WorkbenchProblemState"));
    problemState->setWordWrap(true);
    auto *problemHint = new QLabel(
        tr("Double-click a row to open the file at that line. Everything that could be loaded is "
           "available in the Tree; fix an item, or carry on knowing that part may not work."),
        problemsPage);
    problemHint->setWordWrap(true);
    auto *showNotes = new QCheckBox(tr("Show notes"), problemsPage);
    showNotes->setObjectName(QStringLiteral("WorkbenchProblemShowNotes"));
    showNotes->setToolTip(tr("Notes are informational: modified files, standards issues, and parser "
                             "warnings that do not stop a MIB from working."));
    auto *problemToolbar = new QHBoxLayout;
    problemToolbar->addWidget(problemHint, 1);
    problemToolbar->addWidget(showNotes, 0, Qt::AlignTop);
    auto *problemTable = new QTableWidget(0, 6, problemsPage);
    problemTable->setObjectName(QStringLiteral("WorkbenchProblemTable"));
    problemTable->setHorizontalHeaderLabels({tr("Severity"), tr("Module"), tr("Problem"),
                                             tr("Why"), tr("What it affects"), tr("File")});
    problemTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    problemTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    problemTable->setSelectionMode(QAbstractItemView::SingleSelection);
    problemTable->setWordWrap(true);
    problemTable->verticalHeader()->hide();
    problemTable->horizontalHeader()->setStretchLastSection(true);
    problemTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    problemTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    problemTable->setTextElideMode(Qt::ElideNone);
    connect(problemTable->horizontalHeader(), &QHeaderView::sectionResized, problemTable,
            [problemTable]() { problemTable->resizeRowsToContents(); });
    // The full explanation of the selected row, so nothing is cut off.
    auto *problemDetail = new QPlainTextEdit(problemsPage);
    problemDetail->setObjectName(QStringLiteral("WorkbenchProblemDetail"));
    problemDetail->setReadOnly(true);
    problemDetail->setMaximumHeight(120);
    problemDetail->setPlaceholderText(tr("Select a row to read the full explanation."));
    problemsLayout->addWidget(problemState);
    problemsLayout->addLayout(problemToolbar);
    problemsLayout->addWidget(problemTable, 1);
    problemsLayout->addWidget(problemDetail);

    // The list shown here is a plain copy of the Environment's problem report, so it
    // never holds a pointer into the Environment or the model that built it.
    auto problemList = std::make_shared<QList<MibProblem>>();
    auto problemLoadedCount = std::make_shared<int>(0);
    const auto describeProblems = [problemState, problemList, problemLoadedCount, showNotes]() {
        const MibProblemSummary summary = MibSummarizeProblems(*problemList);
        QString text;
        if (!summary.hasProblems()) {
            text = QObject::tr("%n module(s) loaded. No problems.", nullptr, *problemLoadedCount);
            if (summary.notes > 0)
                text += showNotes->isChecked()
                    ? QObject::tr(" Showing %n note(s).", nullptr, summary.notes)
                    : QObject::tr(" %n note(s) are available (tick \"Show notes\").", nullptr, summary.notes);
        } else {
            text = QObject::tr("%n module(s) loaded, with %1 error(s) and %2 warning(s).",
                               nullptr, *problemLoadedCount).arg(summary.errors).arg(summary.warnings);
        }
        problemState->setText(text);
    };
    const auto repopulateProblems = [problemTable, problemDetail, showNotes, problemList]() {
        problemDetail->clear();
        problemTable->setRowCount(0);
        QList<int> order;
        for (int i = 0; i < problemList->size(); ++i)
            if (problemList->at(i).severity != MibProblemSeverity::Note || showNotes->isChecked())
                order.append(i);
        std::stable_sort(order.begin(), order.end(), [problemList](int a, int b) {
            return static_cast<int>(problemList->at(a).severity) < static_cast<int>(problemList->at(b).severity);
        });
        for (int index : std::as_const(order)) {
            const MibProblem &problem = problemList->at(index);
            const int row = problemTable->rowCount();
            problemTable->insertRow(row);
            const QString where = problem.file.isEmpty() ? QString()
                : problem.line > 0 ? QStringLiteral("%1:%2").arg(QFileInfo(problem.file).fileName()).arg(problem.line)
                                   : QFileInfo(problem.file).fileName();
            const QStringList cells{MibProblemSeverityText(problem.severity), problem.module,
                                    problem.category, problem.why, problem.effect, where};
            QString tip = problem.why;
            if (!problem.effect.isEmpty()) tip += QStringLiteral("\n\n") + problem.effect;
            if (!problem.requestedBy.isEmpty())
                tip += QStringLiteral("\n\n") + tr("Needed by: %1").arg(problem.requestedBy.join(QStringLiteral(", ")));
            if (!problem.suggestion.isEmpty()) tip += QStringLiteral("\n\n") + problem.suggestion;
            if (!problem.file.isEmpty()) tip += QStringLiteral("\n\n") + problem.file;
            for (int column = 0; column < cells.size(); ++column) {
                auto *item = new QTableWidgetItem(cells[column]);
                item->setToolTip(tip);
                if (column == 0) {
                    item->setData(Qt::UserRole, index);
                    if (problem.severity == MibProblemSeverity::Error) item->setForeground(QColor(0x9b, 0x25, 0x25));
                    else if (problem.severity == MibProblemSeverity::Warning) item->setForeground(QColor(0x8a, 0x4b, 0x00));
                }
                problemTable->setItem(row, column, item);
            }
        }
        problemTable->resizeColumnsToContents();
        problemTable->resizeRowsToContents();
    };
    connect(showNotes, &QCheckBox::toggled, problemsPage,
            [repopulateProblems, describeProblems](bool) { describeProblems(); repopulateProblems(); });
    connect(problemTable, &QTableWidget::currentCellChanged, problemsPage,
            [problemTable, problemDetail, problemList](int row, int, int, int) {
        const QTableWidgetItem *first = row >= 0 ? problemTable->item(row, 0) : nullptr;
        const int index = first ? first->data(Qt::UserRole).toInt() : -1;
        if (index < 0 || index >= problemList->size()) { problemDetail->clear(); return; }
        const MibProblem &problem = problemList->at(index);
        QStringList lines;
        lines << QStringLiteral("%1: %2").arg(MibProblemSeverityText(problem.severity), problem.category);
        if (!problem.why.isEmpty()) lines << QObject::tr("Why: %1").arg(problem.why);
        if (!problem.effect.isEmpty()) lines << QObject::tr("What it affects: %1").arg(problem.effect);
        if (!problem.requestedBy.isEmpty())
            lines << QObject::tr("Needed by: %1").arg(problem.requestedBy.join(QStringLiteral(", ")));
        if (!problem.suggestion.isEmpty()) lines << QObject::tr("What you can do: %1").arg(problem.suggestion);
        if (!problem.file.isEmpty())
            lines << QObject::tr("File: %1%2").arg(problem.file)
                         .arg(problem.line > 0 ? QStringLiteral(" (line %1)").arg(problem.line) : QString());
        problemDetail->setPlainText(lines.join(QStringLiteral("\n")));
    });
    connect(problemTable, &QTableWidget::cellDoubleClicked, mw,
            [this, problemTable, problemList](int row, int) {
        const QTableWidgetItem *first = problemTable->item(row, 0);
        if (!first) return;
        const int index = first->data(Qt::UserRole).toInt();
        if (index < 0 || index >= problemList->size()) return;
        const MibProblem problem = problemList->at(index);
        if (problem.file.isEmpty() || !QFileInfo::exists(problem.file)) return;
        editor->MibFileOpen(problem.file);
        editor->GoToLine(problem.line);
        ShowEditorDocument();
    });
    connect(modules, &MibModule::profileRuntimeBuildStarted, problemState,
            [problemState](const QString &) { problemState->setText(QObject::tr("Building the MIB environment…")); });
    connect(modules, &MibModule::profileRuntimeReady, problemState,
            [problemList, problemLoadedCount, describeProblems, repopulateProblems](
                const QString &, const MibEffectivePlan &, MibEnvironmentPtr environment,
                const QStringList &, bool, bool) {
        *problemList = environment ? environment->problems() : QList<MibProblem>{};
        *problemLoadedCount = environment ? static_cast<int>(environment->loadedCount()) : 0;
        describeProblems();
        repopulateProblems();
    });
    connect(modules, &MibModule::profileRuntimeFailed, problemState,
            [problemState, problemList, repopulateProblems](const QString &, const QString &error) {
        problemList->clear();
        MibProblem problem;
        problem.severity = MibProblemSeverity::Error;
        problem.category = QObject::tr("Nothing could be loaded");
        problem.why = error;
        problem.effect = QObject::tr("The previous MIB environment stays active.");
        problemList->append(problem);
        problemState->setText(QObject::tr("Nothing could be loaded for this profile. "
                                          "The previous MIB environment remains active."));
        repopulateProblems();
    });

    auto *diagnosticsPage = new QWidget(mw);
    diagnosticsPage->setObjectName(QStringLiteral("WorkbenchDiagnosticsPage"));
    auto *diagnosticsLayout = new QVBoxLayout(diagnosticsPage);
    diagnosticsLayout->setContentsMargins(0, 0, 0, 0);
    w.LogTab->setParent(diagnosticsPage);
    diagnosticsLayout->addWidget(w.LogTab);

    w.NodeInfoLayout->setObjectName(QStringLiteral("WorkbenchMibInspector"));
    w.NodeInfoL->setText(tr("MIB Object Inspector"));
    devicesDock->hide();
    WorkbenchShell::Pages workbenchPages;
    workbenchPages.tree = w.TreeTab;
    workbenchPages.library = mibLibrary;
    workbenchPages.editor = w.EditorTab;
    workbenchPages.discovery = w.DiscoveryTab;
    workbenchPages.events = w.TrapsTab;
#if SNMPB_ENABLE_QWT
    workbenchPages.graphs = w.GraphsTab;
#endif
    workbench = new WorkbenchShell(mw, w.widget,
        qobject_cast<QGridLayout *>(w.widget->layout()), contextWidget, w.TabW,
        workbenchPages, devicePane, w.NodeInfoLayout, resultsPage, problemsPage,
            diagnosticsPage, rawPage, this);
    startup->advance(startupGeneration, StartupState::ShellReady,
                     tr("Loading MIB Catalogâ€¦"));
    connect(workbench, &WorkbenchShell::mibLibraryModeRequested,
            mibLibrary, &MibLibraryWidget::showWorkspaceMode);
    connect(w.fileNewAction, &QAction::triggered, mw,
            [this]() { ShowEditorDocument(); });
    connect(w.fileOpenAction, &QAction::triggered, mw,
            [this]() { ShowEditorDocument(); });

    auto *statusEnvironment = new QLabel(tr("Environment: Building"), mw->statusBar());
    statusEnvironment->setObjectName(QStringLiteral("WorkbenchStatusEnvironment"));
    auto *statusEvents = new QLabel(
        prefs->GetEnableIPv4() || prefs->GetEnableIPv6()
            ? tr("Events: Capture enabled") : tr("Events: Stopped"), mw->statusBar());
    statusEvents->setObjectName(QStringLiteral("WorkbenchStatusEvents"));
    mw->statusBar()->addPermanentWidget(statusEnvironment);
    mw->statusBar()->addPermanentWidget(statusEvents);
    connect(modules, &MibModule::profileRuntimeBuildStarted, statusEnvironment,
            [statusEnvironment](const QString &) { statusEnvironment->setText(QObject::tr("Environment: Building")); });
    connect(modules, &MibModule::profileRuntimeReady, statusEnvironment,
            [statusEnvironment](const QString &, const MibEffectivePlan &, MibEnvironmentPtr environment,
                                const QStringList &, bool, bool) {
        const EnvironmentPresentation presentation = PresentEnvironment(environment);
        statusEnvironment->setText(!environment ? QObject::tr("Environment: Empty")
            : presentation.needsAttention
                ? QObject::tr("Environment: %1 modules · %2").arg(environment->loadedCount()).arg(presentation.state)
                : QObject::tr("Environment: %1 modules").arg(environment->loadedCount()));
    });
    connect(modules, &MibModule::profileRuntimeFailed, statusEnvironment,
            [statusEnvironment](const QString &, const QString &) {
        statusEnvironment->setText(QObject::tr("Environment: Error"));
    });
    connect(startup, &StartupCoordinator::stateChanged, statusEnvironment,
            [statusEnvironment](StartupState state, const QString &detail) {
        QString text = detail;
        if (text.isEmpty()) {
            if (state == StartupState::ShellReady || state == StartupState::CatalogLoading)
                text = QObject::tr("Catalog: Loadingâ€¦");
            else if (state == StartupState::ProfilePreparing)
                text = QObject::tr("Profile: Preparingâ€¦");
            else if (state == StartupState::EnvironmentBuilding)
                text = QObject::tr("Environment: Buildingâ€¦");
            else if (state == StartupState::Ready)
                text = QObject::tr("Environment: Ready");
            else if (state == StartupState::NeedsAttention || state == StartupState::Failed)
                text = QObject::tr("Environment: Needs attention");
        }
        statusEnvironment->setText(text);
    });

    QSettings windowSettings;
    const QByteArray savedWindowState = windowSettings.value("mainwindow/state").toByteArray();
    if (!savedWindowState.isEmpty()) mw->restoreState(savedWindowState);
    devicesDock->hide();
    devicesDock->deleteLater();
    devicesDock = nullptr;
    workbench->restorePresentationState();
    DiagnosticLogger::log("UI", "saved QMainWindow state restore complete");
    if (windowSettings.contains("mainwindow/device-sidebar-sizes"))
        devicePane->verticalSplitter()->restoreState(
            windowSettings.value("mainwindow/device-sidebar-sizes").toByteArray());
    connect(QApplication::instance(), &QCoreApplication::aboutToQuit,
            mw, [this, mw]() {
                QSettings settings;
                settings.setValue("mainwindow/state", mw->saveState());
                settings.setValue("mainwindow/device-sidebar-sizes",
                                  devicePane->verticalSplitter()->saveState());
                if (workbench) workbench->savePresentationState();
            });

    // Connect some signals
    connect( w.TabW, SIGNAL( currentChanged(int) ),
             this, SLOT( TabSelected() ) );
    connect( w.actionManageAgentProfiles, SIGNAL( triggered(bool) ),
             this, SLOT( ManageAgentProfiles(bool) ) );
    connect( w.actionManageUSMProfiles, SIGNAL( triggered(bool) ),
             this, SLOT( ManageUSMProfiles(bool) ) );
    QAction *manageCommunities = w.optionsMenu->addAction(
        tr("Manage SNMPv1/v2c Community Credentials..."));
    connect(manageCommunities, &QAction::triggered, mw, [this, mw]() {
        CommunityCredentialManager manager(communityCredentialService,
                                           profileService, mw);
        manager.execute();
    });
    connect( w.actionPreferences, SIGNAL( triggered(bool) ),
             this, SLOT( ManagePreferences(bool) ) );
    connect( w.helpAboutAction, SIGNAL( triggered(bool) ),
             this, SLOT( AboutBox(bool) ) );

    // Register every MIB tree to the MIB loader object
    w.MIBTree->RegisterToLoader(&loader);
    TabSelected();
}

void Snmpb::StartDeferredInitialization()
{
    if (!startup || !mibLibrary || !modules) return;
    const quint64 generation = startup->generation();
    if (!mibLibrary->initialRefreshReady()) {
        if (!startupCatalogRequested) {
            startupCatalogRequested = true;
            startup->advance(generation, StartupState::CatalogLoading,
                             tr("Catalog: Loadingâ€¦"));
            connect(mibLibrary, &MibLibraryWidget::initialRefreshFinished, this,
                [this, generation](qint64 preparationMs, qint64 publicationMs) {
                if (!startup || !startup->accepts(generation)) return;
                DiagnosticLogger::log("Startup", tr(
                    "Catalog ready preparation_ms=%1 publication_ms=%2")
                    .arg(preparationMs).arg(publicationMs));
                StartDeferredInitialization();
            });
            mibLibrary->beginInitialRefresh(generation);
        }
        return;
    }
    startup->advance(generation, StartupState::CatalogReady,
                     tr("Catalog ready â€” preparing Profileâ€¦"));

    const QString settingsFile = QSettings().fileName();
    const QString profilesPath = QDir(MibCollection::legacyManagedRoot())
        .filePath(QStringLiteral("profiles-v1.json"));
    const MibDependencyIndex dependencySnapshot = *modules->DependencyIndex();
    const QPointer<Snmpb> context(this);
    QThreadPool::globalInstance()->start(
        [context, generation, settingsFile, profilesPath, dependencySnapshot]() mutable {
        QElapsedTimer timer;
        timer.start();
        QSettings legacySettings(settingsFile, QSettings::IniFormat);
        MibProfileService migrationProfiles{MibProfileRepository(profilesPath)};
        const QString savedProfile = legacySettings.value(
            QStringLiteral("mib-library/current-profile")).toString();
        const bool savedProfileWasValid = !savedProfile.isEmpty() &&
            migrationProfiles.find(savedProfile) != nullptr;
        const MibLegacyMigrationResult migration = MibLegacyMigration::migrate(
            legacySettings, migrationProfiles, dependencySnapshot);
        QString exactMigrationError;
        bool exactMigrationComplete = migrationProfiles.migrateLegacyFolderProfiles(
            &exactMigrationError);
        const bool conversionNeeded = migrationProfiles.requiresLegacyConversion();
        if (exactMigrationComplete && conversionNeeded)
            exactMigrationComplete = migrationProfiles.migrateLegacyProfiles(
                dependencySnapshot, &exactMigrationError);
        if (migration.migrated && !savedProfileWasValid)
            legacySettings.setValue(QStringLiteral("mib-library/current-profile"),
                                    migration.profileId);
        const qint64 elapsed = timer.elapsed();
        QMetaObject::invokeMethod(context,
            [context, generation, migration, exactMigrationComplete,
             exactMigrationError, conversionNeeded, elapsed, dependencySnapshot]() mutable {
            if (!context || !context->startup ||
                !context->startup->accepts(generation)) return;
            DiagnosticLogger::log("MIB", QObject::tr(
                "deferred legacy migration already=%1 migrated=%2 conversion-needed=%3 "
                "conversion-complete=%4 unresolved-retry-deferred=1 elapsed_ms=%5%6")
                .arg(migration.alreadyComplete).arg(migration.migrated)
                .arg(conversionNeeded).arg(exactMigrationComplete).arg(elapsed)
                .arg(exactMigrationError.isEmpty() ? QString() :
                    QObject::tr(" error=\"%1\"").arg(exactMigrationError)));
            context->mibLibrary->reloadProfiles();
            const MibProfileRecord profile = context->mibLibrary->currentProfile();
            DiagnosticLogger::log("Startup", QObject::tr(
                "Profile loaded id=%1 manifest=%2 roots=%3 unresolved=%4")
                .arg(profile.id).arg(MibProfileIsManifest(profile))
                .arg(profile.roots.size()).arg(profile.unresolvedLegacyModules.size()));
            if (profile.id.isEmpty()) {
                context->startup->advance(generation, StartupState::Failed,
                    QObject::tr("Profile: unavailable"));
                return;
            }
            context->startup->advance(generation, StartupState::ProfilePreparing,
                                      QObject::tr("Profile: Preparingâ€¦"));
            QThreadPool::globalInstance()->start(
                [context, generation, profile, dependencySnapshot]() mutable {
                MibEffectivePlan plan;
                if (!MibProfileIsManifest(profile)) {
                    plan.profileId = profile.id;
                    plan.profileName = profile.name;
                    plan.authorityError = QObject::tr(
                        "Profile still uses legacy authority and must be migrated before activation");
                } else {
                    MibEffectivePlanResolverInput input;
                    input.id = profile.id;
                    input.roots = profile.roots;
                    input.orderedScopes = MibProfileScopePaths(profile);
                    input.pins = profile.providerPins;
                    plan = MibEffectiveRuntimePlanResolver().resolve(input, dependencySnapshot);
                    plan.profileName = profile.name;
                    plan.profileType = profile.type;
                    MibEffectivePlanAddListedNotFound(&plan, profile.unresolvedLegacyModules);
                }
                QMetaObject::invokeMethod(context, [context, generation, profile, plan]() {
                    if (!context || !context->startup ||
                        !context->startup->accepts(generation)) return;
                    DiagnosticLogger::log("Startup", QObject::tr(
                        "Profile prepared id=%1 authority-error=%2 files=%3")
                        .arg(profile.id).arg(plan.authorityError.isEmpty() ? QStringLiteral("none")
                                                                         : plan.authorityError)
                        .arg(plan.runtimeFiles.size()));
                    context->startup->advance(generation, StartupState::EnvironmentBuilding,
                                              QObject::tr("Environment: Buildingâ€¦"));
                    context->mibLibrary->publishPreparedProfile(profile.id, plan);
                }, Qt::QueuedConnection);
            });
        }, Qt::QueuedConnection);
    });
}

Ui_MainW* Snmpb::MainUI(void)
{
    return (&w);
}

Agent* Snmpb::AgentObj(void)
{
    return (agent);
}

Trap* Snmpb::TrapObj(void)
{
    return (trap);
}

MibViewLoader* Snmpb::MibLoaderObj(void)
{
    return (&loader);
}

MibModule* Snmpb::MibModuleObj(void)
{
    return (modules);
}

MibEditor* Snmpb::MibEditorObj(void)
{
    return (editor);
}

AgentProfileManager* Snmpb::APManagerObj(void)
{
    return (apm);
}

void Snmpb::ShowEditorDocument()
{
    if (workbench) {
        workbench->setActivity(WorkbenchActivity::Mibs);
        workbench->openDocument(w.EditorTab);
    } else {
        w.TabW->setCurrentWidget(w.EditorTab);
    }
}

void Snmpb::Shutdown()
{
    if (startup) startup->cancel();
    DiagnosticLogger::log("Shutdown", "trap listener shutdown begin", false);
    if (agent) agent->Shutdown();
    DiagnosticLogger::log("Shutdown", "trap listener shutdown end", false);
}

AgentProfileService* Snmpb::AgentProfiles(void)
{
    return profileService;
}

ProfileMetadataService* Snmpb::ProfileMetadata(void)
{
    return profileMetadataService;
}

DeviceTreePlacementService* Snmpb::DevicePlacements(void)
{
    return devicePlacementService;
}

USMProfileManager* Snmpb::UPManagerObj(void)
{
    return (upm);
}

UsmCredentialService* Snmpb::UsmCredentials(void)
{
    return upm ? upm->Credentials() : nullptr;
}

CommunityCredentialService* Snmpb::CommunityCredentials(void)
{
    return communityCredentialService;
}

Preferences* Snmpb::PreferencesObj(void)
{
    return (prefs);
}

void Snmpb::CheckForConfigFiles(void)
{
    QSettings settings;

    if (!settings.isWritable())
    {
        QMessageBox::warning(nullptr, tr("MIB Navigator"),
                             tr("MIB Navigator config file is not writable:\n%1\n"
                                "If it continues to be, changes in preferences will not be saved!")
                             .arg(settings.fileName()),
                             QMessageBox::Ok);
    }
}

QString Snmpb::GetBootCounterConfigFile(void)
{
    QSettings settings;
    QDir cfgdir = QFileInfo(settings.fileName()).dir();
    return cfgdir.filePath(BOOT_COUNTER_CONFIG_FILE);
}

QString Snmpb::GetSmiConfigFile(void)
{
    QSettings settings;
    QDir cfgdir = QFileInfo(settings.fileName()).dir();
    return cfgdir.filePath(SMI_CONFIG_FILE);
}

QString Snmpb::GetUsmUsersConfigFile(void)
{
    QSettings settings;
    QDir cfgdir = QFileInfo(settings.fileName()).dir();
    return cfgdir.filePath(USM_USERS_CONFIG_FILE);
}

QString Snmpb::GetAgentsConfigFile(void)
{
    QSettings settings;
    QDir cfgdir = QFileInfo(settings.fileName()).dir();
    return cfgdir.filePath(AGENTS_CONFIG_FILE);
}

QString Snmpb::GetLogConfigFile(void)
{
    QSettings settings;
    QDir cfgdir = QFileInfo(settings.fileName()).dir();
    return cfgdir.filePath(LOG_CONFIG_FILE);
}

QString Snmpb::GetGraphsConfigFile(void)
{
    QSettings settings;
    QDir cfgdir = QFileInfo(settings.fileName()).dir();
    return cfgdir.filePath(GRAPHS_CONFIG_FILE);
}

void Snmpb::ManageAgentProfiles(bool)
{
    apm->Execute();
}

void Snmpb::ManageUSMProfiles(bool)
{
    upm->Execute();
}

void Snmpb::ManagePreferences(bool)
{
    prefs->Execute();
}

void Snmpb::SetEditorMenus(bool value)
{
    MainUI()->fileNewAction->setEnabled(value);
    MainUI()->fileOpenAction->setEnabled(value);
    MainUI()->fileSaveAction->setEnabled(value);
    MainUI()->fileSaveAsAction->setEnabled(value);
    MainUI()->actionVerifyMIB->setEnabled(value);
    MainUI()->actionExtractMIBfromRFC->setEnabled(value);
    MainUI()->actionGotoLine->setEnabled(value);
    MainUI()->actionFind->setEnabled(value);
    MainUI()->actionReplace->setEnabled(value);
    MainUI()->actionFindNext->setEnabled(value);
}

/* 
 * This is where anything related to a tab being selected happens:
 * graying-out GUI parts, refreshing MIB trees, ...
 */
void Snmpb::TabSelected(void)
{
    QWidget *currentTab = w.TabW->currentWidget();
    if (currentTab == w.TreeTab) {
        SetEditorMenus(false);
        // Set find func to MIB tree
        MainUI()->actionFind->setEnabled(true);
        MainUI()->actionFindNext->setEnabled(true);
        disconnect(MainUI()->actionFind, SIGNAL( triggered() ), 0, 0);
        disconnect(MainUI()->actionFindNext, SIGNAL( triggered() ), 0, 0);
        connect( MainUI()->actionFind, SIGNAL( triggered() ),
                w.MIBTree, SLOT( FindFromNode() ) );
        connect( MainUI()->actionFindNext, SIGNAL( triggered() ),
                w.MIBTree, SLOT( ExecuteFindNext() ) );
        MainUI()->actionMultipleVarbinds->setEnabled(true);
        // Refresh MIB tree if needed
        w.MIBTree->Populate();
    } else if (currentTab == mibLibrary) {
        QElapsedTimer activationTimer; activationTimer.start();
        SetEditorMenus(false);
        MainUI()->actionMultipleVarbinds->setEnabled(false);
        mibLibrary->activate();
        DiagnosticLogger::log("UI", tr("MIBs workspace activation total-ms=%1")
            .arg(activationTimer.elapsed()));
    } else if (currentTab == w.EditorTab) {
        SetEditorMenus(true);
        // Set find func to MIB editor 
        disconnect(MainUI()->actionFind, SIGNAL( triggered() ), 0, 0);
        disconnect(MainUI()->actionFindNext, SIGNAL( triggered() ), 0, 0);
        connect( MainUI()->actionFind, SIGNAL( triggered() ),
                editor, SLOT( Find() ) );
        connect( MainUI()->actionFindNext, SIGNAL( triggered() ),
                editor, SLOT( ExecuteFindNext() ) );
        MainUI()->actionMultipleVarbinds->setEnabled(false);
    } else if (currentTab == w.DiscoveryTab) {
        SetEditorMenus(false);
        MainUI()->actionMultipleVarbinds->setEnabled(false);
    } else if (currentTab == w.TrapsTab) {
        SetEditorMenus(false);
        MainUI()->actionMultipleVarbinds->setEnabled(false);
    } else if (currentTab == w.GraphsTab) {
        SetEditorMenus(false);
        disconnect(MainUI()->actionFind, SIGNAL( triggered() ), 0, 0);
        disconnect(MainUI()->actionFindNext, SIGNAL( triggered() ), 0, 0);
        MainUI()->actionMultipleVarbinds->setEnabled(false);
    } else if (currentTab == w.LogTab) {
        SetEditorMenus(false);
        MainUI()->actionMultipleVarbinds->setEnabled(false);
    }
}

void Snmpb::AboutBox(bool)
{
    QString dependencies = QStringLiteral(
        "SNMP++ %1; LibTomCrypt %2; libsmi %3; Qt %4")
        .arg(SNMP_PP_VERSION_STRING)
        .arg(SCRYPT)
        .arg(MibEngine::instance().libraryVersion())
        .arg(qVersion());
#if SNMPB_ENABLE_QWT
    dependencies += QStringLiteral("; Qwt %1").arg(QWT_VERSION_STR);
#endif
    QMessageBox::about(MainUI()->TabW, tr("About MIB Navigator"),
                       ProductIdentity::aboutHtml(SNMPB_VERSION_STRING,
                                                  dependencies));
}

QString Snmpb::GetDeviceTreeConfigFile(void)
{
    QSettings settings;
    QDir cfgdir = QFileInfo(settings.fileName()).dir();
    return cfgdir.filePath(DEVICE_TREE_CONFIG_FILE);
}

QString Snmpb::GetProfileMetadataConfigFile(void)
{
    QSettings settings;
    QDir cfgdir = QFileInfo(settings.fileName()).dir();
    return cfgdir.filePath(PROFILE_METADATA_CONFIG_FILE);
}

QString Snmpb::GetCredentialIdentitiesConfigFile(void)
{
    QSettings settings;
    QDir cfgdir = QFileInfo(settings.fileName()).dir();
    return cfgdir.filePath(CREDENTIAL_IDENTITIES_CONFIG_FILE);
}

QString Snmpb::GetCommunityCredentialsConfigFile(void)
{
    QSettings settings;
    QDir cfgdir = QFileInfo(settings.fileName()).dir();
    return cfgdir.filePath(COMMUNITY_CREDENTIALS_CONFIG_FILE);
}

QString Snmpb::GetCredentialBindingsConfigFile(void)
{
    QSettings settings;
    QDir cfgdir = QFileInfo(settings.fileName()).dir();
    return cfgdir.filePath(CREDENTIAL_BINDINGS_CONFIG_FILE);
}

