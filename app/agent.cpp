#include "dialoglayout.h"
#include <QRegularExpressionValidator>
#include <QRegularExpression>
#include "queryresultspanel.h"
#include "querymacrorunner.h"
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
#include <qmessagebox.h>
#include <algorithm>
#include <QColor>
#include <QDate>
#include <QLineEdit>
#include <QDialog>
#include <QEventLoop>
#include <QSettings>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <QDateTime>
#include <QFontDatabase>
#include <QTableWidget>
#include <QTableWidgetItem>

#include "mibview.h"
#include "agent.h"
#include "tableindex.h"
#include "communitycredentialservice.h"
#include "connectionbar.h"
#include "diagnosticlogger.h"
#include "udpportowner.h"
#include "preferencesettings.h"
#include "mibmodule.h"
#include "snmp_pp/notifyqueue.h"
#include "preferences.h"
#include "snmprequestconfigadapter.h"
#include "tablenodevalidation.h"
#include "tabletraversal.h"
#include "mibenvironmentregistry.h"
#include "mibselection.h"
#include "mibvaluesemantics.h"
#include "snmpreport.h"
#include "snmpquerytext.h"
#include "requestlog.h"
#include <QLabel>
#include <QListWidget>
#include <QStatusBar>
#include <QStyle>
#include <QTextDocumentFragment>

#define TRAP_TIMER_MSEC 100

typedef struct
{
    Vb vb;
    int syntax;
    QString val;
} vb_data;

namespace {
QTableWidget *structuredResults(Snmpb *snmpb)
{
    return snmpb->MainUI()->Query->window()->findChild<QTableWidget *>(
        QStringLiteral("StructuredQueryResults"));
}

// One line above the Results table saying how the last request ended. Errors (timeouts,
// refused SNMPv3 requests, ...) used to appear only on the Raw tab, so a failed request
// looked like nothing had happened.
// Progress/selection notices must not finish a query or claim that an empty reply arrived.
void presentRequestStatus(Snmpb *snmpb, const QString &html, int objects, bool completed = true)
{
    auto *label = snmpb->MainUI()->Query->window()->findChild<QLabel *>(
        QStringLiteral("StructuredQueryStatus"));
    if (!label) return;
    const bool error = html.contains(QStringLiteral("color=red"));
    QString text = QTextDocumentFragment::fromHtml(html).toPlainText().simplified();
    text.remove(QStringLiteral("-----"));
    text = text.simplified();
    if (completed && !error && objects == 0)
        text += QObject::tr(" The device returned no objects.");
    label->setText(text);
    label->setProperty("failed", error);
    label->setToolTip(text);
    label->style()->unpolish(label);
    label->style()->polish(label);
    label->setVisible(!completed || error || objects == 0 || !snmpb->Results() || !snmpb->Results()->isTableResult());
    if (completed && snmpb->Results()) snmpb->Results()->finishQuery();
}

void clearStructuredResults(Snmpb *snmpb)
{
    if (snmpb->Results()) snmpb->Results()->beginQuery(QString(), QString());
}

QueryResultValue resultValue(const MibResolvedObject &resolved, const Oid &oid, const Vb &vb)
{
    QueryResultValue value;
    value.oid = QString::fromLatin1(oid.get_printable());
    value.object = resolved.node && !resolved.node->qualifiedName.isEmpty() ? resolved.node->qualifiedName
        : resolved.node ? resolved.node->name : value.oid;
    value.instance = resolved.instanceSuffix.join('.');
    value.syntax = resolved.node ? (!resolved.node->syntaxName.isEmpty() ? resolved.node->syntaxName : resolved.node->typeId)
        : SnmpSyntaxName(vb.get_syntax());
    const MibValueCheck check = CheckMibValue(resolved, vb);
    value.type = check.receivedType; value.raw = check.rawValue;
    value.interpreted = RenderMibValue(resolved, vb);
    value.received = check.rawValue == check.receivedType ? check.receivedType
        : QStringLiteral("%1: %2").arg(check.receivedType, check.rawValue);
    value.warnings = check.warnings.join(QStringLiteral("  "));
    value.time = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz"));
    return value;
}
void appendStructuredResult(Snmpb *snmpb, const MibResolvedObject &resolved, const Oid &oid, const Vb &vb)
{
    if (snmpb->Results()) snmpb->Results()->appendValue(resultValue(resolved, oid, vb));
}

}

Q_DECLARE_METATYPE(vb_data);

// C Callback functions for snmp++
void callback_trap(int reason, Snmp *, Pdu &pdu, SnmpTarget &target, void *cd)
{
    if (cd)
    {
        // just call the real callback member function...
        ((Agent*)cd)->AsyncCallbackTrap(reason, pdu, target);
    }
}

Agent::Agent(Snmpb *snmpb, bool offline_mode)
{
    s = snmpb;
    offline = offline_mode;
    snmp = NULL;
    tableRunner = new SnmpTableAsyncRunner(this);
    connect(tableRunner, &SnmpTableAsyncRunner::completed,
            this, &Agent::PresentTableResult);
    instanceRunner = new SnmpInstanceAsyncRunner(this);
    connect(instanceRunner, &SnmpInstanceAsyncRunner::completed,
            this, &Agent::PresentInstanceResult);
    singleRunner = new SnmpSingleAsyncRunner(this);
    connect(singleRunner, &SnmpSingleAsyncRunner::completed,
            this, &Agent::PresentSingleResult);
    macroRunner = new QueryMacroRunner(this);
    macroRunner->setObjectName("QueryMacroRunner");
    connect(this, &Agent::StartWalk, this, &Agent::foregroundBusyChanged);
    connect(macroRunner, &QueryMacroRunner::started, this, [this] {
        const auto &run = macroRunner->snapshot();
        lastTable.clear();
        if (s->Results()) s->Results()->beginGroups(tr("Macro: %1").arg(run.name),
            tr("Target: %1  |  Credential: %2  |  MIB Profile: %3  |  Started: %4")
                .arg(run.target, run.credential, run.profile, run.started.toString(Qt::ISODateWithMs)));
        s->MainUI()->Query->append(tr("Macro started: %1").arg(run.name).toHtmlEscaped());
        stop = false; s->MainUI()->actionStop->setEnabled(true); emit StartWalk(true);
    });
    connect(macroRunner, &QueryMacroRunner::requestStarted, this, [this](const QStringList &oids) {
        if (!requestLog) return;
        const auto &run = macroRunner->snapshot();
        activeLogId = requestLog->begin(tr("Macro %1 / Objects and values / GET").arg(run.name),
            run.target, run.credential, oids.join(QStringLiteral(", ")));
    });
    connect(macroRunner, &QueryMacroRunner::requestFinished, this,
        [this](const QString &detail, int count, bool failed, bool timeout, bool refused) {
            if (requestLog && activeLogId) requestLog->finish(activeLogId, detail.toHtmlEscaped(), count,
                refused ? RequestOutcome::Refused : timeout ? RequestOutcome::TimedOut
                : failed ? RequestOutcome::Failed : RequestOutcome::Succeeded);
            activeLogId = 0;
        });
    connect(macroRunner, &QueryMacroRunner::changed, this, &Agent::PresentMacroResult);
    connect(macroRunner, &QueryMacroRunner::completed, this, [this] {
        if (s->Results()) s->Results()->finishQuery();
        const auto &run = macroRunner->snapshot();
        s->MainUI()->Query->append(run.summary().toHtmlEscaped());
        activeRequestContext.reset(); s->MainUI()->actionStop->setEnabled(false); emit StartWalk(false);
    });
    walkRunner = new SnmpWalkAsyncRunner(this);
    connect(walkRunner, &SnmpWalkAsyncRunner::progress,
            this, &Agent::PresentWalkBatch);
    connect(walkRunner, &SnmpWalkAsyncRunner::completed,
            this, &Agent::PresentWalkResult);

    int status, status2;

    Snmp::socket_startup();  // Initialize socket subsystem

    bool v4 = s->PreferencesObj()->GetEnableIPv4();
    bool v6 = s->PreferencesObj()->GetEnableIPv6();
    int port4 = s->PreferencesObj()->GetTrapPort4();
    int port6 = s->PreferencesObj()->GetTrapPort6();
    DiagnosticLogger::log("Traps", QStringLiteral(
        "trap bind configuration IPv4=%1 address=0.0.0.0 port=%2 IPv6=%3 address=:: port=%4")
        .arg(v4).arg(port4).arg(v6).arg(port6));

    start_err = ""; 
    start_result = true;

    if (offline)
        return;

    // Create our SNMP session object
    if (v4 && v6)
    {
        snmp = new Snmp(status, UdpAddress("0.0.0.0"), UdpAddress("::"));
        if (status != SNMP_CLASS_SUCCESS)
        {
            status2 = status;
            // Try dropping IPv6; the failed session is still owned here.
            delete snmp;
            snmp = new Snmp(status, UdpAddress("0.0.0.0"));
            if (status != SNMP_CLASS_SUCCESS)
            {
                start_err = tr("Could not create IPv4 session.\n%1")
                                    .arg(Snmp::error_msg(status));
                // Disable IPv4, for the current run only
                s->PreferencesObj()->SetEnableIPv4(false);
                // Try dropping IPv4.
                delete snmp;
                snmp = new Snmp(status, UdpAddress("::"));
                if (status != SNMP_CLASS_SUCCESS)
                {
                    start_err = tr("Could not create IPv4 and IPv6 sessions.\n%1\nAborting.")
                                        .arg(Snmp::error_msg(status));
                    start_result = false;
                    return;
                }
            }
            else
            {
                start_err = tr("Could not create IPv6 session.\n%1")
                                    .arg(Snmp::error_msg(status2));
                // Disable IPv6, for the current run only
                s->PreferencesObj()->SetEnableIPv6(false);
            }
        }
    }
    else if (v4)
    {
        snmp = new Snmp(status, UdpAddress("0.0.0.0"));
        if (status != SNMP_CLASS_SUCCESS)
        {
            start_err = tr("Could not create IPv4 session.\n%1\nAborting.")
                                .arg(Snmp::error_msg(status));
            start_result = false;
            return;
        }
    }
    else if (v6)
    {
        snmp = new Snmp(status, UdpAddress("::"));
        if (status != SNMP_CLASS_SUCCESS)
        {
            start_err = tr("Could not create IPv6 session.\n%1\nAborting.")
                                .arg(Snmp::error_msg(status));
            start_result = false;
            return;
        }
    }
    else
    {
        start_err = tr("No transport protocol enabled. Aborting.");
        start_result = false;
        return;
    }

    // Bind on the SNMP trap ports
    snmp->notify_set_listen_port(port4);
    snmp->notify_set_listen_port6(port6);

    OidCollection oidc;
    TargetCollection targetc;

    status = snmp->notify_register(oidc, targetc, callback_trap, this);
    if (status != SNMP_CLASS_SUCCESS)
    {
        QString diagnostics;
        if (status == SNMP_CLASS_TL_IN_USE) {
            const UdpPortOwner owner4 = v4 ?
                UdpPortOwnerLookup::lookup(port4, false) : UdpPortOwner{};
            const UdpPortOwner owner6 = v6 ?
                UdpPortOwnerLookup::lookup(port6, true) : UdpPortOwner{};
            if (v4)
                diagnostics += UdpPortOwnerLookup::conflictDescription(port4, owner4);
            if (v6 && (port6 != port4 || !owner4.found)) {
                if (!diagnostics.isEmpty()) diagnostics += QStringLiteral("\n");
                diagnostics += UdpPortOwnerLookup::conflictDescription(port6, owner6);
            }
        } else {
            diagnostics = tr("Native socket error details are not exposed by this SNMP++ operation.");
        }
        if (status == SNMP_CLASS_TL_ACCESS_DENIED) {
            start_err = tr("Trap reception is off for this session.\n\n"
                "Port %1 is a privileged port, and MIB Navigator is not running with "
                "administrator rights. Everything else works normally.\n\n"
                "To receive traps without administrator rights, open Preferences > "
                "Traps Properties > Trap listening ports, choose a port above 1024 "
                "(for example 1162), restart, and send traps to that port.")
                .arg(v4 ? port4 : port6);
        } else {
            start_err = tr("Trap reception is off for this session.\n\n"
                "Could not bind IPv4 trap port %1 or IPv6 trap port %2.\n%3\n\n%4")
                .arg(port4).arg(port6)
                .arg(QString::fromLocal8Bit(Snmp::error_msg(status)), diagnostics);
        }
        DiagnosticLogger::log("Traps", QStringLiteral(
            "trap bind failed SNMP++ status=%1 error=%2; %3; trap reception disabled")
            .arg(status).arg(QString::fromLocal8Bit(Snmp::error_msg(status)), diagnostics));
    } else {
        DiagnosticLogger::log("Traps", "IPv4/IPv6 trap bind registration succeeded");
    }
}

