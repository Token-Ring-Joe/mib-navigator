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
#include <QDockWidget>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QTextStream>
#include <QTimer>
#include <QElapsedTimer>
#include <QAction>
#include <QTabBar>
#include <QTabWidget>
#include "snmpb.h"
#include "agentprofile.h"
#include "mibeditor.h"
#include "mibview.h"
#include "mibmodule.h"
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
#include "miblibrarywidget.h"
#include <QComboBox>
#include <QToolButton>
#include "agent.h"
#include <QCryptographicHash>
#include "communitycredentialservice.h"
#include <QTextEdit>
#include <QEventLoop>

QString file_to_open;

int main( int argc, char ** argv )
{
    bool launch_smoke_test = false;
    QString smoke_config_dir;
    QString smoke_mib_file;
    bool smoke_network = false;
    QString smoke_live_target;
    QString smoke_live_credential;
    QString smoke_live_walk;
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

    if (launch_smoke_test)
    {
        QDir().mkpath(smoke_config_dir);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                           smoke_config_dir);
    }

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
            QSettings settings;
            check(QFileInfo(settings.fileName()).absoluteFilePath().startsWith(
                      QFileInfo(smoke_config_dir).absoluteFilePath()),
                  "QSettings uses the isolated directory");
            check(mw.isVisible(), "main window is visible");
            check(mw.findChild<QWidget *>("WorkbenchActivityRail") &&
                  mw.findChild<QWidget *>("WorkbenchSidebar"),
                  "workbench activity rail and sidebar created");
            check(mw.findChild<QDockWidget *>("WorkbenchInspectorDock") &&
                  mw.findChild<QDockWidget *>("WorkbenchBottomDock"),
                  "workbench Inspector and bottom panels created");
            QTabWidget *workbenchDocuments = mw.findChild<QTabWidget *>("WorkbenchDocuments");
            QTabWidget *bottomTabs = mw.findChild<QTabWidget *>("WorkbenchBottomTabs");
            check(workbenchDocuments && workbenchDocuments->tabBar()->isHidden(),
                  "legacy primary tab navigation is retired");
            check(bottomTabs && bottomTabs->count() == 3 &&
                  bottomTabs->tabText(0) == "Problems" &&
                  bottomTabs->tabText(1) == "Diagnostics" &&
                  bottomTabs->tabText(2) == "Raw",
                  "bottom Problems, Diagnostics, and Raw views are integrated");
            QAction *eventsActivity = mw.findChild<QAction *>("WorkbenchActivityEvents");
            QAction *mibsActivity = mw.findChild<QAction *>("WorkbenchActivityMIBs");
            if (eventsActivity) eventsActivity->trigger();
            check(eventsActivity && snmpb.MainUI()->TabW->currentWidget() == snmpb.MainUI()->TrapsTab,
                  "Events activity maps to the existing capture workspace");
            if (mibsActivity) mibsActivity->trigger();
            check(mibsActivity && snmpb.MainUI()->TabW->currentWidget() == snmpb.MainUI()->TreeTab,
                  "MIBs activity restores the Tree workflow");
            check(snmpb.MibModuleObj()->CurrentEnvironment() &&
                  !snmpb.MibModuleObj()->CurrentEnvironment()->modules().isEmpty(),
                  "Profile Environment materialized bundled MIB modules");
            check(snmpb.MainUI()->MIBTree->model()->rowCount() > 0,
                  "MIB tree populated");

            snmpb.MainUI()->MIBTree->SelectFromOid("1.3.6.1.2.1.1");
            check(snmpb.MainUI()->MIBTree->currentIndex().isValid(),
                  "MIB OID selection works");
            check(!snmpb.MainUI()->NodeInfo->toPlainText().isEmpty(),
                  "MIB inspector follows Tree selection");

            if (!smoke_mib_file.isEmpty())
            {
                snmpb.MibEditorObj()->MibFileOpen(smoke_mib_file);
                snmpb.ShowEditorDocument();
                snmpb.MibEditorObj()->VerifyMIB();
                check(!snmpb.MainUI()->MIBFile->toPlainText().isEmpty(),
                      "MIB editor opened and verified a local module");
                check(snmpb.MainUI()->MIBLogL->text().startsWith("Verification complete."),
                      "MIB editor parsing completed");
                QTabBar *documentTabs = mw.findChild<QTabBar *>("WorkbenchOpenDocuments");
                check(snmpb.MainUI()->TabW->currentWidget() == snmpb.MainUI()->EditorTab &&
                      documentTabs && documentTabs->count() == 1,
                      "MIB editor opens as a contextual document");
            }

            const QString smoke_profile = "ctest-local-profile";
            snmpb.APManagerObj()->Add(smoke_profile, "127.0.0.1", "161",
                                     true, false, false, "localhost");
            check(snmpb.APManagerObj()->GetAgentProfileRecordByName(smoke_profile) != nullptr,
                  "Agent Profile load/save works");

            QTimer dialog_closer;
            QObject::connect(&dialog_closer, &QTimer::timeout,
                             close_visible_dialogs);
            dialog_closer.start(10);
            snmpb.ManageAgentProfiles(false);
            dialog_closer.stop();
            check(QFileInfo::exists(snmpb.GetAgentsConfigFile()),
                  "Agent Profile Manager opened and persisted configuration");

            snmpb.PreferencesObj()->Save();
            settings.sync();
            check(settings.contains("network/enableipv4") &&
                  settings.contains("mibpaths/size"),
                  "preferences saved to isolated configuration");

            QTranslator translation_probe;
            check(translation_probe.load(":/i18n/snmpb.uk_UA"),
                  "bundled application translation loads");
            check(QFileInfo::exists(snmpb.GetSmiConfigFile()),
                  "loaded-MIB state file initialized");
            check(QFileInfo::exists(snmpb.GetBootCounterConfigFile()),
                  "SNMPv3 boot counter initialized locally");

            // Pictures of the Problems tab as the application really draws it, so automated
            // validation can look at the layout instead of asking a person to send screenshots.
            if (QTableWidget *table = mw.findChild<QTableWidget *>("WorkbenchProblemTable")) {
                QCheckBox *notes = mw.findChild<QCheckBox *>("WorkbenchProblemShowNotes");
                QPlainTextEdit *detail = mw.findChild<QPlainTextEdit *>("WorkbenchProblemDetail");
                QTabWidget *bottom = mw.findChild<QTabWidget *>("WorkbenchBottomTabs");
                if (bottom) bottom->setCurrentIndex(0);
                mw.resize(1500, 1000);
                QCoreApplication::processEvents();
                mw.grab().save(QDir(smoke_config_dir).filePath("problems-tab.png"));
                if (notes) notes->setChecked(true);
                QCoreApplication::processEvents();
                if (table->rowCount() > 0 && detail) {
                    table->selectRow(0);
                    QCoreApplication::processEvents();
                    check(detail->toPlainText().contains("Why:"),
                          "Selecting a Problems row fills the detail pane");
                }
                mw.grab().save(QDir(smoke_config_dir).filePath("problems-tab-notes.png"));
            }
            // The connection bar: Target, Credential and the (single) MIB Profile selector.
            {
                QWidget *bar = mw.findChild<QWidget *>("ConnectionBar");
                QComboBox *targetBox = mw.findChild<QComboBox *>("ConnectionTarget");
                QComboBox *credentialBox = mw.findChild<QComboBox *>("ConnectionCredential");
                QComboBox *profileBox = mw.findChild<QComboBox *>("MibBrowserProfileSelector");
                check(bar && targetBox && credentialBox && profileBox, "connection bar has Target, Credential and MIB Profile");
                check(bar && profileBox && profileBox->parentWidget() == bar,
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
            // A real request through the connection bar to a loopback-only test agent.
            if (!smoke_live_target.isEmpty()) {
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
                    snmpb.AgentObj()->GetFrom("1.3.6.1.2.1.1.1.0", 0);
                    wait.exec();
                    QLabel *requestStatus = mw.findChild<QLabel *>("StructuredQueryStatus");
                    if (QAction *mibsView = mw.findChild<QAction *>("WorkbenchActivityMIBs")) mibsView->trigger();
                    QCoreApplication::processEvents();
                    smoke_output << "launch-smoke: LIVE-VISIBILITY results=" << results->isVisibleTo(&mw)
                                 << " status=" << (requestStatus && requestStatus->isVisibleTo(&mw))
                                 << " inTreeTab=" << snmpb.MainUI()->TreeTab->isAncestorOf(results)
                                 << " statusInTreeTab=" << (requestStatus && snmpb.MainUI()->TreeTab->isAncestorOf(requestStatus))
                                 << " statusText=" << (requestStatus ? requestStatus->text().isEmpty() : -1) << Qt::endl;
                    check(results->isVisibleTo(&mw) && requestStatus && requestStatus->isVisibleTo(&mw) &&
                          snmpb.MainUI()->TreeTab->isAncestorOf(results),
                          "live: the Results table and its status line are visible under the Tree, not in a hidden panel");
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
                    if (!smoke_live_walk.isEmpty()) {
                        // Walk a subtree through the same bar and count what the Results table receives.
                        QEventLoop walkWait; QTimer walkLimit; walkLimit.setSingleShot(true);
                        QObject::connect(&walkLimit, &QTimer::timeout, &walkWait, &QEventLoop::quit);
                        QTimer walkPoll; QObject::connect(&walkPoll, &QTimer::timeout, &walkWait, [&]() {
                            if (!snmpb.MainUI()->actionStop->isEnabled() && results->rowCount() >= 3) walkWait.quit();
                        });
                        walkPoll.start(100); walkLimit.start(20000);
                        snmpb.AgentObj()->WalkFrom(smoke_live_walk);
                        walkWait.exec();
                        int walkRows = 0; QStringList walked;
                        for (int row = 0; row < results->rowCount(); ++row) {
                            ++walkRows;
                            walked << (results->item(row, 2) ? results->item(row, 2)->text() : QString());
                        }
                        smoke_output << "launch-smoke: LIVE-WALK rows=" << walkRows << " oids=" << walked.join(",")
                                     << " stopEnabled=" << snmpb.MainUI()->actionStop->isEnabled()
                                     << " queryText=\"" << snmpb.MainUI()->Query->toPlainText().left(300).replace('\n', ' ') << "\"" << Qt::endl;
                        smoke_output << "launch-smoke: LIVE-STATUS-AFTER-WALK failed=" << (requestStatus && requestStatus->property("failed").toBool())
                                     << " text=\"" << (requestStatus ? requestStatus->text() : QString()) << "\"" << Qt::endl;
                        check(walkRows >= 3 && !snmpb.MainUI()->actionStop->isEnabled(),
                              "live: Walk lists the subtree and finishes");
                    }
                    if (smoke_live_walk.isEmpty()) {
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
                    }
                    QSettings historySettings;
                    check(historySettings.value("connection/target-history").toString().contains(smoke_live_target),
                          "live: the target was remembered in the recent-target history");
                    check(historySettings.value("connection/target-history").toString().contains("community:"),
                          "live: the history remembers the credential used with the target");
                    if (QAction *mibsActivity = mw.findChild<QAction *>("WorkbenchActivityMIBs")) mibsActivity->trigger();
                    mw.resize(1500, 1000);
                    QCoreApplication::processEvents();
                    mw.grab().save(QDir(smoke_config_dir).filePath("live-get.png"));
                }
            }
            check(snmpb.MainUI()->TrapLog->contextMenuPolicy() == Qt::CustomContextMenu,
                  "the trap list offers a right-click menu (Copy source IP)");
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
