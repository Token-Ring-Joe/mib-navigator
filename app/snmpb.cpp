#include "querymacrospanel.h"
#include "querymacroservice.h"
#include "querymacrorunner.h"
#include "mibtreemodel.h"
#include <QDialogButtonBox>
#include <cerrno>
#include <cstring>
#include "dialoglayout.h"
#include "appearancecolors.h"
#include "queryresultspanel.h"
#include "mibproblemsexport.h"
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
#include <QDesktopServices>
#include <QUrl>
#include <QElapsedTimer>
#include <QHeaderView>
#include <QLineEdit>
#include <QTableWidget>
#include <QToolButton>
#include <qmenu.h>
#include "snmpb.h"
#include "mibmodule.h"
#include "agent.h"
#include "csvformat.h"
#include <QClipboard>
#include <QGuiApplication>
#include "connectionbar.h"
#include "credentialsdialog.h"
#include "mibenvironmentregistry.h"
#include "snmptransport.h"
#include "watchpanel.h"
#include "watchservice.h"
#include <QStandardPaths>
#include "collectionsearchpanel.h"
#include "selectedoidfield.h"
#include "requestlog.h"
#include "requestlogpanel.h"
#include "profiledetector.h"
#include "profilematch.h"
#include <QInputDialog>
#include "mibcollectionservice.h"
#include <QSet>
#include <QMenu>
#include <QTimer>
#include <QDialog>
#include <memory>
#include "trap.h"
#include "logsnmpb.h"
#include "mibeditor.h"
#include "mainwindowlayout.h"
#include "workbenchshell.h"
#include <QInputDialog>
#include <QRegularExpression>
#include "startupcoordinator.h"
#include "productidentity.h"
#include "diagnosticlogger.h"
#include "miblibrarywidget.h"
#include "mibprofile.h"
#include "miblegacymigration.h"

#include "mibcollection.h"
#include "usmprofile.h"
#include "communitycredentialservice.h"
#include "credentialprotection.h"
#include "credentialprotectiondialog.h"
#include "communitycredentialmanager.h"
#include "usmcredentialservice.h"
#include "preferences.h"
#include <QPointer>
#include <QThreadPool>
#include <QClipboard>

// These are needed to get the libraries version strings for the about box
#include "mibengine.h"
#include "tomcrypt.h"
#include "snmp_pp/config_snmp_pp.h"

#define SMI_CONFIG_FILE          "smi.conf"
#define BOOT_COUNTER_CONFIG_FILE "boot_counter.conf"
#define USM_USERS_CONFIG_FILE    "usm_users.conf"
#define LOG_CONFIG_FILE          "log.conf"
#define CREDENTIAL_IDENTITIES_CONFIG_FILE "credential-identities.conf"
#define COMMUNITY_CREDENTIALS_CONFIG_FILE "community-credentials.conf"
#define CREDENTIAL_VAULT_FILE "credentials.vault"



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

// Runs an action when a widget is clicked: the environment badge opens Problems.
class ClickToRun final : public QObject
{
public:
    ClickToRun(std::function<void()> run, QObject *parent) : QObject(parent), action(std::move(run)) {}
protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::MouseButtonRelease &&
            static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton) {
            action();
            return true;
        }
        return QObject::eventFilter(watched, event);
    }
private:
    std::function<void()> action;
};
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
        if (setuid(getuid()) < 0)
            qFatal("Unable to drop root privileges: %s", std::strerror(errno));
    }
#endif
    agent = new Agent(this, offline);
    DiagnosticLogger::log("Traps", "trap receiver construction complete");
    start_issuccess = agent->GetStartupResult(start_msg);    