bool Agent::GetStartupResult(QString &err)
{
    err = start_err;
    return start_result;
}

void Agent::StartTrapTimer(void)
{
    if (offline)
        return;

    // Start the timer
    timer.start(TRAP_TIMER_MSEC);
}

void Agent::StopTimer(void)
{
    // Stop the timer
    timer.stop();
}

void Agent::Init(void)
{
    int status;

    // Connect some signals
    connect( s->MainUI()->MIBTree, SIGNAL( WalkFromOid(const QString&) ),
             this, SLOT( WalkFrom(const QString&) ) );
    connect( s->MainUI()->MIBTree, SIGNAL( GetFromOid(const QString&, int) ),
             this, SLOT( GetFrom(const QString&, int) ) );
    connect( s->MainUI()->MIBTree, SIGNAL( GetFromOidPromptInstance(const QString&, int) ),
             this, SLOT( GetFromPromptInstance(const QString&, int) ) );
    connect( s->MainUI()->MIBTree, SIGNAL( GetFromOidSelectInstance(const QString&, int) ),
             this, SLOT( GetFromSelectInstance(const QString&, int) ) );
    connect( s->MainUI()->MIBTree, SIGNAL( SetFromOid(const QString&) ),
             this, SLOT( SetFrom(const QString&) ) );
    connect( s->MainUI()->MIBTree, SIGNAL( Stop() ),
             this, SLOT( Stop() ));
    connect(s->MainUI()->MIBTree, &MibModelView::TableRowSelectFromOid,
            this, &Agent::TableRowSelectFrom);
    connect( s->MainUI()->MIBTree, SIGNAL( TableViewFromOid(const QString&) ),
             this, SLOT( TableViewFrom(const QString&) ) );
    connect(s->MainUI()->QueryTable, &QPushButton::clicked,
            s->MainUI()->MIBTree, &MibModelView::QueryTableFromCurrent);
    connect(s->MainUI()->MIBTree, &MibModelView::QueryTableAvailabilityChanged,
            s->MainUI()->QueryTable, &QPushButton::setEnabled);
    connect( s->MainUI()->MIBTree, SIGNAL( VarbindsFromOid(const QString&) ),
             this, SLOT( VarbindsFrom(const QString&) ) );
    connect( this, SIGNAL( StartWalk(bool) ), 
             s->MainUI()->MIBTree, SLOT ( SetWalkInProgress(bool) ) );
    connect( s->MainUI()->actionStop, SIGNAL( triggered() ),
             this, SLOT( Stop() ) );
    connect( s->MainUI()->actionMultipleVarbinds, SIGNAL( triggered() ),
             this, SLOT( Varbinds() ) );

    // The connection bar (Target, Credential, MIB Profile) supplies the request settings;
    // it announces itself through SetConnectionBar().
    UpdateTableQueryAvailability();

    vbui = new Ui_Varbinds();
    vbd = new QDialog(s->MainUI()->MIBTree); 
    vbui->setupUi(vbd);
    connect( vbui->NewOp, SIGNAL( clicked() ), this, SLOT( VarbindsNew() ));
    connect( vbui->EditOp, SIGNAL( clicked() ), this, SLOT( VarbindsEdit() ));
    connect( vbui->DeleteOp, SIGNAL( clicked() ), this, SLOT( VarbindsDelete() ));
    connect( vbui->DeleteAllOp, SIGNAL( clicked() ), this, SLOT( VarbindsDeleteAll() ));
    connect( vbui->MoveUpOp, SIGNAL( clicked() ), this, SLOT( VarbindsMoveUp() ));
    connect( vbui->MoveDownOp, SIGNAL( clicked() ), this, SLOT( VarbindsMoveDown() ));
    connect( vbui->QuitOp, SIGNAL( clicked() ), this, SLOT( VarbindsQuit() ));
    connect( vbui->GetOp, SIGNAL( clicked() ), this, SLOT( VarbindsGet() ));
    connect( vbui->GetNextOp, SIGNAL( clicked() ), this, SLOT( VarbindsGetNext() ));
    connect( vbui->GetBulkOp, SIGNAL( clicked() ), this, SLOT( VarbindsGetBulk() ));
    connect( vbui->SetOp, SIGNAL( clicked() ), this, SLOT( VarbindsSet() ));
    connect( vbui->VarbindsList, SIGNAL( itemSelectionChanged() ), 
             this, SLOT( VarbindsSelected() ));
    connect( vbui->VarbindsList, SIGNAL( itemDoubleClicked(QTreeWidgetItem*, int) ), 
             this, SLOT( VarbindsEdit() ));

    connect(&timer, SIGNAL(timeout()), this, SLOT(TimerExpired()));
    
    // get the Boot counter (you may use any own method for this)
    char *engineId = (char*)"SnmpB_engine";
    unsigned int snmpEngineBoots = 0;

    status = getBootCounter(s->GetBootCounterConfigFile().toLatin1().data(), 
                            engineId, snmpEngineBoots);
    if ((status != SNMPv3_OK) && (status < SNMPv3_FILEOPEN_ERROR))
    {
        QString err = tr("Error loading snmpEngineBoots counter: %1\n")
                              .arg(status);
        QMessageBox::warning ( NULL, "MIB Navigator", err,
                               QMessageBox::Ok);
    }
    
    // increase the boot counter
    snmpEngineBoots++;
    
    // save the boot counter
    status = saveBootCounter(s->GetBootCounterConfigFile().toLatin1().data(), 
                             engineId, snmpEngineBoots);
    if (status != SNMPv3_OK)
    {
        QString err = tr("Error saving snmpEngineBoots counter: %1\n")
                              .arg(status);
        QMessageBox::warning ( NULL, "MIB Navigator", err,
                               QMessageBox::Ok);
    }
    
    // If _SNMPv3 is enabled we MUST create ONE v3MP object!
    v3mp = new v3MP(engineId, snmpEngineBoots, status);
    if (status != SNMPv3_MP_OK)
    {
        QString err = tr("Could not create v3MP object:\n%1")
                              .arg(QString::fromLatin1(Snmp::error_msg(status)));
        QMessageBox::warning ( NULL, "MIB Navigator", err,
                               QMessageBox::Ok);
    }
    
    // The v3MP creates a USM object, get the pointer to it
    // (null when the v3MP could not be created, e.g. a second v3MP in the same process)
    USM *usm = v3mp->get_usm();
    
    // Load the USM users from a file, if any
    if (usm)
        usm->load_users(s->GetUsmUsersConfigFile().toLatin1().data());
}

