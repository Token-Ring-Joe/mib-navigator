#include "queryresultspanel.h"
#include "appearance.h"
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
#include <qapplication.h>
#include <qmainwindow.h>
#include <QTranslator>
#include <QLibraryInfo>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QTextStream>
#include <QTimer>
#include <QClipboard>
#include <QElapsedTimer>
#include <functional>
#include <QGuiApplication>
#include <QAction>
#include <QTabBar>
#include <QTabWidget>
#include <QSplitter>
#include <QTreeWidget>
#include <QMouseEvent>
#include <QContextMenuEvent>
#include <QKeyEvent>
#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QListWidget>
#include <QStackedWidget>
#include <QSpinBox>
#include <QScrollArea>
#include <QToolBar>
#include <QMenu>
#include <QHash>
#include "snmpb.h"
#include "mibeditor.h"
#include "mibview.h"
#include "mibmodelview.h"
#include "mibtreemodel.h"
#include "mibmodule.h"
#include "mibengine.h"
#include "mibselection.h"
#include <future>
#include <chrono>
#include "mibservice.h"
#include "preferences.h"
#include "snmpbapp.h"
#include "productidentity.h"
#include "diagnosticlogger.h"
#include "startupcoordinator.h"
#include "mibproblem.h"
#include <QCheckBox>
#include <QPlainTextEdit>
#include <QPixmap>
#include <QLabel>
#include <QTableWidget>
#include "mibenvironment.h"
#include "mibenvironmentregistry.h"
#include "miblibrarywidget.h"
#include "collectionsearchpanel.h"
#include <QComboBox>
#include <QToolButton>
#include "agent.h"
#include "trap.h"
#include "csvformat.h"
#include "mibprofile.h"
#include "requestlog.h"
#include "mibcollectionservice.h"
#include <QLineEdit>
#include "credentialsdialog.h"
#include "usmcredentialservice.h"
#include "watchservice.h"
#include <QTableWidget>
#include <QCryptographicHash>
#include "communitycredentialservice.h"
#include <QTextEdit>
#include <QEventLoop>
#include <QPointer>

QString file_to_open;