#ifndef WIN32 
    // Drop root privileges
    if (setuid(getuid()) < 0)
    {
        qFatal("Unable to drop root privileges: %s", std::strerror(errno));
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
        DiagnosticLogger::log("Startup", tr("Startup failed; settings and parser configuration are preserved: %1").arg(start_msg));
        if (!QCoreApplication::instance()->property("snmpb.isolatedStateRoot").toString().isEmpty()) {
            // Isolated acceptance runs must be able to inspect fatal-startup state
            // without leaving a modal dialog waiting for a desktop interaction.
            QTimer::singleShot(0, QCoreApplication::instance(), [] {
                if (auto *message = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) message->accept();
            });
        }
        QMessageBox::critical(nullptr, tr("MIB Navigator"), start_msg, QMessageBox::Ok);
        exit (-1);
    }
    else
    {
        // A trap-port problem is not fatal and must not block startup with a
        // dialog; it is shown on the Events status label and in the Log instead.
        agent->StartTrapTimer();
    }

    w.setupUi(mw);
    DiagnosticLogger::log("UI", "main window UI setup complete");
    prefs->RestoreWindowGeometry(*mw);
    EnsureAccessibleWindowGeometry(mw);
    static auto saver = std::bind(&Preferences::SaveWindowGeometry, std::ref(*mw));
    connect(QApplication::instance(), &QCoreApplication::aboutToQuit, saver);

    connect(&loader, &MibViewLoader::LogError, this, [](const QString &text) {
        DiagnosticLogger::log(QStringLiteral("MIB"), text);
    }, Qt::DirectConnection);

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
    // Password protection is off unless the user turned it on; then the secrets must be
    // unlocked before any credential service reads them. Cancelling starts with the credentials
    // locked: their secrets are not loaded (so nothing can send a blank one) and the
    // Credentials screen offers Unlock.
    credentialProtection = new CredentialProtection(GetCredentialVaultFile(),
                                                    CredentialVault::DefaultIterations, this);
    if (credentialProtection->isEnabled()) PromptToUnlockCredentials(credentialProtection, nullptr);
    communityCredentialService = new CommunityCredentialService(
        GetCommunityCredentialsConfigFile(), credentialProtection->activeSecretStore(), this);
    QElapsedTimer phaseTimer; phaseTimer.start(); prefs->Init();
    DiagnosticLogger::log("Startup", QStringLiteral("settings UI load complete elapsed_ms=%1").arg(phaseTimer.elapsed()));
    trap = new Trap(this);
    agent->Init();
    upm = new USMProfileManager(this);
    editor = new MibEditor(this);
    MibLibraryWidget::Callbacks mibLibraryCallbacks;
    mibLibraryCallbacks.validate = [this](const QString &path, QString *error) {
        return modules->ValidateModuleFile(path, error);
    };
    mibLibraryCallbacks.downloadsCompleted = [this](const QStringList &requested, bool load,
                                                     int downloaded, const QString &destinationProfileId) -> QString {
        modules->RescanPath(); QString indexError; modules->RefreshDependencyIndex(&indexError);
        if (!indexError.isEmpty()) DiagnosticLogger::log("MIB", indexError);
        if (!load) return tr("Downloaded %n MIB file(s) to the Library.", nullptr, downloaded);

        const QString activeProfileId = destinationProfileId;
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
            emit mibLibrary->profilesChanged();
            if (mibLibrary->currentProfile().id == activeProfileId) mibLibrary->selectProfile(activeProfileId);
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
    mibLibraryCallbacks.checkDependencies = [this](
        std::function<void(const QList<MibModuleRecord> &)> prepareInventory,
        std::function<void(const QString &)> completed) {
        modules->CheckProfileDependencies(QStringLiteral("mib-library"), {}, false,
                                         std::move(prepareInventory), std::move(completed));
    };
    mibLibraryCallbacks.automaticProfiles = [this]() {
        return MibBuildAutomaticProfiles(modules->DependencyIndex()->libraryRoot(),
                                         modules->DependencyIndex()->files());
    };
    mibLibraryCallbacks.libraryFiles = [this]() { return modules->DependencyIndex()->files(); };
    mibLibraryCallbacks.libraryGeneration = [this]() { return quint64(modules->DependencyIndex()->generation()); };
    mibLibraryCallbacks.providersFor = [this](const QString &module) { return modules->DependencyIndex()->providersFor(module); };
    mibLibraryCallbacks.effectivePlan = [this](const MibProfileRecord &profile) {
        return modules->BuildEffectivePlan(profile);
    };
    phaseTimer.restart();
    mibLibrary = new MibLibraryWidget(prefs->DefaultMibPaths(), w.TabW, nullptr,
                                      mibLibraryCallbacks);
    DiagnosticLogger::log("Startup", QStringLiteral("Inventory construction complete elapsed_ms=%1").arg(phaseTimer.elapsed()));
    // MIBs is a permanent workspace with Library, Profiles and Editor pages.
    connect(modules, &MibModule::inventoryChanged, mibLibrary, &MibLibraryWidget::refreshOnIndexChange);
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
    queryToolsLayout->setContentsMargins(0, 0, 0, 4);
    queryToolsLayout->setSpacing(4);
    // Search every MIB file in the collection, not only the active profile's tree.
    auto *collectionSearch = new QLineEdit(queryTools);
    collectionSearch->setObjectName(QStringLiteral("CollectionSearch"));
    collectionSearch->setPlaceholderText(tr("Search MIBs by name or OID…"));
    collectionSearch->setToolTip(tr("Shows the OID of the object selected in the tree. Click here and type to search: a numeric OID "
                                    "and Enter selects it in the tree. Anything else, or an OID the active tree does not have, "
                                    "searches every MIB file in the collection and shows which module and which profiles contain it."));
    collectionSearch->setClearButtonEnabled(true);
    collectionSearch->setAccessibleName(tr("Search MIBs by name or OID"));
    collectionSearch->setMinimumWidth(140);
    queryToolsLayout->addWidget(collectionSearch, 1);
    // The same field also shows the OID of whatever is selected in the tree.
    auto *selectedOidField = new SelectedOidField(collectionSearch);
    connect(w.MIBTree, &MibModelView::CurrentObjectChanged, selectedOidField, &SelectedOidField::showOid);
    auto *inspectorTools = new QHBoxLayout;
    inspectorTools->setContentsMargins(10, 6, 10, 6); inspectorTools->addStretch();
    auto *copyInspectorOid = new QToolButton(w.NodeInfoLayout);
    copyInspectorOid->setObjectName("InspectorCopyOid"); copyInspectorOid->setText(tr("Copy OID"));
    copyInspectorOid->setEnabled(false); inspectorTools->addWidget(copyInspectorOid);
    qobject_cast<QVBoxLayout *>(w.NodeInfoLayout->layout())->insertLayout(0, inspectorTools);
    connect(w.MIBTree, &MibModelView::CurrentObjectChanged, copyInspectorOid,
        [copyInspectorOid](const QString &oid) {
            copyInspectorOid->setProperty("selectedOid", oid); copyInspectorOid->setEnabled(!oid.isEmpty());
        });
    connect(copyInspectorOid, &QToolButton::clicked, copyInspectorOid, [copyInspectorOid] {
        QApplication::clipboard()->setText(copyInspectorOid->property("selectedOid").toString());
    });

    // The tree filter sits at the top of the tree pane, under its heading.
    auto *treeFilter = new QLineEdit(w.MIBTreeLayout);
    treeFilter->setObjectName(QStringLiteral("MibTreeFilter"));
    treeFilter->setPlaceholderText(tr("Filter tree: name, module, or OID prefix"));
    treeFilter->setClearButtonEnabled(true);
    treeFilter->setAccessibleName(tr("Filter MIB tree"));
    connect(treeFilter, &QLineEdit::textChanged, w.MIBTree, &MibModelView::SetFilterText);
    if (auto *treeLayout = qobject_cast<QVBoxLayout *>(w.MIBTreeLayout->layout())) {
        treeLayout->insertWidget(treeLayout->indexOf(w.MIBTree), treeFilter);
        treeLayout->setContentsMargins(6, 6, 0, 0);
        treeLayout->setSpacing(4);
    }

    resultsPanel = new QueryResultsPanel(mw);
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
    connect(modules,&MibModule::profileRuntimeReady,mw,
        [this](const QString &id,const MibEffectivePlan &plan,MibEnvironmentPtr environment,
               const QStringList &,bool,bool){
            Q_UNUSED(id)
            const EnvironmentPresentation presentation = PresentEnvironment(environment);
            w.MIBTree->setVisibleModules(plan.effectiveModules);
            w.MIBTree->Populate();
            if (startup) startup->advance(startup->generation(),
                presentation.needsAttention ? StartupState::NeedsAttention : StartupState::Ready,
                environment ? tr("Environment: %1 modules · %2").arg(environment->loadedCount())
                                  .arg(presentation.state) : tr("Environment: Empty"));
        });
    connect(modules,&MibModule::profileRuntimeFailed,mw,[this](const QString &,const QString &error){
        DiagnosticLogger::log("MIB",tr("Unable to apply MIB profile: %1").arg(error));
        if (startup) startup->advance(startup->generation(), StartupState::NeedsAttention,
                                      tr("Environment: Needs attention"));
    });
    initializingMibProfile = false;
    DiagnosticLogger::log("Startup", QStringLiteral("MIB profile initialization deferred until after shell display"));

    auto *contextWidget = new QWidget(w.widget);
    contextWidget->setObjectName(QStringLiteral("CurrentDeviceContext"));
    // The shared header has one stable height across Query, MIBs and Traps.
    // OID navigation and collection search belong inside Query.
    auto *contextLayout = new QVBoxLayout(contextWidget);
    contextLayout->setContentsMargins(0, 0, 0, 0);
    contextLayout->setSpacing(0);
    auto *connectionRow = new QHBoxLayout;
    connectionRow->setContentsMargins(0, 0, 10, 0);
    connectionRow->setSpacing(6);
    auto *environmentContext = new AppearanceColors::SemanticLabel(tr("Environment: Building"),
        AppearanceColors::SemanticLabel::EnvironmentBadge, contextWidget);
    environmentContext->setObjectName(QStringLiteral("CurrentEnvironmentStatus"));
    environmentContext->setWordWrap(true); // Warning text must not force a wider connection bar.
    // The badge may shrink (its text wraps) so the header never sets the window's minimum width.
    environmentContext->setMinimumWidth(96);
    environmentContext->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    environmentContext->setProperty("status", QStringLiteral("building"));
    // The connection bar replaces the saved-device header: Target, Credential and the
    // MIB Profile selector (moved here from the Tree page, so there is only one).
    auto *connectionBar = new ConnectionBar(communityCredentialService, UsmCredentials(),
                                            browserProfile, contextWidget);
    browserProfileRow->hide();
    connectionRow->addWidget(connectionBar, 1);
    connectionRow->addWidget(environmentContext);
    contextLayout->addLayout(connectionRow);
    queryToolsLayout->setContentsMargins(0, 0, 0, 4);
    agent->SetConnectionBar(connectionBar);
    if (auto *centralLayout = qobject_cast<QGridLayout *>(w.widget->layout())) {
        centralLayout->removeWidget(w.TabW);
        centralLayout->setContentsMargins(8, 8, 8, 8);
        centralLayout->setSpacing(6);
        centralLayout->addWidget(contextWidget, 0, 0);
        centralLayout->addWidget(w.TabW, 1, 0);
        centralLayout->setRowStretch(1, 1);
    }

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
        environmentContext->setToolTip(QObject::tr("%1 modules loaded; %2. Click to show MIB Problems.")
            .arg(environment ? environment->loadedCount() : 0).arg(presentation.state));
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

    // Rehost the established feature widgets in a workbench shell. This only
    // changes presentation ownership: request, Profile, Environment, parser,
    // trap, editor, and graph services continue to own their existing state.
    auto *resultsPage = resultsPanel;
    resultsPanel->addControl(w.QueryTable);

    // Output: the SnmpB-style text of every request (names, interpreted values, errors), the
    // bottom panel history. It is interpreted text, not raw packets.
    auto *outputPage = new QWidget(mw);
    outputPage->setObjectName(QStringLiteral("WorkbenchOutputPage"));
    auto *outputLayout = new QVBoxLayout(outputPage);
    outputLayout->setContentsMargins(4, 4, 4, 4);
    auto *outputHeader = new QHBoxLayout;
    outputHeader->addStretch();
    auto *clearOutput = new QPushButton(tr("Clear"), outputPage);
    clearOutput->setObjectName(QStringLiteral("OutputClear"));
    clearOutput->setToolTip(tr("Clear the text output. Results, Watch and the Log are not affected."));
    connect(clearOutput, &QPushButton::clicked, w.Query, &QTextEdit::clear);
    outputHeader->addWidget(clearOutput);
    outputLayout->addLayout(outputHeader);
    w.Query->setParent(outputPage);
    outputLayout->addWidget(w.Query, 1);
    w.QueryLayout->hide();

    // Watch: repeat a GET on one object. Each row keeps the target and credential it was created
    // with, so the connection bar can change freely afterwards.
    auto *watchService = new WatchService(
        [this](const QString &target, const QString &credentialKey, SnmpRequestConfig *config, QString *error) {
            QSettings settings;
            AgentRequestSelection selection;
            const ConnectionError problem = ConnectionModel::build(
                target, credentialKey, communityCredentialService, UsmCredentials(),
                PreferencesSettings::load(settings), &selection);
            if (problem != ConnectionError::None) { *error = ConnectionModel::errorText(problem); return false; }
            return selection.requestConfig(config);
        },
        [](const SnmpRequestConfig &config) { return std::make_unique<SnmpPlusTransport>(config); },
        []() { return MibEnvironmentRegistry::active(); }, this);
    watchService->startTimer();
    auto *watchPanel = new WatchPanel(watchService,
        [this](const QString &key) {
            for (const ConnectionCredentialChoice &choice :
                 ConnectionModel::credentialChoices(communityCredentialService, UsmCredentials()))
                if (choice.key == key) return choice.label;
            return tr("(deleted credential)");
        }, mw);
    const QString isolatedWatchRoot = QCoreApplication::instance()->property("snmpb.isolatedStateRoot").toString();
    const QString watchRoot = isolatedWatchRoot.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) : isolatedWatchRoot;
    watchService->restoreDefinitions(QDir(watchRoot).filePath("watches-v1.json"));
    // Both entry points capture the same target/credential and create rows in one Watch service.
    const auto startWatch = [this, mw, watchService, watchPanel](QString oid, bool column, bool fromTree) {
        auto *bar = mw->findChild<ConnectionBar *>(QStringLiteral("ConnectionBar"));
        if (!bar || oid.isEmpty()) return;
        WatchSpec spec;
        spec.target = bar->target(); spec.credentialKey = bar->credentialKey();
        spec.intervalSeconds = watchPanel->intervalSeconds();
        QSettings settings;
        AgentRequestSelection probe;
        const ConnectionError problem = ConnectionModel::build(
            spec.target, spec.credentialKey, communityCredentialService, UsmCredentials(),
            PreferencesSettings::load(settings), &probe);
        if (problem != ConnectionError::None) {
            QMessageBox::warning(mw, tr("MIB Navigator"), ConnectionModel::errorText(problem));
            return;
        }
        // Freeze the resolved port and context; credential values are still re-resolved for each poll.
        const auto &captured = probe.profile;
        spec.target = captured.address.contains(':')
            ? QStringLiteral("[%1]:%2").arg(captured.address, captured.port)
            : QStringLiteral("%1:%2").arg(captured.address, captured.port);
        spec.contextName = captured.contextname;
        if (column) {
            bool accepted = false;
            const QString instance = QInputDialog::getText(mw, tr("Watch table value"),
                tr("Instance suffix (for example 1 or 4.101.116.104.49):"), QLineEdit::Normal,
                QString(), &accepted).trimmed();
            if (!accepted) return;
            static const QRegularExpression suffix(QStringLiteral(R"(^[0-9]+(?:\.[0-9]+)*$)"));
            bool validInstance = suffix.match(instance).hasMatch();
            for (const QString &part : instance.split('.')) {
                bool number = false;
                const auto value = part.toULongLong(&number);
                validInstance = validInstance && number && value <= 0xffffffffULL;
            }
            if (!validInstance) {
                QMessageBox::warning(mw, tr("Watch table value"),
                    tr("Enter a numeric instance suffix separated by dots. Each number must be between 0 and 4294967295."));
                return;
            }
            oid += QStringLiteral(".") + instance;
        } else if (fromTree) oid += QStringLiteral(".0");
        if (!Oid(oid.toLatin1().constData()).valid()) {
            QMessageBox::warning(mw, tr("Watch value"), tr("The object and instance do not form a valid OID."));
            return;
        }
        spec.oid = oid;
        watchService->add(spec);
        if (workbench) workbench->openDocument(watchPanel);
    };
    connect(w.MIBTree, &MibModelView::WatchFromOid, watchPanel,
            [startWatch](const QString &oid, bool column) { startWatch(oid, column, true); });
    connect(resultsPanel, &QueryResultsPanel::watchRequested, watchPanel,
            [startWatch](const QString &oid) { startWatch(oid, false, false); });

    auto *macroService = new QueryMacroService(QDir(watchRoot).filePath("query-macros-v1.json"), this);
    macroService->setObjectName("QueryMacroService");
    // Unlike the legacy varbind chooser, this picker has no instance discovery path.
    const auto macroPicker = [this](QWidget *parent, QString *error) -> std::optional<QueryMacroEntry> {
        if (error) error->clear();
        const MibEnvironmentPtr environment = modules->CurrentEnvironment();
        QDialog dialog(parent); dialog.setObjectName("QueryMacroObjectPicker"); dialog.setWindowTitle(tr("Add MIB object to macro"));
        auto *layout = new QVBoxLayout(&dialog);
        auto *filter = new QLineEdit; filter->setObjectName("QueryMacroPickerFilter"); filter->setPlaceholderText(tr("Filter MIB objects"));
        filter->setAccessibleName(tr("Filter MIB objects")); layout->addWidget(filter);
        auto *tree = new MibModelView(&dialog, MibModelView::Mode::OidSelection); tree->setObjectName("QueryMacroPickerTree");
        auto *model = new MibTreeModel(tree); model->setSnapshot(loader.TreeSnapshot()); tree->setTreeModel(model); tree->Populate();
        layout->addWidget(tree, 1);
        auto *message = new QLabel(tr("Choose a readable scalar, column, row or table. No requests are sent."));
        message->setTextFormat(Qt::PlainText); message->setWordWrap(true); layout->addWidget(message);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel); layout->addWidget(buttons);
        buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
        std::optional<QueryMacroEntry> selected;
        connect(filter, &QLineEdit::textChanged, tree, &MibModelView::SetFilterText);
        connect(tree->selectionModel(), &QItemSelectionModel::currentChanged, &dialog, [&](const QModelIndex &, const QModelIndex &) {
            QString why; selected = QueryMacroEntryForObject(environment, tree->currentObjectOid(), &why);
            buttons->button(QDialogButtonBox::Ok)->setEnabled(selected.has_value());
            message->setText(selected ? tr("Numeric identities are saved. No MIB Profile or connection is stored.") : why);
        });
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        DialogLayout::initialize(dialog, QSize(650, 650));
        if (dialog.exec() != QDialog::Accepted) return {};
        return selected;
    };
    auto *macroPanel = new QueryMacrosPanel(macroService, macroPicker,
        [this](const QString &oid) {
            const auto environment = modules->CurrentEnvironment(); // same source as the picker
            const auto *node = environment ? environment->nodeByOid(oid) : nullptr;
            return node ? node->qualifiedName : QString();
        }, mw);
    connect(macroPanel, &QueryMacrosPanel::runRequested, agent, &Agent::RunMacro);
    connect(agent, &Agent::foregroundBusyChanged, macroPanel, &QueryMacrosPanel::setQueryBusy);
    connect(agent, &Agent::macroRejected, macroPanel, &QueryMacrosPanel::showError);
    connect(w.MIBTree, &MibModelView::MacroFromOid, macroPanel, [this, macroPanel](const QString &oid) {
        QString error; const auto entry = QueryMacroEntryForObject(modules->CurrentEnvironment(), oid, &error);
        if (entry) macroPanel->addObject(*entry); else macroPanel->showError(error);
        if (workbench) workbench->openDocument(macroPanel);
    });

    // Collection-wide search: an index of every definition in every MIB file, rebuilt in the
    // background when the collection changes.
    auto *collectionService = new MibCollectionService([this]() {
        QStringList files;
        for (const MibDependencyFileRecord &record : modules->DependencyIndex()->files())
            files << record.canonicalPath;
        return files;
    }, this);
    auto profileModules = std::make_shared<QHash<QString, QList<ProfileMatchProfile>>>();
    auto profileModulesValid = std::make_shared<bool>(false);
    const auto forgetProfileModules = [profileModulesValid]() { *profileModulesValid = false; };
    connect(modules, &MibModule::inventoryChanged, this, forgetProfileModules);
    connect(mibLibrary, &MibLibraryWidget::profilesChanged, this, forgetProfileModules);
    connect(modules, &MibModule::inventoryChanged, collectionService, &MibCollectionService::invalidate);
    collectionService->invalidate();   // first build shortly after start-up
    CollectionSearchPanel::Lookups searchLookups;
    // module -> the profiles whose environment includes it (rebuilt when the Library or profiles change)
    const auto profilesHolding = [this, profileModules, profileModulesValid](const QString &module) {
        if (!*profileModulesValid) {
            profileModules->clear();
            for (const MibProfileRecord &profile : mibLibrary->profileService()->profiles())
                for (const QString &name : modules->BuildEffectivePlan(profile).effectiveModules)
                    (*profileModules)[name] << ProfileMatchProfile{profile.id, profile.name, profile.type == MibProfileType::Automatic};
            *profileModulesValid = true;
        }
        return profileModules->value(module);
    };
    searchLookups.profilesContaining = [profilesHolding](const QString &module) {
        QStringList names;
        for (const ProfileMatchProfile &profile : profilesHolding(module)) names << profile.name;
        return names;
    };
    mibLibrary->setProfilesContaining(searchLookups.profilesContaining);
    searchLookups.activeModules = [this]() {
        QSet<QString> active;
        if (const MibEnvironmentPtr environment = modules->CurrentEnvironment())
            for (const auto &module : environment->modules()) active.insert(module.identity);
        return active;
    };
    auto *searchPanel = new CollectionSearchPanel(collectionService, searchLookups, mw);
    connect(searchPanel, &CollectionSearchPanel::showInTreeRequested, this, [this](const QString &oid) {
        // The tree is always on screen; select the object and give the tree the keyboard.
        w.MIBTree->SelectFromOid(oid);
        w.MIBTree->setFocus(Qt::OtherFocusReason);
    });
    connect(searchPanel, &CollectionSearchPanel::openInEditorRequested, this, [this](const QString &file, int line) {
        editor->MibFileOpen(file);
        ShowEditorDocument();
        editor->GoToLine(line);
    });
    connect(collectionSearch, &QLineEdit::returnPressed, this, [this, collectionService, searchPanel, collectionSearch]() {
        // A numeric OID the active tree has is simply selected there; everything else is a search.
        static const QRegularExpression numericOid(QStringLiteral(R"(^\.?\d+(\.\d+)*$)"));
        const QString typed = collectionSearch->text().trimmed();
        if (numericOid.match(typed).hasMatch() &&
            w.MIBTree->LocateOid(typed.startsWith('.') ? typed.mid(1) : typed)) {
            w.MIBTree->setFocus(Qt::OtherFocusReason);
            return;
        }
        if (!collectionService->hasIndex() && !collectionService->isBuilding()) collectionService->rebuildNow();
        searchPanel->search(collectionSearch->text());
        if (workbench) workbench->openDocument(searchPanel);
    });

    // Log: every request and how it ended.
    auto *requestLog = new RequestLog(this);
    agent->SetRequestLog(requestLog);
    auto *requestLogPanel = new RequestLogPanel(requestLog, mw);

    // Detect: on click, ask the device what it is and pick the profile holding its MIBs. Nothing
    // is ever sent unless the user clicks the button.
    auto *detector = new ProfileDetector(
        [this](const QString &target, const QString &credentialKey, SnmpRequestConfig *config, QString *error) {
            QSettings settings;
            AgentRequestSelection selection;
            const ConnectionError problem = ConnectionModel::build(
                target, credentialKey, communityCredentialService, UsmCredentials(),
                PreferencesSettings::load(settings), &selection);
            if (problem != ConnectionError::None) { *error = ConnectionModel::errorText(problem); return false; }
            return selection.requestConfig(config);
        },
        [](const SnmpRequestConfig &config) { return std::make_unique<SnmpPlusTransport>(config); }, this);
    auto detectLogId = std::make_shared<quint64>(0);
    if (auto *bar = mw->findChild<ConnectionBar *>(QStringLiteral("ConnectionBar"))) {
        connect(bar, &ConnectionBar::detectRequested, this,
                [this, mw, bar, detector, requestLog, collectionService, detectLogId]() {
            if (!collectionService->hasIndex()) {
                if (!collectionService->isBuilding()) collectionService->rebuildNow();
                QMessageBox::information(mw, tr("Detect profile"),
                    tr("The MIB collection is still being indexed. Try again in a moment."));
                return;
            }
            QString error;
            *detectLogId = requestLog->begin(tr("Detect profile"), bar->target(), bar->credentialLabel(),
                                             QStringLiteral("sysObjectID.0, sysDescr.0"));
            bar->setDetectBusy(true);
            if (!detector->detect(bar->target(), bar->credentialKey(), &error)) {
                bar->setDetectBusy(false);
                requestLog->finish(*detectLogId, QStringLiteral("<font color=red>%1</font>").arg(error.toHtmlEscaped()), 0);
                QMessageBox::warning(mw, tr("Detect profile"), error);
            }
        });
        connect(detector, &ProfileDetector::finished, this,
                [this, mw, bar, requestLog, collectionService, detectLogId, profilesHolding]
                (const QString &target, const DeviceIdentity &identity) {
            bar->setDetectBusy(false);
            const auto closeLog = [&](const QString &text, bool failed, int objects) {
                requestLog->finish(*detectLogId, failed ? QStringLiteral("<font color=red>%1</font>").arg(text.toHtmlEscaped())
                                                        : text.toHtmlEscaped(), objects);
            };
            if (!identity.ok) {
                closeLog(identity.error, true, 0);
                QMessageBox::warning(mw, tr("Detect profile"),
                    tr("Could not read the device's identity from %1.\n\n%2\n\nNothing was changed.").arg(target, identity.error));
                return;
            }
            const std::shared_ptr<const MibCollectionIndex> index = collectionService->snapshot();
            if (!index) { closeLog(tr("The MIB collection index is not ready"), true, 0); return; }
            const ProfileMatchResult match = MatchProfileForSysObjectId(*index, identity.sysObjectId,
                [profilesHolding](const QString &module) {
                    QList<ProfileMatchProfile> detectable;
                    for (const ProfileMatchProfile &profile : profilesHolding(module))
                        if (profile.id != MibProfileDefinitions::standardsId()) detectable << profile;   // Standards fits every device
                    return detectable;
                });
            const QString identityText = tr("sysObjectID %1\nsysDescr: %2").arg(identity.sysObjectId, identity.sysDescr);
            if (match.candidates.isEmpty()) {
                // Say where it looked: a vendor whose MIBs are simply not in the Library is the usual cause.
                QSettings librarySettings;
                const QString searched = tr("Searched %n MIB file(s) in %1.", nullptr, index->fileCount())
                    .arg(QDir::toNativeSeparators(MibCollection::configuredRoot(librarySettings)));
                closeLog(tr("%1 %2 No profile matched; nothing was changed.").arg(match.explanation, searched), true, 0);
                QMessageBox::information(mw, tr("Detect profile"),
                    tr("%1\n\n%2\n%3\n\nNothing was changed.").arg(identityText, match.explanation, searched));
                return;
            }
            ProfileMatchCandidate chosen = match.candidates.first();
            if (match.candidates.size() > 1) {
                QStringList items;
                for (const ProfileMatchCandidate &c : match.candidates)
                    items << tr("%1  (MIBs: %2)").arg(c.profileName, c.viaModules.mid(0, 3).join(QStringLiteral(", ")));
                bool accepted = false;
                const QString picked = QInputDialog::getItem(mw, tr("Detect profile"),
                    tr("%1\n\n%2\n\nSeveral profiles hold MIBs for this device. Choose one:").arg(identityText, match.explanation),
                    items, 0, false, &accepted);
                if (!accepted) { closeLog(tr("%1 Several profiles matched; none was chosen.").arg(match.explanation), false, 0); return; }
                chosen = match.candidates.at(items.indexOf(picked));
            }
            mibLibrary->selectProfile(chosen.profileId);
            const QString done = tr("Detected %1; MIB Profile set to \"%2\". %3").arg(identity.sysDescr.left(80), chosen.profileName, match.explanation);
            closeLog(done, false, 1);
            mw->statusBar()->showMessage(done, 15000);
        });
    }

    class ProblemSeverityItem final : public QTableWidgetItem {
    public:
        using QTableWidgetItem::QTableWidgetItem;
        bool operator<(const QTableWidgetItem &other) const override {
            return data(Qt::UserRole + 1).toInt() < other.data(Qt::UserRole + 1).toInt();
        }
    };
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
    auto *problemFilter = new QLineEdit(problemsPage);
    problemFilter->setObjectName("WorkbenchProblemFilter");
    problemFilter->setPlaceholderText(tr("Filter problems"));
    problemFilter->setClearButtonEnabled(true);
    problemFilter->setAccessibleName(tr("Filter MIB Problems"));
    auto *severityFilter = new QComboBox(problemsPage);
    severityFilter->setObjectName("WorkbenchProblemSeverity");
    severityFilter->setAccessibleName(tr("Problem severity"));
    severityFilter->addItems({tr("All severities"), tr("Errors"), tr("Warnings")});
    auto *copyProblems = new QPushButton(tr("Copy CSV"), problemsPage);
    copyProblems->setObjectName("WorkbenchProblemCopy");
    auto *exportProblems = new QPushButton(tr("Export CSV…"), problemsPage);
    exportProblems->setObjectName("WorkbenchProblemExport");
    auto *visibleProblems = new QLabel(problemsPage);
    visibleProblems->setObjectName("WorkbenchProblemVisibleCount");
    auto *filterBar = new QHBoxLayout;
    filterBar->addWidget(problemFilter, 1); filterBar->addWidget(severityFilter);
    filterBar->addWidget(visibleProblems); filterBar->addWidget(copyProblems); filterBar->addWidget(exportProblems);
    auto *problemToolbar = new QHBoxLayout;
    problemToolbar->addWidget(problemHint, 1);
    problemToolbar->addWidget(showNotes, 0, Qt::AlignTop);
    auto *problemTable = new QTableWidget(0, 6, problemsPage);
    problemTable->setObjectName(QStringLiteral("WorkbenchProblemTable"));
    problemTable->setItemDelegate(new AppearanceColors::ItemDelegate(problemTable));
    problemTable->setHorizontalHeaderLabels({tr("Severity"), tr("Module"), tr("Problem"),
                                             tr("Why"), tr("What it affects"), tr("File")});
    problemTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    problemTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    problemTable->setSelectionMode(QAbstractItemView::SingleSelection);
    problemTable->setWordWrap(true);
    problemTable->verticalHeader()->hide();
    {
        QHeaderView *header = problemTable->horizontalHeader();
        header->setStretchLastSection(false);
        for (int column : {0, 1, 2, 5}) header->setSectionResizeMode(column, QHeaderView::Interactive);
        header->setSectionResizeMode(3, QHeaderView::Stretch);   // Why
        header->setSectionResizeMode(4, QHeaderView::Stretch);   // What it affects
        header->resizeSection(0, 80);
        header->resizeSection(1, 170);
        header->resizeSection(2, 170);
        header->resizeSection(5, 210);
    }
    problemTable->setSortingEnabled(true);
    problemTable->sortItems(0, Qt::AscendingOrder);
    problemTable->setTextElideMode(Qt::ElideNone);
    connect(problemTable->horizontalHeader(), &QHeaderView::sectionResized, problemTable,
            [problemTable]() { problemTable->resizeRowsToContents(); });
    // The full explanation of the selected row, so nothing is cut off.
    auto *problemDetail = new QPlainTextEdit(problemsPage);
    problemDetail->setObjectName(QStringLiteral("WorkbenchProblemDetail"));
    problemDetail->setReadOnly(true);
    problemDetail->setMinimumHeight(48);
    problemDetail->setPlaceholderText(tr("Select a row to read the full explanation."));
    problemsLayout->addWidget(problemState);
    problemsLayout->addLayout(problemToolbar);
    problemsLayout->addLayout(filterBar);
    auto *problemSplitter = new QSplitter(Qt::Vertical, problemsPage);
    problemSplitter->setObjectName(QStringLiteral("WorkbenchProblemSplitter"));
    problemSplitter->setChildrenCollapsible(false);
    problemSplitter->addWidget(problemTable);
    problemSplitter->addWidget(problemDetail);
    problemSplitter->setStretchFactor(0, 4);
    problemSplitter->setStretchFactor(1, 1);
    problemsLayout->addWidget(problemSplitter, 1);

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
    const auto repopulateProblems = [problemTable, problemDetail, showNotes, problemList,
            problemFilter, severityFilter, copyProblems, exportProblems, visibleProblems]() {
        problemTable->setSortingEnabled(false);
        problemDetail->clear();
        problemTable->setRowCount(0);
        QList<int> order;
        const QString needle = problemFilter->text().trimmed();
        for (int i = 0; i < problemList->size(); ++i) {
            const auto &p = problemList->at(i);
            if (p.severity == MibProblemSeverity::Note && !showNotes->isChecked()) continue;
            if (severityFilter->currentIndex() == 1 && p.severity != MibProblemSeverity::Error) continue;
            if (severityFilter->currentIndex() == 2 && p.severity != MibProblemSeverity::Warning) continue;
            const QString searchable = QStringList{p.module, p.category, p.why, p.effect, p.file,
                QString::number(p.line), p.requestedBy.join(' '), p.suggestion, p.parserTag}.join(' ');
            if (!needle.isEmpty() && !searchable.contains(needle, Qt::CaseInsensitive)) continue;
            order.append(i);
        }
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
                QTableWidgetItem *item = column == 0
                    ? new ProblemSeverityItem(cells[column]) : new QTableWidgetItem(cells[column]);
                item->setToolTip(tip);
                if (column == 0) {
                    item->setData(Qt::UserRole, index);
                    item->setData(Qt::UserRole + 1, static_cast<int>(problem.severity));
                    if (problem.severity == MibProblemSeverity::Error) item->setData(AppearanceColors::ForegroundToneRole, AppearanceColors::Error);
                    else if (problem.severity == MibProblemSeverity::Warning) item->setData(AppearanceColors::ForegroundToneRole, AppearanceColors::Warning);
                }
                problemTable->setItem(row, column, item);
            }
        }
        problemTable->setSortingEnabled(true);
        problemTable->resizeRowsToContents();
        copyProblems->setEnabled(!order.isEmpty()); exportProblems->setEnabled(!order.isEmpty());
        visibleProblems->setText(QObject::tr("%1 shown").arg(order.size()));
    };
    const auto snapshotProblems = [problemTable, problemList]() {
        QList<int> order;
        for (int row = 0; row < problemTable->rowCount(); ++row)
            if (auto *item = problemTable->item(row, 0)) order.append(item->data(Qt::UserRole).toInt());
        return MibProblemsAsCsv(*problemList, order);
    };
    connect(copyProblems, &QPushButton::clicked, problemsPage, [snapshotProblems]() {
        QGuiApplication::clipboard()->setText(snapshotProblems());
    });
    connect(exportProblems, &QPushButton::clicked, problemsPage, [problemsPage, snapshotProblems]() {
        const QString snapshot = snapshotProblems(); // freeze before the modal chooser/profile publications
        const QString path = QFileDialog::getSaveFileName(problemsPage, QObject::tr("Export MIB Problems"),
            QStringLiteral("mib-problems.csv"), QObject::tr("CSV files (*.csv)"));
        if (path.isEmpty()) return;
        QString error;
        if (!SaveMibProblemsCsv(path, snapshot, &error))
            QMessageBox::warning(problemsPage, QObject::tr("MIB Problems could not be exported"), error);
    });
    connect(problemFilter, &QLineEdit::textChanged, problemsPage, [repopulateProblems]() { repopulateProblems(); });
    connect(severityFilter, &QComboBox::currentIndexChanged, problemsPage, [repopulateProblems]() { repopulateProblems(); });
    repopulateProblems();
    connect(showNotes, &QCheckBox::toggled, problemsPage,
            [repopulateProblems, describeProblems](bool) { describeProblems(); repopulateProblems(); });
    connect(problemTable, &QTableWidget::itemSelectionChanged, problemsPage,
            [problemTable, problemDetail, problemList]() {
        // Selecting a row need not change the current cell (for example after
        // focusing the table or clearing its selection). Follow selection itself.
        const QModelIndexList selected = problemTable->selectionModel()->selectedRows();
        const int row = selected.isEmpty() ? -1 : selected.first().row();
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
        if (problem.file.isEmpty() || !QFileInfo::exists(problem.file)) {
            DiagnosticLogger::log("UI", QStringLiteral("Problems row opened without a file: %1 / %2")
                                  .arg(problem.category, problem.module));
            return;
        }
        DiagnosticLogger::log("UI", QStringLiteral("Problems row opens %1 at line %2 (%3)")
                              .arg(problem.file).arg(problem.line).arg(problem.category));
        editor->MibFileOpen(problem.file);
        editor->GoToLine(problem.line);
        ShowEditorDocument();
    });
    connect(modules, &MibModule::profileRuntimeBuildStarted, problemState,
            [problemState](const QString &) { problemState->setText(QObject::tr("Building the MIB environment…")); });
    connect(modules, &MibModule::profileRuntimeReady, problemState,
            [this, problemsPage, problemList, problemLoadedCount, describeProblems, repopulateProblems](
                const QString &, const MibEffectivePlan &, MibEnvironmentPtr environment,
                const QStringList &, bool, bool) {
        *problemList = environment ? environment->problems() : QList<MibProblem>{};
        *problemLoadedCount = environment ? static_cast<int>(environment->loadedCount()) : 0;
        const MibProblemSummary reported = MibSummarizeProblems(*problemList);
        DiagnosticLogger::log("UI", QStringLiteral(
            "Problems tab updated modules=%1 errors=%2 warnings=%3 notes=%4")
            .arg(*problemLoadedCount).arg(reported.errors).arg(reported.warnings).arg(reported.notes));
        describeProblems();
        repopulateProblems();
        if (workbench) {
            const int count = reported.errors + reported.warnings;
            workbench->setBottomPageNote(problemsPage, count ? QString::number(count) : QString());
            workbench->setBottomPageVisible(problemsPage, count > 0);
        }
    });
    connect(modules, &MibModule::profileRuntimeFailed, problemState,
            [this, problemsPage, problemState, problemList, repopulateProblems](const QString &, const QString &error) {
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
        if (workbench) {
            workbench->setBottomPageNote(problemsPage, QStringLiteral("1"));
            workbench->setBottomPageVisible(problemsPage, true);
        }
    });

    // The SNMP library's own log is a troubleshooting tool, so it lives in Help > Diagnostics
    // (a separate, non-modal window) rather than in the bottom panel.
    auto *diagnosticsWindow = new QDialog(mw);
    diagnosticsWindow->setObjectName(QStringLiteral("DiagnosticsWindow"));
    diagnosticsWindow->setWindowTitle(tr("Diagnostics - Application and SNMP library"));
    diagnosticsWindow->setModal(false);
    auto *diagnosticsLayout = new QVBoxLayout(diagnosticsWindow);
    w.LogTab->setParent(diagnosticsWindow);
    diagnosticsLayout->addWidget(w.LogTab);
    w.LogTab->show(); // setParent() hides a page removed from a QTabWidget.
    auto *diagnosticsButtons = new QDialogButtonBox(QDialogButtonBox::Close, diagnosticsWindow);
    diagnosticsLayout->addWidget(diagnosticsButtons);
    connect(diagnosticsButtons, &QDialogButtonBox::rejected, diagnosticsWindow, &QDialog::reject);
    DialogLayout::initialize(*diagnosticsWindow, QSize(800, 500));
    auto *showDiagnostics = new QAction(tr("&Diagnostics…"), mw);
    showDiagnostics->setObjectName(QStringLiteral("HelpDiagnostics"));
    connect(showDiagnostics, &QAction::triggered, diagnosticsWindow, [diagnosticsWindow]() {
        DiagnosticLogger::log("Diagnostics", tr("Diagnostics window opened"));
        diagnosticsWindow->show();
        diagnosticsWindow->raise();
        diagnosticsWindow->activateWindow();
    });
    w.helpMenu->insertAction(w.helpMenu->actions().value(0, nullptr), showDiagnostics);
    // The guide is also in the package (getting-started.md); the online copy is the one that stays current.
    auto *showGuide = new QAction(tr("&Getting Started Guide"), mw);
    showGuide->setObjectName(QStringLiteral("HelpGettingStarted"));
    showGuide->setShortcut(QKeySequence::HelpContents);
    connect(showGuide, &QAction::triggered, mw, []() {
        QDesktopServices::openUrl(QUrl(QStringLiteral(
            "https://github.com/Token-Ring-Joe/mib-navigator/blob/main/docs/getting-started.md")));
    });
    w.helpMenu->insertAction(w.helpMenu->actions().value(0, nullptr), showGuide);

    // Problems describes the active MIB environment. Its bottom tab is conditional; the badge
    // and View menu can also open it when the report contains only notes.
    auto *problemHeading = new QLabel(tr("Problems loading the active MIB profile"), problemsPage);
    problemHeading->setObjectName(QStringLiteral("WorkbenchProblemHeading"));
    {
        QFont font = problemHeading->font(); font.setBold(true); problemHeading->setFont(font);
    }
    problemsLayout->insertWidget(0, problemHeading);
    const auto nameProblemsFor = [problemHeading](const QString &profileName) {
        problemHeading->setText(profileName.isEmpty() ? tr("Problems loading the active MIB profile")
                                                      : tr("Problems loading %1").arg(profileName));
    };
    connect(modules, &MibModule::profileRuntimeBuildStarted, problemHeading, nameProblemsFor);
    connect(modules, &MibModule::profileRuntimeReady, problemHeading,
            [nameProblemsFor](const QString &, const MibEffectivePlan &plan, MibEnvironmentPtr,
                              const QStringList &, bool, bool) { nameProblemsFor(plan.profileName); });
    if (auto *badge = mw->findChild<QLabel *>(QStringLiteral("CurrentEnvironmentStatus"))) {
        badge->setCursor(Qt::PointingHandCursor);
        badge->setToolTip(tr("Show what happened loading this MIB profile (Problems)"));
        badge->installEventFilter(new ClickToRun([this, problemsPage]() {
            if (workbench) workbench->showBottomPage(problemsPage);
        }, badge));
    }

    // Traps is a top-level workspace: history on the left, bindings and
    // notification metadata on the right.
    w.TrapLogSplitter->setOrientation(Qt::Horizontal);
    w.NodeInfoLayout->setObjectName(QStringLiteral("WorkbenchMibInspector"));
    w.NodeInfoL->hide(); // the Inspector dock already supplies the heading
    mibLibrary->setEditorPage(w.EditorTab);
    w.EditorTab->setProperty("workbenchFocusTarget", w.MIBFile->objectName());
    w.TrapsTab->setProperty("workbenchFocusTarget", w.TrapLog->objectName());
    w.NodeInfoLayout->setProperty("workbenchFocusTarget", w.NodeInfo->objectName());
    outputPage->setProperty("workbenchFocusTarget", w.Query->objectName());
    problemsPage->setProperty("workbenchFocusTarget", problemTable->objectName());
    WorkbenchShell::Documents documents;
    documents.results = resultsPage;
    documents.library = mibLibrary;
    documents.editor = w.EditorTab;
    documents.search = searchPanel;
    WorkbenchShell::BottomPages bottomPages;
    bottomPages.output = outputPage;
    documents.watch = watchPanel;
    documents.macros = macroPanel;
    documents.traps = w.TrapsTab;
    bottomPages.log = requestLogPanel;
    bottomPages.problems = problemsPage;
    // The tree pane (heading, filter, tree) becomes the permanent left pane; the Tree tab that
    // used to hold it is left empty and is no longer shown.
    workbench = new WorkbenchShell(mw, w.widget,
        qobject_cast<QGridLayout *>(w.widget->layout()), contextWidget, w.MIBTreeLayout, w.TabW,
        documents, w.NodeInfoLayout, bottomPages, this, connectionRow);
    // The query toolbar and Inspector belong to Query; the shared panel spans all workspaces.
    // Go to OID and collection search act on the tree, so they sit at the top of the tree pane.
    // That leaves the Results / Watch / Macros tabs starting at the same height as the MIBs tabs.
    if (auto *treeLayout = qobject_cast<QVBoxLayout *>(w.MIBTreeLayout->layout())) {
        QWidget *filter = w.MIBTreeLayout->findChild<QWidget *>(QStringLiteral("MibTreeFilter"));
        treeLayout->insertWidget(filter ? treeLayout->indexOf(filter) : 0, queryTools);
        queryTools->show();
    }
    connect(resultsPanel, &QueryResultsPanel::queryStarted, workbench, &WorkbenchShell::showResults);
    // Activity is derived from existing operation owners; visibility never controls their lifetimes.
    const auto mibBuilding = std::make_shared<bool>(false);
    const auto updateActivity = [this, requestLog, problemList, mibBuilding]() {
        int running = (*mibBuilding ? 1 : 0) + (editor->VerificationRunning() ? 1 : 0) +
                      (mibLibrary->operationRunning() ? 1 : 0);
        const auto summary = MibSummarizeProblems(*problemList);
        int problems = summary.errors + summary.warnings;
        const auto *macro = agent->findChild<QueryMacroRunner *>("QueryMacroRunner");
        const bool macroBusy = macro && macro->isBusy();
        for (const auto &entry : requestLog->entries()) {
            running += entry.outcome == RequestOutcome::Running &&
                !(macroBusy && entry.id == agent->ActiveRequestLogId());
            problems += entry.outcome == RequestOutcome::Failed ||
                        entry.outcome == RequestOutcome::TimedOut || entry.outcome == RequestOutcome::Refused;
        }
        // The lifecycle owner remains authoritative even if Log is cleared or
        // there is no batch in flight. Its batch is not a second operation.
        if (macroBusy) ++running;
        workbench->setActivityCounts(running, problems);
    };
    connect(mibLibrary, &MibLibraryWidget::activityChanged, workbench, updateActivity);
    connect(w.actionVerifyMIB, &QAction::triggered, workbench, updateActivity);
    connect(editor, &MibEditor::VerificationFinished, workbench, updateActivity);
    connect(requestLog, &RequestLog::entryAdded, workbench, updateActivity);
    connect(requestLog, &RequestLog::entryChanged, workbench, updateActivity);
    connect(requestLog, &RequestLog::cleared, workbench, updateActivity);
    if (auto *macro = agent->findChild<QueryMacroRunner *>("QueryMacroRunner")) {
        connect(macro, &QueryMacroRunner::changed, workbench, updateActivity);
        connect(macro, &QueryMacroRunner::completed, workbench, updateActivity);
    }
    connect(modules, &MibModule::profileRuntimeBuildStarted, workbench,
            [mibBuilding, updateActivity]() { *mibBuilding = true; updateActivity(); });
    connect(modules, &MibModule::profileRuntimeReady, workbench,
            [mibBuilding, updateActivity]() { *mibBuilding = false; updateActivity(); });
    connect(modules, &MibModule::profileRuntimeFailed, workbench,
            [mibBuilding, updateActivity]() { *mibBuilding = false; updateActivity(); });
    updateActivity();
    // Coalesce streamed warning changes without scanning every cell of a growing Walk.
    auto *warningCount = new QTimer(resultsPanel);
    warningCount->setSingleShot(true);
    connect(warningCount, &QTimer::timeout, resultsPanel, [this]() {
        const int count = resultsPanel->warningCount();
        if (workbench) workbench->setResultsNote(count == 0 ? QString()
            : count == 1 ? tr("1 warning") : tr("%1 warnings").arg(count));
    });
    connect(resultsPanel, &QueryResultsPanel::warningsChanged, warningCount,
            [warningCount]() { warningCount->start(0); });
    startup->advance(startupGeneration, StartupState::ShellReady,
                     tr("Loading MIB Catalog…"));
    connect(workbench, &WorkbenchShell::mibLibraryModeRequested,
            mibLibrary, &MibLibraryWidget::showWorkspaceMode);
    connect(w.fileNewAction, &QAction::triggered, mw,
            [this]() { ShowEditorDocument(); });
    connect(w.fileOpenAction, &QAction::triggered, mw,
            [this]() { ShowEditorDocument(); });

    auto *statusEvents = new QLabel(
        prefs->GetEnableIPv4() || prefs->GetEnableIPv6()
            ? tr("Traps: Listening") : tr("Traps: Stopped"), mw->statusBar());
    statusEvents->setObjectName(QStringLiteral("WorkbenchStatusEvents"));
    if (!start_msg.isEmpty()) {
        statusEvents->setText(tr("Traps: Needs attention (hover for details)"));
        statusEvents->setToolTip(start_msg);
        w.LogOutput->append(start_msg.section(QStringLiteral("\n\n"), 0, 1)
                            .replace(QStringLiteral("\n\n"), QStringLiteral(" ")));
    }
    mw->statusBar()->addPermanentWidget(statusEvents);
    // A single header indicator also describes startup progress/failures. The
    // state machine and diagnostics retain their detail without duplicate status text.
    connect(startup, &StartupCoordinator::stateChanged, environmentContext,
            [environmentContext](StartupState state, const QString &detail) {
        if (state == StartupState::Ready) return; // the environment report supplies the final label
        QString text;
        QString status = QStringLiteral("building");
        if (state == StartupState::CatalogLoading) text = QObject::tr("Catalog: Loading…");
        else if (state == StartupState::ProfilePreparing) text = QObject::tr("Profile: Preparing…");
        else if (state == StartupState::EnvironmentBuilding) text = QObject::tr("Environment: Building…");
        else if (state == StartupState::NeedsAttention || state == StartupState::Failed) {
            text = QObject::tr("Environment: Needs attention"); status = QStringLiteral("error");
        }
        if (text.isEmpty()) return;
        environmentContext->setText(text); environmentContext->setProperty("status", status);
        environmentContext->setToolTip(detail + QObject::tr("\nClick to show MIB Problems."));
        environmentContext->style()->unpolish(environmentContext);
        environmentContext->style()->polish(environmentContext);
    });

    QSettings windowSettings;
    const QByteArray savedWindowState = windowSettings.value("mainwindow/state").toByteArray();
    if (!savedWindowState.isEmpty()) mw->restoreState(savedWindowState);
    workbench->restorePresentationState();
    DiagnosticLogger::log("UI", "saved QMainWindow state restore complete");
    connect(QApplication::instance(), &QCoreApplication::aboutToQuit,
            mw, [this, mw]() {
                QSettings settings;
                settings.setValue("mainwindow/state", mw->saveState());
                if (workbench) workbench->savePresentationState();
            });

    // Connect some signals
    connect( w.TabW, SIGNAL( currentChanged(int) ),
             this, SLOT( TabSelected() ) );
    connect(mibLibrary, &MibLibraryWidget::workspaceModeChanged, this, &Snmpb::TabSelected);
    credentialsDialog = new CredentialsDialog(communityCredentialService, upm->Credentials(), mw);
    credentialsDialog->setProtectionHandler([this]() {
        CredentialProtectionDialog dialog(credentialProtection, communityCredentialService,
                                          upm->Credentials(), GetUsmUsersConfigFile(), credentialsDialog);
        dialog.exec();
    });
    connect( w.actionCredentials, &QAction::triggered, this, [this](bool) {
        credentialsDialog->execute(CredentialsDialog::Communities);
    });
    if (auto *bar = mw->findChild<ConnectionBar *>(QStringLiteral("ConnectionBar")))
        connect(bar, &ConnectionBar::manageCredentialsRequested, this, [this](int protocol) {
            credentialsDialog->execute(protocol == 2 ? CredentialsDialog::Users
                                                     : CredentialsDialog::Communities);
        });
    // Keep the window shortcut after removing the redundant Stop menu entry.
    mw->addAction(w.actionStop);
    w.actionVerifyMIB->setToolTip(tr("Verify the MIB open in MIBs > Editor"));
    w.mibMenu->setToolTipsVisible(true);
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
                             tr("Catalog: Loading…"));
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
                     tr("Catalog ready — preparing Profile…"));

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
                                      QObject::tr("Profile: Preparing…"));
            QThreadPool::globalInstance()->start(
                [context, generation, profile, dependencySnapshot]() mutable {
                // The same planning as every later activation (see MibPlanForProfile).
                const MibEffectivePlan plan = MibPlanForProfile(profile, dependencySnapshot);
                QMetaObject::invokeMethod(context, [context, generation, profile, plan]() {
                    if (!context || !context->startup ||
                        !context->startup->accepts(generation)) return;
                    DiagnosticLogger::log("Startup", QObject::tr(
                        "Profile prepared id=%1 authority-error=%2 files=%3")
                        .arg(profile.id).arg(plan.authorityError.isEmpty() ? QStringLiteral("none")
                                                                         : plan.authorityError)
                        .arg(plan.runtimeFiles.size()));
                    context->startup->advance(generation, StartupState::EnvironmentBuilding,
                                              QObject::tr("Environment: Building…"));
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

void Snmpb::ShowEditorDocument()
{
    if (workbench) {
        workbench->openDocument(w.EditorTab);
    } else {
        w.TabW->setCurrentWidget(w.EditorTab);
    }
}

void Snmpb::Shutdown()
{
    if (startup) startup->cancel();
    if (modules) modules->StopBackgroundWork();
    DiagnosticLogger::log("Shutdown", "trap listener shutdown begin", false);
    if (agent) agent->Shutdown();
    DiagnosticLogger::log("Shutdown", "trap listener shutdown end", false);
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

QString Snmpb::GetLogConfigFile(void)
{
    QSettings settings;
    QDir cfgdir = QFileInfo(settings.fileName()).dir();
    return cfgdir.filePath(LOG_CONFIG_FILE);
}

void Snmpb::ManagePreferences(bool)
{
    prefs->Execute();
}

void Snmpb::SetEditorMenus(bool value)
{
    // New and Open stay available everywhere: they bring the MIB Editor tab forward.
    MainUI()->fileNewAction->setEnabled(true);
    MainUI()->fileOpenAction->setEnabled(true);
    MainUI()->fileSaveAction->setEnabled(value);
    MainUI()->fileSaveAsAction->setEnabled(value);
    MainUI()->actionVerifyMIB->setEnabled(value && (!editor || !editor->VerificationRunning()));
    MainUI()->actionExtractMIBfromRFC->setEnabled(true);
    MainUI()->actionGotoLine->setEnabled(value);
    MainUI()->actionFind->setEnabled(value);
    MainUI()->actionReplace->setEnabled(value);
    MainUI()->actionFindNext->setEnabled(value);
}

/*
 * Runs when the middle-area tab changes. The MIB tree is always on screen, so Find works on the
 * tree unless the MIB Editor is in front. The tree is not repopulated here: it is populated when a
 * profile's environment is ready, and repopulating would collapse what the user expanded.
 */
void Snmpb::TabSelected(void)
{
    QWidget *currentTab = w.TabW->currentWidget();
    const auto *mibTabs = mibLibrary->findChild<QTabWidget *>("MibLibraryTabs");
    const bool editing = currentTab == mibLibrary && mibTabs && mibTabs->currentWidget() == w.EditorTab;
    SetEditorMenus(editing);
    disconnect(MainUI()->actionFind, SIGNAL( triggered() ), 0, 0);
    disconnect(MainUI()->actionFindNext, SIGNAL( triggered() ), 0, 0);
    if (editing) {
        connect( MainUI()->actionFind, SIGNAL( triggered() ),
                editor, SLOT( Find() ) );
        connect( MainUI()->actionFindNext, SIGNAL( triggered() ),
                editor, SLOT( ExecuteFindNext() ) );
    } else {
        const bool query = workbench && !workbench->isFullPage();
        MainUI()->actionFind->setEnabled(query);
        MainUI()->actionFindNext->setEnabled(query);
        connect( MainUI()->actionFind, SIGNAL( triggered() ),
                w.MIBTree, SLOT( FindFromNode() ) );
        connect( MainUI()->actionFindNext, SIGNAL( triggered() ),
                w.MIBTree, SLOT( ExecuteFindNext() ) );
    }
    if (currentTab == mibLibrary) {
        QElapsedTimer activationTimer; activationTimer.start();
        mibLibrary->activate();
        DiagnosticLogger::log("UI", tr("MIBs workspace activation total-ms=%1")
            .arg(activationTimer.elapsed()));
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
    QMessageBox about(mainWindow);
    about.setWindowTitle(tr("About MIB Navigator"));
    about.setTextFormat(Qt::RichText);
    about.setText(ProductIdentity::aboutHtml(SNMPB_VERSION_STRING, dependencies));
    about.setStandardButtons(QMessageBox::Ok);
    about.exec();
}

QString Snmpb::GetCredentialIdentitiesConfigFile(void)
{
    QSettings settings;
    QDir cfgdir = QFileInfo(settings.fileName()).dir();
    return cfgdir.filePath(CREDENTIAL_IDENTITIES_CONFIG_FILE);
}

QString Snmpb::GetCredentialVaultFile(void)
{
    QSettings settings;
    QDir cfgdir = QFileInfo(settings.fileName()).dir();
    return cfgdir.filePath(CREDENTIAL_VAULT_FILE);
}

QString Snmpb::GetCommunityCredentialsConfigFile(void)
{
    QSettings settings;
    QDir cfgdir = QFileInfo(settings.fileName()).dir();
    return cfgdir.filePath(COMMUNITY_CREDENTIALS_CONFIG_FILE);
}


bool Snmpb::CopyTableAsCsv()
{
    if (!resultsPanel || !resultsPanel->isTableResult()) return false;
    return CopyResultsAsCsv();
}

bool Snmpb::CopyResultsAsCsv()
{
    if (!resultsPanel) return false;
    const auto records = resultsPanel->csvRecords();
    if (records.size() < 2) return false;
    QGuiApplication::clipboard()->setText(FormatCsv(records));
    return true;
}