void Agent::UpdateTableQueryAvailability()
{
    AgentRequestSelection selection;
    s->MainUI()->MIBTree->SetQueryPrerequisitesAvailable(
        ResolveCurrentSelection(&selection) == AgentSelectionError::None);
}

void Agent::SetConnectionBar(ConnectionBar *bar)
{
    connectionBar = bar;
    if (!bar) return;
    connect(bar, &ConnectionBar::selectionChanged, this,
            &Agent::ConnectionSelectionChanged);
    ConnectionSelectionChanged();
}

void Agent::ConnectionSelectionChanged()
{
    if (!connectionBar) return;
    s->MainUI()->MIBTree->SetCurrentAgentIsV1(connectionBar->credentialProtocol() == 0);
    UpdateTableQueryAvailability();
}

bool Agent::ResolveForRequest(AgentRequestSelection *selection)
{
    if (ResolveCurrentSelection(selection) == AgentSelectionError::None)
        return true;
    if (connectionBar && lastConnectionError != ConnectionError::None)
        QMessageBox::warning(nullptr, tr("MIB Navigator"),
                             ConnectionModel::errorText(lastConnectionError), QMessageBox::Ok);
    return false;
}

AgentSelectionError Agent::ResolveCurrentSelection(
    AgentRequestSelection *selection) const
{
    if (connectionBar)
    {
        QSettings settings;
        lastConnectionError = ConnectionModel::build(
            connectionBar->target(), connectionBar->credentialKey(),
            s->CommunityCredentials(), s->UsmCredentials(),
            PreferencesSettings::load(settings), selection);
        return lastConnectionError == ConnectionError::None
            ? AgentSelectionError::None : AgentSelectionError::ProfileNotFound;
    }
    // No connection bar yet (start-up): nothing can be requested.
    lastConnectionError = ConnectionError::NoTarget;
    return AgentSelectionError::ProfileNotFound;
}

Agent::~Agent()
{
    Shutdown();
    tableRunner->cancel();
    tableRunner->wait();
    instanceRunner->cancel();
    instanceRunner->wait();
    // A walk is only checked for cancellation between requests, so a request still waiting for
    // an answer could hold shutdown for its whole timeout. The runner waits a bounded time
    // (SnmpWalkAsyncRunner::kShutdownWaitMs) and then leaves the worker to finish by itself.
    delete walkRunner;
    walkRunner = nullptr;
    delete singleRunner;
    singleRunner = nullptr;
    delete macroRunner;
    macroRunner = nullptr;
    delete snmp;
    snmp = nullptr;
    delete vbui; // Generated Ui wrapper; the dialog owns its widgets.
    vbui = nullptr;
    Snmp::socket_cleanup();
}

void Agent::Shutdown()
{
    if (shutdownComplete) return;
    shutdownComplete = true;
    DiagnosticLogger::log("Traps", "Agent cooperative shutdown begin", false);
    timer.stop();
    if (snmp) snmp->notify_unregister();
    if (tableRunner) tableRunner->cancel();
    if (instanceRunner) instanceRunner->cancel();
    if (walkRunner) walkRunner->cancel();
    if (macroRunner) macroRunner->shutdown();
    DiagnosticLogger::log("Traps", "Agent cooperative shutdown end", false);
}

int Agent::SetupFromCurrentSelection(const QString& oid, SnmpTarget **t,
                                     Pdu **p, bool usevblist,
                                     SnmpRequestConfig *resolvedConfig)
{
    AgentRequestSelection selection;
    if (!ResolveForRequest(&selection))
        return -1;
    const int result = Setup(selection, oid, t, p, usevblist, resolvedConfig);
    if (result == 0 && connectionBar)
        connectionBar->rememberCurrentTarget();
    return result;
}

int Agent::Setup(const AgentRequestSelection &selection, const QString& oid,
                 SnmpTarget **t, Pdu **p, bool usevblist,
                 SnmpRequestConfig *resolvedConfig)
{
    if (!snmp)
        return -1;

    SnmpRequestConfig config;
    if (!selection.requestConfig(&config))
        return -1;
    if (resolvedConfig)
        *resolvedConfig = config;
    
    // Create an address object from the entered values
    UdpAddress address(config.endpoint().toLatin1().data());
    
    // check if the address is valid
    // One problem here: if a hostname is entered, a blocking DNS lookup
    // is done by the address object.
    if (!address.valid())
    {
        QString err = tr("Invalid Address or DNS Name: %1\n")
                              .arg(config.address);
        QMessageBox::warning ( NULL, "MIB Navigator", err,
                               QMessageBox::Ok);
        return -1;
    }

    Pdu *pdu = new Pdu();

    if (config.version == SnmpRequestVersion::V3)
    {
        // For SNMPv3 we need a UTarget object
        UTarget *utarget = new UTarget(address);

        ConfigTargetFromSettings(config, utarget);
        theoid = ConfigPduFromSettings(config, oid, pdu, usevblist);
        *t = utarget;
    }
    else
    {
        // For SNMPv1/v2c we need a CTarget
        CTarget *ctarget = new CTarget(address);

        ConfigTargetFromSettings(config, ctarget);
        theoid = ConfigPduFromSettings(config, oid, pdu, usevblist);

        *t = ctarget;
    }

    *p = pdu;
    
    return 0;
}

quint64 Agent::startLog(const QString &name, const SnmpRequestConfig &config, const QString &oid)
{
    if (!requestLog) return 0;
    const QString identity = config.version == SnmpRequestVersion::V3
        ? tr("SNMPv3 %1 (%2)").arg(config.securityName,
              config.securityLevel == 2 ? QStringLiteral("authPriv")
              : config.securityLevel == 1 ? QStringLiteral("authNoPriv") : QStringLiteral("noAuthNoPriv"))
        : config.version == SnmpRequestVersion::V1 ? QStringLiteral("SNMPv1") : QStringLiteral("SNMPv2c");
    return requestLog->begin(name, QStringLiteral("%1:%2").arg(config.address, config.port), identity, oid);
}

void Agent::BeginRequest(const SnmpRequestConfig &config,
                         SnmpRequestOperation operation)
{
    activeRequestContext =
        std::make_unique<SnmpRequestContext>(config, operation,
                                             MibEnvironmentRegistry::active());
    QString name = QStringLiteral("Request");
    switch (operation) {
    case SnmpRequestOperation::Get: name = QStringLiteral("Get"); break;
    case SnmpRequestOperation::GetNext: name = QStringLiteral("GetNext"); break;
    case SnmpRequestOperation::GetBulk: name = QStringLiteral("GetBulk"); break;
    case SnmpRequestOperation::Walk: name = QStringLiteral("Walk"); break;
    case SnmpRequestOperation::Set: name = QStringLiteral("Set"); break;
    default: break;
    }
    const QString numeric = QString::fromLatin1(theoid.get_printable());
    if (s->Results()) s->Results()->beginQuery(name + QStringLiteral(" ") +
        (theoid.valid() ? RenderMibOid(activeRequestContext->environment(), theoid) : tr("multiple objects")), numeric);
    activeLogId = startLog(name, config, numeric);
    emit foregroundBusyChanged(true);
}