int main( int argc, char ** argv )
{
    bool launch_smoke_test = false;
    QString smoke_config_dir;
    QString smoke_mib_file;
    bool smoke_network = false;
    bool smoke_dependency_check = false;
    QString smoke_live_target;
    QString smoke_live_credential;
    QString smoke_live_walk;
    bool smoke_live_single = false;
    QString smoke_live_bigwalk;
    QString smoke_live_table;
    QString smoke_edit_from;
    QString smoke_edit_to;
    QString startup_close_state;
    for (int i = 1; i < argc; ++i)
    {
        const QString argument = QString::fromLocal8Bit(argv[i]);
        if (argument == "--launch-smoke-test" && i + 1 < argc)
        {
            launch_smoke_test = true;
            smoke_config_dir = QString::fromLocal8Bit(argv[++i]);
        }
        else if (argument == "--smoke-mib" && i + 1 < argc)
        {
            smoke_mib_file = QString::fromLocal8Bit(argv[++i]);
        }
        else if (argument == "--smoke-dependency-check")
        {
            smoke_dependency_check = true;
        }
        else if (argument == "--smoke-network")
        {
            smoke_network = true;   // real SNMP session; only ever pointed at a loopback test agent
        }
        else if (argument == "--smoke-live-target" && i + 1 < argc)
        {
            smoke_live_target = QString::fromLocal8Bit(argv[++i]);
        }
        else if (argument == "--smoke-live-walk" && i + 1 < argc)
        {
            smoke_live_walk = QString::fromLocal8Bit(argv[++i]);
        }
        else if (argument == "--smoke-live-bigwalk" && i + 1 < argc)
        {
            smoke_live_bigwalk = QString::fromLocal8Bit(argv[++i]);   // a large subtree: walk it, then walk and Stop
        }
        else if (argument == "--smoke-live-single")
        {
            smoke_live_single = true;   // also run GetNext, GetBulk and failing Gets and print what they show
        }
        else if (argument == "--smoke-live-table" && i + 1 < argc)
        {
            smoke_live_table = QString::fromLocal8Bit(argv[++i]);   // row (entry) OID of a table to query
        }
        else if (argument == "--smoke-live-credential" && i + 1 < argc)
        {
            smoke_live_credential = QString::fromLocal8Bit(argv[++i]);
        }
        else if (argument == "--smoke-edit-from" && i + 1 < argc)
        {
            smoke_edit_from = QString::fromLocal8Bit(argv[++i]);
        }
        else if (argument == "--smoke-edit-to" && i + 1 < argc)
        {
            smoke_edit_to = QString::fromLocal8Bit(argv[++i]);
        }
        else if (argument == "--startup-close-state" && i + 1 < argc)
        {
            startup_close_state = QString::fromLocal8Bit(argv[++i]);
        }
    }

    // The dead-port, Log and Detect checks run once, in the main live scenario; the Walk and
    // single-request scenarios skip them so the suite does not wait for the same timeouts again.
    const bool smoke_live_core = smoke_live_walk.isEmpty() && !smoke_live_single && smoke_live_bigwalk.isEmpty();

    if (launch_smoke_test)
    {
        QDir().mkpath(smoke_config_dir);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                           smoke_config_dir);
    }

    if (launch_smoke_test) QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    SnmpBApplication app( argc, argv );
    if (launch_smoke_test)
        app.setProperty("snmpb.isolatedStateRoot", QDir(smoke_config_dir).filePath("state"));
    QCoreApplication::setOrganizationDomain(ProductIdentity::LegacySettingsDomain);
    QCoreApplication::setApplicationName(ProductIdentity::LegacySettingsApplication);
    // Keep applicationName="SnmpB" for QSettings compatibility; only the
    // user-visible display identity is rebranded.
    app.setApplicationDisplayName(QString::fromLatin1(ProductIdentity::Name));
    DiagnosticLogger::initialize(QStringLiteral(SNMPB_VERSION_STRING),
                                 QCoreApplication::arguments());
    DiagnosticLogger::log("Startup", "QApplication creation complete");
    DiagnosticLogger::log("Startup", "application identity setup complete");

    Snmpb snmpb(launch_smoke_test && !smoke_network);
    // Preferences establishes the legacy INI format/root before any theme load.
    Appearance appearance;

    // Qt translations
    QTranslator l10n_qt;
    const bool qt_translation_loaded =
        l10n_qt.load("qt_" + QLocale::system().name(),
                     QLibraryInfo::location(QLibraryInfo::TranslationsPath));
    Q_UNUSED(qt_translation_loaded);
    app.installTranslator(&l10n_qt);

    // SnmpB translations
    QTranslator l10n_app;
    const bool app_translation_loaded =
        l10n_app.load(":/i18n/snmpb." + QLocale::system().name());
    Q_UNUSED(app_translation_loaded);
    app.installTranslator(&l10n_app);

    QElapsedTimer mainWindowTimer; mainWindowTimer.start();
    QMainWindow mw;
    DiagnosticLogger::installMainWindowLifecycle(&mw);
    DiagnosticLogger::log("Startup", "main window construction begin");
    const auto close_visible_dialogs = []() {
        const auto top_level_widgets = QApplication::topLevelWidgets();
        for (QWidget *widget : top_level_widgets)
        {
            if (QDialog *dialog = qobject_cast<QDialog *>(widget);
                dialog && dialog->isVisible())
                dialog->accept();
        }
    };
    QTimer startup_dialog_closer;
    if (launch_smoke_test)
    {
        QObject::connect(&startup_dialog_closer, &QTimer::timeout,
                         close_visible_dialogs);
        startup_dialog_closer.start(10);
    }
    snmpb.BindToGUI(&mw);
    appearance.attach(&mw);
    DiagnosticLogger::log("Startup", QStringLiteral("main window construction end elapsed_ms=%1").arg(mainWindowTimer.elapsed()));
    startup_dialog_closer.stop();
    mw.show();
    DiagnosticLogger::log("Startup", QStringLiteral("main window show request elapsed_ms=%1").arg(mainWindowTimer.elapsed()));
    if (!startup_close_state.isEmpty())
    {
        const auto requestedState = [startup_close_state]() {
            if (startup_close_state == "CatalogLoading") return StartupState::CatalogLoading;
            if (startup_close_state == "ProfilePreparing") return StartupState::ProfilePreparing;
            return StartupState::EnvironmentBuilding;
        }();
        QObject::connect(snmpb.StartupCoordinatorObj(), &StartupCoordinator::stateChanged,
                         &mw, [&mw, requestedState](StartupState state, const QString &) {
            if (state == requestedState) QTimer::singleShot(0, &mw, &QWidget::close);
        });
        QTimer::singleShot(45000, &app, [&app]() { app.exit(2); });
    }
    QTimer::singleShot(0, &snmpb, &Snmpb::StartDeferredInitialization);
    app.connect(&app, SIGNAL( lastWindowClosed() ), &app, SLOT( quit() ));
    QObject::connect(&app, &QGuiApplication::lastWindowClosed, [] {
        DiagnosticLogger::log("Shutdown", "QApplication lastWindowClosed", false);
    });
    QObject::connect(&app, &QCoreApplication::aboutToQuit, [&snmpb] {
        DiagnosticLogger::log("Shutdown", "QApplication aboutToQuit", false);
        snmpb.Shutdown();
    });

    // Load a file specified as argument in the Mib Editor
    if (!launch_smoke_test &&
        (!file_to_open.isEmpty() || QCoreApplication::arguments().count() > 1))
    {
        snmpb.MibEditorObj()->MibFileOpen(file_to_open.isEmpty()?QCoreApplication::arguments().at(1):file_to_open);
        snmpb.ShowEditorDocument();
    }

    if (launch_smoke_test && startup_close_state.isEmpty())
    {
        const auto runSmoke = [&]() {
            bool passed = true;
            QFile smoke_log(QDir(smoke_config_dir).filePath("launch-smoke.log"));
            if (!smoke_log.open(QIODevice::WriteOnly | QIODevice::Text))
            {
                app.exit(1);
                return;
            }
            QTextStream smoke_output(&smoke_log);
            const auto check = [&](bool condition, const char *description) {
                smoke_output << "launch-smoke: "
                             << (condition ? "PASS" : "FAIL") << ": "
                             << description << Qt::endl;
                passed = passed && condition;
            };

            // Record what the Environment build reported, so scripted tests can verify
            // that problems are tolerated and described.
            if (const MibEnvironmentPtr built = snmpb.MibModuleObj()->CurrentEnvironment()) {
                const MibProblemSummary summary = MibSummarizeProblems(built->problems());
                smoke_output << "launch-smoke: ENVIRONMENT status="
                             << (built->status() == MibEnvironmentStatus::Complete ? "Complete"
                                 : built->status() == MibEnvironmentStatus::Partial ? "Partial"
                                                                                    : "Unusable")
                             << " loaded=" << built->loadedCount() << " failed=" << built->failedCount()
                             << " errors=" << summary.errors << " warnings=" << summary.warnings
                             << " notes=" << summary.notes << Qt::endl;
                auto *problemTabs = mw.findChild<QTabWidget *>("WorkbenchBottomTabs");
                QWidget *problemPage = mw.findChild<QWidget *>("WorkbenchProblemsPage");
                const int problemIndex = problemTabs ? problemTabs->indexOf(problemPage) : -1;
                auto *workspaces = mw.findChild<QTabWidget *>("WorkbenchDocuments");
                check(problemIndex >= 0 && !problemTabs->isTabVisible(problemIndex),
                      "Problems is hidden outside the MIBs workspace even when the Environment has problems");
                if (auto *mibs = mw.findChild<QAction *>("ViewMibLibrary")) mibs->trigger();
                check(problemIndex >= 0 && problemTabs->isTabVisible(problemIndex) == summary.hasProblems(),
                      "MIBs shows Problems exactly when the Environment has errors or warnings");
                if (auto *results = mw.findChild<QAction *>("ViewResults")) results->trigger();
                check(workspaces && workspaces->currentIndex() == 0 && !problemTabs->isTabVisible(problemIndex),
                      "returning to Query hides MIB Problems without changing the report");
                if (problemIndex >= 0 && summary.hasProblems())
                    check(problemTabs->tabText(problemIndex) == QStringLiteral("Problems (%1)")
                              .arg(summary.errors + summary.warnings),
                          "Problems bottom tab counts the Environment's errors and warnings");
                // The Problems tab and the header badge must agree with the Environment's report.
                QTableWidget *problemTable = mw.findChild<QTableWidget *>("WorkbenchProblemTable");
                QCheckBox *showNotes = mw.findChild<QCheckBox *>("WorkbenchProblemShowNotes");
                QLabel *problemState = mw.findChild<QLabel *>("WorkbenchProblemState");
                QLabel *badge = mw.findChild<QLabel *>("CurrentEnvironmentStatus");
                check(problemTable && showNotes && problemState && badge,
                      "Problems tab and Environment badge exist");
                if (problemTable && showNotes && problemState && badge) {
                    const int expectedRows = summary.errors + summary.warnings;
                    check(problemTable->rowCount() == expectedRows,
                          "Problems tab lists errors and warnings and hides notes by default");
                    showNotes->setChecked(true);
                    check(problemTable->rowCount() == expectedRows + summary.notes,
                          "Show notes adds the informational rows");
                    showNotes->setChecked(false);
                    check(problemState->text().contains("loaded"),
                          "Problems tab summary states what was loaded");
                    check(summary.hasProblems() ? badge->text().contains("problem")
                          : built->loadedCount() == 0 ? badge->text().contains("Empty")
                                                      : badge->text().contains("Ready"),
                          "Environment badge reflects the problem report");
                    if (summary.hasProblems() && problemTable->rowCount() > 0) {
                        const QTableWidgetItem *first = problemTable->item(0, 0);
                        const QString firstSeverity = first ? first->text() : QString();
                        check(firstSeverity == MibProblemSeverityText(MibProblemSeverity::Error) ||
                              firstSeverity == MibProblemSeverityText(MibProblemSeverity::Warning),
                              "Problems tab lists the most serious items first");
                    }
                }
                for (const MibProblem &problem : built->problems())
                    smoke_output << "launch-smoke: PROBLEM severity=" << MibProblemSeverityText(problem.severity)
                                 << " category=\"" << problem.category << "\" module=" << problem.module
                                 << " file=" << QFileInfo(problem.file).fileName() << " line=" << problem.line
                                 << " why=\"" << problem.why << "\"" << Qt::endl;
            }
            if (smoke_dependency_check) {
                // Exercise the actual Library action with an unrelated module in its collection.
                // All files/settings here belong to the isolated smoke-test directory.
                auto *modules = snmpb.MibModuleObj();
                auto *button = mw.findChild<QPushButton *>("MibLibraryCheckDependencies");
                const auto before = modules->CurrentEnvironment();
                check(before && before->profileId() == "custom.test" && button,
                      "dependency-check: the selected profile and Library action are available");
                const auto parserNames = [] {
                    QStringList names;
                    for (const auto &module : MibService().moduleInventory()) names.append(module.name);
                    names.sort(); return names;
                };
                const auto initialNames = parserNames();
                check(!initialNames.contains("EXTRA-MIB"),
                      "dependency-check: the unselected Library module is initially outside the parser");
                // Instrumented memory-check builds can explicitly use a larger
                // display-latency budget. Production/normal validation retains
                // 1.5 s, and the worker-gate/500 ms action-return checks always apply.
                const int uiBudgetMsecs = qMax(1500,
                    qEnvironmentVariableIntValue("MIB_NAVIGATOR_SMOKE_UI_BUDGET_MS"));
                const QString concurrentPath = QDir(smoke_config_dir).filePath("MIB-Library/Acme/Other/EXTRA-MIB");
                const auto runCheck = [&](const QString &profileId, bool expectBuild,
                                          bool queueRefresh = false) {
                    QEventLoop wait; QTimer limit, pulse; limit.setSingleShot(true);
                    bool restored = false, cacheHit = true, completed = false;
                    int pulses = 0; qint64 longestGap = 0;
                    QElapsedTimer heartbeat; heartbeat.start();
                    QObject::connect(&pulse, &QTimer::timeout, &wait, [&] {
                        longestGap = qMax(longestGap, heartbeat.restart()); ++pulses;
                        if (!modules->DependencyCheckRunning()) {
                            completed = true;
                            if (!expectBuild || restored) wait.quit();
                        }
                    });
                    QObject::connect(&limit, &QTimer::timeout, &wait, &QEventLoop::quit);
                    QObject::connect(modules, &MibModule::profileRuntimeReady, &wait,
                        [&](const QString &id, const MibEffectivePlan &, MibEnvironmentPtr,
                            const QStringList &, bool cached, bool) {
                            if (id == profileId && (!expectBuild || !cached)) {
                                restored = true; cacheHit = cached;
                                if (completed) wait.quit();
                            }
                        });
                    // A queued engine gate is released by a UI timer. A blocking
                    // action cannot pass this assertion, even on a very fast CPU.
                    auto release = std::make_shared<std::promise<void>>();
                    auto released = release->get_future().share();
                    MibEngine::instance().submit([released] { released.wait_for(std::chrono::seconds(5)); });
                    bool uiRan = false, browsed = false;
                    QTimer::singleShot(50, &wait, [&] {
                        uiRan = true;
                        auto *tabs = mw.findChild<QTabWidget *>("WorkbenchDocuments");
                        if (tabs) { tabs->setCurrentIndex(0); tabs->setCurrentIndex(1); }
                        modules->ModuleMetadata("GOOD-MIB");
                        if (modules->CurrentEnvironment() && modules->CurrentEnvironment()->profileId() != profileId)
                            snmpb.MibLibraryObj()->selectProfile(profileId);
                        if (queueRefresh) {
                            QString queuedError;
                            modules->RefreshDependencyIndex(&queuedError);
                            check(queuedError.contains("queued"),
                                  "dependency-check: a concurrent refresh is deferred without blocking browsing");
                        }
                        browsed = modules->DependencyCheckRunning() && button && !button->isEnabled();
                        release->set_value();
                    });
                    QElapsedTimer returned; returned.start();
                    if (button) button->click();
                    const qint64 clickMsecs = returned.elapsed();
                    pulse.start(25); limit.start(60000); wait.exec();
                    check(clickMsecs < 500 && uiRan && browsed && pulses > 0 && longestGap < uiBudgetMsecs,
                          "dependency-check: action returns promptly and browsing stays responsive while the worker is busy");
                    check(completed && button && button->isEnabled(),
                          "dependency-check: asynchronous completion restores the action controls");
                    smoke_output << "launch-smoke: DEPENDENCY-RESPONSIVENESS click-ms=" << clickMsecs
                                 << " longest-ui-gap-ms=" << longestGap << " ui-budget-ms=" << uiBudgetMsecs
                                 << " pulses=" << pulses << Qt::endl;
                    check(expectBuild ? restored && !cacheHit : !restored || cacheHit,
                        expectBuild ? "dependency-check: a fresh build restores the requested profile"
                                    : "dependency-check: a cached recheck leaves the profile untouched");
                };
                const QString extraPath = QDir(smoke_config_dir).filePath("MIB-Library/Acme/Other/EXTRA-MIB");
                check(!modules->DependencyIndex()->moduleNames().contains("EXTRA-MIB") &&
                      QDir().mkpath(QFileInfo(extraPath).absolutePath()) &&
                      QFile::copy(QDir(smoke_config_dir).filePath("extra-fixture.mib"), extraPath),
                      "dependency-check: a new module is added after startup without a manual refresh");
                runCheck("custom.test", true);
                auto report = modules->DependencyIndex()->profileCheck("mib-library");
                check(report.effectiveModules.contains("EXTRA-MIB") && !report.unresolved.contains("EXTRA-MIB") &&
                      report.effectiveModules.contains("GOOD-MIB") && !report.unresolved.contains("GOOD-MIB"),
                      "dependency-check: the unrelated module and equivalent duplicate providers were checked");
                check(parserNames() == initialNames && modules->CurrentEnvironment() &&
                      modules->CurrentEnvironment()->profileId() == "custom.test",
                      "dependency-check: the parser and selected environment return to the original profile");
                runCheck("custom.test", false);
                check(parserNames() == initialNames,
                      "dependency-check: a repeated check does not contaminate the parser");

                QFile extra(extraPath);
                QByteArray content;
                if (extra.open(QIODevice::ReadOnly)) { content = extra.readAll(); extra.close(); }
                const QByteArray originalContent = content;
                content.replace("SYNTAX Integer32", "SYNTAX");
                check(!content.isEmpty() && extra.open(QIODevice::WriteOnly) && extra.write(content) == content.size(),
                      "dependency-check: the isolated fixture is edited to contain a syntax error");
                extra.close();
                runCheck("custom.test", true);
                report = modules->DependencyIndex()->profileCheck("mib-library");
                check(report.unresolved.contains("EXTRA-MIB"),
                      "dependency-check: changed MIB bytes are checked rather than trusting an already-loaded identity");
                check(parserNames() == initialNames,
                      "dependency-check: failed semantic validation also restores the selected parser");

                auto *profiles = snmpb.MibLibraryObj()->profileService();
                QString error;
                const QString pending = profiles->create("Pending selection", &error);
                snmpb.MibLibraryObj()->reloadProfiles();
                emit snmpb.MibLibraryObj()->profilesChanged();
                // Change profiles from the UI timer while verification is busy.
                content = originalContent;
                if (extra.open(QIODevice::WriteOnly)) { extra.write(content); extra.close(); }
                runCheck(pending, true);
                check(!pending.isEmpty() && modules->CurrentEnvironment() &&
                      modules->CurrentEnvironment()->profileId() == pending && !parserNames().contains("EXTRA-MIB"),
                      "dependency-check: restoration respects a profile selection whose build was pending");
                // Inject an index commit failure after a fresh semantic pass.
                // Do not touch user data: this index is in the isolated test state.
                const QString indexPath = modules->DependencyIndex()->path();
                const QString savedIndex = indexPath + ".saved";
                modules->DependencyIndex()->setProfileCheck("mib-library", {});
                const bool blockedSave = QFile::rename(indexPath, savedIndex) && QDir().mkdir(indexPath);
                check(blockedSave, "dependency-check: the isolated index save failure is injected");
                if (blockedSave) {
                    runCheck(pending, true);
                    auto *status = mw.findChild<QLabel *>("MibLibraryStatus");
                    const auto signature = MibDependencyIndex::profileSignature(
                        modules->DependencyIndex()->moduleNames(), false);
                    check(status && !status->text().contains("complete", Qt::CaseInsensitive) &&
                          !modules->DependencyIndex()->profileCheckCurrent("mib-library", signature),
                          "dependency-check: a failed index save is reported and does not mark the check current");
                    check(modules->CurrentEnvironment() && modules->CurrentEnvironment()->profileId() == pending &&
                          !parserNames().contains("EXTRA-MIB"),
                          "dependency-check: index save failure also restores the selected parser");
                    check(QDir().rmdir(indexPath) && QFile::rename(savedIndex, indexPath),
                          "dependency-check: the isolated index file is restored");
                    runCheck(pending, true);
                    check(modules->DependencyIndex()->profileCheckCurrent("mib-library", signature) &&
                          QFileInfo(indexPath).isFile(),
                          "dependency-check: a retry rechecks and persists the result after the save failure");
                }
                check(QFile::remove(extraPath), "dependency-check: the unselected module is removed from the isolated Library");
                runCheck(pending, true, true);
                report = modules->DependencyIndex()->profileCheck("mib-library");
                check(!report.effectiveModules.contains("EXTRA-MIB") && !report.unresolved.contains("EXTRA-MIB") &&
                      !parserNames().contains("EXTRA-MIB"),
                      "dependency-check: removed files are excluded from the refreshed check without a stale missing-module error");
                modules->DependencyIndex()->setProfileCheck("mib-library", {});
                // Inject a file change through the worker's inventory-preparation
                // boundary, after verification and before the final scan. A timed
                // copy can race the initial scan on slower/instrumented machines.
                QEventLoop changedWait; QTimer changedLimit; changedLimit.setSingleShot(true);
                struct ChangedCheckState {
                    std::atomic<bool> injected{false}, copied{false};
                    bool finished = false, restored = false;
                    QString error;
                };
                auto changedState = std::make_shared<ChangedCheckState>();
                QPointer<QEventLoop> changedContext(&changedWait);
                QObject::connect(&changedLimit, &QTimer::timeout, &changedWait, &QEventLoop::quit);
                QObject::connect(modules, &MibModule::profileRuntimeReady, &changedWait,
                    [changedState, changedContext, pending](const QString &id,
                        const MibEffectivePlan &, MibEnvironmentPtr,
                        const QStringList &, bool cached, bool) {
                        if (id == pending && !cached) {
                            changedState->restored = true;
                            if (changedContext && changedState->finished) changedContext->quit();
                        }
                    });
                const QString changedFixture = QDir(smoke_config_dir).filePath("extra-fixture.mib");
                modules->CheckProfileDependencies("mib-library", {}, false,
                    [changedState, changedFixture, concurrentPath](const QList<MibModuleRecord> &) {
                        if (!changedState->injected.exchange(true))
                            changedState->copied = QFile::copy(changedFixture, concurrentPath);
                    }, [changedState, changedContext](const QString &error) {
                        changedState->error = error; changedState->finished = true;
                        if (changedContext && changedState->restored) changedContext->quit();
                    });
                changedLimit.start(60000); changedWait.exec();
                check(changedState->copied && changedState->finished && changedState->restored,
                      "dependency-check: a file is added after verification and the selected parser is restored");
                const auto changedSignature = MibDependencyIndex::profileSignature(
                    modules->DependencyIndex()->moduleNames(), false);
                check(changedState->error.contains("changed during checking") &&
                      !modules->DependencyIndex()->profileCheckCurrent("mib-library", changedSignature),
                      "dependency-check: a mid-check file change invalidates the report and requests a retry");
                runCheck(pending, true);
                check(modules->DependencyIndex()->profileCheckCurrent("mib-library", changedSignature) &&
                      modules->DependencyIndex()->profileCheck("mib-library").effectiveModules.contains("EXTRA-MIB"),
                      "dependency-check: retry verifies the file added during the preceding check");
                smoke_output << "launch-smoke: DEPENDENCY-CHECK completed" << Qt::endl;
                smoke_log.close(); app.exit(passed ? 0 : 1); return;
            }
            QSettings settings;
            check(QFileInfo(settings.fileName()).absoluteFilePath().startsWith(
                      QFileInfo(smoke_config_dir).absoluteFilePath()),
                  "QSettings uses the isolated directory");
            check(mw.isVisible(), "main window is visible");
            // Permanent workspaces, nested MIB tools, Query tree and a shared collapsible panel.
            check(!mw.findChild<QWidget *>("WorkbenchActivityRail") &&
                  !mw.findChild<QWidget *>("WorkbenchSidebar") &&
                  !mw.findChild<QAction *>("WorkbenchActivityMIBs"),
                  "the activity rail, its sidebar and its activities are gone");
            check(mw.findChild<QDockWidget *>("WorkbenchInspectorDock") &&
                  mw.findChild<QDockWidget *>("WorkbenchBottomDock"),
                  "workbench Inspector and bottom panels created");
            QTabWidget *workbenchDocuments = mw.findChild<QTabWidget *>("WorkbenchDocuments");
            QTabWidget *queryDocuments = mw.findChild<QTabWidget *>("WorkbenchQueryDocuments");
            QTabWidget *bottomTabs = mw.findChild<QTabWidget *>("WorkbenchBottomTabs");
            QSplitter *treeSplitter = mw.findChild<QSplitter *>("WorkbenchTreeSplitter");
            QWidget *resultsPage = mw.findChild<QWidget *>("WorkbenchResultsPage");
            QWidget *outputPage = mw.findChild<QWidget *>("WorkbenchOutputPage");
            check(treeSplitter && treeSplitter->count() == 2 &&
                  treeSplitter->widget(0)->isAncestorOf(snmpb.MainUI()->MIBTree) &&
                  treeSplitter->widget(0)->isAncestorOf(mw.findChild<QLineEdit *>("MibTreeFilter")) &&
                  treeSplitter->widget(1) == queryDocuments,
                  "the MIB tree and its filter are a permanent left pane beside the middle tabs");
            check(workbenchDocuments && outputPage && queryDocuments &&
                  !workbenchDocuments->tabBar()->isHidden() && !workbenchDocuments->tabsClosable() &&
                  workbenchDocuments->count() == 3 && workbenchDocuments->tabText(0) == "Query" &&
                  workbenchDocuments->tabText(1) == "MIBs" && workbenchDocuments->tabText(2) == "Traps" &&
                  queryDocuments->count() == 2 && queryDocuments->tabText(1) == "Watch" &&
                  queryDocuments->widget(0) == resultsPage && !queryDocuments->tabBar()->isHidden() &&
                  outputPage->isAncestorOf(snmpb.MainUI()->Query) &&
                  outputPage->isAncestorOf(mw.findChild<QPushButton *>("OutputClear")),
                  "permanent Query, MIBs and Traps tabs; Query contains Results and Watch");
            check(bottomTabs && resultsPage && bottomTabs->count() == 3 &&
                  bottomTabs->tabText(0) == "Output" && bottomTabs->widget(0) == outputPage &&
                  resultsPage->isAncestorOf(mw.findChild<QTableWidget *>("StructuredQueryResults")) &&
                  bottomTabs->tabText(1) == "Log" &&
                  workbenchDocuments->widget(2) == snmpb.MainUI()->TrapsTab,
                  "shared panel contains Output and Log, with Problems restricted to MIBs; Traps is a workspace");
            {
                // Problems belongs to MIBs; Library, Profiles and Editor are peers within MIBs.
                QTabWidget *libraryTabs = snmpb.MibLibraryObj()->findChild<QTabWidget *>("MibLibraryTabs");
                QWidget *problemsPage = mw.findChild<QWidget *>("WorkbenchProblemsPage");
                QLabel *problemHeading = mw.findChild<QLabel *>("WorkbenchProblemHeading");
                check(libraryTabs && problemsPage && libraryTabs->count() == 3 && libraryTabs->tabText(2) == "Editor" &&
                      libraryTabs->widget(2) == snmpb.MainUI()->EditorTab && bottomTabs &&
                      bottomTabs->widget(2) == problemsPage && libraryTabs->indexOf(problemsPage) < 0 &&
                      problemHeading && problemHeading->text().startsWith("Problems loading ") &&
                      problemHeading->text() != "Problems loading the active MIB profile",
                      "Problems is a bottom tab headed with the active profile's name");
            }
            // Focus shortcuts reacquire named controls without changing Qt Tab navigation.
            {
                mw.activateWindow(); QCoreApplication::processEvents();
                const auto focus = [&](const char *name, QWidget *expected) {
                    auto *action = mw.findChild<QAction *>(name);
                    check(action, "keyboard navigation action exists");
                    if (action) action->trigger();
                    QCoreApplication::processEvents();
                    check(QApplication::focusWidget() == expected, "keyboard navigation reaches the real content control");
                };
                focus("WorkbenchFocusDocument", mw.findChild<QTableWidget *>("StructuredQueryResults"));
                mw.findChild<QAction *>("ViewMibLibrary")->trigger();
                focus("WorkbenchFocusDocument", mw.findChild<QLineEdit *>("MibLibrarySearch"));
                mw.findChild<QAction *>("ViewMibProfiles")->trigger();
                focus("WorkbenchFocusDocument", mw.findChild<QComboBox *>("MibProfileEditorSelector"));
                mw.findChild<QAction *>("ViewMibEditor")->trigger();
                focus("WorkbenchFocusDocument", snmpb.MainUI()->MIBFile);
                mw.findChild<QAction *>("ViewTraps")->trigger();
                focus("WorkbenchFocusDocument", snmpb.MainUI()->TrapLog);
                mw.findChild<QAction *>("ViewWatch")->trigger();
                focus("WorkbenchFocusDocument", mw.findChild<QTableWidget *>("WatchTable"));
                focus("WorkbenchFocusInspector", snmpb.MainUI()->NodeInfo);
                mw.findChild<QAction *>("ViewBottomOutput")->trigger();
                focus("WorkbenchFocusBottom", snmpb.MainUI()->Query);
                mw.findChild<QAction *>("WorkbenchToggleBottom")->trigger();
                mw.findChild<QAction *>("ViewResults")->trigger();
                auto *target = mw.findChild<QComboBox *>("ConnectionTarget");
                focus("WorkbenchFocusTarget", target);
                focus("WorkbenchFocusOid", mw.findChild<QLineEdit *>("QueryObjectAddress"));
                QSet<QWidget *> reached;
                target->setFocus();
                for (int i = 0; i < 80; ++i) {
                    QWidget *current = QApplication::focusWidget();
                    if (!current) break;
                    reached.insert(current);
                    QKeyEvent tab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
                    QApplication::sendEvent(current, &tab);
                }
                for (const char *name : {"ConnectionTarget", "ConnectionCredential", "ConnectionManageCredentials",
                    "MibBrowserProfileSelector", "ConnectionDetect", "WorkbenchPanelToggle", "QueryObjectAddress",
                    "CollectionSearch", "MibTreeFilter", "MIBTree", "ResultsFilter", "ResultsDisplayMode",
                    "StructuredQueryResults", "NodeInfo"}) {
                    auto *control = mw.findChild<QWidget *>(name);
                    if (control && control->isVisible() && control->isEnabled() && (control->focusPolicy() & Qt::TabFocus))
                        check(reached.contains(control), qPrintable(QString("Tab reaches %1").arg(name)));
                }
                focus("WorkbenchFocusDocument", mw.findChild<QTableWidget *>("StructuredQueryResults"));
            }
            {
                auto *log = snmpb.findChild<RequestLog *>();
                auto *copy = mw.findChild<QPushButton *>("LogCopy");
                auto *clear = mw.findChild<QPushButton *>("RequestLogClear");
                check(log && copy && clear && !copy->isEnabled() && !clear->isEnabled(),
                      "empty request log disables Copy and Clear");
                if (log && copy && clear) {
                    const auto id = log->begin("UI validation", "synthetic", "SNMPv2c", "1.3.6.1");
                    check(copy->isEnabled() && clear->isEnabled(), "first log entry enables its actions");
                    log->finish(id, "Stopped", 0); clear->click();
                    check(log->entries().isEmpty() && !copy->isEnabled() && !clear->isEnabled(),
                          "clearing the Log restores its empty state");
                }
                auto *filter = mw.findChild<QLineEdit *>("MibLibrarySearch");
                auto *empty = mw.findChild<QLabel *>("MibLibraryEmptyState");
                filter->setText("no-module-matches-this-filter");
                check(!empty->isHidden() && empty->text().contains("filters"),
                      "filtered-empty Library gives filter guidance rather than an import instruction");
                filter->clear();
                check(empty->isHidden(), "clearing Library filters restores its populated view");
            }
            // Appearance is applied to the real workbench without rebuilding feature widgets.
            {
                const auto originalMode = appearance.mode();
                const QString savedTheme = QSettings().value("ui/theme", "system").toString();
                const auto expectedTheme = savedTheme == "dark" ? Appearance::Mode::Dark :
                    savedTheme == "light" ? Appearance::Mode::Light : Appearance::Mode::System;
                check(originalMode == expectedTheme && QSettings::defaultFormat() == QSettings::IniFormat,
                      "startup loads the saved theme from the same legacy INI settings store as Preferences");
                QWidget *workspace = workbenchDocuments->currentWidget();
                QWidget *queryPage = queryDocuments->currentWidget();
                const QString inspectorText = snmpb.MainUI()->NodeInfo->toPlainText();
                const bool panelHidden = mw.findChild<QDockWidget *>("WorkbenchBottomDock")->isHidden();
                check(!mw.findChild<QLabel *>("WorkbenchStatusEnvironment") && snmpb.MainUI()->NodeInfoL->isHidden(),
                      "Environment has one header indicator and Inspector has no repeated heading");
                auto *dark = mw.findChild<QAction *>("Appearancedark");
                auto *light = mw.findChild<QAction *>("Appearancelight");
                check(dark && light && mw.findChild<QAction *>("Appearancesystem"),
                      "Settings Appearance offers Follow System, Light and Dark");
                if (dark && light) {
                    dark->trigger(); QCoreApplication::processEvents();
                    check(qApp->palette().color(QPalette::Base).lightness() < 128 && dark->isChecked(),
                          "Dark theme applies to the live workbench");
                    mw.grab().save(QDir(smoke_config_dir).filePath("appearance-dark.png"));
                    light->trigger(); QCoreApplication::processEvents();
                    check(qApp->palette().color(QPalette::Base).lightness() > 128 && light->isChecked(),
                          "Light theme applies to the live workbench");
                    bool allControlsLight = true;
                    const auto background = [](const QWidget *widget, QPalette::ColorRole role) {
                        for (const QWidget *owner = widget; owner; owner = owner->parentWidget()) {
                            const QColor color = owner->palette().color(role);
                            if (color.alpha() != 0) return color;
                        }
                        return qApp->palette().color(role);
                    };
                    for (QWidget *control : mw.findChildren<QWidget *>()) {
                        bool matches = background(control, QPalette::Window).lightness() > 128;
                        if (qobject_cast<QLineEdit *>(control) || qobject_cast<QAbstractItemView *>(control) ||
                            qobject_cast<QTextEdit *>(control) || qobject_cast<QAbstractButton *>(control))
                            matches = matches && background(control, QPalette::Base).lightness() > 128 &&
                                control->palette().color(QPalette::Active, QPalette::Text) == qApp->palette().color(QPalette::Active, QPalette::Text);
                        if (!matches) smoke_output << "launch-smoke: THEME-CONTROL class=" << control->metaObject()->className()
                            << " name=" << control->objectName() << " visible=" << control->isVisibleTo(&mw)
                            << " window=" << control->palette().color(QPalette::Window).name()
                            << " base=" << control->palette().color(QPalette::Base).name()
                            << " text=" << control->palette().color(QPalette::Active, QPalette::Text).name() << Qt::endl;
                        allControlsLight = allControlsLight && matches;
                    }
                    check(allControlsLight, "switching to Light updates every existing input, view and button palette");
                    check(workbenchDocuments->currentWidget() == workspace && queryDocuments->currentWidget() == queryPage &&
                          mw.findChild<QDockWidget *>("WorkbenchBottomDock")->isHidden() == panelHidden &&
                          snmpb.MainUI()->NodeInfo->toPlainText() == inspectorText,
                          "theme switches preserve navigation, panel visibility and MIB inspector content");
                    mw.grab().save(QDir(smoke_config_dir).filePath("appearance-light.png"));
                }
                appearance.setMode(originalMode);
            }
            QWidget *queryToolbar = mw.findChild<QWidget *>("QueryToolBar");
            QWidget *header = mw.findChild<QWidget *>("CurrentDeviceContext");
            check(queryToolbar && header && !header->isAncestorOf(queryToolbar) &&
                  mw.findChild<QWidget *>("WorkbenchQueryWorkspace")->isAncestorOf(queryToolbar) &&
                  queryToolbar->isAncestorOf(mw.findChild<QLineEdit *>("QueryObjectAddress")) &&
                  queryToolbar->isAncestorOf(mw.findChild<QLineEdit *>("CollectionSearch")),
                  "OID navigation and collection search belong inside Query, below permanent workspace navigation");
            bool duplicateOperations = false;
            if (queryToolbar) for (auto *button : queryToolbar->findChildren<QToolButton *>())
                duplicateOperations |= !button->property("appearanceIcon").toString().isEmpty();
            check(queryToolbar && !duplicateOperations, "Query has no duplicate SNMP operation toolbar buttons");
            check(queryToolbar && queryToolbar->height() <= queryToolbar->fontMetrics().height()*4,
                  "Query navigation remains one compact row and reserves space for data");
            QWidget *searchPage = mw.findChild<QWidget *>("WorkbenchSearchPage");
            check(searchPage && !searchPage->isVisible() && workbenchDocuments && queryDocuments &&
                  queryDocuments->indexOf(searchPage) < 0,
                  "the Search page is not drawn anywhere until a search opens it");
            QToolBar *headerBar = mw.findChild<QToolBar *>("WorkbenchHeaderBar");
            check(headerBar && header && headerBar->isAncestorOf(header) && header->isVisible() &&
                  mw.corner(Qt::BottomRightCorner) == Qt::BottomDockWidgetArea,
                  "the header spans the window and the bottom panel spans the full width");
            {
                QAction *diagnostics = mw.findChild<QAction *>("HelpDiagnostics");
                QDialog *diagnosticsWindow = mw.findChild<QDialog *>("DiagnosticsWindow");
                check(diagnostics && snmpb.MainUI()->helpMenu->actions().contains(diagnostics) &&
                      diagnosticsWindow && diagnosticsWindow->isAncestorOf(snmpb.MainUI()->LogOutput) &&
                      !diagnosticsWindow->isVisible(),
                      "Help > Diagnostics holds the SNMP library log in its own window");
                if (diagnostics && diagnosticsWindow) {
                    diagnostics->trigger();
                    QCoreApplication::processEvents();
                    check(snmpb.MainUI()->LogTab->isVisibleTo(diagnosticsWindow) &&
                          snmpb.MainUI()->LogOutput->isVisibleTo(diagnosticsWindow) &&
                          snmpb.MainUI()->LogOutput->toPlainText().contains("Diagnostics window opened"),
                          "Help Diagnostics shows its actual controls and accumulated application log");
                    const bool logging = snmpb.MainUI()->LogEnable->isChecked();
                    snmpb.MainUI()->LogEnable->setChecked(false);
                    check(snmpb.MainUI()->LogOutput->isEnabled(),
                          "Diagnostics remains readable with SNMP library logging disabled");
                    snmpb.MainUI()->LogEnable->setChecked(logging);
                    diagnosticsWindow->close();
                }
            }
            {
                auto *ui = snmpb.MainUI();
                QStringList titles, mnemonics;
                bool uniqueMnemonics = true;
                for (QAction *action : ui->MenuBar->actions()) {
                    QString text = action->text();
                    const int marker = text.indexOf('&');
                    const QString key = marker >= 0 ? text.mid(marker + 1, 1).toLower() : QString();
                    uniqueMnemonics = uniqueMnemonics && !key.isEmpty() && !mnemonics.contains(key);
                    mnemonics << key;
                    titles << text.remove('&');
                }
                check(uniqueMnemonics, "top-level menus have distinct keyboard mnemonics");
                check(titles == QStringList{"File", "Edit", "View", "MIB", "SNMP", "Settings", "Help"},
                      "main menu order is File, Edit, View, MIB, SNMP, Settings, Help");
                check(ui->editMenu->actions().contains(ui->actionFind) &&
                      ui->editMenu->actions().contains(ui->actionReplace) &&
                      ui->editMenu->actions().contains(ui->actionGotoLine) &&
                      ui->mibMenu->actions().contains(ui->actionVerifyMIB) &&
                      ui->settingsMenu->actions().contains(ui->actionCredentials) &&
                      ui->snmpMenu->actions().contains(ui->actionMultipleVarbinds) &&
                      !ui->snmpMenu->actions().contains(ui->actionStop) &&
                      mw.actions().contains(ui->actionStop) && ui->actionStop->shortcut() == QKeySequence("Esc") &&
                      ui->mibMenu->actions().contains(ui->actionExtractMIBfromRFC) &&
                      ui->settingsMenu->actions().contains(ui->actionPreferences),
                      "editing, MIB, SNMP and Settings commands belong to their intended menus");
                check(ui->actionFind->shortcut() == QKeySequence("Ctrl+F") &&
                      ui->actionReplace->shortcut() == QKeySequence("Ctrl+H") &&
                      ui->actionVerifyMIB->shortcut() == QKeySequence("Ctrl+R") &&
                      ui->actionMultipleVarbinds->shortcut() == QKeySequence("Ctrl+M"),
                      "menu reorganization preserves existing command shortcuts");
            }
            {
                auto *appearanceMenu = mw.findChild<QMenu *>("AppearanceMenu");
                check(appearanceMenu && snmpb.MainUI()->settingsMenu->actions().contains(appearanceMenu->menuAction()) &&
                      !mw.findChild<QMenu *>("WorkbenchViewMenu")->actions().contains(appearanceMenu->menuAction()),
                      "Appearance belongs to Settings and keeps its existing actions");
                if (!smoke_mib_file.isEmpty() && smoke_live_target.isEmpty()) {
                    QTimer::singleShot(0, &mw, [&]() {
                        auto *about = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
                        check(about && about->text().contains("Build (UTC):") && about->text().contains("Source revision:"),
                              "Help About displays this executable's build timestamp and source revision");
                        if (about) about->accept();
                    });
                    snmpb.MainUI()->helpAboutAction->trigger();
                    for (int workspace = 0; workspace < 3; ++workspace) {
                        workbenchDocuments->setCurrentIndex(workspace);
                        check(snmpb.MainUI()->actionExtractMIBfromRFC->isEnabled(),
                              "Extract MIB from RFC is available from every workspace");
                        QTimer::singleShot(0, &mw, [&]() {
                            auto *chooser = qobject_cast<QFileDialog *>(QApplication::activeModalWidget());
                            check(chooser != nullptr, "RFC extraction opens its file chooser from the MIB menu");
                            if (chooser) chooser->reject();
                        });
                        snmpb.MainUI()->actionExtractMIBfromRFC->trigger();
                        check(workbenchDocuments->currentWidget() == snmpb.MibLibraryObj() &&
                              snmpb.MibLibraryObj()->findChild<QTabWidget *>("MibLibraryTabs")->currentWidget() == snmpb.MainUI()->EditorTab &&
                              snmpb.MainUI()->actionVerifyMIB->isEnabled(),
                              "RFC extraction activates Editor, where Verify Current MIB is available");
                    }
                    mw.findChild<QAction *>("ViewResults")->trigger();
                    check(!snmpb.MainUI()->actionVerifyMIB->isEnabled() &&
                          snmpb.MainUI()->mibMenu->toolTipsVisible() &&
                          snmpb.MainUI()->actionVerifyMIB->toolTip().contains("MIBs > Editor"),
                          "Verify is Editor-specific and explains where to use it");
                }
            }
            // Workspace navigation preserves the shared panel and permanent top-level tabs.
            if (workbenchDocuments) {
                QAction *viewLibrary = mw.findChild<QAction *>("ViewMibLibrary");
                QAction *viewOutput = mw.findChild<QAction *>("ViewResults");
                check(viewLibrary && viewOutput && mw.findChild<QAction *>("ViewBottomOutput") &&
                      mw.findChild<QMenu *>("WorkbenchViewMenu"),
                      "the View menu offers Results, Output and the MIB Library");
                if (viewLibrary && viewOutput) {
                    QDockWidget *inspectorDock = mw.findChild<QDockWidget *>("WorkbenchInspectorDock");
                    QDockWidget *bottomDock = mw.findChild<QDockWidget *>("WorkbenchBottomDock");
                    const bool panelWasHidden = bottomDock && bottomDock->isHidden();
                    const int navigationTop = workbenchDocuments->mapTo(&mw, QPoint()).y();
                    const int connectionHeight = header->height();
                    viewLibrary->trigger();
                    QCoreApplication::processEvents();
                    check(workbenchDocuments->mapTo(&mw, QPoint()).y() == navigationTop && header->height() == connectionHeight,
                          "workspace navigation and connection bar keep the same height in Query and MIBs");
                    mw.grab().save(QDir(smoke_config_dir).filePath("mibs-workspace.png"));
                    QWidget *queryRow = mw.findChild<QWidget *>("QueryToolBar");
                    check(workbenchDocuments->count() == 3 &&
                          workbenchDocuments->currentWidget() == snmpb.MibLibraryObj() &&
                          workbenchDocuments->tabText(1) == "MIBs",
                          "View > MIB Library selects the permanent MIBs workspace");
                    check(!snmpb.MainUI()->MIBTree->isVisibleTo(&mw) && inspectorDock && inspectorDock->isHidden() &&
                          bottomDock && bottomDock->isHidden() == panelWasHidden && queryRow && !queryRow->isVisibleTo(&mw) &&
                          mw.findChild<QWidget *>("ConnectionBar")->isVisibleTo(&mw),
                          "MIBs fills the workspace; Query tools hide and shared panel state is preserved");
                    viewLibrary->trigger();
                    check(workbenchDocuments->count() == 3, "workspace navigation does not duplicate tabs");
                    viewOutput->trigger();
                    QCoreApplication::processEvents();
                    check(workbenchDocuments->currentIndex() == 0 &&
                          snmpb.MainUI()->MIBTree->isVisibleTo(&mw) && inspectorDock && !inspectorDock->isHidden() &&
                          bottomDock && bottomDock->isHidden() == panelWasHidden && queryRow && !!queryRow->isVisibleTo(&mw),
                          "returning to Query restores tree and Inspector and preserves panel state");
                    // The environment badge opens MIB Problems from Query.
                    viewOutput->trigger();
                    if (QLabel *badge = mw.findChild<QLabel *>("CurrentEnvironmentStatus")) {
                        QMouseEvent press(QEvent::MouseButtonPress, QPointF(4, 4), badge->mapToGlobal(QPointF(4, 4)),
                                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(4, 4), badge->mapToGlobal(QPointF(4, 4)),
                                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
                        QCoreApplication::sendEvent(badge, &press);
                        QCoreApplication::sendEvent(badge, &release);
                        check(workbenchDocuments->currentWidget() == snmpb.MibLibraryObj() && bottomTabs &&
                              bottomTabs->currentWidget() == mw.findChild<QWidget *>("WorkbenchProblemsPage") &&
                              bottomTabs->isTabVisible(bottomTabs->currentIndex()) && !bottomDock->isHidden(),
                              "clicking the environment badge from Query opens MIBs and its Problems");
                        viewOutput->trigger();
                        check(!bottomTabs->isTabVisible(bottomTabs->indexOf(mw.findChild<QWidget *>("WorkbenchProblemsPage"))),
                              "leaving MIBs hides Problems while keeping the shared panel open");
                        if (QAction *toggle = mw.findChild<QAction *>("WorkbenchToggleBottom")) toggle->trigger();
                    }
                }
            }
            // Every keyboard shortcut does one thing: Qt runs neither action when two share one.
            {
                QHash<QString, QStringList> byShortcut;
                for (QAction *action : mw.findChildren<QAction *>())
                    for (const QKeySequence &key : action->shortcuts())
                        if (!key.isEmpty()) byShortcut[key.toString()] << (action->objectName().isEmpty() ? action->text() : action->objectName());
                QStringList clashes;
                for (auto it = byShortcut.cbegin(); it != byShortcut.cend(); ++it)
                    if (it.value().size() > 1) clashes << it.key() + ": " + it.value().join(", ");
                for (const QString &clash : clashes) smoke_output << "launch-smoke: SHORTCUT-CLASH " << clash << Qt::endl;
                check(clashes.isEmpty(), "no two actions share a keyboard shortcut");
            }
            check(snmpb.MibModuleObj()->CurrentEnvironment() &&
                  !snmpb.MibModuleObj()->CurrentEnvironment()->modules().isEmpty(),
                  "Profile Environment materialized bundled MIB modules");
            check(snmpb.MainUI()->MIBTree->model()->rowCount() > 0,
                  "MIB tree populated");

            // Select an object the active profile really holds: system when MIB-II is loaded,
            // otherwise the deepest node on the tree's first branch (a scenario profile may hold
            // only its own MIBs). The tree must land on exactly that object; a row that merely
            // happens to be current (for example after the tree takes the keyboard) does not count.
            {
                MibModelView *tree = snmpb.MainUI()->MIBTree;
                QString wantedOid = QStringLiteral("1.3.6.1.2.1.1");
                tree->SelectFromOid(wantedOid);
                if (tree->currentIndex().data(MibTreeModel::OidRole).toString() != wantedOid) {
                    QModelIndex branch = tree->model()->index(0, 0);
                    while (branch.isValid() && tree->model()->rowCount(branch) > 0)
                        branch = tree->model()->index(0, 0, branch);
                    wantedOid = branch.data(MibTreeModel::OidRole).toString();
                    tree->SelectFromOid(wantedOid);
                }
                smoke_output << "launch-smoke: TREE-SELECT " << wantedOid << Qt::endl;
                check(!wantedOid.isEmpty() &&
                      tree->currentIndex().data(MibTreeModel::OidRole).toString() == wantedOid,
                      "MIB OID selection works");
            }
            check(!snmpb.MainUI()->NodeInfo->toPlainText().isEmpty(),
                  "MIB inspector follows Tree selection");

            {
                auto *tree = snmpb.MainUI()->MIBTree;
                auto *copy = mw.findChild<QToolButton *>("InspectorCopyOid");
                auto *address = mw.findChild<QLineEdit *>("QueryObjectAddress");
                const QString selected = tree->currentObjectOid();
                if (!selected.isEmpty() && copy && address) {
                    address->setText("1.3.6.1.4.1.999999"); copy->click();
                    check(QApplication::clipboard()->text() == selected,
                          "Inspector copies its selected OID independently of unsubmitted navigation text");
                    tree->setCurrentIndex({});
                    check(!copy->isEnabled() && address->text().isEmpty() && snmpb.MainUI()->NodeInfo->toPlainText().isEmpty(),
                          "clearing Query selection clears OID navigation and disables Inspector Copy");
                    tree->SelectFromOid(selected);
                }
                auto *iana = mw.findChild<QDialog *>("IanaMibDialog");
                auto *openIana = mw.findChild<QPushButton *>("MibLibraryGetIana");
                check(iana && openIana && !iana->isVisible(), "IANA downloads are in a separate initially closed window");
                if (iana && openIana) {
                    openIana->click(); QCoreApplication::processEvents();
                    check(iana->isVisible() && iana->isAncestorOf(mw.findChild<QPushButton *>("MibLibraryDownload")),
                          "Get MIBs from IANA opens the catalog and its download controls together");
                    auto *catalog = iana->findChild<QTableWidget *>("IanaCatalogTable");
                    check(catalog && (catalog->rowCount() || iana->findChild<QLabel *>("IanaStatus")->text().contains("Refresh Catalog")),
                          "an empty IANA catalog explains how to retrieve its module list");
                    iana->grab().save(QDir(smoke_config_dir).filePath("iana-window.png"));
                    iana->close();
                }
            }

            if (!smoke_mib_file.isEmpty())
            {
                snmpb.ShowEditorDocument();
                auto *openButton = mw.findChild<QToolButton *>("EditorOpenMib");
                auto *verifyButton = mw.findChild<QToolButton *>("EditorVerifyMib");
                check(openButton && verifyButton && openButton->isVisibleTo(&mw),
                      "Editor exposes visible Open MIB and Verify buttons");
                QTimer::singleShot(0, &mw, [&]() {
                    auto *chooser = qobject_cast<QFileDialog *>(QApplication::activeModalWidget());
                    check(chooser != nullptr, "Editor Open MIB button opens the file chooser");
                    if (chooser) {
                        chooser->selectFile(QFileInfo(smoke_mib_file).absoluteFilePath());
                        QMetaObject::invokeMethod(chooser, "accept", Qt::DirectConnection);
                    }
                });
                if (openButton) openButton->click();
                // Reproduce closing the OID chooser before Verify restores the profile.
                {
                    MibSelection chooser(&snmpb, &mw, "Choose OID", MIBSELECTION_NONE);
                    QTimer::singleShot(0, &mw, [&]() {
                        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                        check(dialog != nullptr, "OID chooser opens before Verify");
                        auto *tree = dialog ? dialog->findChild<MibModelView *>("OidChooserTree") : nullptr;
                        auto *proxy = tree ? qobject_cast<QSortFilterProxyModel *>(tree->model()) : nullptr;
                        const auto chooserEnvironment = chooser.GetEnvironment();
                        const bool scalarAvailable = chooserEnvironment &&
                            chooserEnvironment->nodeByOid("1.3.6.1.2.1.1.1");
                        check(tree && proxy && proxy->sourceModel() != snmpb.MibLoaderObj()->TreeModel() &&
                              chooser.GetSemanticOid() == (scalarAvailable ? QStringLiteral("1.3.6.1.2.1.1.1") : QString()) &&
                              chooser.GetOid() == "1.3.6.1.2.1.1.1.0",
                              "OID chooser uses its own model snapshot with scalar instances or numeric fallback");
                        if (dialog) dialog->reject();
                    });
                    check(!chooser.run("1.3.6.1.2.1.1.1.0"), "OID chooser cancels before Verify");
                }
                auto *editor = snmpb.MibEditorObj();
                auto *modules = snmpb.MibModuleObj();
                const auto environmentBeforeVerify = modules->CurrentEnvironment();
                const QString originalProfile = environmentBeforeVerify ? environmentBeforeVerify->profileId() : QString();
                const QString originalContent = snmpb.MainUI()->MIBFile->toPlainText();
                QString profileError;
                const QString pendingProfile = snmpb.MibLibraryObj()->profileService()->create(
                    "Verify pending selection", &profileError);
                check(!pendingProfile.isEmpty(), "Verify profile-switch fixture is created");
                snmpb.MibLibraryObj()->reloadProfiles();
                const auto runVerification = [&](bool changeDocument, const QString &profile) {
                    QEventLoop wait;
                    QTimer limit; limit.setSingleShot(true);
                    bool completed = false, current = false, restored = false, uiRan = false;
                    int completions = 0;
                    QObject::connect(&limit, &QTimer::timeout, &wait, &QEventLoop::quit);
                    QObject::connect(editor, &MibEditor::VerificationFinished, &wait, [&](bool matches) {
                        completed = true; current = matches; ++completions;
                        if (restored) wait.quit();
                    });
                    QObject::connect(modules, &MibModule::profileRuntimeReady, &wait,
                        [&](const QString &id, const MibEffectivePlan &, MibEnvironmentPtr,
                            const QStringList &, bool cached, bool) {
                            if (completed && id == profile && !cached) { restored = true; wait.quit(); }
                        });
                    auto release = std::make_shared<std::promise<void>>();
                    const auto released = release->get_future().share();
                    MibEngine::instance().submit([released] { released.wait_for(std::chrono::seconds(5)); });
                    QTimer::singleShot(50, &wait, [&] {
                        uiRan = editor->VerificationRunning() && modules->EditorVerificationRunning();
                        auto *tabs = snmpb.MibLibraryObj()->findChild<QTabWidget *>("MibLibraryTabs");
                        if (tabs) { tabs->setCurrentIndex(0); tabs->setCurrentWidget(snmpb.MainUI()->EditorTab); }
                        check(!snmpb.MainUI()->actionVerifyMIB->isEnabled(),
                              "Verify remains disabled after switching subtabs during a check");
                        modules->ModuleMetadata("SNMPv2-MIB");
                        if (changeDocument) {
                            QString refreshError;
                            modules->RefreshDependencyIndex(&refreshError);
                            check(refreshError.contains("queued"), "Verify defers Library refresh without blocking the UI");
                            bool rejected = false;
                            modules->CheckProfileDependencies("mib-library", {}, false, {},
                                [&](const QString &error) { rejected = error.contains("already running"); });
                            check(rejected, "Library checking cannot overlap Editor verification");
                        }
                        check(!modules->VerifyEditorSource("ignored", {}, MibValidationLevel::Errors),
                              "Verify rejects a second operation while busy");
                        editor->VerifyMIB(); // A direct duplicate invocation must also be harmless.
                        if (changeDocument) snmpb.MainUI()->MIBFile->insertPlainText("-- edited while verifying\n");
                        snmpb.MibLibraryObj()->selectProfile(profile);
                        release->set_value();
                    });
                    const auto publishedBeforeClick = modules->CurrentEnvironment();
                    QElapsedTimer returned; returned.start();
                    if (verifyButton) verifyButton->click();
                    const auto clickMsecs = returned.elapsed();
                    check(modules->CurrentEnvironment() == publishedBeforeClick,
                          "Verify preserves the published Query environment while queued");
                    limit.start(60000); wait.exec();
                    check(clickMsecs < 500 && uiRan, "Verify returns promptly and the UI runs while its worker is gated");
                    check(completed && completions == 1 && current == !changeDocument && !editor->VerificationRunning(),
                          "Verify completes once and detects document changes");
                    check(restored && modules->CurrentEnvironment() && modules->CurrentEnvironment()->profileId() == profile,
                          "Verify restores the latest profile selected while checking");
                    smoke_output << "launch-smoke: VERIFY-RESPONSIVENESS click-ms=" << clickMsecs
                                 << " changed-document=" << changeDocument << Qt::endl;
                };
                runVerification(true, pendingProfile);
                check(editor->Diagnostics().isEmpty() && snmpb.MainUI()->MIBLogL->text().startsWith("Document changed"),
                      "Verify discards stale diagnostics and asks to check the changed document again");
                snmpb.MainUI()->MIBFile->setPlainText(originalContent);
                snmpb.MainUI()->MIBFile->document()->setModified(false);
                runVerification(false, originalProfile);
                check(snmpb.MibLibraryObj()->profileService()->remove(pendingProfile, &profileError),
                      "Verify temporary profile is removed");
                snmpb.MibLibraryObj()->reloadProfiles();
                if (originalContent.contains("SMOKE-EDITOR-ROOT-MIB")) {
                    bool missingImport = false;
                    for (const auto &diagnostic : editor->Diagnostics()) missingImport |= diagnostic.tag == "module-not-found";
                    check(!missingImport && environmentBeforeVerify &&
                          !environmentBeforeVerify->nodeByQualifiedName("SMOKE-EDITOR-DEPENDENCY-MIB::smokeEditorBranch"),
                          "Editor resolves filename-independent Library imports outside the active Query profile");
                }
                check(!snmpb.MainUI()->MIBFile->toPlainText().isEmpty(), "MIB editor opened and verified a local module");
                check(snmpb.MainUI()->MIBLogL->text().startsWith("Verification complete."), "MIB editor parsing completed");
                QTabWidget *documentTabs = mw.findChild<QTabWidget *>("WorkbenchDocuments");
                QCoreApplication::processEvents();
                check(documentTabs && documentTabs->currentWidget() == snmpb.MibLibraryObj() &&
                      snmpb.MibLibraryObj()->findChild<QTabWidget *>("MibLibraryTabs")->currentWidget() == snmpb.MainUI()->EditorTab &&
                      snmpb.MainUI()->actionVerifyMIB->isEnabled() &&
                      !snmpb.MainUI()->MIBTree->isVisibleTo(&mw),
                      "MIB editor opens inside MIBs with its menu actions");
                if (auto *mibTabs = snmpb.MibLibraryObj()->findChild<QTabWidget *>("MibLibraryTabs")) {
                    mibTabs->setCurrentIndex(0);
                    check(!snmpb.MainUI()->actionVerifyMIB->isEnabled(),
                          "Library subtab disables editor actions");
                    mibTabs->setCurrentWidget(snmpb.MainUI()->EditorTab);
                    check(snmpb.MainUI()->actionVerifyMIB->isEnabled(),
                          "Editor subtab restores editor actions without changing workspace");
                    mw.grab().save(QDir(smoke_config_dir).filePath("editor-workspace.png"));
                }
                if (QAction *viewOutput = mw.findChild<QAction *>("ViewResults")) viewOutput->trigger();
                check(documentTabs && documentTabs->currentWidget() != snmpb.MainUI()->EditorTab &&
                      !snmpb.MainUI()->actionVerifyMIB->isEnabled() &&
                      snmpb.MainUI()->fileOpenAction->isEnabled() && snmpb.MainUI()->actionFind->isEnabled(),
                      "away from the editor its actions are off, but Open MIB and Find (in the tree) stay available");
            }

            QDialog *preferencesDialog = mw.findChild<QDialog *>("Preferences");
            auto *preferenceSections = preferencesDialog ? preferencesDialog->findChild<QTreeWidget *>("PreferencesTree") : nullptr;
            check(preferenceSections && preferenceSections->topLevelItemCount() == 3 &&
                  preferenceSections->topLevelItem(0)->text(0) == "General" &&
                  preferenceSections->topLevelItem(1)->text(0) == "SNMP" &&
                  preferenceSections->topLevelItem(2)->text(0) == "Traps" &&
                  !mw.findChild<QWidget *>("MibLoadingEnable") &&
                  !mw.findChild<QWidget *>("ModulePaths"),
                  "Preferences has General, SNMP and Traps without duplicate MIB collection/path controls");
            settings.sync();
            const PreferencesSettings beforePreferences = PreferencesSettings::load(settings);
            const bool hadLegacyPaths = settings.contains("mibpaths/size");
            if (preferencesDialog && preferenceSections && smoke_live_target.isEmpty() && !smoke_mib_file.isEmpty()) {
                const auto savedBytes = [&]() {
                    settings.sync(); QFile file(settings.fileName());
                    if (!file.open(QIODevice::ReadOnly)) {
                        check(false, "isolated Preferences settings file is readable");
                        return QByteArray();
                    }
                    return file.readAll();
                };
                const QByteArray originalBytes = savedBytes();
                QTimer::singleShot(0, preferencesDialog, [&]() {
                    // Editor verification may have queued a runtime restore. Capture
                    // this baseline at dialog entry, before making pending edits.
                    const auto originalEnvironment = snmpb.MibModuleObj()->CurrentEnvironment();
                    check(preferencesDialog->isVisible(), "Settings Preferences opens the actual dialog");
                    preferencesDialog->resize(640, 480); QCoreApplication::processEvents();
                    auto *scroll = preferencesDialog->findChild<QScrollArea *>("PreferencesScroll");
                    auto *footer = preferencesDialog->findChild<QDialogButtonBox *>("OKCancelBox");
                    check(scroll && footer && preferencesDialog->width() <= 640 && preferencesDialog->height() <= 480 &&
                          preferencesDialog->rect().contains(footer->mapTo(preferencesDialog, footer->rect().bottomRight())),
                          "dialog-polish: compact Preferences keeps its footer outside scrollable sections");
                    if (scroll) {
                        preferenceSections->setCurrentItem(preferenceSections->topLevelItem(1));
                        auto *last = preferencesDialog->findChild<QSpinBox *>("BulkMaxRepetitions");
                        scroll->ensureWidgetVisible(last); last->setFocus(); QCoreApplication::processEvents();
                        check(scroll->viewport()->rect().intersects(QRect(last->mapTo(scroll->viewport(), QPoint()), last->size())),
                              "dialog-polish: the last SNMP setting remains reachable in compact Preferences");
                        preferencesDialog->grab().save(QDir(smoke_config_dir).filePath("dialog-preferences.png"));
                    }

                    preferencesDialog->findChild<QSpinBox *>("TrapPort")->setValue(30162);
                    preferencesDialog->findChild<QSpinBox *>("RequestTimeout")->setValue(27);
                    preferencesDialog->findChild<QSpinBox *>("TrapHistoryLimit")->setValue(77);
                    preferencesDialog->findChild<QCheckBox *>("ExpandTrapBinding")->setChecked(!beforePreferences.expandTrapBinding);
                    preferencesDialog->findChild<QCheckBox *>("ShowAgentName")->setChecked(!beforePreferences.showAgentName);
                    for (int i = 0; i < 3; ++i)
                        preferenceSections->setCurrentItem(preferenceSections->topLevelItem(i));
                    check(preferencesDialog->findChild<QSpinBox *>("RequestTimeout")->value() == 27,
                          "switching Preferences sections retains pending edits");
                    check(savedBytes() == originalBytes &&
                          snmpb.MibModuleObj()->CurrentEnvironment() == originalEnvironment &&
                          snmpb.PreferencesObj()->GetTrapPort4() == beforePreferences.trapPort4 &&
                          snmpb.PreferencesObj()->GetExpandTrapBinding() == beforePreferences.expandTrapBinding &&
                          snmpb.PreferencesObj()->GetShowAgentName() == beforePreferences.showAgentName &&
                          snmpb.TrapObj()->service()->history().maximumRecords() == beforePreferences.trapHistoryLimit,
                          "pending Preferences edits preserve settings, the environment and live behavior");
                    auto *ipv4 = preferencesDialog->findChild<QCheckBox *>("EnableIPv4");
                    auto *ipv6 = preferencesDialog->findChild<QCheckBox *>("EnableIPv6");
                    auto *buttons = preferencesDialog->findChild<QDialogButtonBox *>("OKCancelBox");
                    ipv4->setChecked(false); ipv6->setChecked(false);
                    check(!buttons->button(QDialogButtonBox::Ok)->isEnabled(),
                          "Preferences cannot accept with both transports disabled");
                    buttons->button(QDialogButtonBox::Cancel)->click();
                });
                snmpb.MainUI()->actionPreferences->trigger();
                check(savedBytes() == originalBytes &&
                      snmpb.PreferencesObj()->GetTrapPort4() == beforePreferences.trapPort4 &&
                      snmpb.PreferencesObj()->GetTrapHistoryLimit() == beforePreferences.trapHistoryLimit,
                      "Cancel restores every pending preference and preserves the settings file");
                QTimer::singleShot(0, preferencesDialog, [&]() {
                    check(preferencesDialog->findChild<QSpinBox *>("RequestTimeout")->value() == beforePreferences.requestTimeout,
                          "reopening Preferences starts with applied values after cancellation");
                    preferencesDialog->findChild<QSpinBox *>("TrapHistoryLimit")->setValue(123);
                    preferencesDialog->findChild<QDialogButtonBox *>("OKCancelBox")->button(QDialogButtonBox::Ok)->click();
                });
                snmpb.MainUI()->actionPreferences->trigger();
                check(snmpb.PreferencesObj()->GetTrapHistoryLimit() == 123 &&
                      snmpb.TrapObj()->service()->history().maximumRecords() == 123,
                      "OK commits the actual Preferences dialog and updates live trap retention");
            }
            QSpinBox *trapHistoryLimit = mw.findChild<QSpinBox *>("TrapHistoryLimit");
            const int previousTrapLimit = beforePreferences.trapHistoryLimit;
            check(trapHistoryLimit != nullptr, "trap history preference control exists");
            if (trapHistoryLimit) trapHistoryLimit->setValue(123);
            snmpb.PreferencesObj()->Save();
            check(snmpb.TrapObj()->service()->history().maximumRecords() == 123,
                  "saving trap history limit applies immediately to the running service");
            if (trapHistoryLimit) trapHistoryLimit->setValue(previousTrapLimit);
            snmpb.PreferencesObj()->Save();
            check(snmpb.TrapObj()->service()->history().maximumRecords() == previousTrapLimit,
                  "restoring trap history preference updates the service again");
            settings.sync();
            check(PreferencesSettings::load(settings).mibPaths == beforePreferences.mibPaths &&
                  PreferencesSettings::load(settings).automaticLoading == beforePreferences.automaticLoading,
                  "repeated Preferences saves neither duplicate paths nor rewrite retired loading settings");
            check(settings.contains("network/enableipv4") &&
                  settings.contains("mibpaths/size") == hadLegacyPaths,
                  "preferences saved to isolated configuration");

            QTranslator translation_probe;
            check(translation_probe.load(":/i18n/snmpb.uk_UA"),
                  "bundled application translation loads");
            if (beforePreferences.mibPaths.join(';').contains("Legacy-only-MIBs")) {
                QFile config(snmpb.GetSmiConfigFile());
                check(config.open(QIODevice::ReadOnly) && !config.readAll().contains("Legacy-only-MIBs"),
                      "startup libsmi paths exclude retained legacy folders outside the MIB Library");
            }
            check(QFileInfo::exists(snmpb.GetSmiConfigFile()),
                  "loaded-MIB state file initialized");
            check(QFileInfo::exists(snmpb.GetBootCounterConfigFile()),
                  "SNMPv3 boot counter initialized locally");

            // Pictures of the Problems tab as the application really draws it, so automated
            // validation can look at the layout instead of asking a person to send screenshots.
            if (QTableWidget *table = mw.findChild<QTableWidget *>("WorkbenchProblemTable")) {
                QCheckBox *notes = mw.findChild<QCheckBox *>("WorkbenchProblemShowNotes");
                QPlainTextEdit *detail = mw.findChild<QPlainTextEdit *>("WorkbenchProblemDetail");
                if (QAction *viewProblems = mw.findChild<QAction *>("ViewBottomProblems")) viewProblems->trigger();
                mw.resize(1500, 1000);
                QCoreApplication::processEvents();
                mw.grab().save(QDir(smoke_config_dir).filePath("problems-tab.png"));
                if (notes) notes->setChecked(true);
                QCoreApplication::processEvents();
                if (table->rowCount() > 0 && detail) {
                    // Keep the current cell fixed while changing selection: the
                    // detail pane must not depend on currentCellChanged firing.
                    table->setCurrentCell(0, 0, QItemSelectionModel::NoUpdate);
                    table->clearSelection();
                    table->selectRow(0);
                    // Selection signals update the detail synchronously. Do not
                    // yield here to an unrelated editor runtime/report restore.
                    check(detail->toPlainText().contains("Why:"),
                          "Selecting a Problems row fills the detail pane even when its current cell is unchanged");
                    table->clearSelection();
                    check(detail->toPlainText().isEmpty(),
                          "clearing Problems selection clears the detail pane");
                    table->selectRow(0);
                    check(detail->toPlainText().contains("Why:"),
                          "reselecting the same Problems row restores its explanation");
                }
                mw.grab().save(QDir(smoke_config_dir).filePath("problems-tab-notes.png"));
                if (QAction *viewOutput = mw.findChild<QAction *>("ViewResults")) viewOutput->trigger();
            }
            // The connection bar: Target, Credential and the (single) MIB Profile selector.
            {
                QWidget *bar = mw.findChild<QWidget *>("ConnectionBar");
                QComboBox *targetBox = mw.findChild<QComboBox *>("ConnectionTarget");
                QComboBox *credentialBox = mw.findChild<QComboBox *>("ConnectionCredential");
                QComboBox *profileBox = mw.findChild<QComboBox *>("MibBrowserProfileSelector");
                check(bar && targetBox && credentialBox && profileBox, "connection bar has Target, Credential and MIB Profile");
                check(bar && profileBox && bar->isAncestorOf(profileBox),
                      "the MIB Profile selector lives in the connection bar (there is only one)");
                check(mw.findChild<QToolButton *>("ConnectionManageCredentials") != nullptr,
                      "the connection bar offers Manage… for saved credentials");
                QAction *devicesActivity = mw.findChild<QAction *>("WorkbenchActivityDevices");
                check(!devicesActivity && !mw.findChild<QWidget *>("DevicesDock"),
                      "the retired Devices activity and dock no longer exist");
                if (bar && targetBox && credentialBox) {
                    const int before = credentialBox->count();
                    CommunityCredentialRecord community;
                    community.displayName = "Smoke lab";
                    community.readCommunity = CredentialSecret("smokero");
                    community.writeCommunity = CredentialSecret("smokerw");
                    const QString created = snmpb.CommunityCredentials()->create(community);
                    check(!created.isEmpty() && credentialBox->count() == before + 2,
                          "a newly saved community credential appears in the Credential list (v2c and v1)");
                    check(credentialBox->findText("Smoke lab - SNMPv2c") >= 0 &&
                          credentialBox->findText("Smoke lab - SNMPv1") >= 0,
                          "credential entries say which SNMP version they use");
                    credentialBox->setCurrentIndex(credentialBox->findText("Smoke lab - SNMPv1"));
                    targetBox->setEditText("127.0.0.1");
                    QCoreApplication::processEvents();
                    mw.resize(1500, 1000);
                    QCoreApplication::processEvents();
                    mw.grab().save(QDir(smoke_config_dir).filePath("connection-bar.png"));
                    smoke_output << "launch-smoke: CONNECTION target=" << targetBox->currentText()
                                 << " credential=\"" << credentialBox->currentText() << "\" profile=\""
                                 << (profileBox ? profileBox->currentText() : QString()) << "\"" << Qt::endl;
                }
            }
            // Query Table through the connection bar: reads the table, then reports what the
            // user would see (rows, Index column, status line) and the text "Copy as CSV" copies.
            if (!smoke_live_target.isEmpty() && !smoke_live_table.isEmpty()) {
                QComboBox *targetBox = mw.findChild<QComboBox *>("ConnectionTarget");
                QComboBox *credentialBox = mw.findChild<QComboBox *>("ConnectionCredential");
                QLabel *requestStatus = mw.findChild<QLabel *>("StructuredQueryStatus");
                check(targetBox && credentialBox && requestStatus, "live-table: bar and status line exist");
                if (targetBox && credentialBox && requestStatus) {
                    const int credentialIndex = credentialBox->findText(smoke_live_credential);
                    check(credentialIndex >= 0, "live-table: the requested credential is offered");
                    credentialBox->setCurrentIndex(credentialIndex);
                    targetBox->setEditText(smoke_live_target);
                    const bool loopbackTable = smoke_live_target.startsWith("127.0.0.1");
                    // The three-row goodTable has exact expectations; other loopback tables are
                    // checked by the scenario's text patterns and the Walk comparison.
                    for (const QString &tableOid : smoke_live_table.split(',', Qt::SkipEmptyParts)) {
                    smoke_output << "launch-smoke: LIVE-TABLE-START " << tableOid << Qt::endl;
                    QEventLoop wait; QTimer limit; limit.setSingleShot(true);
                    QObject::connect(&limit, &QTimer::timeout, &wait, &QEventLoop::quit);
                    QTimer poll; QObject::connect(&poll, &QTimer::timeout, &wait, [&]() {
                        if (!snmpb.MainUI()->actionStop->isEnabled()) wait.quit();
                    });
                    poll.start(100); limit.start(60000);
                    if (QAction *watch = mw.findChild<QAction *>("ViewWatch")) watch->trigger();
                    const bool panelWasHidden = mw.findChild<QDockWidget *>("WorkbenchBottomDock")->isHidden();
                    snmpb.AgentObj()->TableViewFrom(tableOid);
                    check(queryDocuments->currentWidget() == resultsPage &&
                          mw.findChild<QDockWidget *>("WorkbenchBottomDock")->isHidden() == panelWasHidden,
                          "live-table: a table query selects Results from Watch without changing panel visibility");
                    if (QAction *watch = mw.findChild<QAction *>("ViewWatch")) watch->trigger();
                    // Overlapping entry points must not replace the pending table presentation.
                    snmpb.AgentObj()->GetFrom("1.3.6.1.2.1.1.1.0", 0);
                    snmpb.AgentObj()->WalkFrom("1.3.6.1.2.1.1");
                    wait.exec();
                    check(queryDocuments->currentWidget() &&
                          queryDocuments->currentWidget()->objectName() == "WorkbenchWatchPage",
                          "live-table: background table completion preserves the selected Watch tab");
                    if (QAction *results = mw.findChild<QAction *>("ViewResults")) results->trigger();
                    const QList<QStringList> &records = snmpb.AgentObj()->LastTableRecords();
                    const QList<QStringList> fullTableRecords = records;
                    check(snmpb.Results()->isTableResult() && snmpb.Results()->table()->rowCount() == records.size()-1,
                          "live-table: Results displays one grid row per instance and ignores overlapping queries");
                    if (loopbackTable && tableOid == "1.3.6.1.4.1.90001.2.1") {
                        snmpb.Results()->resize(1200, 500);
                        snmpb.Results()->grab().save(QDir(smoke_config_dir).filePath("results-table-grid.png"));
                    }
                    for (const QStringList &record : records)
                        smoke_output << "launch-smoke: LIVE-TABLE-" << (&record == &records.first() ? "HEADER " : "ROW ")
                                     << record.join(',') << Qt::endl;
                    smoke_output << "launch-smoke: LIVE-TABLE-ROWS " << (records.isEmpty() ? 0 : records.size() - 1) << Qt::endl;
                    smoke_output << "launch-smoke: LIVE-TABLE-STATUS failed=" << requestStatus->property("failed").toBool()
                                 << " text=\"" << requestStatus->text() << "\"" << Qt::endl;
                    if (RequestLog *shownLog = snmpb.findChild<RequestLog *>()) {
                        const RequestLogEntry *tableEntry = nullptr;
                        for (const RequestLogEntry &e : shownLog->entries())
                            if (e.operation == "Table" && e.oid == tableOid) tableEntry = &e;
                        if (tableEntry)
                            smoke_output << "launch-smoke: LIVE-LOG op=Table target=" << tableEntry->target << " identity=\""
                                         << tableEntry->identity << "\" result=\"" << RequestOutcomeText(tableEntry->outcome)
                                         << "\" objects=" << tableEntry->objects << " detail=\"" << tableEntry->detail.left(120) << "\"" << Qt::endl;
                        check(tableEntry && (!loopbackTable || (tableEntry->outcome == RequestOutcome::Succeeded && tableEntry->objects == records.size() - 1)),
                              "live-table: the Log lists the table request with its outcome and row count");
                    }
                    if (loopbackTable && tableOid == "1.3.6.1.4.1.90001.2.1") check(records.size() == 4 && !requestStatus->property("failed").toBool(),
                          "live-table: the table finished with three rows and no error line");
                    if (loopbackTable && tableOid == "1.3.6.1.4.1.90001.2.1") check(requestStatus->text().startsWith("Table: 3 row(s)."),
                          "live-table: the status line summarises the table");
                    const QString csv = FormatCsv(records);
                    if (loopbackTable && tableOid == "1.3.6.1.4.1.90001.2.1") check(csv.startsWith("Instance,Index,goodCount,goodNote\r\n") && csv.count("\r\n") == 4 &&
                          csv.contains("\"goodName=\"\"eth1\"\"\""),
                          "live-table: Copy as CSV text matches the table");
                    if (loopbackTable && tableOid == "1.3.6.1.4.1.90001.2.1") {
                        // The two right-click actions put CSV on the clipboard.
                        QGuiApplication::clipboard()->clear();
                        const bool copiedTable = snmpb.CopyTableAsCsv();
                        const QString tableClip = QGuiApplication::clipboard()->text();
                        const bool copiedResults = snmpb.CopyResultsAsCsv();
                        const QString resultsClip = QGuiApplication::clipboard()->text();
                        check(copiedTable && tableClip == FormatCsv(snmpb.Results()->csvRecords()) &&
                              tableClip.startsWith("Index,goodCount,goodNote\r\n"),
                              "live-table: Copy table as CSV puts the table on the clipboard");
                        check(copiedResults && resultsClip == tableClip && resultsClip.count("\r\n") == 4,
                              "live-table: Copy results as CSV puts the Results table on the clipboard");
                    }
                    if (QTableWidget *results = mw.findChild<QTableWidget *>("StructuredQueryResults")) {
                        // Independent check: a plain Walk of the same subtree, which takes the
                        // other code path, must find exactly the objects the table shows.
                        const auto collect = [results]() {
                            QSet<QString> oids;
                            for (int row = 0; row < results->rowCount(); ++row)
                                for (int column = 0; column < results->columnCount(); ++column)
                                    if (const auto *item = results->item(row, column); item && !item->data(QueryResultsPanel::OidRole).toString().isEmpty())
                                        oids.insert(item->data(QueryResultsPanel::OidRole).toString());
                            return oids;
                        };
                        const QSet<QString> fromTable = collect();
                        QEventLoop walkWait; QTimer walkLimit; walkLimit.setSingleShot(true);
                        QObject::connect(&walkLimit, &QTimer::timeout, &walkWait, &QEventLoop::quit);
                        QElapsedTimer started; started.start();
                        QTimer walkPoll; QObject::connect(&walkPoll, &QTimer::timeout, &walkWait, [&]() {
                            if (started.elapsed() > 500 && !snmpb.MainUI()->actionStop->isEnabled()) walkWait.quit();
                        });
                        walkPoll.start(100); walkLimit.start(180000);
                        snmpb.AgentObj()->WalkFrom(tableOid);
                        walkWait.exec();
                        const QSet<QString> fromWalk = collect();
                        // Everything the Results table holds for the Walk (the time column varies), in
                        // arrival order, so a change to the Walk code can be compared object for object.
                        for (int row = 0; row < results->rowCount(); ++row) {
                            QStringList cells;
                            for (int column = 0; column < 7; ++column)
                                cells << (results->item(row, column) ? results->item(row, column)->text() : QString());
                            smoke_output << "launch-smoke: LIVE-WALK-OBJECT " << cells.join(" | ") << Qt::endl;
                        }
                        if (RequestLog *walkLog = snmpb.findChild<RequestLog *>())
                            for (const RequestLogEntry &e : walkLog->entries())
                                if (e.operation == "Walk" && e.oid == tableOid)
                                    smoke_output << "launch-smoke: LIVE-WALK-LOG result=\"" << RequestOutcomeText(e.outcome)
                                                 << "\" objects=" << e.objects << Qt::endl;
                        smoke_output << "launch-smoke: LIVE-WALK-STATUS text=\""
                                     << (requestStatus ? requestStatus->text() : QString()) << "\"" << Qt::endl;
                        smoke_output << "launch-smoke: LIVE-TABLE-COMPARE table=" << fromTable.size()
                                     << " walk=" << fromWalk.size() << Qt::endl;
                        QStringList onlyWalk = (fromWalk - fromTable).values(), onlyTable = (fromTable - fromWalk).values();
                        onlyWalk.sort(); onlyTable.sort();
                        for (int i = 0; i < qMin<qsizetype>(onlyWalk.size(), 10); ++i)
                            smoke_output << "launch-smoke: LIVE-TABLE-ONLY-IN-WALK " << onlyWalk.at(i) << Qt::endl;
                        for (int i = 0; i < qMin<qsizetype>(onlyTable.size(), 10); ++i)
                            smoke_output << "launch-smoke: LIVE-TABLE-ONLY-IN-TABLE " << onlyTable.at(i) << Qt::endl;
                        check((!loopbackTable || !fromTable.isEmpty()) && fromTable == fromWalk,
                              "live-table: the table holds exactly the objects a plain Walk of the same subtree returns");
                    }
                    // Exercise the real table-level context menu, asynchronous discovery,
                    // decoded picker, exact row retrieval, captured connection, and Cancel.
                    if (loopbackTable && fullTableRecords.size() > 1) {
                        for (int selectionPass = 0; selectionPass < 3; ++selectionPass) {
                            const bool cancelSelection = selectionPass == 2;
                            const QStringList wanted = fullTableRecords.at(selectionPass == 0 ? 1 : fullTableRecords.size()-1);
                            const auto beforeCancel = snmpb.AgentObj()->LastTableRecords();
                            bool picked = false;
                            QEventLoop rowWait; QTimer rowLimit; rowLimit.setSingleShot(true);
                            QObject::connect(&rowLimit, &QTimer::timeout, &rowWait, [&]() {
                                if (auto *d = qobject_cast<QDialog *>(QApplication::activeModalWidget())) d->reject();
                                if (auto *m = qobject_cast<QMenu *>(QApplication::activePopupWidget())) m->close();
                                snmpb.AgentObj()->Stop(); rowWait.quit();
                            });
                            QTimer rowPoll;
                            QObject::connect(&rowPoll, &QTimer::timeout, &rowWait, [&]() {
                                auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                                if (!picked && dialog && dialog->objectName() == "TableInstanceDialog") {
                                    picked = true;
                                    auto *list = dialog->findChild<QListWidget *>("TableInstanceList");
                                    check(list && list->count() == fullTableRecords.size()-1,
                                          "live-row: picker discovers all sparse instances without duplicates");
                                    if (list) for (int i = 0; i < list->count(); ++i)
                                        if (list->item(i)->data(Qt::UserRole).toString() == wanted.first()) {
                                            list->setCurrentRow(i);
                                            check(list->item(i)->text().contains(wanted.value(1)), "live-row: picker shows decoded indexes");
                                        }
                                    if (cancelSelection) dialog->reject();
                                    else {
                                        // The selected row must still use the original target and credential.
                                        targetBox->setEditText("127.0.0.1:1"); credentialBox->setCurrentIndex(-1);
                                        dialog->accept();
                                    }
                                } else if (picked && !snmpb.MainUI()->actionStop->isEnabled()) rowWait.quit();
                            });
                            rowPoll.start(10); rowLimit.start(60000);
                            auto *tree = snmpb.MainUI()->MIBTree;
                            const auto environment = MibEnvironmentRegistry::active();
                            const auto *entry = environment ? environment->nodeByOid(tableOid) : nullptr;
                            if (selectionPass == 0) check(entry && !entry->tableOid.isEmpty(), "live-row: entry resolves to its parent table for table-level querying");
                            const QString menuOid = selectionPass == 0 && entry && !entry->tableOid.isEmpty() ? entry->tableOid : tableOid;
                            tree->SelectFromOid(menuOid);
                            QTimer::singleShot(0, &rowWait, [&]() {
                                auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
                                auto *sub = menu ? menu->findChild<QMenu *>("QueryTableMenu") : nullptr;
                                check(sub && sub->isEnabled(), "live-row: table context menu offers Query Table");
                                if (!sub) { if (menu) menu->close(); return; }
                                menu->setActiveAction(sub->menuAction());
                                QKeyEvent right(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier);
                                QApplication::sendEvent(menu, &right);
                                QTimer::singleShot(0, &rowWait, [&, sub]() {
                                    auto *action = sub->findChild<QAction *>("QueryTableSelectInstance");
                                    check(action && action->isEnabled(), "live-row: Select Instance is available for tables and entries");
                                    if (!action) { sub->close(); return; }
                                    sub->setActiveAction(action);
                                    QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
                                    QApplication::sendEvent(sub, &enter);
                                });
                            });
                            QContextMenuEvent menuEvent(QContextMenuEvent::Keyboard,QPoint(4,4),tree->viewport()->mapToGlobal(QPoint(4,4)));
                            QApplication::sendEvent(tree->viewport(), &menuEvent);
                            check(requestStatus->text() == QString::fromUtf8("Finding table instances…") &&
                                  !requestStatus->property("failed").toBool(),
                                  "live-row: discovery shows progress without a false empty-result message");
                            rowWait.exec(); rowPoll.stop(); rowLimit.stop();
                            credentialBox->setCurrentIndex(credentialIndex); targetBox->setEditText(smoke_live_target);
                            check(picked, "live-row: table-level action opens the instance picker");
                            const auto selected = snmpb.AgentObj()->LastTableRecords();
                            if (cancelSelection)
                                check(selected == beforeCancel && !requestStatus->text().contains("returned no"),
                                      "live-row: cancelling selection preserves previous results");
                            else {
                                check(selected.size() == 2 && selected.first() == fullTableRecords.first() && selected.last() == wanted &&
                                      snmpb.Results()->isTableResult() && snmpb.Results()->table()->rowCount() == 1,
                                      "live-row: Results contains only the selected row with every readable column");
                                check(!requestStatus->property("failed").toBool(), "live-row: captured target and credential survive picker changes");
                            }
                        }
                        const auto beforeStop = snmpb.AgentObj()->LastTableRecords();
                        QEventLoop stopWait; QTimer stopLimit; stopLimit.setSingleShot(true);
                        QObject::connect(&stopLimit,&QTimer::timeout,&stopWait,&QEventLoop::quit);
                        QTimer stopPoll; QObject::connect(&stopPoll,&QTimer::timeout,&stopWait,[&]() {
                            if (!snmpb.MainUI()->actionStop->isEnabled()) stopWait.quit();
                        });
                        stopPoll.start(10); stopLimit.start(10000);
                        snmpb.AgentObj()->TableRowSelectFrom(tableOid);
                        snmpb.AgentObj()->Stop(); stopWait.exec();
                        check(!snmpb.MainUI()->actionStop->isEnabled() && requestStatus->text().contains("cancelled") &&
                              snmpb.AgentObj()->LastTableRecords() == beforeStop,
                              "live-row: Stop cancels discovery and preserves existing results");
                    }
                    }
                }
            }
            // A real request through the connection bar to a loopback-only test agent.
            if (!smoke_live_target.isEmpty() && smoke_live_table.isEmpty()) {
                QComboBox *targetBox = mw.findChild<QComboBox *>("ConnectionTarget");
                QComboBox *credentialBox = mw.findChild<QComboBox *>("ConnectionCredential");
                QTableWidget *results = mw.findChild<QTableWidget *>("StructuredQueryResults");
                check(targetBox && credentialBox && results, "live: bar and result table exist");
                if (targetBox && credentialBox && results) {
                    const int credentialIndex = credentialBox->findText(smoke_live_credential);
                    check(credentialIndex >= 0, "live: the requested credential is offered");
                    credentialBox->setCurrentIndex(credentialIndex);
                    targetBox->setEditText(smoke_live_target);
                    QEventLoop wait; QTimer limit; limit.setSingleShot(true);
                    QObject::connect(&limit, &QTimer::timeout, &wait, &QEventLoop::quit);
                    QTimer poll; QObject::connect(&poll, &QTimer::timeout, &wait, [&]() {
                        for (int row = 0; row < results->rowCount(); ++row)
                            for (int column = 0; column < results->columnCount(); ++column)
                                if (results->item(row, column) &&
                                    results->item(row, column)->text().contains("Loopback test agent")) {
                                    wait.quit(); return;
                                }
                    });
                    poll.start(100); limit.start(15000);
                    if (QAction *watch = mw.findChild<QAction *>("ViewWatch")) watch->trigger();
                    snmpb.AgentObj()->GetFrom("1.3.6.1.2.1.1.1.0", 0);
                    wait.exec();
                    QLabel *requestStatus = mw.findChild<QLabel *>("StructuredQueryStatus");
                    check(queryDocuments->currentWidget() == resultsPage && results->isVisibleTo(&mw),
                          "live: starting a Get automatically selects the main Results tab");
                    // Output keeps its text history in the bottom panel.
                    if (QAction *viewOutput = mw.findChild<QAction *>("ViewBottomOutput")) viewOutput->trigger();
                    QCoreApplication::processEvents();
                    check(snmpb.MainUI()->Query->isVisibleTo(&mw) &&
                          snmpb.MainUI()->Query->toPlainText().contains("Loopback test agent"),
                          "live: the Output text in the bottom panel shows the answer");
                    if (QAction *viewResults = mw.findChild<QAction *>("ViewResults")) viewResults->trigger();
                    QCoreApplication::processEvents();
                    QWidget *resultsTab = mw.findChild<QWidget *>("WorkbenchResultsPage");
                    smoke_output << "launch-smoke: LIVE-VISIBILITY results=" << results->isVisibleTo(&mw)
                                 << " status=" << (requestStatus && requestStatus->isVisibleTo(&mw))
                                 << " inResultsTab=" << (resultsTab && resultsTab->isAncestorOf(results))
                                 << " statusInResultsTab=" << (requestStatus && resultsTab && resultsTab->isAncestorOf(requestStatus))
                                 << " statusText=" << (requestStatus ? requestStatus->text().isEmpty() : -1) << Qt::endl;
                    check(results->isVisibleTo(&mw) && requestStatus && requestStatus->isVisibleTo(&mw) &&
                          resultsTab && resultsTab->isAncestorOf(results) &&
                          snmpb.MainUI()->MIBTree->isVisibleTo(&mw),
                          "live: View > Results shows the Results table and its status line beside the Tree");
                    smoke_output << "launch-smoke: LIVE-STATUS-AFTER-GET failed=" << (requestStatus && requestStatus->property("failed").toBool())
                                 << " text=\"" << (requestStatus ? requestStatus->text() : QString()) << "\"" << Qt::endl;
                    QString found;
                    for (int row = 0; row < results->rowCount(); ++row) {
                        QStringList cells;
                        for (int column = 0; column < results->columnCount(); ++column)
                            if (results->item(row, column)) cells << results->item(row, column)->text();
                        smoke_output << "launch-smoke: LIVE-RESULT " << cells.join(" | ") << Qt::endl;
                        if (cells.join(' ').contains("Loopback test agent")) found = cells.join(' ');
                    }
                    check(!found.isEmpty(), "live: the Get through the connection bar returned the agent's sysDescr");

                    // An object the agent answers with the wrong type: the Results table must say so.
                    QEventLoop wait2; QTimer limit2; limit2.setSingleShot(true);
                    QObject::connect(&limit2, &QTimer::timeout, &wait2, &QEventLoop::quit);
                    QTimer poll2; QObject::connect(&poll2, &QTimer::timeout, &wait2, [&]() {
                        for (int row = 0; row < results->rowCount(); ++row)
                            if (results->item(row, 6) && results->item(row, 6)->text().contains("Wrong type")) {
                                wait2.quit(); return;
                            }
                    });
                    poll2.start(100); limit2.start(15000);
                    snmpb.AgentObj()->GetFrom("1.3.6.1.4.1.90001.1.0", 0);
                    wait2.exec();
                    QString warning, receivedCell;
                    for (int row = 0; row < results->rowCount(); ++row) {
                        QStringList cells;
                        for (int column = 0; column < results->columnCount(); ++column)
                            if (results->item(row, column)) cells << results->item(row, column)->text();
                        smoke_output << "launch-smoke: LIVE-RESULT " << cells.join(" | ") << Qt::endl;
                        if (results->item(row, 6) && results->item(row, 6)->text().contains("Wrong type")) {
                            warning = results->item(row, 6)->text();
                            receivedCell = results->item(row, 5) ? results->item(row, 5)->text() : QString();
                        }
                    }
                    check(warning.contains("Gauge32") && warning.contains("Integer32"),
                          "live: a wrong-type answer shows a plain-English warning naming both types");
                    check(receivedCell.startsWith("Gauge32: 42"),
                          "live: the Results table shows the received type and raw value beside the interpretation");
                    // The main Results tab counts rows carrying MIB warnings.
                    QCoreApplication::processEvents();
                    if (QTabWidget *resultTabs = mw.findChild<QTabWidget *>("WorkbenchQueryDocuments")) {
                        int warningRows = 0;
                        for (int row = 0; row < results->rowCount(); ++row)
                            warningRows += results->item(row, 6) && !results->item(row, 6)->text().trimmed().isEmpty();
                        const int resultsTab = resultTabs->indexOf(mw.findChild<QWidget *>("WorkbenchResultsPage"));
                        smoke_output << "launch-smoke: RESULTS-TAB \"" << resultTabs->tabText(resultsTab) << "\"" << Qt::endl;
                        check(warningRows > 0 &&
                              resultTabs->tabText(resultsTab) == QStringLiteral("Results (%1 warning%2)")
                                  .arg(warningRows).arg(warningRows == 1 ? "" : "s"),
                              "live: the Results tab title counts the rows with MIB warnings");
                    }
                    if (smoke_live_single) {
                        // Single requests: what the Query pane, status line, Results table and Log show.
                        RequestLog *singleLog = snmpb.findChild<RequestLog *>();
                        const auto runSingle = [&](const char *label, const std::function<void()> &start) {
                            const int before = singleLog ? singleLog->entries().size() : 0;
                            QEventLoop singleWait; QTimer singleLimit; singleLimit.setSingleShot(true);
                            QObject::connect(&singleLimit, &QTimer::timeout, &singleWait, &QEventLoop::quit);
                            QTimer singlePoll; QObject::connect(&singlePoll, &QTimer::timeout, &singleWait, [&]() {
                                if (singleLog && singleLog->entries().size() > before &&
                                    singleLog->entries().last().outcome != RequestOutcome::Running) singleWait.quit();
                            });
                            singlePoll.start(50); singleLimit.start(30000);
                            start();
                            singleWait.exec();
                            QEventLoop settle; QTimer::singleShot(300, &settle, &QEventLoop::quit); settle.exec();
                            smoke_output << "launch-smoke: LIVE-SINGLE " << label << " query=\""
                                         << snmpb.MainUI()->Query->toPlainText().simplified() << "\"" << Qt::endl;
                            smoke_output << "launch-smoke: LIVE-SINGLE " << label << " status failed="
                                         << (requestStatus && requestStatus->property("failed").toBool()) << " text=\""
                                         << (requestStatus ? requestStatus->text() : QString()) << "\"" << Qt::endl;
                            for (int row = 0; row < results->rowCount(); ++row) {
                                QStringList cells;
                                for (int column = 0; column < 7; ++column)
                                    cells << (results->item(row, column) ? results->item(row, column)->text() : QString());
                                smoke_output << "launch-smoke: LIVE-SINGLE " << label << " row " << cells.join(" | ") << Qt::endl;
                            }
                            if (singleLog && singleLog->entries().size() > before) {
                                const RequestLogEntry &e = singleLog->entries().last();
                                smoke_output << "launch-smoke: LIVE-SINGLE " << label << " log op=" << e.operation << " oid=" << e.oid
                                             << " result=\"" << RequestOutcomeText(e.outcome) << "\" objects=" << e.objects
                                             << " detail=\"" << e.detail << "\"" << Qt::endl;
                            }
                        };
                        const auto queryTypedInstance = [&](const QString &root, const QString &suffix, int operation) {
                            QTimer::singleShot(0, &mw, [&]() {
                                auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                                auto *field = dialog ? dialog->findChild<QLineEdit *>("InstanceSuffix") : nullptr;
                                auto *buttons = dialog ? dialog->findChild<QDialogButtonBox *>() : nullptr;
                                check(field && buttons, "dialog-polish: typed instance opens for the scripted query");
                                if (field && buttons) { field->setText(suffix); buttons->button(QDialogButtonBox::Ok)->click(); }
                                else if (dialog) dialog->reject();
                            });
                            snmpb.AgentObj()->GetFromPromptInstance(root, operation);
                            check(singleLog && !singleLog->entries().isEmpty() && singleLog->entries().last().oid == root + "." + suffix,
                                  "dialog-polish: accepting instance entry queries the exact typed suffix");
                        };
                        runSingle("get", [&] { queryTypedInstance("1.3.6.1.2.1.1.1", "0", 0); });
                        runSingle("getnext", [&] { queryTypedInstance("1.3.6.1.2.1.1", "1", 1); });
                        runSingle("getbulk", [&] { queryTypedInstance("1.3.6.1.2.1", "1", 2); });
                        runSingle("get-missing", [&] { snmpb.AgentObj()->GetFrom("1.3.6.1.2.1.1.9.0", 0); });
                        runSingle("getnext-end", [&] { snmpb.AgentObj()->GetFrom("1.3.6.1.4.1.90001.9.9", 1); });
                        // Set, against the test agent only (it keeps the new value in memory): the one
                        // writable object, then the common refusals.
                        const auto setValue = [&](const char *oid, const char *text, int number, bool asText) {
                            Vb vb{Oid(oid)};
                            if (asText) vb.set_value(text); else vb.set_value(number);
                            snmpb.AgentObj()->SetVarbinds({vb});
                        };
                        runSingle("set", [&] { setValue("1.3.6.1.2.1.1.5.0", "renamed-by-set", 0, true); });
                        runSingle("set-readback", [&] { snmpb.AgentObj()->GetFrom("1.3.6.1.2.1.1.5.0", 0); });
                        runSingle("set-readonly", [&] { setValue("1.3.6.1.2.1.1.1.0", "nope", 0, true); });
                        runSingle("set-wrongtype", [&] { setValue("1.3.6.1.2.1.1.5.0", "", 5, false); });
                        runSingle("set-wrongvalue", [&] { setValue("1.3.6.1.4.1.90001.1.0", "", 7, false); });
                        runSingle("set-unknown", [&] { setValue("1.3.6.1.2.1.1.99.0", "x", 0, true); });
                    }
                    if (!smoke_live_bigwalk.isEmpty()) {
                        // A large Walk must not freeze the window, and Stop must end it promptly.
                        // A timer ticking every 20 ms shows how long the event loop was ever blocked.
                        QElapsedTimer sinceTick; sinceTick.start();
                        qint64 longestStall = 0;
                        QTimer ticker; QObject::connect(&ticker, &QTimer::timeout, [&]() {
                            longestStall = qMax(longestStall, sinceTick.elapsed());
                            sinceTick.restart();
                        });
                        ticker.start(20);
                        const auto waitForWalkEnd = [&](int limitMs) {
                            QEventLoop endWait; QTimer endLimit; endLimit.setSingleShot(true);
                            QObject::connect(&endLimit, &QTimer::timeout, &endWait, &QEventLoop::quit);
                            QElapsedTimer begun; begun.start();
                            QTimer endPoll; QObject::connect(&endPoll, &QTimer::timeout, &endWait, [&]() {
                                if (begun.elapsed() > 300 && !snmpb.MainUI()->actionStop->isEnabled()) endWait.quit();
                            });
                            endPoll.start(20); endLimit.start(limitMs);
                            endWait.exec();
                        };
                        QElapsedTimer whole; whole.start();
                        snmpb.AgentObj()->WalkFrom(smoke_live_bigwalk);
                        waitForWalkEnd(120000);
                        const qint64 wholeMs = whole.elapsed();
                        smoke_output << "launch-smoke: LIVE-BIGWALK rows=" << results->rowCount() << " ms=" << wholeMs
                                     << " longestStallMs=" << longestStall << " status=\""
                                     << (requestStatus ? requestStatus->text() : QString()) << "\"" << Qt::endl;
                        check(results->rowCount() == 3000 && !snmpb.MainUI()->actionStop->isEnabled(),
                              "live: a large Walk lists every object and finishes");
                        check(longestStall < 750,
                              "live: the window stays responsive during a large Walk");

                        // Now press Stop (the real action) shortly after the walk has started.
                        longestStall = 0; sinceTick.restart();
                        snmpb.AgentObj()->WalkFrom(smoke_live_bigwalk);
                        QEventLoop firstRows; QTimer firstRowsLimit; firstRowsLimit.setSingleShot(true);
                        QObject::connect(&firstRowsLimit, &QTimer::timeout, &firstRows, &QEventLoop::quit);
                        QTimer firstRowsPoll; QObject::connect(&firstRowsPoll, &QTimer::timeout, &firstRows, [&]() {
                            if (results->rowCount() >= 600 || !snmpb.MainUI()->actionStop->isEnabled()) firstRows.quit();
                        });
                        firstRowsPoll.start(5); firstRowsLimit.start(20000);
                        firstRows.exec();
                        const int rowsAtStop = results->rowCount();
                        QElapsedTimer stopTimer; stopTimer.start();
                        const bool stopWasAvailable = snmpb.MainUI()->actionStop->isEnabled();
                        // Exercise the actual window shortcut after removing its menu entry.
                        bool escapeTriggeredStop = false;
                        const auto shortcutCheck = QObject::connect(snmpb.MainUI()->actionStop, &QAction::triggered,
                            &mw, [&]() { escapeTriggeredStop = true; });
                        mw.activateWindow(); mw.setFocus();
                        QCoreApplication::processEvents();
                        QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
                        QApplication::sendEvent(&mw, &escape);
                        QObject::disconnect(shortcutCheck);
                        check(escapeTriggeredStop, "Esc triggers Stop even without a Stop menu entry");
                        QEventLoop stopWait; QTimer stopLimit; stopLimit.setSingleShot(true);
                        QObject::connect(&stopLimit, &QTimer::timeout, &stopWait, &QEventLoop::quit);
                        QTimer stopPoll; QObject::connect(&stopPoll, &QTimer::timeout, &stopWait, [&]() {
                            if (!snmpb.MainUI()->actionStop->isEnabled()) stopWait.quit();
                        });
                        stopPoll.start(5); stopLimit.start(30000);
                        stopWait.exec();
                        const qint64 stopMs = stopTimer.elapsed();
                        smoke_output << "launch-smoke: LIVE-STOP available=" << stopWasAvailable << " rowsAtStop=" << rowsAtStop
                                     << " rowsAfter=" << results->rowCount() << " stopMs=" << stopMs
                                     << " longestStallMs=" << longestStall << " status=\""
                                     << (requestStatus ? requestStatus->text() : QString()) << "\"" << Qt::endl;
                        check(stopWasAvailable && !snmpb.MainUI()->actionStop->isEnabled() && stopMs < 3000 &&
                                  results->rowCount() < 3000 && requestStatus && requestStatus->text().startsWith("Stopped after"),
                              "live: Stop ends a large Walk promptly and says it was stopped");
                        check(longestStall < 750, "live: the window stays responsive while a Walk is stopped");
                        ticker.stop();
                    }
                    if (!smoke_live_walk.isEmpty()) {
                        // Walk a subtree through the same bar and count what the Results table receives.
                        QElapsedTimer walkStarted;
                        QEventLoop walkWait; QTimer walkLimit; walkLimit.setSingleShot(true);
                        QObject::connect(&walkLimit, &QTimer::timeout, &walkWait, &QEventLoop::quit);
                        QTimer walkPoll; QObject::connect(&walkPoll, &QTimer::timeout, &walkWait, [&]() {
                            if (walkStarted.elapsed() > 300 && !snmpb.MainUI()->actionStop->isEnabled()) walkWait.quit();
                        });
                        walkPoll.start(100); walkLimit.start(20000);
                        walkStarted.start();
                        snmpb.AgentObj()->WalkFrom(smoke_live_walk);
                        walkWait.exec();
                        // A walk that does not end by itself is stopped here so the run still finishes.
                        const bool neededStop = snmpb.MainUI()->actionStop->isEnabled();
                        if (neededStop) {
                            smoke_output << "launch-smoke: LIVE-WALK-NEEDED-STOP" << Qt::endl;
                            snmpb.AgentObj()->Stop();
                            QEventLoop settle; QTimer::singleShot(1500, &settle, &QEventLoop::quit); settle.exec();
                        }
                        int walkRows = 0; QStringList walked;
                        for (int row = 0; row < results->rowCount(); ++row) {
                            ++walkRows;
                            walked << (results->item(row, 2) ? results->item(row, 2)->text() : QString());
                        }
                        smoke_output << "launch-smoke: LIVE-WALK rows=" << walkRows << " oids=" << walked.join(",")
                                     << " stopEnabled=" << snmpb.MainUI()->actionStop->isEnabled()
                                     << " queryText=\"" << snmpb.MainUI()->Query->toPlainText().left(300).replace('\n', ' ') << "\"" << Qt::endl;
                        if (RequestLog *shownLog = snmpb.findChild<RequestLog *>())
                            for (const RequestLogEntry &e : shownLog->entries())
                                smoke_output << "launch-smoke: LIVE-LOG op=" << e.operation << " target=" << e.target << " identity=\""
                                             << e.identity << "\" result=\"" << RequestOutcomeText(e.outcome) << "\" objects=" << e.objects
                                             << " detail=\"" << e.detail.left(120) << "\"" << Qt::endl;
                        smoke_output << "launch-smoke: LIVE-STATUS-AFTER-WALK failed=" << (requestStatus && requestStatus->property("failed").toBool())
                                     << " text=\"" << (requestStatus ? requestStatus->text() : QString()) << "\"" << Qt::endl;
                        check(walkRows >= 1 && !neededStop && !snmpb.MainUI()->actionStop->isEnabled(),
                              "live: Walk lists the subtree and finishes");
                    }
                    if (smoke_live_core) {
                        // Nothing is listening here: the failure must be visible above the table, not only on the Raw tab.
                        targetBox->setEditText("127.0.0.1:1");
                        QEventLoop deadWait; QTimer deadLimit; deadLimit.setSingleShot(true);
                        QObject::connect(&deadLimit, &QTimer::timeout, &deadWait, &QEventLoop::quit);
                        QTimer deadPoll; QObject::connect(&deadPoll, &QTimer::timeout, &deadWait, [&]() {
                            if (requestStatus && requestStatus->property("failed").toBool()) deadWait.quit();
                        });
                        deadPoll.start(100); deadLimit.start(30000);
                        snmpb.AgentObj()->GetFrom("1.3.6.1.2.1.1.1.0", 0);
                        deadWait.exec();
                        smoke_output << "launch-smoke: LIVE-STATUS-DEAD-PORT text=\"" << (requestStatus ? requestStatus->text() : QString()) << "\"" << Qt::endl;
                        check(requestStatus && requestStatus->property("failed").toBool() && !requestStatus->text().isEmpty(),
                              "live: a request that gets no answer shows an error line above the Results table");
                        targetBox->setEditText(smoke_live_target);

                        // The Log tab recorded all of that, and nothing secret.
                        RequestLog *requestLog = snmpb.findChild<RequestLog *>();
                        QTableWidget *logTable = mw.findChild<QTableWidget *>("RequestLogTable");
                        check(requestLog && logTable && logTable->rowCount() == requestLog->entries().size() &&
                              requestLog->entries().size() >= 3, "live: the Log tab lists every request");
                        if (requestLog) {
                            const QString expectedIdentity = smoke_live_credential.contains("SNMPv1") ? "SNMPv1" : "SNMPv2c";
                            bool okGet = false, timedOut = false, leaked = false;
                            for (const RequestLogEntry &e : requestLog->entries()) {
                                okGet = okGet || (e.operation == "Get" && e.outcome == RequestOutcome::Succeeded &&
                                                  e.target == smoke_live_target && e.identity == expectedIdentity && e.objects == 1);
                                timedOut = timedOut || e.outcome == RequestOutcome::TimedOut;
                                for (const QString &field : {e.target, e.identity, e.oid, e.detail, e.operation})
                                    leaked = leaked || field.contains("smokero") || field.contains("smokerw");
                            }
                            check(okGet, "live: a successful Get is logged with its target, identity and object count");
                            check(timedOut, "live: a request that got no answer is logged as a timeout");
                            check(!leaked, "live: the log never contains a community string");
                            check(!RequestLogAsText(requestLog->entries()).contains("smokero"), "live: the exported log contains no community string");
                        }
                    }
                    if (smoke_live_core) {
                        // Watch: a moving counter on the test agent, polled every second while the
                        // connection bar is pointed somewhere else.
                        WatchService *watchService = snmpb.findChild<WatchService *>();
                        check(watchService != nullptr, "live: the Watch service exists");
                        if (watchService) {
                            // Exercise the user's entry point, including the column instance dialog.
                            const auto treeWatch = [&](const QString &oid, const QString &suffix, bool cancel, bool invalid = false) {
                                if (QAction *query = mw.findChild<QAction *>("ViewResults")) query->trigger();
                                auto *tree = snmpb.MainUI()->MIBTree;
                                tree->SelectFromOid(oid);
                                QTimer::singleShot(0, &mw, [&, suffix, cancel, invalid]() {
                                    auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
                                    auto *action = menu ? menu->findChild<QAction *>("QueryTreeWatch") : nullptr;
                                    check(action && action->isEnabled(), "Query tree offers Watch on a value");
                                    if (!menu) return;
                                    if (!action || !action->isEnabled()) { menu->close(); return; }
                                    if (!suffix.isEmpty()) QTimer::singleShot(0, &mw, [&, suffix, cancel, invalid]() {
                                        auto *dialog = qobject_cast<QInputDialog *>(QApplication::activeModalWidget());
                                        check(dialog != nullptr, "column Watch prompts for its instance");
                                        if (dialog) {
                                            dialog->setTextValue(suffix);
                                            if (invalid) QTimer::singleShot(0, &mw, [&]() {
                                                auto *warning = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
                                                check(warning != nullptr, "invalid Watch instance is explained");
                                                if (warning) warning->accept();
                                            });
                                            if (cancel) dialog->reject(); else dialog->accept();
                                        }
                                    });
                                    menu->setActiveAction(action);
                                    QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
                                    QApplication::sendEvent(menu, &enter);
                                });
                                QContextMenuEvent event(QContextMenuEvent::Keyboard, QPoint(4, 4),
                                    tree->viewport()->mapToGlobal(QPoint(4, 4)));
                                QApplication::sendEvent(tree->viewport(), &event);
                            };
                            const auto waitForFirstWatch = [&](quint64 id) {
                                QEventLoop loop;
                                QTimer limit; limit.setSingleShot(true);
                                QTimer poll;
                                QObject::connect(&limit, &QTimer::timeout, &loop, &QEventLoop::quit);
                                QObject::connect(&poll, &QTimer::timeout, &loop, [&]() {
                                    const auto *row = watchService->row(id);
                                    if (row && row->polls > 0) loop.quit();
                                });
                                limit.start(10000); poll.start(20); loop.exec();
                                const auto *row = watchService->row(id);
                                return row && row->polls > 0;
                            };
                            treeWatch("1.3.6.1.4.1.90001.1", QString(), false);
                            check(watchService->ids().size() == 1, "tree scalar Watch creates one row");
                            if (!watchService->ids().isEmpty()) {
                                const auto id = watchService->ids().first();
                                check(waitForFirstWatch(id), "tree scalar Watch poll completes");
                                const auto *row = watchService->row(id);
                                check(row && row->spec.oid == "1.3.6.1.4.1.90001.1.0" && row->polls >= 1 && row->failures == 0,
                                      "tree scalar Watch appends .0 and receives a value");
                                watchService->remove(id);
                            }
                            treeWatch("1.3.6.1.4.1.90001.2.1.2", "4.101.116.104.49", true);
                            check(watchService->ids().isEmpty(), "cancelling an instance creates no watch");
                            for (const QString &bad : {QStringLiteral("4294967296"), QStringLiteral("1..2")}) {
                                treeWatch("1.3.6.1.4.1.90001.2.1.2", bad, false, true);
                                check(watchService->ids().isEmpty(), "invalid instance creates no watch");
                            }
                            treeWatch("1.3.6.1.4.1.90001.2.1.2", "4.101.116.104.49", false);
                            check(watchService->ids().size() == 1, "tree column Watch creates one row");
                            if (!watchService->ids().isEmpty()) {
                                const auto id = watchService->ids().first();
                                check(waitForFirstWatch(id), "tree column Watch poll completes");
                                const auto *row = watchService->row(id);
                                check(row && row->spec.oid == "1.3.6.1.4.1.90001.2.1.2.4.101.116.104.49" &&
                                      row->polls >= 1 && row->failures == 0 && row->value.contains("20"),
                                      "tree column Watch polls the exact compound instance");
                                auto *queryTabs = mw.findChild<QTabWidget *>("WorkbenchQueryDocuments");
                                check(queryTabs && queryTabs->currentWidget()->objectName() == "WorkbenchWatchPage" &&
                                      snmpb.MainUI()->MIBTree->isVisibleTo(&mw), "Watch results appear beside the Query tree");
                                mw.grab().save(QDir(smoke_config_dir).filePath("query-watch.png"));
                                watchService->remove(id);
                            }
                            WatchSpec spec;
                            spec.target = smoke_live_target;
                            spec.credentialKey = credentialBox->currentData().toString();
                            spec.oid = "1.3.6.1.2.1.2.2.1.10.1";
                            spec.intervalSeconds = 1;
                            const quint64 watchId = watchService->add(spec);
                            targetBox->setEditText("127.0.0.1:1");   // the bar no longer points at the agent
                            if (QAction *viewWatch = mw.findChild<QAction *>("ViewWatch")) viewWatch->trigger();
                            if (QAction *viewLibrary = mw.findChild<QAction *>("ViewMibLibrary")) viewLibrary->trigger();
                            if (auto *panel = mw.findChild<QDockWidget *>("WorkbenchBottomDock")) panel->hide();
                            QEventLoop watchWait; QTimer watchLimit; watchLimit.setSingleShot(true);
                            QObject::connect(&watchLimit, &QTimer::timeout, &watchWait, &QEventLoop::quit);
                            QTimer watchPoll; QObject::connect(&watchPoll, &QTimer::timeout, &watchWait, [&]() {
                                const WatchRow *row = watchService->row(watchId);
                                if (row && row->polls >= 4) watchWait.quit();
                            });
                            watchPoll.start(100); watchLimit.start(30000);
                            watchWait.exec();
                            const WatchRow *row = watchService->row(watchId);
                            if (row)
                                smoke_output << "launch-smoke: LIVE-WATCH polls=" << row->polls << " failures=" << row->failures
                                             << " status=\"" << row->status << "\" value=" << row->value << " change=\""
                                             << row->change.change << "\" rate=\"" << row->change.rate << "\" received=\""
                                             << row->received << "\" target=" << row->spec.target << Qt::endl;
                            check(row && row->polls >= 4 && row->status == "OK" && row->failures == 0 &&
                                  row->change.change == "+1000" && !row->change.rate.isEmpty(),
                                  "live: a watch continues polling with its workspace hidden and panel collapsed");
                            check(row && row->spec.target == smoke_live_target,
                                  "live: the watch kept its own target after the connection bar changed");
                            QTableWidget *watchTable = mw.findChild<QTableWidget *>("WatchTable");
                            check(watchTable && watchTable->rowCount() == 1 &&
                                  watchTable->item(0, 4)->text() == "+1000",
                                  "live: the Watch tab shows the row and its change");
                            if (QAction *viewWatch = mw.findChild<QAction *>("ViewWatch")) viewWatch->trigger();
                            if (QAction *viewOutput = mw.findChild<QAction *>("ViewResults")) viewOutput->trigger();
                            mw.resize(1500, 1000);
                            QCoreApplication::processEvents();
                            mw.grab().save(QDir(smoke_config_dir).filePath("watch-tab.png"));
                            if (QAction *viewWatch = mw.findChild<QAction *>("ViewWatch")) viewWatch->trigger();
                            QCoreApplication::processEvents();
                            QObject watchMenuContext;
                            QTimer::singleShot(0, &watchMenuContext, [&]() {
                                auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
                                check(menu, "live: Watch context menu opens");
                                if (!menu) return;
                                QAction *pause = menu->actions().first();
                                watchService->remove(watchId); // invalidate the old row during the modal menu
                                menu->setActiveAction(pause);
                                QKeyEvent key(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
                                QApplication::sendEvent(menu, &key);
                            });
                            const QPoint position = watchTable->visualItemRect(watchTable->item(0, 0)).center();
                            emit watchTable->customContextMenuRequested(position);
                            check(watchTable && watchTable->rowCount() == 0,
                                  "live: removing a watch removes its row");
                            check(watchTable && watchTable->rowCount() == 0 && !watchService->row(watchId),
                                  "live: a removed Watch row cannot be accessed or recreated by its open menu");
                            check(mw.findChild<QLabel *>("WatchHint")->text().startsWith("No watches yet"),
                                  "live: removing the final watch restores empty-state guidance");
                        }
                        if (smoke_live_target.startsWith("127.0.0.1") && smoke_live_core) {
                            // Detect profile: start from another profile; the test agent reports a
                            // vendor registration that only the test profile's MIBs define.
                            QComboBox *profileCombo = mw.findChild<QComboBox *>("MibBrowserProfileSelector");
                            QToolButton *detectButton = mw.findChild<QToolButton *>("ConnectionDetect");
                            MibCollectionService *collection = snmpb.findChild<MibCollectionService *>();
                            check(profileCombo && detectButton && collection, "live: the Detect button exists");
                            if (profileCombo && detectButton && collection) {
                                collection->rebuildNow();
                                collection->waitForBuild(60000);
                                // Both the Automatic profile for the Acme/Prod folder and the hand-made Test profile hold the
                                // MIB the agent registers under; Detect prefers the Automatic one.
                                const QString before = QStringLiteral("Acme / Prod");
                                const int standards = profileCombo->findData(QStringLiteral("custom.other"));   // the empty second profile
                                snmpb.MibLibraryObj()->selectProfile(QStringLiteral("custom.other"));
                                targetBox->setEditText(smoke_live_target);
                                // The selector follows the Library a moment later: wait until it really shows Standards.
                                const auto waitUntil = [](const std::function<bool()> &done, int limitMs) {
                                    QEventLoop loop; QTimer limit, poll; limit.setSingleShot(true);
                                    QObject::connect(&limit, &QTimer::timeout, &loop, &QEventLoop::quit);
                                    QObject::connect(&poll, &QTimer::timeout, &loop, [&]() { if (done()) loop.quit(); });
                                    poll.start(50); limit.start(limitMs);
                                    if (!done()) loop.exec();
                                    return done();
                                };
                                check(standards >= 0 && waitUntil([&]() { return profileCombo->currentIndex() == standards; }, 30000),
                                      "live: a different profile is selected before Detect");
                                QEventLoop detectWait; QTimer detectLimit; detectLimit.setSingleShot(true);
                                QObject::connect(&detectLimit, &QTimer::timeout, &detectWait, &QEventLoop::quit);
                                QTimer detectPoll; QObject::connect(&detectPoll, &QTimer::timeout, &detectWait, [&]() {
                                    if (profileCombo->currentText() == before) detectWait.quit();
                                });
                                detectButton->click();
                                check(!detectButton->isEnabled(), "live: the Detect button is disabled while a detection runs");
                                detectPoll.start(100); detectLimit.start(60000);
                                detectWait.exec();
                                smoke_output << "launch-smoke: DETECT before=\"" << before << "\" after=\"" << profileCombo->currentText() << "\"" << Qt::endl;
                                check(profileCombo->currentText() == before,
                                      "live: Detect read the device and switched to the Automatic profile that holds its MIBs");
                                QCoreApplication::processEvents();
                                check(detectButton->isEnabled(), "live: the Detect button is available again");
                                bool logged = false;
                                if (RequestLog *detectLog = snmpb.findChild<RequestLog *>())
                                    for (const RequestLogEntry &e : detectLog->entries())
                                        logged = logged || (e.operation == "Detect profile" && e.outcome == RequestOutcome::Succeeded &&
                                                            e.detail.contains(before));
                                check(logged, "live: the detection is in the Log with the profile it chose");
                            }
                        }
                        if (smoke_live_target.startsWith("127.0.0.1")) {
                            // Ask the test agent to exit so the scenario ends without waiting.
                            targetBox->setEditText(smoke_live_target);
                            QEventLoop quitWait; QTimer::singleShot(1500, &quitWait, &QEventLoop::quit);
                            snmpb.AgentObj()->GetFrom("1.3.6.1.4.1.99999.0", 0);
                            quitWait.exec();
                        }
                    }
                    QSettings historySettings;
                    check(historySettings.value("connection/target-history").toString().contains(smoke_live_target),
                          "live: the target was remembered in the recent-target history");
                    check(historySettings.value("connection/target-history").toString().contains("community:"),
                          "live: the history remembers the credential used with the target");
                    if (QAction *viewResults = mw.findChild<QAction *>("ViewResults")) viewResults->trigger();
                    mw.resize(1500, 1000);
                    QCoreApplication::processEvents();
                    mw.grab().save(QDir(smoke_config_dir).filePath("live-get.png"));
                }
            }
            {
                // Exercise the application's actual receive-to-widget wiring using
                // a local value PDU, without sending any network traffic.
                Pdu notification;
                notification.set_type(sNMP_PDU_TRAP);
                notification.set_notify_id(Oid("1.3.6.1.6.3.1.1.5.1"));
                Vb binding(Oid("1.3.6.1.2.1.2.2.1.8.1"));
                binding.set_value(SnmpInt32(7));
                notification += binding;
                TrapEndpoint endpoint{QStringLiteral("192.0.2.10"), 9162,
                    TrapSnmpVersion::V2c, QStringLiteral("synthetic"), {}};
                check(snmpb.TrapObj()->Receive(notification, endpoint),
                    "synthetic trap enters the application receiver");
                QTreeWidget *content = snmpb.MainUI()->TrapContent;
                check(snmpb.MainUI()->TrapLog->topLevelItemCount() == 1 &&
                    content->topLevelItemCount() == 2 && content->topLevelItem(1)->childCount() == 1 &&
                    content->topLevelItem(1)->child(0)->text(2) == "Integer32" &&
                    content->topLevelItem(1)->child(0)->text(3) == "7",
                    "synthetic trap displays typed raw data in the actual Traps workspace");
                snmpb.TrapObj()->service()->clear();
            }
            check(snmpb.MainUI()->TrapLog->contextMenuPolicy() == Qt::CustomContextMenu,
                  "the trap list offers a right-click menu (Copy source IP)");
            // Search all MIBs: an index of every MIB file in the collection, not just the active profile.
            {
                MibCollectionService *collection = snmpb.findChild<MibCollectionService *>();
                QLineEdit *searchBox = mw.findChild<QLineEdit *>("CollectionSearch");
                QTableWidget *searchTable = mw.findChild<QTableWidget *>("SearchResults");
                QTabWidget *bottomSearch = mw.findChild<QTabWidget *>("WorkbenchQueryDocuments");
                check(collection && searchBox && searchTable && bottomSearch, "collection search box, index and Search tab exist");
                if (collection && searchBox && searchTable && bottomSearch) {
                    collection->rebuildNow();
                    check(collection->waitForBuild(60000) && collection->hasIndex(),
                          "the collection index builds in the background");
                    const auto cell = [searchTable](int row, int column) {
                        return searchTable->item(row, column) ? searchTable->item(row, column)->text() : QString(); };
                    // GOOD-MIB exists only in the scenarios that build a library with it.
                    const bool hasGoodMib = QFileInfo::exists(QDir(smoke_config_dir).filePath("MIB-Library/Acme/Prod/GOOD-MIB"));
                    if (hasGoodMib) {
                        // Automatic profiles: the Library folder Acme/Prod is a profile without anyone creating it.
                        QComboBox *autoCombo = mw.findChild<QComboBox *>("MibBrowserProfileSelector");
                        const int autoIndex = autoCombo ? autoCombo->findData(QStringLiteral("auto:Acme/Prod")) : -1;
                        check(autoIndex >= 0 && autoCombo->itemText(autoIndex) == "Acme / Prod",
                              "an Automatic profile exists for the Acme/Prod folder without anyone creating it");
                        bool reservedMakeProfiles = false;
                        for (int i = 0; autoCombo && i < autoCombo->count(); ++i)
                            reservedMakeProfiles = reservedMakeProfiles || autoCombo->itemData(i).toString().startsWith("auto:Standards") ||
                                                   autoCombo->itemData(i).toString().startsWith("auto:Unassigned");
                        check(!reservedMakeProfiles, "Standards and Unassigned never become Automatic profiles");
                        if (autoIndex >= 0) {
                            QEventLoop envWait; QTimer envLimit; envLimit.setSingleShot(true);
                            QObject::connect(&envLimit, &QTimer::timeout, &envWait, &QEventLoop::quit);
                            QString readyProfile;
                            QObject::connect(snmpb.MibModuleObj(), &MibModule::profileRuntimeReady, &envWait,
                                [&](const QString &id, const MibEffectivePlan &, MibEnvironmentPtr, const QStringList &, bool, bool) {
                                    if (id == "auto:Acme/Prod") { readyProfile = id; envWait.quit(); } });
                            envLimit.start(60000);
                            snmpb.MibLibraryObj()->selectProfile(QStringLiteral("auto:Acme/Prod"));
                            envWait.exec();
                            bool hasGood = false;
                            if (const MibEnvironmentPtr built = snmpb.MibModuleObj()->CurrentEnvironment())
                                for (const auto &module : built->modules()) hasGood = hasGood || module.identity == "GOOD-MIB";
                            check(readyProfile == "auto:Acme/Prod" && hasGood,
                                  "selecting the Automatic profile builds an environment from the folder's MIBs");
                            // A Custom profile that follows the folder: the real application loads
                            // the folder's MIBs, and a followed folder that disappears is explained
                            // on Problems rather than silently dropped.
                            {
                                MibProfileService *profiles = snmpb.MibLibraryObj()->profileService();
                                QString folderError;
                                const QString followId = profiles->create("Follows Acme Prod", &folderError);
                                emit snmpb.MibLibraryObj()->profilesChanged();   // as New Profile does
                                // The Library the application actually uses (its own setting).
                                const QDir libraryRoot(QSettings().value("mib-library/root").toString());
                                const QString prodFolder = QFileInfo(libraryRoot.filePath("Acme/Prod")).canonicalFilePath();
                                const QString doomed = libraryRoot.filePath("Acme/Doomed");
                                QDir().mkpath(doomed);
                                const bool followed = !followId.isEmpty() &&
                                    profiles->followFolders(followId, {prodFolder, doomed}, &folderError);
                                QDir(doomed).removeRecursively();
                                snmpb.MibLibraryObj()->refresh();
                                // Its own event loop is the connection's context, so the handler (which
                                // refers to this block's locals) is gone when the block ends.
                                QString builtFor;
                                QEventLoop folderWait; QTimer folderLimit; folderLimit.setSingleShot(true);
                                QObject::connect(&folderLimit, &QTimer::timeout, &folderWait, &QEventLoop::quit);
                                QObject::connect(snmpb.MibModuleObj(), &MibModule::profileRuntimeReady, &folderWait,
                                    [&](const QString &id, const MibEffectivePlan &, MibEnvironmentPtr, const QStringList &, bool, bool) {
                                        if (id == followId) { builtFor = id; folderWait.quit(); } });
                                folderLimit.start(60000);
                                snmpb.MibLibraryObj()->selectProfile(followId);
                                if (builtFor.isEmpty()) folderWait.exec();
                                bool loadsGood = false;
                                if (const MibEnvironmentPtr built = snmpb.MibModuleObj()->CurrentEnvironment())
                                    for (const auto &module : built->modules()) loadsGood = loadsGood || module.identity == "GOOD-MIB";
                                check(followed && builtFor == followId && loadsGood,
                                      "a Custom profile that follows a Library folder loads that folder's MIBs");
                                QCoreApplication::processEvents();
                                bool explained = false;
                                if (QTableWidget *problemRows = mw.findChild<QTableWidget *>("WorkbenchProblemTable"))
                                    for (int row = 0; row < problemRows->rowCount(); ++row)
                                        explained = explained || (problemRows->item(row, 2) &&
                                            problemRows->item(row, 2)->text() == "Followed folder not found");
                                QLabel *heading = mw.findChild<QLabel *>("WorkbenchProblemHeading");
                                check(explained && heading && heading->text() == "Problems loading Follows Acme Prod",
                                      "a followed folder that disappeared is listed on Problems under the profile's name");
                                // A picture of the Profiles page with the followed folder, for review.
                                if (QAction *viewProfiles = mw.findChild<QAction *>("ViewMibProfiles")) viewProfiles->trigger();
                                if (QTreeWidget *memberTree = mw.findChild<QTreeWidget *>("MibProfileMemberTree")) memberTree->expandAll();
                                mw.resize(1500, 1000);
                                QCoreApplication::processEvents();
                                mw.grab().save(QDir(smoke_config_dir).filePath("profiles-tab.png"));
                                if (QAction *viewOutput = mw.findChild<QAction *>("ViewResults")) viewOutput->trigger();
                                profiles->remove(followId, &folderError);
                            }
                            snmpb.MibLibraryObj()->selectProfile(QStringLiteral("custom.test"));
                        }
                    searchBox->setText("goodValue");
                    emit searchBox->returnPressed();
                    check(searchTable->rowCount() == 1 && cell(0, 0) == "goodValue" && cell(0, 2) == "GOOD-MIB" &&
                          cell(0, 3) == "1.3.6.1.4.1.90001.1" && cell(0, 4).contains("Test profile"),
                          "a search finds the object, its module, its OID and the profile that contains it");
                    check(bottomSearch->tabText(bottomSearch->currentIndex()) == "Search" &&
                          bottomSearch->currentWidget()->isAncestorOf(searchTable),
                          "searching opens the Search tab in the middle area and brings it forward");
                    }
                    searchBox->setText("sysDescr");
                    emit searchBox->returnPressed();
                    int snmpv2Row = -1;
                    for (int row = 0; row < searchTable->rowCount(); ++row)
                        if (cell(row, 2) == "SNMPv2-MIB") snmpv2Row = row;
                    check(searchTable->rowCount() >= 2 && snmpv2Row >= 0 && cell(snmpv2Row, 3) == "1.3.6.1.2.1.1.1",
                          "the same name defined in several modules gives one result per module");
                    check(snmpv2Row >= 0 && !cell(snmpv2Row, 4).isEmpty(),
                          "every result says which profiles contain it, or that none does");
                    if (searchTable->rowCount() > 0) {
                        const QString savedEditorText = snmpb.MainUI()->MIBFile->toPlainText();
                        const bool savedEditorModified = snmpb.MainUI()->MIBFile->document()->isModified();
                        const auto savedEditorCursor = snmpb.MainUI()->MIBFile->textCursor();
                        const int savedEditorPosition = savedEditorCursor.position();
                        const int savedEditorAnchor = savedEditorCursor.anchor();
                        auto *searchPanel = mw.findChild<CollectionSearchPanel *>();
                        const int row = 0;
                        const QString file = searchTable->item(row, 5)->data(Qt::UserRole).toString();
                        const int line = searchTable->item(row, 5)->data(Qt::UserRole + 1).toInt();
                        int deliveries = 0;
                        const auto connection = QObject::connect(searchPanel, &CollectionSearchPanel::openInEditorRequested,
                            &mw, [&](const QString &chosen, int at) {
                                ++deliveries; check(chosen == file && at == line,
                                    "Search menu keeps the captured source after its table is rebuilt");
                            });
                        QObject searchMenuContext;
                        QTimer::singleShot(0, &searchMenuContext, [&]() {
                            auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
                            check(menu, "Search source menu opens"); if (!menu) return;
                            auto *open = menu->actions().value(1);
                            searchPanel->search("no-such-definition-during-menu");
                            menu->setActiveAction(open);
                            QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
                            QApplication::sendEvent(menu, &enter);
                        });
                        const QPoint position = searchTable->visualItemRect(searchTable->item(row, 0)).center();
                        emit searchTable->customContextMenuRequested(position);
                        QObject::disconnect(connection);
                        check(deliveries == 1, "Search menu opens its captured definition exactly once");
                        // Keyboard activation uses the same source-opening path as a double-click.
                        searchBox->setText("SNMPv2-MIB::sysDescr"); emit searchBox->returnPressed();
                        if (searchTable->rowCount()) {
                            int activations = 0;
                            const auto activated = QObject::connect(searchPanel, &CollectionSearchPanel::showInTreeRequested,
                                &mw, [&](const QString &) { ++activations; });
                            const auto opened = QObject::connect(searchPanel, &CollectionSearchPanel::openInEditorRequested,
                                &mw, [&](const QString &, int) { ++activations; });
                            searchTable->setCurrentCell(0, 0); searchTable->setFocus();
                            QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
                            QApplication::sendEvent(searchTable, &enter);
                            check(activations == 1, "Enter activates a Search result exactly once");
                            QObject::disconnect(activated); QObject::disconnect(opened);
                        }
                        // Source-opening regressions must not replace the later edit/save fixture.
                        if (!smoke_mib_file.isEmpty()) snmpb.MibEditorObj()->MibFileOpen(smoke_mib_file);
                        snmpb.MainUI()->MIBFile->setPlainText(savedEditorText);
                        QTextCursor restoredCursor(snmpb.MainUI()->MIBFile->document());
                        restoredCursor.setPosition(savedEditorAnchor);
                        restoredCursor.setPosition(savedEditorPosition, QTextCursor::KeepAnchor);
                        snmpb.MainUI()->MIBFile->setTextCursor(restoredCursor);
                        snmpb.MainUI()->MIBFile->document()->setModified(savedEditorModified);
                        check(snmpb.MainUI()->MIBFile->toPlainText() == savedEditorText,
                              "Search regression restores the independent Editor fixture");
                    }
                    if (hasGoodMib) {
                    searchBox->setText("1.3.6.1.4.1.90001.1.0");
                    emit searchBox->returnPressed();
                    check(searchTable->rowCount() == 1 && cell(0, 0) == "goodValue",
                          "an instance OID finds the definition above it");
                    }
                    searchBox->setText("no-such-thing-anywhere");
                    emit searchBox->returnPressed();
                    check(searchTable->rowCount() == 0, "a search with no match shows no rows");
                    searchBox->setText(hasGoodMib ? "goodValue" : "sysDescr");
                    emit searchBox->returnPressed();
                    mw.resize(1500, 1000);
                    QCoreApplication::processEvents();
                    mw.grab().save(QDir(smoke_config_dir).filePath("search-tab.png"));
                }
            }
            // A smaller desktop must retain the fixed workspace navigation and connection controls.
            if (smoke_live_target.isEmpty() && !smoke_mib_file.isEmpty()) {
                auto *tabs = mw.findChild<QTabWidget *>("WorkbenchDocuments");
                auto *header = mw.findChild<QWidget *>("CurrentDeviceContext");
                auto *bottom = mw.findChild<QDockWidget *>("WorkbenchBottomDock");
                const QSize previousSize = mw.size();
                const int previousTab = tabs ? tabs->currentIndex() : 0;
                const bool bottomHidden = bottom && bottom->isHidden();
                if (bottom) bottom->hide();
                int navigationY = -1;
                for (int page = 0; tabs && page < tabs->count(); ++page) {
                    tabs->setCurrentIndex(page); QCoreApplication::processEvents();
                    mw.resize(1024, 720); QCoreApplication::processEvents();
                    bool controlsFit = header;
                    for (const auto *name : {"ConnectionTarget", "ConnectionCredential", "MibBrowserProfileSelector", "ConnectionDetect"})
                        if (auto *control = mw.findChild<QWidget *>(name))
                            controlsFit &= header && header->rect().contains(control->mapTo(header, control->rect().bottomRight()));
                        else controlsFit = false;
                    const int y = tabs->mapTo(&mw, QPoint()).y();
                    if (navigationY < 0) navigationY = y;
                    smoke_output << "launch-smoke: SMALL-WINDOW page=" << page << " size=" << mw.width() << "x" << mw.height()
                                 << " minimum=" << mw.minimumSize().width() << "x" << mw.minimumSize().height()
                                 << " controls-fit=" << controlsFit << " navigation-y=" << y << Qt::endl;
                    check(mw.width() <= 1024 && mw.height() <= 720 && controlsFit && navigationY == y,
                          "dialog-polish: smaller desktop keeps workspace navigation fixed and connection controls visible");
                    mw.grab().save(QDir(smoke_config_dir).filePath(QString("small-window-workspace-%1.png").arg(page)));
                }
                if (tabs) tabs->setCurrentIndex(previousTab);
                if (bottom) bottom->setVisible(!bottomHidden);
                mw.resize(previousSize); QCoreApplication::processEvents();
            }
            // Actual Editor dialogs: action/default-button behavior and compact layout.
            if (smoke_live_target.isEmpty() && !smoke_mib_file.isEmpty()) {
                snmpb.ShowEditorDocument();
                auto *document = snmpb.MainUI()->MIBFile;
                const QString original = document->toPlainText();
                const bool wasModified = document->document()->isModified();
                document->setPlainText("alpha\nbeta alpha\ngamma\n");
                document->moveCursor(QTextCursor::Start);
                const auto dialogFits = [&](QDialog *dialog, const QString &image) {
                    if (!dialog) { check(false, "dialog-polish: expected modal dialog opens"); return; }
                    dialog->resize(640, 480); QCoreApplication::processEvents();
                    auto *buttons = dialog->findChild<QDialogButtonBox *>();
                    check(dialog->width() <= 640 && dialog->height() <= 480 && buttons &&
                          dialog->rect().contains(buttons->mapTo(dialog, buttons->rect().bottomRight())),
                          "dialog-polish: compact dialog keeps its standard footer visible");
                    dialog->grab().save(QDir(smoke_config_dir).filePath(image));
                };
                QTimer::singleShot(0, &mw, [&]() {
                    auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                    dialogFits(dialog, "dialog-find.png"); if (!dialog) return;
                    auto *find = dialog->findChild<QComboBox *>("comboFind");
                    auto *next = dialog->findChild<QPushButton *>("buttonFindNext");
                    check(find && next && !next->isEnabled(), "dialog-polish: empty Find has no active search action");
                    if (find && next) {
                        find->setEditText("alpha");
                        QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
                        QApplication::sendEvent(dialog, &enter);
                        check(next->isEnabled() && next->isDefault() && dialog->isVisible() &&
                              document->textCursor().selectedText() == "alpha",
                              "dialog-polish: Enter finds text without closing Find");
                    }
                    dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Close)->click();
                });
                snmpb.MainUI()->actionFind->trigger();
                document->moveCursor(QTextCursor::Start);
                QTimer::singleShot(0, &mw, [&]() {
                    auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                    dialogFits(dialog, "dialog-replace.png"); if (!dialog) return;
                    auto *find = dialog->findChild<QComboBox *>("comboFind");
                    auto *replacement = dialog->findChild<QComboBox *>("comboReplace");
                    auto *next = dialog->findChild<QPushButton *>("buttonFindNext");
                    auto *replace = dialog->findChild<QPushButton *>("buttonReplace");
                    auto *all = dialog->findChild<QPushButton *>("buttonReplaceAll");
                    if (find && replacement && next && replace && all) {
                        find->setEditText("");
                        check(!replace->isEnabled() && !all->isEnabled(), "dialog-polish: empty Replace disables replacement actions");
                        find->setEditText("alpha"); replacement->setEditText("omega"); next->click(); replace->click(); all->click();
                        check(document->toPlainText() == "omega\nbeta omega\ngamma\n" && dialog->isVisible(),
                              "dialog-polish: Replace and Replace All edit the actual document without closing");
                    } else check(false, "dialog-polish: replacement controls exist");
                    dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Close)->click();
                });
                snmpb.MainUI()->actionReplace->trigger();
                QTimer::singleShot(0, &mw, [&]() {
                    auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                    dialogFits(dialog, "dialog-go-to-line.png"); if (!dialog) return;
                    auto *line = dialog->findChild<QSpinBox *>("spinLine");
                    check(line && line->minimum() == 1, "dialog-polish: Go to Line starts at the first valid line");
                    if (line) line->setValue(2);
                    dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
                });
                snmpb.MainUI()->actionGotoLine->trigger();
                check(document->textCursor().blockNumber() == 1, "dialog-polish: Go to Line moves the actual Editor cursor");
                document->setPlainText(original); document->document()->setModified(wasModified);

                auto *requestLog = snmpb.findChild<RequestLog *>();
                const auto requestCount = requestLog ? requestLog->entries().size() : -1;
                for (int operation = 0; operation < 3; ++operation) {
                    QTimer::singleShot(0, &mw, [&]() {
                        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                        dialogFits(dialog, "dialog-enter-instance.png"); if (!dialog) return;
                        auto *suffix = dialog->findChild<QLineEdit *>("InstanceSuffix");
                        auto *buttons = dialog->findChild<QDialogButtonBox *>();
                        check(suffix && buttons && !buttons->button(QDialogButtonBox::Ok)->isEnabled(),
                              "dialog-polish: Enter Instance requires a suffix");
                        if (suffix && buttons) {
                            suffix->setText("1.4");
                            check(buttons->button(QDialogButtonBox::Ok)->isEnabled(), "dialog-polish: a numeric compound suffix enables Query");
                            suffix->setText("1..4");
                            check(!buttons->button(QDialogButtonBox::Ok)->isEnabled(), "dialog-polish: an incomplete suffix cannot start a query");
                            suffix->setText("7"); buttons->button(QDialogButtonBox::Cancel)->click();
                        } else dialog->reject();
                    });
                    snmpb.AgentObj()->GetFromPromptInstance("1.3.6.1.2.1.2.2.1.10", operation);
                }
                check(requestLog && requestLog->entries().size() == requestCount,
                      "dialog-polish: cancelling Get, Get Next and Get Bulk instance entry issues no request");
            }
            // Unified credentials: explicit versions, independent display names and stable IDs.
            if (auto *dialog = mw.findChild<CredentialsDialog *>("CredentialsDialog")) {
                dialog->prepare();
                auto *list = dialog->credentialList();
                auto *name = dialog->findChild<QLineEdit *>("CredentialDisplayName");
                auto *user = dialog->findChild<QLineEdit *>("CredentialUsmUser");
                auto *read = dialog->findChild<QLineEdit *>("CredentialReadCommunity");
                auto *auth = dialog->findChild<QComboBox *>("CredentialAuthProtocol");
                auto *authSecret = dialog->findChild<QLineEdit *>("CredentialAuthSecret");
                auto *privacy = dialog->findChild<QComboBox *>("CredentialPrivacyProtocol");
                auto *privSecret = dialog->findChild<QLineEdit *>("CredentialPrivacySecret");
                auto *fields = dialog->findChild<QStackedWidget *>("CredentialFields");
                const auto communitiesBefore = snmpb.CommunityCredentials()->records();
                const auto usersBefore = snmpb.UsmCredentials()->records();
                check(list && name && user && fields && !dialog->findChild<QTabWidget *>("CredentialsTabs") &&
                      list->count() == communitiesBefore.size() + usersBefore.size(),
                      "Credentials uses one list for every version");
                check(dialog->applyChanges() && snmpb.CommunityCredentials()->records() == communitiesBefore &&
                      snmpb.UsmCredentials()->records() == usersBefore,
                      "pressing OK in the Credentials screen with no edits keeps every credential");
                if (list && name && user && fields && auth && authSecret && privacy && privSecret && read) {
                    const auto add = [&](int version) {
                        auto *action = dialog->findChild<QAction *>(QStringLiteral("CredentialNewV%1").arg(version));
                        check(action != nullptr, "New offers the requested SNMP version");
                        if (action) action->trigger();
                    };
                    add(2); name->setText("Discarded draft"); user->setText("not-saved");
                    dialog->reject(); dialog->prepare();
                    check(list->count() == communitiesBefore.size() + usersBefore.size(), "Cancel discards new credential drafts");
                    add(2); name->setText("Product A"); user->setText("shared-user");
                    auth->setCurrentIndex(auth->findData(SNMP_AUTHPROTOCOL_HMACSHA)); authSecret->setText("product-a-auth");
                    const QString first = list->currentItem()->data(Qt::UserRole).toString();
                    auto *duplicate = dialog->findChild<QPushButton *>("CredentialDuplicate");
                    check(duplicate && fields->currentIndex() == 1, "v3 creation shows USM fields and Duplicate");
                    if (duplicate) duplicate->click();
                    name->setText("Product B");
                    check(user->text() == "shared-user", "Duplicate preserves the USM username");
                    auth->setCurrentIndex(auth->findData(SNMP_AUTHPROTOCOL_HMAC192SHA256)); authSecret->setText("product-b-auth");
                    privacy->setCurrentIndex(privacy->findData(SNMP_PRIVPROTOCOL_AES128)); privSecret->setText("product-b-privacy");
                    const QString second = list->currentItem()->data(Qt::UserRole).toString();
                    QStringList added{first, second};
                    for (int version : {0, 1}) {
                        add(version); name->setText(version == 0 ? "Only v1" : "Only v2c"); read->setText("unified-test-read");
                        check(fields->currentIndex() == 0, "community creation shows community fields");
                        added << list->currentItem()->data(Qt::UserRole).toString();
                    }
                    check(dialog->applyChanges(), "unified edits save both credential kinds");
                    const auto *a = snmpb.UsmCredentials()->find(first), *b = snmpb.UsmCredentials()->find(second);
                    check(a && b && first != second && a->securityName == b->securityName &&
                          a->displayName == "Product A" && b->displayName == "Product B" &&
                          a->authProtocol != b->authProtocol && a->authSecret.bytes() != b->authSecret.bytes() &&
                          a->privacyProtocol != b->privacyProtocol, "same USM user retains independent labels, protocols and secrets");
                    const auto choices = ConnectionModel::credentialChoices(snmpb.CommunityCredentials(), snmpb.UsmCredentials());
                    bool sawA = false, sawB = false;
                    for (const auto &choice : choices) {
                        if (choice.key == ConnectionModel::usmKey(first)) sawA = choice.label.contains("Product A");
                        if (choice.key == ConnectionModel::usmKey(second)) sawB = choice.label.contains("Product B");
                    }
                    check(sawA && sawB, "connection bar choices use independent credential display names");
                    dialog->prepare(CredentialsDialog::Users);
                    check(fields->currentIndex() == 1 && list->count() == communitiesBefore.size() + usersBefore.size() + 4,
                          "reopening credentials retains a unified list and selects the requested kind");
                    dialog->grab().save(QDir(smoke_config_dir).filePath("credentials-workspace.png"));
                    const QSize previousSize = dialog->size(); const bool wasVisible = dialog->isVisible();
                    dialog->show(); dialog->resize(640, 480); QCoreApplication::processEvents();
                    auto *scroll = dialog->findChild<QScrollArea *>("CredentialEditorScroll");
                    auto *footer = dialog->findChild<QDialogButtonBox *>();
                    if (scroll) { scroll->ensureWidgetVisible(privSecret); privSecret->setFocus(); QCoreApplication::processEvents(); }
                    check(scroll && footer && dialog->width() <= 640 && dialog->height() <= 480 &&
                          dialog->rect().contains(footer->mapTo(dialog, footer->rect().bottomRight())) &&
                          scroll->viewport()->rect().intersects(QRect(privSecret->mapTo(scroll->viewport(), QPoint()), privSecret->size())),
                          "dialog-polish: compact Credentials keeps privacy fields reachable and footer visible");
                    check(!dialog->findChild<QPushButton *>("CredentialDelete")->autoDefault() &&
                          !dialog->findChild<QPushButton *>("CredentialDuplicate")->autoDefault(),
                          "dialog-polish: list actions cannot become the credential dialog default");
                    dialog->grab().save(QDir(smoke_config_dir).filePath("dialog-credentials-compact.png"));
                    dialog->resize(previousSize); dialog->setVisible(wasVisible);

                    // Removing a secondary variant must also discard its learned notification keys.
                    auto *notificationUsm = snmpb.AgentObj()->GetUSMObj();
                    const OctetStr testEngine("credential-deletion-test-engine");
                    const auto secret = [](const CredentialSecret &value) {
                        return OctetStr(reinterpret_cast<const unsigned char *>(value.bytes().constData()), value.bytes().size());
                    };
                    check(notificationUsm->add_usm_user(OctetStr("shared-user"), OctetStr("shared-user"),
                              b->authProtocol, b->privacyProtocol, secret(b->authSecret), secret(b->privacySecret), testEngine) == SNMPv3_USM_OK,
                          "secondary credential notification keys can be learned");
                    for (int row = 0; row < list->count(); ++row)
                        if (list->item(row)->data(Qt::UserRole).toString() == second) { list->setCurrentRow(row); break; }
                    dialog->findChild<QPushButton *>("CredentialDelete")->click();
                    check(dialog->applyChanges() && !snmpb.UsmCredentials()->find(second), "secondary credential deletion commits");
                    auto *remaining = notificationUsm->get_user(testEngine, OctetStr("shared-user"));
                    check(remaining && remaining->authProtocol == SNMP_AUTHPROTOCOL_HMACSHA &&
                          remaining->privProtocol == SNMP_PRIVPROTOCOL_NONE,
                          "deleting a secondary variant removes its learned notification keys");
                    notificationUsm->free_user(remaining); added.removeAll(second);
                    // Restore the fixture through the same Delete and OK workflow.
                    for (const auto &id : added) {
                        for (int row = 0; row < list->count(); ++row)
                            if (list->item(row)->data(Qt::UserRole).toString() == id) { list->setCurrentRow(row); break; }
                        if (auto *remove = dialog->findChild<QPushButton *>("CredentialDelete")) remove->click();
                    }
                    check(dialog->applyChanges() && snmpb.CommunityCredentials()->records() == communitiesBefore &&
                          snmpb.UsmCredentials()->records() == usersBefore, "Delete removes only the selected credential entries");
                }
            } else check(false, "the Credentials screen exists");
            // Deleting the chosen credential must not silently switch to another one.
            if (QComboBox *credentialBox = mw.findChild<QComboBox *>("ConnectionCredential")) {
                CommunityCredentialRecord doomed;
                doomed.displayName = "Doomed";
                doomed.readCommunity = CredentialSecret("x");
                const QString doomedId = snmpb.CommunityCredentials()->create(doomed);
                credentialBox->setCurrentIndex(credentialBox->findText("Doomed - SNMPv2c"));
                check(credentialBox->currentText() == "Doomed - SNMPv2c", "doomed credential selected");
                snmpb.CommunityCredentials()->remove(doomedId);
                check(credentialBox->currentIndex() == -1 && credentialBox->currentData().toString().isEmpty(),
                      "deleting the chosen credential leaves nothing chosen instead of switching to another");
            }
            // Edit -> Save -> Rebuild: fix the opened MIB in the editor, save it, and check that the
            // active profile rebuilds from the saved file and the problem goes away.
            if (!smoke_edit_from.isEmpty() && !smoke_mib_file.isEmpty()) {
                const auto writeEnvironment = [&](const char *tag) {
                    if (const MibEnvironmentPtr now = snmpb.MibModuleObj()->CurrentEnvironment()) {
                        const MibProblemSummary summary = MibSummarizeProblems(now->problems());
                        smoke_output << "launch-smoke: " << tag << " ENVIRONMENT loaded=" << now->loadedCount()
                                     << " failed=" << now->failedCount() << " errors=" << summary.errors
                                     << " warnings=" << summary.warnings << " notes=" << summary.notes << Qt::endl;
                        for (const MibProblem &problem : now->problems())
                            smoke_output << "launch-smoke: " << tag << " PROBLEM severity="
                                         << MibProblemSeverityText(problem.severity) << " category=\""
                                         << problem.category << "\" module=" << problem.module
                                         << " line=" << problem.line << Qt::endl;
                    }
                };
                writeEnvironment("BEFORE-EDIT");
                {
                    // The editor's own diagnostics must name the real file, never the staging copy.
                    QAbstractItemModel *shown = snmpb.MainUI()->MIBLog->model();
                    bool stagingNameShown = false;
                    for (int row = 0; row < shown->rowCount(); ++row)
                        for (int column = 0; column < shown->columnCount(); ++column)
                            stagingNameShown = stagingNameShown ||
                                shown->index(row, column).data().toString().contains("mib-navigator-verify-");
                    check(shown->rowCount() > 0, "edit: the editor listed the syntax error before the fix");
                    check(!stagingNameShown, "edit: editor diagnostics never show the internal staging file name");
                }
                QTextEdit *editorText = snmpb.MainUI()->MIBFile;
                QString content = editorText->toPlainText();
                check(content.contains(smoke_edit_from), "edit: the text to fix is present in the editor");
                content.replace(smoke_edit_from, smoke_edit_to);
                editorText->setPlainText(content);

                QEventLoop rebuildWait;
                QTimer rebuildTimeout; rebuildTimeout.setSingleShot(true);
                bool rebuilt = false;
                QObject::connect(&rebuildTimeout, &QTimer::timeout, &rebuildWait, &QEventLoop::quit);
                const QMetaObject::Connection readyConnection = QObject::connect(
                    snmpb.MibModuleObj(), &MibModule::profileRuntimeReady, &rebuildWait,
                    [&](const QString &, const MibEffectivePlan &, MibEnvironmentPtr, const QStringList &, bool, bool) {
                        rebuilt = true; rebuildWait.quit();
                    });
                rebuildTimeout.start(40000);
                snmpb.MibEditorObj()->MibFileSave();
                if (!rebuilt) rebuildWait.exec();
                QObject::disconnect(readyConnection);
                check(rebuilt, "edit: saving rebuilt the active profile's environment");
                check(snmpb.MainUI()->MIBLog->model()->rowCount() == 0 &&
                      snmpb.MainUI()->MIBLogL->text().startsWith("Saved."),
                      "edit: the editor does not keep showing the pre-save verification result");
                writeEnvironment("AFTER-EDIT");

                snmpb.MainUI()->MIBTree->SelectFromOid("1.3.6.1.4.1.90002.1");
                check(snmpb.MainUI()->MIBTree->currentIndex().isValid(),
                      "edit: the object defined by the fixed MIB is now in the Tree");
                QFile savedFile(smoke_mib_file);
                QString savedHash;
                if (savedFile.open(QIODevice::ReadOnly))
                    savedHash = QString::fromLatin1(QCryptographicHash::hash(
                        savedFile.readAll(), QCryptographicHash::Sha256).toHex());
                bool hashRecorded = false;
                const QString savedCanonical = QFileInfo(smoke_mib_file).canonicalFilePath();
                for (const MibProfileMember &root : snmpb.MibLibraryObj()->currentProfile().roots)
                    if (QFileInfo(root.canonicalPath).canonicalFilePath() == savedCanonical)
                        hashRecorded = (root.sha256 == savedHash) && !savedHash.isEmpty();
                check(hashRecorded, "edit: the profile records the saved file's new hash (no Modified note)");
                if (QTableWidget *table = mw.findChild<QTableWidget *>("WorkbenchProblemTable")) {
                    QCoreApplication::processEvents();
                    mw.grab().save(QDir(smoke_config_dir).filePath("problems-tab-after-edit.png"));
                    smoke_output << "launch-smoke: AFTER-EDIT problems-table-rows=" << table->rowCount() << Qt::endl;
                }
                QFile diagnosticLog(DiagnosticLogger::logFilePath());
                check(diagnosticLog.open(QIODevice::ReadOnly) &&
                      diagnosticLog.readAll().contains("rebuilding"),
                      "edit: the diagnostic log records the save as rebuilding the active profile");
            }
            if (!smoke_mib_file.isEmpty()) {
                bool deliveredAfterStop = false;
                QObject completionContext;
                QObject::connect(snmpb.MibModuleObj(), &MibModule::editorVerificationFinished,
                    &completionContext, [&] { deliveredAfterStop = true; });
                MibEngine::instance().submit([] {
                    std::promise<void> gate;
                    gate.get_future().wait_for(std::chrono::milliseconds(100));
                });
                snmpb.MibEditorObj()->VerifyMIB();
                check(snmpb.MibEditorObj()->VerificationRunning(),
                    "Editor Verify can be queued immediately before shutdown");
                snmpb.Shutdown();
                QCoreApplication::processEvents();
                check(!deliveredAfterStop && !snmpb.MibModuleObj()->VerifyEditorSource(
                    "ignored", {}, MibValidationLevel::Errors),
                    "shutdown drains Verify safely and ignores late completion/new work");
            }
            mw.close();
            smoke_log.close();
            app.exit(passed ? 0 : 1);
        };
        if (snmpb.MibModuleObj()->CurrentEnvironment())
            QTimer::singleShot(0, &app, runSmoke);
        else {
            QObject::connect(snmpb.MibModuleObj(), &MibModule::profileRuntimeReady, &app,
                [runSmoke](const QString &,const MibEffectivePlan &,MibEnvironmentPtr,
                           const QStringList &,bool,bool){runSmoke();},Qt::SingleShotConnection);
            QObject::connect(snmpb.MibModuleObj(), &MibModule::profileRuntimeFailed, &app,
                [&app, smoke_config_dir](const QString &profileId, const QString &error) {
                    QFile smokeLog(QDir(smoke_config_dir).filePath("launch-smoke.log"));
                    if (smokeLog.open(QIODevice::WriteOnly | QIODevice::Text)) {
                        QTextStream output(&smokeLog);
                        output << "launch-smoke: FAIL: Profile Environment build failed for "
                               << profileId << ": " << error << Qt::endl;
                    }
                    app.exit(1);
                }, Qt::SingleShotConnection);
        }
    }

    DiagnosticLogger::log("Startup", "event-loop entry");
    const int result = app.exec();
    DiagnosticLogger::log("Shutdown",
                          QStringLiteral("event loop returned code=%1").arg(result), false);
    DiagnosticLogger::shutdown();
    return result;
}