bool Agent::ForegroundQueryBusy() const
{
    return shutdownComplete || activeRequestContext || tableQueryPending ||
        (singleRunner && singleRunner->isBusy()) || (walkRunner && walkRunner->isBusy()) ||
        (tableRunner && tableRunner->isRunning()) || (instanceRunner && instanceRunner->isRunning()) ||
        (macroRunner && macroRunner->isBusy());
}

void Agent::RunMacro(const QueryMacroDefinition &definition)
{
    if (ForegroundQueryBusy()) { emit macroRejected(tr("Another query is running. Stop it or wait before running a macro.")); return; }
    if (offline || !snmp) {
        emit macroRejected(tr("SNMP transport is unavailable in this session. Previous results have been kept.")); return;
    }
    AgentRequestSelection selection;
    if (ResolveCurrentSelection(&selection) != AgentSelectionError::None) {
        emit macroRejected(ConnectionModel::errorText(lastConnectionError)); return;
    }
    SnmpRequestConfig config;
    if (!selection.requestConfig(&config) || !UdpAddress(config.endpoint().toLatin1().constData()).valid()) {
        emit macroRejected(tr("The target or credential is invalid. Previous results have been kept.")); return;
    }
    const auto environment = MibEnvironmentRegistry::active();
    if (!connectionBar || !environment || environment->profileId() != connectionBar->mibProfileId()) {
        emit macroRejected(tr("The selected MIB Profile is still loading or unavailable. Wait for it to finish, or choose another Profile. Previous results have been kept."));
        return;
    }
    activeRequestContext = std::make_unique<SnmpRequestContext>(config, SnmpRequestOperation::Get, environment);
    QString error;
    const QString target = config.address.contains(':') ? QStringLiteral("[%1]:%2").arg(config.address, config.port)
                                                       : QStringLiteral("%1:%2").arg(config.address, config.port);
    if (!macroRunner->start(definition, *activeRequestContext, target,
            connectionBar->credentialLabel(), connectionBar->mibProfileLabel(),
            [](const SnmpRequestConfig &captured) { return std::make_unique<SnmpPlusTransport>(captured); }, &error)) {
        activeRequestContext.reset(); emit macroRejected(error); return;
    }
    if (connectionBar) connectionBar->rememberCurrentTarget();
}

void Agent::PresentMacroResult()
{
    const auto &run = macroRunner->snapshot();
    QList<QueryResultGroup> groups;
    for (const auto &source : run.groups) {
        QueryResultGroup group; group.id = source.id; group.name = source.name; group.status = source.problem;
        for (const auto &read : source.reads) {
            if (!read.finished) continue;
            QueryResultValue value;
            const Oid oid(read.oid.toLatin1().constData());
            if (read.value) {
                value = resultValue(ResolveMibObject(run.environment, oid), oid, *read.value);
                value.time = read.received.toString(QStringLiteral("HH:mm:ss.zzz"));
            } else { value.oid = read.oid; value.object = read.oid; value.problem = read.problem; }
            value.id = read.id;
            if (!read.label.isEmpty()) value.object = read.label;
            group.values.append(value);
        }
        groups.append(group);
    }
    if (s->Results()) s->Results()->updateGroups(groups, run.summary(),
        run.state == QueryMacroRunState::Partial || run.state == QueryMacroRunState::Failed || run.state == QueryMacroRunState::Refused);
}

void Agent::finishLog(const QString &resultHtml, int objectCount)
{
    if (requestLog && activeLogId) requestLog->finish(activeLogId, resultHtml, objectCount);
    activeLogId = 0;
}

void Agent::ConfigTargetFromSettings(const SnmpRequestConfig& config,
                                     SnmpTarget *t)
{
    ApplySnmpRequestConfig(config, *t);
}

Oid Agent::ConfigPduFromSettings(const SnmpRequestConfig& config,
                                 const QString& oid, Pdu *p, bool usevblist)
{
    Vb vb;
    Oid oidobj(oid.toLatin1().data());

    if (usevblist == true)
    {
        p->clear();
        p->set_vblist(vblist.data(), vblist.size());
    }
    else
    {
        // Set the Oid part of the Vb & add it to pdu
        vb.set_oid(oidobj);
        p->clear();
        *p += vb;
    }

    ApplySnmpV3PduConfig(config, *p);

    return oidobj;
}

void Agent::TimerExpired(void)
{
  // When using async requests or if we are waiting for traps or
  // informs, we must call this member function periodically, as
  // snmp++ does not use an internal thread.
  snmp->get_eventListHolder()->SNMPProcessPendingEvents();
}

void Agent::AsyncCallbackTrap(int reason, Pdu &pdu, SnmpTarget &target)
{
    static unsigned int nbr = 1;
    Vb vb;
    GenAddress addr;
    TimeTicks ts;
    Oid id;
    int status = 0;
                
    // Bad message type or if there's an error in the pdu, bail out ...
    if ((reason != SNMP_CLASS_NOTIFICATION) || pdu.get_error_status())
        return;
    
    // Create string objects and collect info below
    QString no, date, time, timestamp, nottype, 
            msgtype, version, agtaddr, agtport, 
            community, seclevel, ctxname, ctxid, msgid;
    
    target.get_address(addr);
    IpAddress agent(addr);
    UdpAddress agentUDP(addr);
    
    no = QString("%1").arg(nbr, 4);
    date = QDate::currentDate().toString(Qt::ISODate);
    time = QTime::currentTime().toString(Qt::ISODate);  
    pdu.get_notify_timestamp(ts);
    timestamp = ts.get_printable();
  
    pdu.get_notify_id(id);
    nottype = RenderMibOid(MibEnvironmentRegistry::active(), id);
      
    switch(pdu.get_type())
    {
    case sNMP_PDU_V1TRAP:
        msgtype = "Trap(v1)";
        break;
    case sNMP_PDU_TRAP:
        msgtype = "Trap(v2)";
        break;
    case sNMP_PDU_INFORM:
        msgtype = "Inform";
        break;
    case sNMP_PDU_REPORT:
        msgtype = "Report";
        break;
    default:
        msgtype = "Unknown";
        break;
    }
  
    switch(target.get_version())
    {
    case version1:
        version = "SNMPv1";
        break;
    case version2c:
        version = "SNMPv2c";
        break;
    case version3:
        version = "SNMPv3";
        break;
    default:
        version = "Unknown";
        break;
    }
    
    char *add = (char*)agent.get_printable();
    const char *name;

    if ((s->PreferencesObj()->GetShowAgentName() == true) &&
        ((name = agent.friendly_name(status)) != NULL) &&
        (strlen(name) != 0))
        agtaddr = QString("%1/%2").arg(name).arg(add);
    else
        agtaddr = add;
    
    agtport = QString("%1").arg(agentUDP.get_port());
            
    if (target.get_type() == SnmpTarget::type_ctarget)
    {
        community = ((CTarget*)&target)->get_readcommunity();
    }
    else
    {
        ctxname = pdu.get_context_name().get_printable();
        ctxid = pdu.get_context_engine_id().get_printable();
        msgid = QString::number(pdu.get_message_id());
        switch(pdu.get_security_level())
        {
            case SNMP_SECURITY_LEVEL_NOAUTH_NOPRIV:
                seclevel = "NoAuthNoPriv";
            break;
            case SNMP_SECURITY_LEVEL_AUTH_NOPRIV:
                seclevel = "AuthNoPriv";
            break;
            case SNMP_SECURITY_LEVEL_AUTH_PRIV:
                seclevel = "AuthPriv";
            break;
            default:
                seclevel = "Unknown";
        }
    }

    TrapEndpoint endpoint;
    endpoint.address = agtaddr;
    endpoint.port = agentUDP.get_port();
    endpoint.community = community;
    if (target.get_version() == version1)
        endpoint.version = TrapSnmpVersion::V1;
    else if (target.get_version() == version2c)
        endpoint.version = TrapSnmpVersion::V2c;
    else if (target.get_version() == version3)
        endpoint.version = TrapSnmpVersion::V3;
    if (target.get_type() == SnmpTarget::type_utarget)
        endpoint.securityName = ((UTarget*)&target)->get_security_name().get_printable();
    if (s->TrapObj())
        s->TrapObj()->Receive(pdu, endpoint);
  
    // If its an inform, we have to reply ...
    if (pdu.get_type() == sNMP_PDU_INFORM)
    {
        // Copy the PDU object to feed back in the response
        Pdu ipdu = pdu;
        Vb t(Oid("1.3.6.1.2.1.1.3.0"));
        t.set_value(ts);
        Vb d(Oid("1.3.6.1.6.3.1.1.4.1.0"));
        d.set_value(id);
        ipdu.trim(pdu.get_vb_count()); // Remove all varbinds first
        ipdu += t; ipdu += d;
        for (int i=0; i < pdu.get_vb_count(); i++)
            ipdu += pdu[i];

        snmp->response(ipdu, target, snmp->get_notify_callback_fd());
    }
  
    nbr++;
}

// While results arrive the Results table stays unsorted and is sorted once when the query is
// over. Re-sorting after every row grew with the table, so a large Walk froze the window (8000
// rows took minutes) and Stop could not be handled.
void Agent::HoldResultSorting()
{
    if (resultSortingHeld) return;
    if (auto *table = structuredResults(s)) {
        resultSortingWasOn = table->isSortingEnabled();
        table->setSortingEnabled(false);
        resultSortingHeld = true;
    }
}

void Agent::ReleaseResultSorting()
{
    if (!resultSortingHeld) return;
    resultSortingHeld = false;
    if (auto *table = structuredResults(s))
        if (resultSortingWasOn)
            table->setSortingEnabled(true);
}

void Agent::WalkFrom(const QString& oid)
{
    if (ForegroundQueryBusy())
        return;
    SnmpRequestConfig requestConfig;

    // Validate the selection and address; the walk itself builds its own requests.
    SnmpTarget *target;
    Pdu *pdu;
    if (SetupFromCurrentSelection(oid, &target, &pdu, false,
                                  &requestConfig) < 0)
        return;
    delete target;
    delete pdu;

    // Clear the Query window ...
    s->MainUI()->Query->clear();
    clearStructuredResults(s);
    HoldResultSorting();
    lastTable.clear();
    s->MainUI()->Query->append(tr("<font color=black>-----SNMP query started-----</font>"));

    // Clear some global vars
    requests = 0;
    objects  = 0;
    msg = "";
    stop = false;
    emit StartWalk(true);
    s->MainUI()->actionStop->setEnabled(true);

    BeginRequest(requestConfig, SnmpRequestOperation::Walk);
    if (auto *table = structuredResults(s)) table->setSortingEnabled(false);
    if (!walkRunner->start(*activeRequestContext, theoid,
                           std::make_unique<SnmpPlusTransport>(requestConfig)))
    {
        msg = tr("<font color=red>The walk could not be started.</font>");
        s->MainUI()->Query->append(msg);
        presentRequestStatus(s, msg, 0);
        finishLog(msg, 0);
        activeRequestContext.reset();
        ReleaseResultSorting();
        s->MainUI()->actionStop->setEnabled(false);
        emit StartWalk(false);
    }
}

// Objects arrive while the walk is still running, in order, a batch at a time.
void Agent::PresentWalkBatch(const QVector<Vb> &batch)
{
    const MibEnvironmentPtr environment =
        activeRequestContext ? activeRequestContext->environment() : MibEnvironmentPtr{};
    QStringList lines;
    for (const Vb &vb : batch)
    {
        const Oid oid = vb.get_oid();
        ++objects;
        const auto resolved = ResolveMibObject(environment, oid);
        appendStructuredResult(s, resolved, oid, vb);
        lines << FormatObjectLine(objects, resolved, oid, vb);
    }
    if (!lines.isEmpty())
        s->MainUI()->Query->append(lines.join(QStringLiteral("<br>")));
}

void Agent::PresentWalkResult(const SnmpWalkResult &result)
{
    FinishQuery(FormatWalkEnd(result));
}

void Agent::GetFrom(const QString& oid, int op)
{
    switch(op)
    {
    case 0:
        Get(oid);
        break;
    case 1:
        GetNext(oid);
        break;
    case 2:
        GetBulk(oid);
        break;
    default:
        break;
    }
}

void Agent::Get(const QString& oid, bool usevblist)
{
    RunSingleRequest(oid, usevblist, SnmpRequestOperation::Get);
}

void Agent::GetNext(const QString& oid, bool usevblist)
{
    RunSingleRequest(oid, usevblist, SnmpRequestOperation::GetNext);
}

void Agent::GetBulk(const QString& oid, bool usevblist)
{
    RunSingleRequest(oid, usevblist, SnmpRequestOperation::GetBulk);
}

// Get, GetNext, GetBulk and Set are one request on a worker thread; the answer is shown by
// PresentSingleResult. A request already in flight is not replaced.
void Agent::RunSingleRequest(const QString& oid, bool usevblist, SnmpRequestOperation operation)
{
    if (ForegroundQueryBusy())
        return;
    SnmpRequestConfig requestConfig;

    // Initialize agent & pdu objects
    SnmpTarget *target;
    Pdu *pdu;
    if (SetupFromCurrentSelection(oid, &target, &pdu, usevblist,
                                  &requestConfig) < 0)
        return;
    QVector<Vb> varbinds;
    for (int i = 0; i < pdu->get_vb_count(); ++i)
    {
        Vb vb;
        pdu->get_vb(vb, i);
        varbinds.append(vb);
    }
    delete target;
    delete pdu;

    // Clear the Query window ...
    s->MainUI()->Query->clear();
    clearStructuredResults(s);
    lastTable.clear();
    s->MainUI()->Query->append(operation == SnmpRequestOperation::Set
        ? tr("<font color=black>-----SNMP set started-----</font>")
        : tr("<font color=black>-----SNMP query started-----</font>"));

    // Clear some global vars
    requests = 0;
    objects  = 0;
    msg = "";
    stop = false;

    BeginRequest(requestConfig, operation);
    if (!singleRunner->start(*activeRequestContext, varbinds,
                             std::make_unique<SnmpPlusTransport>(requestConfig)))
    {
        msg = tr("<font color=red>The request could not be started.</font>");
        s->MainUI()->Query->append(msg);
        presentRequestStatus(s, msg, 0);
        finishLog(msg, 0);
        activeRequestContext.reset();
        emit foregroundBusyChanged(false);
    }
}

void Agent::PresentSingleResult(const SnmpSingleResult &result)
{
    const MibEnvironmentPtr environment =
        activeRequestContext ? activeRequestContext->environment() : MibEnvironmentPtr{};
    const SnmpObjectResolver resolve = [&environment](const Oid &oid) { return ResolveMibObject(environment, oid); };
    // A Set never touched the Stop button or the "walking" state, so finishing one leaves them alone.
    if (activeRequestContext && activeRequestContext->operation() == SnmpRequestOperation::Set)
        FinishQuery(FormatSetResult(result, resolve), false);
    else
        FinishQuery(FormatSingleResult(result, resolve, stop));
}

// Results rows and Query pane text, the status line and the Log entry for a finished query,
// shared by Walk, Get, GetNext, GetBulk and Set.
void Agent::FinishQuery(const SnmpQueryText &text, bool restoreControls)
{
    HoldResultSorting();
    for (const SnmpQueryRow &row : text.rows)
        appendStructuredResult(s, row.resolved, row.oid, row.vb);
    ReleaseResultSorting();
    const QString last = text.paragraphs.isEmpty() ? QString() : text.paragraphs.last();
    for (int i = 0; i + 1 < text.paragraphs.size(); ++i)
        s->MainUI()->Query->append(text.paragraphs.at(i));
    const QString summary = text.statusHtml.isEmpty() ? last : text.statusHtml;
    presentRequestStatus(s, summary, text.objects);
    finishLog(summary, text.objects);
    s->MainUI()->Query->append(last);
    if (restoreControls) {
        emit StartWalk(false);
        s->MainUI()->actionStop->setEnabled(false);
    }
    activeRequestContext.reset();
    emit foregroundBusyChanged(false);
}

void Agent::Set(const QString& oid, bool usevblist)
{
    RunSingleRequest(oid, usevblist, SnmpRequestOperation::Set);
}

// Sets exactly these varbinds (each carries the value to write) as one request.
void Agent::SetVarbinds(const QVector<Vb> &varbinds, const QString& oid)
{
    vblist = varbinds;
    Set(!oid.isEmpty() || varbinds.isEmpty() ? oid
                                             : QString::fromLatin1(varbinds.first().get_printable_oid()),
        true);
}

void Agent::SetFrom(const QString& oid)
{
    // Create and run the mib selection dialog
    MibSelection ms(s, s->MainUI()->MIBTree, "Set", MIBSELECTION_SET|MIBSELECTION_VALUE);

    if (ms.run(oid))
    {
        Vb *vb = ms.GetVarbind();
        if (vb)
            SetVarbinds({*vb}, ms.GetOid());
    }
}

void Agent::Stop(void)
{
    stop = true;
    if (macroRunner) macroRunner->cancel();
    if (tableRunner)
        tableRunner->cancel();
    if (instanceRunner)
        instanceRunner->cancel();
    if (walkRunner)
        walkRunner->cancel();
}

void Agent::TableViewFrom(const QString& oid) { QueryTableFrom(oid, false); }
void Agent::TableRowSelectFrom(const QString& oid) { QueryTableFrom(oid, true); }

void Agent::QueryTableFrom(const QString &oid, bool selectInstance)
{
    if (ForegroundQueryBusy())
        return;
    AgentRequestSelection selection;
    if (!ResolveForRequest(&selection))
        return;

    SnmpTarget *validationTarget;
    Pdu *validationPdu;
    SnmpRequestConfig config;
    if (Setup(selection, oid, &validationTarget, &validationPdu,
              false, &config) < 0)
        return;
    delete validationTarget;
    delete validationPdu;
    
    /* Set the parent oid & parent node */
    Oid poid(oid.toLatin1().data());
    const auto environment=MibEnvironmentRegistry::active();
    const auto *pnode=environment?environment->longestPrefixNode(oid):nullptr;
    const bool tableNode=pnode&&pnode->kind==MibEnvironmentNodeKind::Table;
    const MibEnvironmentNodeRecord *rowNode=nullptr;


    /* Make sure the parent is a table or row entry ... */
    if (!environment || ResolveTableRowNode(pnode,pnode?environment->nodeByOid(pnode->rowOid):nullptr,&rowNode) !=
        TableNodeValidation::Valid)
    {
        const QString error = tr("<font color=red>Abort, not a table or row entry</font>");
        s->MainUI()->Query->append(error); presentRequestStatus(s, error, 0);
        return;
    }
   
    /* If the oid is the table element, get the row entry element */ 
    if (tableNode)
    {
        if (!RenderEnvironmentNodeOid(rowNode, &poid))
        {
            const QString error = tr("<font color=red>Abort, not a table or row entry</font>");
            s->MainUI()->Query->append(error); presentRequestStatus(s, error, 0);
            return;
        }
    }
    SnmpTablePlan plan;
    plan.rowOid = poid;
    plan.indexSpecs = TableIndexSpecsForRow(*environment, *rowNode);
    for (const QString &column : rowNode->columnOids)
    {
        const auto *node=environment->nodeByOid(column);
        if (!HasValidColumnInfo(node))
            continue;
        // A not-accessible column (typically the INDEX itself) is never returned by an agent;
        // reading it would only fill the table with "not available". Its value is shown in the
        // Index column, decoded from the instance.
        if (node->access == MibEnvironmentAccess::NotAccessible ||
            node->access == MibEnvironmentAccess::NotImplemented ||
            node->access == MibEnvironmentAccess::Notify)
            continue;
        Oid columnOid;
        if (RenderEnvironmentNodeOid(node, &columnOid))
            plan.columns.append({node->name, columnOid});
    }
    const SnmpRequestContext context(config, SnmpRequestOperation::Walk,
                                     environment);
    if (plan.columns.isEmpty()) {
        presentRequestStatus(s, tr("<font color=red>This table has no readable columns.</font>"), 0);
        return;
    }
    if (selectInstance) {
        pendingTableRowContext = std::make_unique<SnmpRequestContext>(context);
        pendingTableRowPlan = plan;
        pendingInstanceOid = oid;
        QVector<Oid> roots;
        for (const auto &column : plan.columns) roots.append(column.oid);
        tableQueryPending = true;
        s->MainUI()->actionStop->setEnabled(true);
        emit StartWalk(true);
        activeLogId = startLog(tr("Table Instances"), config, oid);
        presentRequestStatus(s, tr("Finding table instances…"), 0, false);
        if (!instanceRunner->start(context, roots, std::make_unique<SnmpPlusTransport>(config))) {
            pendingTableRowContext.reset(); tableQueryPending = false;
            const QString error = tr("<font color=red>Instance discovery could not be started.</font>");
            presentRequestStatus(s, error, 0); finishLog(error, 0);
            s->MainUI()->actionStop->setEnabled(false); emit StartWalk(false);
        }
        return;
    }
    StartTableQuery(context, plan, oid);
}

void Agent::StartTableQuery(const SnmpRequestContext &context, const SnmpTablePlan &plan, const QString &oid)
{
    s->MainUI()->Query->clear();
    clearStructuredResults(s);
    lastTable.clear();
    s->MainUI()->Query->append(tr("<font color=black>-----SNMP query started-----</font>"));
    const QString title = plan.instance.isEmpty() ? tr("Table ") : tr("Table row %1 · ").arg(plan.instance);
    if (s->Results()) s->Results()->beginQuery(title + RenderMibOid(context.environment(), Oid(oid.toLatin1().constData())), oid);
    s->MainUI()->actionStop->setEnabled(true);
    emit StartWalk(true);
    activeLogId = startLog(plan.instance.isEmpty() ? QStringLiteral("Table") : QStringLiteral("Table Row"), context.config(), oid);
    tableQueryPending = true;
    if (!tableRunner->start(context, plan, std::make_unique<SnmpPlusTransport>(context.config()))) {
        tableQueryPending = false;
        const QString error = tr("<font color=red>The table could not be started.</font>");
        presentRequestStatus(s, error, 0); finishLog(error, 0);
        s->MainUI()->actionStop->setEnabled(false); emit StartWalk(false);
    }
}

void Agent::PresentTableResult(const SnmpTableResult &result)
{
    bool anyIndexText = false;
    for (const SnmpTableRow &row : result.rows) anyIndexText |= !row.indexText.isEmpty();
    QString output = tr("<table border=\"1\"><tr bgcolor=yellow><td>Instance</td>");
    QStringList header{tr("Instance")};
    if (anyIndexText) header << tr("Index");
    for (const SnmpTableColumn &column : result.columns)
    {
        output += QString("<td>%1</td>").arg(column.name);
        header << column.name;
    }
    output += QString("</tr>");
    lastTable.clear();
    lastTable.append(header);
    QList<QueryResultTableRow> grid;
    QStringList columnNames;
    for (const auto &column : result.columns) columnNames << column.name;
    for (const SnmpTableRow &row : result.rows)
    {
        output += QString("<tr><td bgcolor=pink>%1%2</td>").arg(
            row.instance,
            row.indexText.isEmpty() ? QString()
                                    : QString("<br>%1").arg(row.indexText.toHtmlEscaped()));
        QueryResultTableRow gridRow; gridRow.instance = row.instance; gridRow.index = row.indexText;
        QStringList record{row.instance};
        if (anyIndexText) record << row.indexText;
        for (int i = 0; i < qMin(row.cells.size(), result.columns.size()); ++i)
        {
            if (!row.cells[i].available)
            {
                output += tr("<td>not available</td>");
                record << QString(); gridRow.cells.append(QueryResultValue{});
                continue;
            }
            Vb vb = row.cells[i].varbind;
            Oid oid = result.columns[i].oid;
            Oid instanceOid;
            vb.get_oid(instanceOid);
            if (instanceOid.len() == 0) instanceOid = oid;
            gridRow.cells.append(resultValue(ResolveMibObject(result.environment, instanceOid), instanceOid, vb));
            const QString value = RenderMibValue(ResolveMibObject(result.environment,oid),vb);
            output += QString("<td>%1</td>").arg(value.toHtmlEscaped());
            record << value;
        }
        lastTable.append(record); grid.append(gridRow);
        output += QString("</tr>");
    }
    if (s->Results()) s->Results()->showTable(columnNames, grid);
    output += QString("</table>");
    QString ending;
    if (result.status == SnmpOperationStatus::Cancelled)
        ending = tr("<font color=red>Operation cancelled</font><br>");
    else if (result.status == SnmpOperationStatus::Timeout)
        ending = tr("<font color=red>Timeout: the device stopped answering, so the table may be incomplete.</font><br>");
    else if (result.status == SnmpOperationStatus::SnmpError && result.problem.isEmpty())
        ending = tr("<font color=red>SNMP error %1: the device refused to continue, so the table may be incomplete.</font><br>")
                     .arg(result.snmpErrorStatus);
    else if (result.status == SnmpOperationStatus::TransportFailure && result.problem.isEmpty())
        ending = tr("<font color=red>No response received</font><br>");
    if (!result.problem.isEmpty())
        ending += tr("<font color=red>%1</font><br>").arg(result.problem.toHtmlEscaped());
    output += ending;
    output += tr("-----SNMP query finished-----<br>");
    output += QString("<font color=#009000>Total # of rows = %1<br>")
                  .arg(result.rows.size());
    s->MainUI()->Query->append(output);
    const QString summary = result.requestedInstance.isEmpty() ? tr("Table: %1 row(s).").arg(result.rows.size())
        : tr("Table row %1: %2 row(s).").arg(result.requestedInstance).arg(result.rows.size());
    presentRequestStatus(s, ending.isEmpty() ? summary : summary + QLatin1Char(' ') + ending,
                         result.rows.size());
    finishLog(ending.isEmpty() ? summary : summary + QLatin1Char(' ') + ending, result.rows.size());
    tableQueryPending = false;
    s->MainUI()->actionStop->setEnabled(false);
    emit StartWalk(false);
}

QString Agent::GetValueString(MibSelection &ms, Vb* vb)
{
    if (!ms.GetValue().isEmpty())
    {
        const Oid oid(ms.GetSemanticOid().toLatin1().constData());
        return RenderMibValue(ResolveMibObject(ms.GetEnvironment(),oid),*vb);
    }

    return "";
}

void Agent::Varbinds(void)
{
    vbui->GetBulkOp->setEnabled(!(connectionBar && connectionBar->credentialProtocol() == 0));
    vbd->exec(); 
}

void Agent::VarbindsFrom(const QString& oid)
{
    // Do a background run of the mib selection dialog
    MibSelection ms(s, vbd, tr("New VarBind"), MIBSELECTION_NONE);

    ms.bgrun(oid);

    Vb *vb = ms.GetVarbind();
    if (vb)
    {
        QStringList sl;
        sl << ms.GetName() << ms.GetOid() 
           << ms.GetSyntaxName() << GetValueString(ms, vb);

        vb_data data;
        data.vb = *vb;
        data.syntax = ms.GetSyntax();
        data.val = ms.GetValue();
        QTreeWidgetItem *qtwi = new QTreeWidgetItem(sl);
        QVariant qv;
        qv.setValue(data);
        qtwi->setData(0, Qt::UserRole, qv);
        vbui->VarbindsList->addTopLevelItem(qtwi);
        
        vbui->VarbindsList->setCurrentItem(qtwi); 

        vbui->SNMPOps->setEnabled(true);
        vbui->GetBulkOp->setEnabled(!(connectionBar && connectionBar->credentialProtocol() == 0));
    }

    vbd->exec(); 
}

void Agent::VarbindsNew(void)
{
    // Create and run the mib selection dialog
    MibSelection ms(s, vbd, tr("New VarBind"), MIBSELECTION_SET|MIBSELECTION_VALUE);

    if (ms.run())
    {
        Vb *vb = ms.GetVarbind();
        if (vb)
        {
            QStringList sl;
            sl << ms.GetName() << ms.GetOid() 
               << ms.GetSyntaxName() << GetValueString(ms, vb);

            vb_data data;
            data.vb = *vb;
            data.syntax = ms.GetSyntax();
            data.val = ms.GetValue();
            QTreeWidgetItem *qtwi = new QTreeWidgetItem(sl);
            QVariant qv;
            qv.setValue(data);
            qtwi->setData(0, Qt::UserRole, qv);
            vbui->VarbindsList->addTopLevelItem(qtwi);

            vbui->SNMPOps->setEnabled(true);
            vbui->GetBulkOp->setEnabled(!(connectionBar && connectionBar->credentialProtocol() == 0));
        }
    }
}

void Agent::VarbindsEdit(void)
{
    QTreeWidget *vbl = vbui->VarbindsList;
    QList<QTreeWidgetItem *> items = vbl->selectedItems();
    if (items.size() != 1)
    {
        QMessageBox::critical( NULL, tr("Edit VarBind"),
                tr("Please select only one VarBind"),
                QMessageBox::Ok);
        return;
    }

    // Create and run the mib selection dialog
    MibSelection ms(s, vbd, tr("Edit VarBind"), MIBSELECTION_SET|MIBSELECTION_VALUE);

    vb_data data = items[0]->data(0, Qt::UserRole).value<vb_data>();

    if (ms.run(items[0]->text(1), data.syntax, data.val))
    {
        Vb *vb = ms.GetVarbind();
        if (vb)
        {
            items[0]->setText(0, ms.GetName());
            items[0]->setText(1, ms.GetOid());
            items[0]->setText(2, ms.GetSyntaxName());
            items[0]->setText(3, GetValueString(ms, vb));
 
            data.vb = *vb;
            data.syntax = ms.GetSyntax();
            data.val = ms.GetValue();
            QVariant qv;
            qv.setValue(data);
            items[0]->setData(0, Qt::UserRole, qv);
        }
    }
}

void Agent::VarbindsDelete(void)
{
    QTreeWidget *vbl = vbui->VarbindsList;
    QList<QTreeWidgetItem *> items = vbl->selectedItems();

    QTreeWidgetItem *next = vbl->itemBelow(items[items.size()-1]);
    if (!next)
        next = vbl->itemAbove(items[items.size()-1]);

    for (int i = 0; i < items.size(); i++)
        delete vbl->takeTopLevelItem(vbl->indexOfTopLevelItem(items[i]));

    if (next)
        vbl->setCurrentItem(next); 

    if (vbui->VarbindsList->topLevelItemCount() == 0)
        vbui->SNMPOps->setEnabled(false);
}

void Agent::VarbindsDeleteAll(void)
{
    vbui->VarbindsList->clear();

    if (vbui->VarbindsList->topLevelItemCount() == 0)
        vbui->SNMPOps->setEnabled(false);
}

// External C function
bool CompareItemPositions(QTreeWidgetItem *i1, QTreeWidgetItem *i2)
{
    return (i1->treeWidget()->indexOfTopLevelItem(i1) < 
            i1->treeWidget()->indexOfTopLevelItem(i2));
}

void Agent::VarbindsMoveUp(void)
{
    QTreeWidget *vbl = vbui->VarbindsList;
    QList<QTreeWidgetItem *> items = vbl->selectedItems();
    bool cleared = false;

    std::sort(items.begin(), items.end(), CompareItemPositions);

    for (int i = 0; i < items.size(); i++)
    {
        int idx = vbl->indexOfTopLevelItem(items[i]);
        int previdx = vbl->indexOfTopLevelItem(vbl->itemAbove(items[i]));

        if (previdx >= 0)
        {
            if (!cleared)
            {
                vbl->clearSelection();
                cleared = true;
            }

            QTreeWidgetItem *item = vbl->takeTopLevelItem(idx);

            vbl->insertTopLevelItem(previdx, item);
            item->setSelected(true);
        }
        else
            break;
    }
}

void Agent::VarbindsMoveDown(void)
{
    QTreeWidget *vbl = vbui->VarbindsList;
    QList<QTreeWidgetItem *> items = vbl->selectedItems();
    bool cleared = false;

    std::sort(items.begin(), items.end(), CompareItemPositions);

    for (int i = items.size()-1; i >= 0; i--)
    {
        int idx = vbl->indexOfTopLevelItem(items[i]);
        int nextidx = vbl->indexOfTopLevelItem(vbl->itemBelow(items[i]));

        if (nextidx > 0)
        {
            if (!cleared)
            {
                vbl->clearSelection();
                cleared = true;
            }

            QTreeWidgetItem *item = vbl->takeTopLevelItem(idx);

            vbl->insertTopLevelItem(nextidx, item);
            item->setSelected(true);
        }
        else
            break;
    }
}

void Agent::VarbindsQuit(void)
{
    vbd->accept();
}

void Agent::VarbindsBuildList(void)
{
    QTreeWidget *vbl = vbui->VarbindsList;

    vblist.clear();

    for(int i = 0; i < vbl->topLevelItemCount(); i++)
        vblist += vbl->topLevelItem(i)->data(0, Qt::UserRole).value<vb_data>().vb; 
}

void Agent::VarbindsGet(void)
{
    // Build the vblist
    VarbindsBuildList();

    // Do the operation 
    Get("", true);
}

void Agent::VarbindsGetNext(void)
{
    // Build the vblist
    VarbindsBuildList();

    // Do the operation 
    GetNext("", true);
}

void Agent::VarbindsGetBulk(void)
{
    // Build the vblist
    VarbindsBuildList();

    // Do the operation 
    GetBulk("", true);
}

void Agent::VarbindsSet(void)
{
    // Build the vblist
    VarbindsBuildList();

    // Do the operation 
    Set("", true);
}

// Controls buttons to gray out
void Agent::VarbindsSelected(void)
{
    QList<QTreeWidgetItem *> items = vbui->VarbindsList->selectedItems();

    if (items.isEmpty())
    {
        vbui->EditOp->setEnabled(false);
        vbui->DeleteOp->setEnabled(false);
        vbui->MoveUpOp->setEnabled(false);
        vbui->MoveDownOp->setEnabled(false);
    }
    else
    {
        vbui->EditOp->setEnabled(true);
        vbui->DeleteOp->setEnabled(true);
        vbui->MoveUpOp->setEnabled(true);
        vbui->MoveDownOp->setEnabled(true);
    }
}

void Agent::PresentInstanceResult(const SnmpInstanceResult &result)
{
    auto rowContext = std::move(pendingTableRowContext);
    SnmpTablePlan rowPlan = pendingTableRowPlan;
    const QString selectedTableOid = pendingInstanceOid;
    s->MainUI()->actionStop->setEnabled(false);
    emit StartWalk(false);
    // Looking for instances is a request like any other: say how it ended, even when nothing
    // is left to choose from (it used to end silently).
    QString outcome;
    if (result.status == SnmpOperationStatus::Cancelled)
        outcome = tr("<font color=red>Operation cancelled</font>");
    else if (result.status == SnmpOperationStatus::Timeout)
        outcome = tr("<font color=red>Timeout: the device did not answer.</font>");
    else if (!result.problem.isEmpty())
        outcome = tr("<font color=red>%1</font>").arg(result.problem.toHtmlEscaped());
    else if (result.status == SnmpOperationStatus::SnmpError)
        outcome = tr("<font color=red>SNMP error %1: the device refused the request.</font>").arg(result.snmpErrorStatus);
    else if (result.status != SnmpOperationStatus::Complete)
        outcome = tr("<font color=red>No response received</font>");
    else if (result.instances.isEmpty())
        outcome = tr("<font color=red>The device has no instances of this object.</font>");
    else
        outcome = tr("%1 instance(s) found.").arg(result.instances.size());
    finishLog(outcome, result.instances.size());
    if (result.status != SnmpOperationStatus::Complete || result.instances.isEmpty()) {
        if (rowContext) tableQueryPending = false;
        presentRequestStatus(s, outcome, result.instances.size());
        return;
    }

    // The selected instance is a copied suffix, independent of model lifetime.
    QDialog dlist(s->MainUI()->MIBTree);
    dlist.setObjectName("TableInstanceDialog");
    QVBoxLayout layout(&dlist);
    QLabel label(tr("Select an instance to query."), &dlist); label.setWordWrap(true);
    QListWidget ilist(&dlist); ilist.setObjectName("TableInstanceList");
    ilist.setAccessibleName(tr("Available table instances"));
    QDialogButtonBox box(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlist);
    auto *queryButton = box.button(QDialogButtonBox::Ok);
    queryButton->setText(tr("Query")); queryButton->setEnabled(false);
    connect(&ilist, &QListWidget::itemSelectionChanged, queryButton, [&ilist, queryButton]() {
        queryButton->setEnabled(!ilist.selectedItems().isEmpty());
    });
    layout.addWidget(&label); layout.addWidget(&ilist, 1); layout.addWidget(&box);
    connect(&box, &QDialogButtonBox::rejected, &dlist, &QDialog::reject);
    connect(&box, &QDialogButtonBox::accepted, &dlist, &QDialog::accept);
    connect(&ilist, &QListWidget::itemDoubleClicked, &dlist, &QDialog::accept);
    dlist.setWindowTitle(tr("Select Instance"));
    DialogLayout::initialize(dlist, QSize(rowContext ? 560 : 420, 400));
    ilist.setFocus();

    for (const QString &instance : result.instances) {
        QString decoded;
        if (rowContext) {
            QVector<quint32> suffix;
            for (const QString &part : instance.split('.')) suffix.append(part.toUInt());
            DecodeTableInstance(rowPlan.indexSpecs, suffix, &decoded);
        }
        auto *item = new QListWidgetItem(decoded.isEmpty() ? instance : instance + "  ·  " + decoded, &ilist);
        item->setData(Qt::UserRole, instance);
    }
    const int accepted = dlist.exec();
    if (rowContext) tableQueryPending = false;
    if (accepted != QDialog::Accepted || ilist.selectedItems().isEmpty()) {
        if (rowContext) presentRequestStatus(s, tr("Table row selection cancelled."), 0, false);
        return;
    }
    if (rowContext) {
        rowPlan.instance = ilist.selectedItems().first()->data(Qt::UserRole).toString();
        StartTableQuery(*rowContext, rowPlan, selectedTableOid);
        return;
    }
    const QString requestOid = pendingInstanceOid + "." +
                               ilist.selectedItems().at(0)->text();
    if (pendingInstanceOperation == 0) Get(requestOid);
    else if (pendingInstanceOperation == 1) GetNext(requestOid);
    else if (pendingInstanceOperation == 2) GetBulk(requestOid);
}

int Agent::SelectTableInstance(const QString &oid, QString &outinstance)
{
    if (ForegroundQueryBusy()) return 0;
    AgentRequestSelection selection;
    if (!ResolveForRequest(&selection)) return 0;
    SnmpTarget *target;
    Pdu *pdu;
    SnmpRequestConfig config;
    if (Setup(selection, oid, &target, &pdu, false, &config) < 0) return 0;
    delete target;
    delete pdu;
    Oid root(oid.toLatin1().constData());
    const auto environment=MibEnvironmentRegistry::active();
    if (!environment || !IsValidTableColumnNode(environment->longestPrefixNode(oid))) return 0;
    SnmpInstanceAsyncRunner runner;
    QEventLoop loop;
    SnmpInstanceResult result;
    connect(&runner, &SnmpInstanceAsyncRunner::completed, &loop,
            [&](const SnmpInstanceResult &value) { result = value; loop.quit(); });
    runner.start(SnmpRequestContext(config, SnmpRequestOperation::Walk,
                                   environment), root,
                 std::make_unique<SnmpPlusTransport>(config));
    loop.exec();
    runner.wait();
    if (result.status != SnmpOperationStatus::Complete || result.instances.isEmpty())
        return 0;
    QDialog dialog(s->MainUI()->MIBTree);
    dialog.setObjectName("ValueInstanceDialog"); dialog.setWindowTitle(tr("Select Instance"));
    QVBoxLayout layout(&dialog);
    QLabel heading(tr("Select an instance for this value."), &dialog); heading.setWordWrap(true);
    QListWidget list(&dialog); list.setAccessibleName(tr("Available value instances"));
    list.addItems(result.instances);
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    auto *select = buttons.button(QDialogButtonBox::Ok);
    select->setText(tr("Select")); select->setEnabled(false);
    layout.addWidget(&heading); layout.addWidget(&list, 1); layout.addWidget(&buttons);
    connect(&list, &QListWidget::itemSelectionChanged, select, [&list, select]() {
        select->setEnabled(!list.selectedItems().isEmpty());
    });
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(&list, &QListWidget::itemDoubleClicked, &dialog, &QDialog::accept);
    DialogLayout::initialize(dialog, QSize(420, 400)); list.setFocus();
    if (dialog.exec() != QDialog::Accepted || list.selectedItems().isEmpty()) return 0;
    outinstance = list.selectedItems().first()->text();
    return 1;
}

void Agent::GetFromSelectInstance(const QString& oid, int op)
{
    if (ForegroundQueryBusy()) return;
    AgentRequestSelection selection;
    if (!ResolveForRequest(&selection)) return;
    SnmpTarget *target;
    Pdu *pdu;
    SnmpRequestConfig config;
    if (Setup(selection, oid, &target, &pdu, false, &config) < 0) return;
    delete target;
    delete pdu;
    Oid root(oid.toLatin1().constData());
    const auto environment=MibEnvironmentRegistry::active();
    if (!environment || !IsValidTableColumnNode(environment->longestPrefixNode(oid))) return;
    pendingInstanceOid = oid;
    pendingInstanceOperation = op;
    s->MainUI()->actionStop->setEnabled(true);
    emit StartWalk(true);
    activeLogId = startLog(QStringLiteral("Instances"), config, oid);
    instanceRunner->start(SnmpRequestContext(config, SnmpRequestOperation::Walk,
                                             environment),
                          root, std::make_unique<SnmpPlusTransport>(config));
}


void Agent::GetFromPromptInstance(const QString& oid, int op)
{
    QDialog prompt(s->MainUI()->MIBTree);
    prompt.setObjectName("TypeInstanceDialog"); prompt.setWindowTitle(tr("Enter Instance"));
    QVBoxLayout layout(&prompt);
    QLabel label(tr("&Instance suffix"), &prompt);
    QLineEdit instance(&prompt); instance.setObjectName("InstanceSuffix");
    instance.setPlaceholderText(tr("For example, 3 or 1.4")); label.setBuddy(&instance);
    instance.setValidator(new QRegularExpressionValidator(QRegularExpression("[0-9]+(?:\\.[0-9]+)*"), &instance));
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &prompt);
    auto *query = buttons.button(QDialogButtonBox::Ok);
    query->setText(tr("Query")); query->setEnabled(false);
    layout.addWidget(&label); layout.addWidget(&instance); layout.addWidget(&buttons);
    connect(&instance, &QLineEdit::textChanged, query, [&instance, query]() { query->setEnabled(instance.hasAcceptableInput()); });
    connect(&buttons, &QDialogButtonBox::accepted, &prompt, &QDialog::accept);
    connect(&buttons, &QDialogButtonBox::rejected, &prompt, &QDialog::reject);
    DialogLayout::initialize(prompt, QSize(420, 180)); instance.setFocus();
    if (prompt.exec() != QDialog::Accepted || !instance.hasAcceptableInput()) return;
    const QString requestedOid = oid + "." + instance.text();
    switch (op) {
    case 0: Get(requestedOid); break;
    case 1: GetNext(requestedOid); break;
    case 2: GetBulk(requestedOid); break;
    default: break;
    }

}

