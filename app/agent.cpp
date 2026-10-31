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
#include <QDate>
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
#include "communitycredentialservice.h"
#include "connectionrequestsettings.h"
#include "diagnosticlogger.h"
#include "udpportowner.h"
#include "profilemetadataservice.h"
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

#define ASYNC_TIMER_MSEC 5
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

void clearStructuredResults(Snmpb *snmpb)
{
    if (auto *table = structuredResults(snmpb)) {
        table->setSortingEnabled(false);
        table->setRowCount(0);
        table->setSortingEnabled(true);
    }
}

void appendStructuredResult(Snmpb *snmpb, const MibResolvedObject &resolved,
                            const Oid &oid, const Vb &vb)
{
    auto *table = structuredResults(snmpb);
    if (!table) return;
    const bool sorting = table->isSortingEnabled();
    table->setSortingEnabled(false);
    const int row = table->rowCount();
    table->insertRow(row);
    const QString numericOid = QString::fromLatin1(oid.get_printable());
    const QString object = resolved.node && !resolved.node->qualifiedName.isEmpty()
        ? resolved.node->qualifiedName : resolved.node ? resolved.node->name : numericOid;
    const QString syntax = resolved.node
        ? (!resolved.node->syntaxName.isEmpty() ? resolved.node->syntaxName
                                                : resolved.node->typeId)
        : QString::number(vb.get_syntax());
    const QStringList values{object, resolved.instanceSuffix.join('.'), numericOid, syntax,
                             RenderMibValue(resolved, vb),
                             QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz"))};
    for (int column = 0; column < values.size(); ++column) {
        auto *item = new QTableWidgetItem(values[column]);
        if (column == 1 || column == 2 || column == 4)
            item->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        table->setItem(row, column, item);
    }
    table->setSortingEnabled(sorting);
}
}

Q_DECLARE_METATYPE(vb_data);

// C Callback functions for snmp++
void callback_walk(int reason, Snmp *, Pdu &pdu, SnmpTarget &target, void *cd)
{
    if (cd)
    {
        // just call the real callback member function...
        ((Agent*)cd)->AsyncCallback(reason, pdu, target, 1);
    }
}

void callback_set(int reason, Snmp *, Pdu &pdu, SnmpTarget &target, void *cd)
{
    if (cd)
    {
        // just call the real callback member function...
        ((Agent*)cd)->AsyncCallbackSet(reason, pdu, target);
    }
}

void callback_trap(int reason, Snmp *, Pdu &pdu, SnmpTarget &target, void *cd)
{
    if (cd)
    {
        // just call the real callback member function...
        ((Agent*)cd)->AsyncCallbackTrap(reason, pdu, target);
    }
}

void callback(int reason, Snmp *, Pdu &pdu, SnmpTarget &target, void *cd)
{
    if (cd)
    {
        // just call the real callback member function...
        ((Agent*)cd)->AsyncCallback(reason, pdu, target, 0);
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
            // Try dropping IPv6
            snmp = new Snmp(status, UdpAddress("0.0.0.0"));
            if (status != SNMP_CLASS_SUCCESS)
            {
                start_err = tr("Could not create IPv4 session.\n%1")
                                    .arg(Snmp::error_msg(status));
                // Disable IPv4, for the current run only
                s->PreferencesObj()->SetEnableIPv4(false);
                // Try dropping IPv4
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
    connect( s->MainUI()->MIBTree, SIGNAL( TableViewFromOid(const QString&) ),
             this, SLOT( TableViewFrom(const QString&) ) );
    connect(s->MainUI()->QueryTable, &QPushButton::clicked,
            s->MainUI()->MIBTree, &MibModelView::QueryTableFromCurrent);
    connect(s->MainUI()->MIBTree, &MibModelView::QueryTableAvailabilityChanged,
            s->MainUI()->QueryTable, &QPushButton::setEnabled);
    connect( s->MainUI()->MIBTree, SIGNAL( VarbindsFromOid(const QString&) ),
             this, SLOT( VarbindsFrom(const QString&) ) );
    connect( s->MainUI()->AgentSettings, 
             SIGNAL( clicked() ), this, SLOT( ShowAgentSettings() ));
    connect( s->APManagerObj(), SIGNAL( AgentProfileListChanged() ), 
             this, SLOT ( AgentProfileListChange() ) );
    connect( this, SIGNAL( StartWalk(bool) ), 
             s->MainUI()->MIBTree, SLOT ( SetWalkInProgress(bool) ) );
    connect( s->MainUI()->actionStop, SIGNAL( triggered() ),
             this, SLOT( Stop() ) );
    connect( s->MainUI()->actionMultipleVarbinds, SIGNAL( triggered() ),
             this, SLOT( Varbinds() ) );

    // Select the default profile from preferences
    QString cp;
    QString cpid = s->PreferencesObj()->GetCurrentProfileId();
    int prefproto = s->PreferencesObj()->GetCurrentProfile(cp);
    // Fill-in the list of agent profiles from profiles manager
    AgentProfileListChange();
    SelectAgentProfile(&cp, prefproto, &cpid);
    s->MainUI()->MIBTree->SetCurrentAgentIsV1(
        s->MainUI()->AgentProtoV1->isChecked()?true:false);
    UpdateTableQueryAvailability();

    // then connect the signals (order is important)
    connect( s->MainUI()->AgentProfile, SIGNAL( currentIndexChanged( int ) ), 
             this, SLOT ( SelectAgentProfile() ) );
    connect( s->MainUI()->AgentProtoV1, SIGNAL( toggled(bool) ),
             this, SLOT( SelectAgentProto() ) );
    connect( s->MainUI()->AgentProtoV2, SIGNAL( toggled(bool) ),
             this, SLOT( SelectAgentProto() ) );
    connect( s->MainUI()->AgentProtoV3, SIGNAL( toggled(bool) ),
             this, SLOT( SelectAgentProto() ) );

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
                               QMessageBox::Ok, Qt::NoButton);
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
                               QMessageBox::Ok, Qt::NoButton);
    }
    
    // If _SNMPv3 is enabled we MUST create ONE v3MP object!
    v3mp = new v3MP(engineId, snmpEngineBoots, status);
    if (status != SNMPv3_MP_OK)
    {
        QString err = tr("Could not create v3MP object:\n")
                              .arg(Snmp::error_msg(status));
        QMessageBox::warning ( NULL, "MIB Navigator", err,
                               QMessageBox::Ok, Qt::NoButton);
    }
    
    // The v3MP creates a USM object, get the pointer to it
    USM *usm = v3mp->get_usm();
    
    // Load the USM users from a file, if any
    usm->load_users(s->GetUsmUsersConfigFile().toLatin1().data());
}

void Agent::ShowAgentSettings(void)
{
     s->APManagerObj()->SetSelectedAgentById(
         s->MainUI()->AgentProfile->currentData().toString());
     s->APManagerObj()->Execute();
}

void Agent::AgentProfileListChange(void)
{
    int prefproto = -1;
    if (s->MainUI()->AgentProtoV1->isChecked()) prefproto = 0;
    else if (s->MainUI()->AgentProtoV2->isChecked()) prefproto = 1;
    else if (s->MainUI()->AgentProtoV3->isChecked()) prefproto = 2;

    QString cap = s->MainUI()->AgentProfile->currentData().toString();
    s->MainUI()->AgentProfile->clear();
    const QList<AgentProfileRecord> profiles =
        s->APManagerObj()->GetAgentProfileRecords();
    for (const AgentProfileRecord &profile : profiles)
        s->MainUI()->AgentProfile->addItem(profile.name, profile.profileId);
    if (cap.isEmpty() == false)
    {
        int idx = s->MainUI()->AgentProfile->findData(cap);
        s->MainUI()->AgentProfile->setCurrentIndex(idx>=0?idx:0);
        if (idx < 0) prefproto = -1;
    }
    else
        prefproto = -1;

    SelectAgentProfile(NULL, prefproto);
}

void Agent::SelectAgentProto(void)
{
    int prefproto = -1;
    if (s->MainUI()->AgentProtoV1->isChecked()) prefproto = 0;
    else if (s->MainUI()->AgentProtoV2->isChecked()) prefproto = 1;
    else if (s->MainUI()->AgentProtoV3->isChecked()) prefproto = 2;

    s->MainUI()->MIBTree->SetCurrentAgentIsV1(prefproto==0?true:false);

    SelectAgentProfile(NULL, prefproto);
}

void Agent::SelectAgentProfile(QString *prefprofile, int prefproto,
                               QString *prefprofileid)
{
    const AgentProfileRecord *ap = NULL;
    if (prefprofileid && !prefprofileid->isEmpty())
        ap = s->APManagerObj()->GetAgentProfileRecord(*prefprofileid);
    if (!ap && prefprofile)
    {
        const QString migratedId = AgentSelectionResolver::UniqueProfileIdForName(
            s->APManagerObj()->GetAgentProfileRecords(), *prefprofile);
        if (!migratedId.isEmpty())
            ap = s->APManagerObj()->GetAgentProfileRecord(migratedId);
    }
    if (!ap)
        ap = s->APManagerObj()->GetAgentProfileRecord(
            s->MainUI()->AgentProfile->currentData().toString());
    if (ap)
    {
        bool v1,v2,v3;
        int selectedproto = -1;
        v1 = ap->v1; v2 = ap->v2; v3 = ap->v3;

        s->MainUI()->AgentProtoV1->setEnabled(v1);
        s->MainUI()->AgentProtoV2->setEnabled(v2);
        s->MainUI()->AgentProtoV3->setEnabled(v3);

        if ((prefproto == 0) && v1)
        {
            s->MainUI()->AgentProtoV1->setChecked(true);
            selectedproto = 0;
        }
        else 
        if ((prefproto == 1) && v2)
        {
            s->MainUI()->AgentProtoV2->setChecked(true);
            selectedproto = 1;
        }
        else
        if ((prefproto == 2) && v3)
        {
            s->MainUI()->AgentProtoV3->setChecked(true);
            selectedproto = 2;
        }
        else
        if (v1)
        {
            s->MainUI()->AgentProtoV1->setChecked(true);
            selectedproto = 0;
        }
        else
        if (v2)
        {
            s->MainUI()->AgentProtoV2->setChecked(true);
            selectedproto = 1;
        }
        else
        if (v3)
        {
            s->MainUI()->AgentProtoV3->setChecked(true);
            selectedproto = 2;
        }

        if (prefprofile || (prefprofileid && !prefprofileid->isEmpty()))
        {
            int index = s->MainUI()->AgentProfile->findData(
                ap->profileId);
            if (index >= 0)
                s->MainUI()->AgentProfile->setCurrentIndex(index);
            s->PreferencesObj()->SaveCurrentProfile(
                ap->name, ap->profileId, selectedproto);
        }
        else
        {
            // The agent is selected by the user, save it in the preference file
            s->PreferencesObj()->SaveCurrentProfile(
                ap->name, ap->profileId, selectedproto);
        }
    }
    else
    {
        s->MainUI()->AgentProtoV1->setEnabled(false);
        s->MainUI()->AgentProtoV2->setEnabled(false);
        s->MainUI()->AgentProtoV3->setEnabled(false);
    }
    UpdateTableQueryAvailability();
}

void Agent::UpdateTableQueryAvailability()
{
    AgentRequestSelection selection;
    s->MainUI()->MIBTree->SetQueryPrerequisitesAvailable(
        ResolveCurrentSelection(&selection) == AgentSelectionError::None);
}

AgentSelectionError Agent::ResolveCurrentSelection(
    AgentRequestSelection *selection) const
{
    int selectedProtocol = 0;
    if (s->MainUI()->AgentProtoV3->isChecked())
        selectedProtocol = 2;
    else if (s->MainUI()->AgentProtoV2->isChecked())
        selectedProtocol = 1;

    const AgentSelectionError error = AgentSelectionResolver::ResolveById(
        s->APManagerObj()->GetAgentProfileRecords(),
        s->MainUI()->AgentProfile->currentData().toString(),
        selectedProtocol, selection);
    if (error == AgentSelectionError::None && selection && selectedProtocol < 2)
    {
        selection->credentials = s->CommunityCredentials()->resolve(
            selection->profile).values;
        selection->hasResolvedCredentials = true;
    }
    if (error == AgentSelectionError::None && selection)
    {
        QSettings settings;
        selection->profile = ConnectionRequestSettings::effectiveProfile(
            selection->profile,
            s->ProfileMetadata()->metadataForProfile(selection->profile.profileId),
            PreferencesSettings::load(settings));
    }
    return error;
}

Agent::~Agent()
{
    Shutdown();
    tableRunner->cancel();
    tableRunner->wait();
    instanceRunner->cancel();
    instanceRunner->wait();
    delete snmp;
    snmp = nullptr;
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
    DiagnosticLogger::log("Traps", "Agent cooperative shutdown end", false);
}

void Agent::SelectProfileByName(const QString &profileName)
{
    const int index = s->MainUI()->AgentProfile->findText(profileName);
    if (index >= 0)
        s->MainUI()->AgentProfile->setCurrentIndex(index);
}

void Agent::SelectProfileById(const QString &profileId)
{
    DiagnosticLogger::log("Connections", QStringLiteral(
        "hidden legacy control synchronization begin profile=%1").arg(profileId));
    const int index = s->MainUI()->AgentProfile->findData(profileId);
    if (index < 0) return;
    int legacyProtocol = s->MainUI()->AgentProtoV3->isChecked() ? 2
        : (s->MainUI()->AgentProtoV2->isChecked() ? 1 : 0);
    const AgentProfileRecord *profile =
        s->APManagerObj()->GetAgentProfileRecord(profileId);
    if (!profile) return;
    const int protocol = ConnectionRequestSettings::activeProtocol(
        *profile, s->ProfileMetadata()->metadataForProfile(profileId),
        legacyProtocol);
    {
        const QSignalBlocker blocker(s->MainUI()->AgentProfile);
        s->MainUI()->AgentProfile->setCurrentIndex(index);
    }
    SelectAgentProfile(nullptr, protocol);
    DiagnosticLogger::log("Connections", QStringLiteral(
        "hidden legacy control synchronization end profile=%1 protocol=%2")
        .arg(profileId).arg(protocol));
}

int Agent::SetupFromCurrentSelection(const QString& oid, SnmpTarget **t,
                                     Pdu **p, bool usevblist,
                                     SnmpRequestConfig *resolvedConfig)
{
    AgentRequestSelection selection;
    if (ResolveCurrentSelection(&selection) != AgentSelectionError::None)
        return -1;
    return Setup(selection, oid, t, p, usevblist, resolvedConfig);
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
                               QMessageBox::Ok, Qt::NoButton);
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

void Agent::BeginRequest(const SnmpRequestConfig &config,
                         SnmpRequestOperation operation)
{
    activeRequestContext =
        std::make_unique<SnmpRequestContext>(config, operation,
                                             MibEnvironmentRegistry::active());
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

void Agent::AsyncCallback(int reason, Pdu &pdu,
                          SnmpTarget &target, int iswalk)
{
    int pdu_error;
    int vb_error = 0;
    int pdu_index = 0;
    int start_index = 0;
    int status;
    Vb vb;   // empty Vb
    int z = 0;

    switch(reason)
    {
    case SNMP_CLASS_NOTIFICATION:
    case SNMP_CLASS_ASYNC_RESPONSE:
    case SNMP_CLASS_SESSION_DESTROYED:
        break;
    case SNMP_CLASS_TIMEOUT:
        msg = tr("<font color=red>Timeout</font>");
        goto cleanup;
    default:
        msg = tr("<font color=red>No response received: (%1) %2</font>")
                       .arg(reason).arg(Snmp::error_msg(reason));
        goto cleanup;
    }
    
    // Look at the error status of the Pdu
    pdu_error = pdu.get_error_status();

    if (pdu_error)
    {
        if (iswalk && (pdu_error == SNMP_ERROR_NO_SUCH_NAME))
            goto end;

        pdu_index = pdu.get_error_index();
        if (pdu_index > 0)
            start_index = objects = pdu_index-1;
        else
        {
            msg = tr("<font color=red>%1</font><br>")
                .arg(Snmp::error_msg(pdu_error));
            goto cleanup;
        }
    }

    // The Pdu must contain at least one Vb
    if (pdu.get_vb_count() == 0)
    {
        msg = tr("<font color=red>Pdu is empty</font>");
        goto cleanup;
    }

    requests++;
 
    for ( z=start_index; z < pdu.get_vb_count(); z++)
    {
        pdu.get_vb( vb, z );

        // look for var bind exception, applies to v2 only   
        if ( (vb_error = vb.get_syntax()) != sNMP_SYNTAX_ENDOFMIBVIEW )
        {
            Oid tmp;
            vb.get_oid(tmp);

            if ((vb_error != sNMP_SYNTAX_NOSUCHOBJECT) && 
                (vb_error != sNMP_SYNTAX_NOSUCHINSTANCE))
                vb_error = 0;

            // Stop there if we're out of scope
            if (iswalk && tmp.nCompare(theoid.len(), theoid))
            {
                goto end;
            }
            else
            {
                objects++;

                const auto resolved=ResolveMibObject(
                    activeRequestContext?activeRequestContext->environment():MibEnvironmentPtr{},tmp);
                appendStructuredResult(s, resolved, tmp, vb);
                if (resolved.node)
                {
                    if (vb_error || (pdu_error && (z+1 == pdu_index)))
                        msg += tr("<font color=red>ERROR on varbind #</font>");

                    // Print the OID part
                    msg += QString("%1: %2").arg(objects).arg(RenderMibOid(resolved.environment,tmp));

                    if (vb_error || (pdu_error && (z+1 == pdu_index)))
                    {
                        if (pdu_error)
                            msg += tr("<font color=red><br>%1</font><br>")
                                           .arg(Snmp::error_msg(pdu_error));
                        else
                            msg += tr("<font color=red><br>%1</font><br>")
                                    .arg(vb_error==sNMP_SYNTAX_NOSUCHOBJECT?
                                             //: These two strings might best be left untranslated. It's like "not found" in a "404 Not Found".
                                             tr("No Such Object"):tr("No Such Instance"));
                        goto end;
                    }

                    // Print the value part
                    msg += tr("    <font color=blue>%1</font>")
                               .arg(RenderMibValue(resolved,vb).toHtmlEscaped());
                }
                else
                {
                    if (vb_error || (pdu_error && (z+1 == pdu_index)))
                    {
                        msg += tr("<font color=red>ERROR on varbind #</font>%1: %2")
                                       .arg(objects).arg(vb.get_printable_oid());
                        if (pdu_error)
                            msg += tr("<font color=red><br>%3</font><br>")
                                           .arg(Snmp::error_msg(pdu_error));
                        else
                            msg += tr("<font color=red><br>%3</font><br>")
                                           .arg(vb_error==sNMP_SYNTAX_NOSUCHOBJECT?
                                            "No Such Object":"No Such Instance");
                    }
                    else
                    {
                        /* Unknown OID */
                        msg += tr("%1: %2    <font color=blue>%3</font>")
                            .arg(objects)
                            .arg(vb.get_printable_oid())
                            .arg(vb.get_printable_value());
                    }
                }
            }
        }
        else
            goto end;
        
        // TextEdit append is too slow ... :-( 
        // Buffer each 10 objects before displaying.
        if (!(objects%10))
        {
            s->MainUI()->Query->append(msg);
            msg = "";
        }
        else
            msg += "<br>";
    } // for  

    // Walk request, reissue a get_bulk ...
    if (iswalk && (stop == false))
    {
        // Issue next get_bulk ...
        // last vb becomes seed of next request
        pdu.set_vblist(&vb, 1);
 
        // Now do an async get_bulk
        if (!activeRequestContext)
            goto cleanup;
        status = snmp->get_bulk(pdu, target,
                                activeRequestContext->nonRepeaters(),
                                activeRequestContext->maxRepetitions(),
                                callback_walk, this);

        // Could we send it?
        if (status == SNMP_CLASS_SUCCESS)
        {
            timer.start(ASYNC_TIMER_MSEC);
            return;
        }
        else
        {
            msg = tr("<font color=red>Could not send GETBULK request: %1</font>")
                           .arg(Snmp::error_msg(status));
            goto cleanup;
        }
    }
   
end:
    if (stop == true)
        msg += tr("<font color=red>-----SNMP query stopped-----</font><br>");
    else
        msg += tr("-----SNMP query finished-----<br>");
    msg += tr("<font color=#009000>Total # of Requests = ");
    msg += tr("%1<br>Total # of Objects = %2</font>")
                    .arg(requests).arg(objects);
cleanup:
    s->MainUI()->Query->append(msg);
    // Dont stop the timer, but put it back to the lower-rate trap timer value
    timer.start(TRAP_TIMER_MSEC);
    emit StartWalk(false);
    s->MainUI()->actionStop->setEnabled(false);
    activeRequestContext.reset();
}

void Agent::AsyncCallbackSet(int reason, Pdu &pdu, SnmpTarget &target)
{
    int pdu_error;
    int vb_error = 0;
    int pdu_index = 0;
    int start_index = 0;
    int z = 0;
    Vb vb;   // empty Vb
    (void)target;

    switch(reason)
    {
    case SNMP_CLASS_NOTIFICATION:
    case SNMP_CLASS_ASYNC_RESPONSE:
    case SNMP_CLASS_SESSION_DESTROYED:
        break;
    case SNMP_CLASS_TIMEOUT:
        msg = tr("<font color=red>Timeout</font>");
        goto cleanup;
    default:
        msg = tr("<font color=red>No response received: (%1) %2</font>")
                       .arg(reason).arg(Snmp::error_msg(reason));
        goto cleanup;
    }
    
    // Look at the error status of the Pdu
    pdu_error = pdu.get_error_status();

    if (pdu_error)
    {
        pdu_index = pdu.get_error_index();
        if (pdu_index > 0)
            start_index = objects = pdu_index-1;
        else
        {
            msg = tr("<font color=red>%1</font><br>")
                .arg(Snmp::error_msg(pdu_error));
            goto cleanup;
        }
    }

    // The Pdu must contain at least one Vb
    if (pdu.get_vb_count() == 0)
    {
        msg = tr("<font color=red>Pdu is empty</font>");
        goto cleanup;
    }

    for ( z=start_index; z < pdu.get_vb_count(); z++)
    {
        pdu.get_vb( vb, z );
         
        // look for var bind exception, applies to v2 only   
        if ( (vb_error = vb.get_syntax()) != sNMP_SYNTAX_ENDOFMIBVIEW )
        {          
            Oid tmp;
            vb.get_oid(tmp);

            if ((vb_error != sNMP_SYNTAX_NOSUCHOBJECT) && 
                (vb_error != sNMP_SYNTAX_NOSUCHINSTANCE))
                vb_error = 0;

            objects++;

            const auto resolved=ResolveMibObject(
                activeRequestContext?activeRequestContext->environment():MibEnvironmentPtr{},tmp);
            if (resolved.node)
            {
                if (vb_error || (pdu_error && (z+1 == pdu_index)))
                    msg += tr("<font color=red>ERROR on varbind #</font>");

                // Print the OID part
                msg += tr("%1: %2").arg(objects).arg(RenderMibOid(resolved.environment,tmp));

                if (vb_error || (pdu_error && (z+1 == pdu_index)))
                {
                    if (pdu_error)
                        msg += tr("<font color=red><br>%1</font><br>")
                                       .arg(Snmp::error_msg(pdu_error));
                    else
                        msg += tr("<font color=red><br>%1</font><br>")
                                       .arg(vb_error==sNMP_SYNTAX_NOSUCHOBJECT?
                                            tr("No Such Object"):tr("No Such Instance"));
                    goto end;
                }

                // Print the value part
                msg += tr("    <font color=blue>%1</font>")
                           .arg(RenderMibValue(resolved,vb).toHtmlEscaped());
            }
            else
            {
                /* Unknown OID */
                if (vb_error || (pdu_error && (z+1 == pdu_index)))
                {
                    msg += tr("<font color=red>ERROR on varbind #</font>%1: %2")
                                   .arg(objects).arg(vb.get_printable_oid());
                    if (pdu_error)
                        msg += tr("<font color=red><br>%3</font><br>")
                                       .arg(Snmp::error_msg(pdu_error));
                    else
                        msg += tr("<font color=red><br>%3</font><br>")
                                       .arg(vb_error==sNMP_SYNTAX_NOSUCHOBJECT?
                                            tr("No Such Object"):tr("No Such Instance"));
                }
                else
                {
                    msg += tr("%1: %2    <font color=blue>%3</font>")
                        .arg(objects)
                        .arg(vb.get_printable_oid())
                        .arg(vb.get_printable_value());
                }
            }
        }
        else
            goto end;
        
        // TextEdit append is too slow ... :-( 
        // Buffer each 10 objects before displaying.
        if (!(objects%10))
        {
            s->MainUI()->Query->append(msg);
            msg = "";
        }
        else
            msg += "<br>";
    } // for  

end:
    msg += tr("-----SNMP set finished-----<br>");

cleanup:
    s->MainUI()->Query->append(msg);
    // Dont stop the timer, but put it back to the lower-rate trap timer value
    timer.start(TRAP_TIMER_MSEC);
    activeRequestContext.reset();
}


void Agent::WalkFrom(const QString& oid)
{
    int status;
    SnmpRequestConfig requestConfig;
    
    // Initialize agent & pdu objects
    SnmpTarget *target;
    Pdu *pdu;
    if (SetupFromCurrentSelection(oid, &target, &pdu, false,
                                  &requestConfig) < 0)
        return;
    
    // Clear the Query window ...
    s->MainUI()->Query->clear();
    clearStructuredResults(s);
    s->MainUI()->Query->append(tr("<font color=black>-----SNMP query started-----</font>"));
    
    // Clear some global vars
    requests = 0;
    objects  = 0;
    msg = "";
    stop = false;
    emit StartWalk(true);
    s->MainUI()->actionStop->setEnabled(true);
 
    // Now do an async get_bulk
    BeginRequest(requestConfig, SnmpRequestOperation::Walk);
    status = snmp->get_bulk(*pdu, *target,
                            activeRequestContext->nonRepeaters(),
                            activeRequestContext->maxRepetitions(),
                            callback_walk, this);

    // Could we send it?
    if (status == SNMP_CLASS_SUCCESS)
    {
        timer.start(ASYNC_TIMER_MSEC);
    }
    else
    {
        msg = tr("<font color=red>Could not send GETBULK request: %1</font>")
                       .arg(Snmp::error_msg(status));
        s->MainUI()->Query->append(msg);
        activeRequestContext.reset();
    }
    
    delete target;
    delete pdu;
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
    int status;
    SnmpRequestConfig requestConfig;
    
    // Initialize agent & pdu objects
    SnmpTarget *target;
    Pdu *pdu;
    if (SetupFromCurrentSelection(oid, &target, &pdu, usevblist,
                                  &requestConfig) < 0)
        return;
    
    // Clear the Query window ...
    s->MainUI()->Query->clear();
    clearStructuredResults(s);
    s->MainUI()->Query->append(tr("<font color=black>-----SNMP query started-----</font>"));
    
    // Clear some global vars
    requests = 0;
    objects  = 0;
    msg = "";
    stop = false;

    // Now do an async get
    BeginRequest(requestConfig, SnmpRequestOperation::Get);
    status = snmp->get(*pdu, *target, callback, this);

    // Could we send it?
    if (status == SNMP_CLASS_SUCCESS)
    {
        timer.start(ASYNC_TIMER_MSEC);
    }
    else
    {
        msg = QString(tr("<font color=red>Could not send GET request: %1</font>"))
                       .arg(Snmp::error_msg(status));
        s->MainUI()->Query->append(msg);
        activeRequestContext.reset();
    }
    
    delete target;
    delete pdu;
}

void Agent::GetNext(const QString& oid, bool usevblist)
{
    int status;
    SnmpRequestConfig requestConfig;
    
    // Initialize agent & pdu objects
    SnmpTarget *target;
    Pdu *pdu;    
    if (SetupFromCurrentSelection(oid, &target, &pdu, usevblist,
                                  &requestConfig) < 0)
        return;
        
    // Clear the Query window ...
    s->MainUI()->Query->clear();
    clearStructuredResults(s);
    s->MainUI()->Query->append(tr("<font color=black>-----SNMP query started-----</font>"));
    
    // Clear some global vars
    requests = 0;
    objects  = 0;
    msg = "";
    stop = false;
 
    // Now do an async get_next
    BeginRequest(requestConfig, SnmpRequestOperation::GetNext);
    status = snmp->get_next(*pdu, *target, callback, this);

    // Could we send it?
    if (status == SNMP_CLASS_SUCCESS)
    {
        timer.start(ASYNC_TIMER_MSEC);
    }
    else
    {
        msg = QString(tr("<font color=red>Could not send GETNEXT request: %1</font>"))
                       .arg(Snmp::error_msg(status));
        s->MainUI()->Query->append(msg);
        activeRequestContext.reset();
    }
    
    delete target;
    delete pdu;
}

void Agent::GetBulk(const QString& oid, bool usevblist)
{
    int status;
    SnmpRequestConfig requestConfig;
    
    // Initialize agent & pdu objects
    SnmpTarget *target;
    Pdu *pdu;    
    if (SetupFromCurrentSelection(oid, &target, &pdu, usevblist,
                                  &requestConfig) < 0)
        return;
        
    // Clear the Query window ...
    s->MainUI()->Query->clear();
    clearStructuredResults(s);
    s->MainUI()->Query->append(tr("<font color=black>-----SNMP query started-----</font>"));
    
    // Clear some global vars
    requests = 0;
    objects  = 0;
    msg = "";
    stop = false;
 
    // Now do an async get_bulk
    BeginRequest(requestConfig, SnmpRequestOperation::GetBulk);
    status = snmp->get_bulk(*pdu, *target,
                            activeRequestContext->nonRepeaters(),
                            activeRequestContext->maxRepetitions(),
                            callback, this);

    // Could we send it?
    if (status == SNMP_CLASS_SUCCESS)
    {
        timer.start(ASYNC_TIMER_MSEC);
    }
    else
    {
        msg = tr("<font color=red>Could not send GETBULK request: %1</font>")
                       .arg(Snmp::error_msg(status));
        s->MainUI()->Query->append(msg);
        activeRequestContext.reset();
    }
    
    delete target;
    delete pdu;
}

void Agent::Set(const QString& oid, bool usevblist)
{
    int status;
    SnmpRequestConfig requestConfig;

    // Initialize agent & pdu objects
    SnmpTarget *target;
    Pdu *pdu;    
    if (SetupFromCurrentSelection(oid, &target, &pdu, usevblist,
                                  &requestConfig) < 0)
        return;

    // Clear the Query window ...
    s->MainUI()->Query->clear();
    clearStructuredResults(s);
    s->MainUI()->Query->append(tr("<font color=black>-----SNMP set started-----</font>"));

    // Clear some global vars
    requests = 0;
    objects = 0;
    msg = "";
    stop = false;

    // Now do an async set 
    BeginRequest(requestConfig, SnmpRequestOperation::Set);
    status = snmp->set(*pdu, *target, callback_set, this);

    // Could we send it?
    if (status == SNMP_CLASS_SUCCESS)
    {
        timer.start(ASYNC_TIMER_MSEC);
    }
    else
    {
        msg = tr("<font color=red>Could not send SET request: %1</font>")
            .arg(Snmp::error_msg(status));
        s->MainUI()->Query->append(msg);
        activeRequestContext.reset();
    }


    delete target;
    delete pdu;
}

void Agent::SetFrom(const QString& oid)
{
    // Create and run the mib selection dialog
    MibSelection ms(s, s->MainUI()->MIBTree, "Set", MIBSELECTION_SET|MIBSELECTION_VALUE);

    if (ms.run(oid))
    {
        Vb *vb = ms.GetVarbind();
        if (vb)
        {
            vblist.clear();
            vblist += *vb;
            Set(ms.GetOid(), true);
        }
    }
}

void Agent::Stop(void)
{
    stop = true;
    if (tableRunner)
        tableRunner->cancel();
    if (instanceRunner)
        instanceRunner->cancel();
}

void Agent::TableViewFrom(const QString& oid)
{
    if (tableRunner->isRunning())
        return;
    AgentRequestSelection selection;
    if (ResolveCurrentSelection(&selection) != AgentSelectionError::None)
        return;

    SnmpTarget *validationTarget;
    Pdu *validationPdu;
    SnmpRequestConfig config;
    if (Setup(selection, oid, &validationTarget, &validationPdu,
              false, &config) < 0)
        return;
    delete validationTarget;
    delete validationPdu;
    
    // Clear the Query window ...
    s->MainUI()->Query->clear();
    clearStructuredResults(s);
    s->MainUI()->Query->append(tr("<font color=black>-----SNMP query started-----</font>"));
    
    s->MainUI()->Query->append(tr("Collecting table objects, please wait ...<br>"));
    
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
        s->MainUI()->Query->append(tr("<font color=red>Abort, not a table or row entry</font>"));
        return;
    }
   
    /* If the oid is the table element, get the row entry element */ 
    if (tableNode)
    {
        if (!RenderEnvironmentNodeOid(rowNode, &poid))
        {
            s->MainUI()->Query->append(tr("<font color=red>Abort, not a table or row entry</font>"));
            return;
        }
    }
    SnmpTablePlan plan;
    plan.rowOid = poid;
    for (const QString &column : rowNode->columnOids)
    {
        const auto *node=environment->nodeByOid(column);
        if (!HasValidColumnInfo(node))
            continue;
        Oid columnOid;
        if (RenderEnvironmentNodeOid(node, &columnOid))
            plan.columns.append({node->name, columnOid});
    }
    const SnmpRequestContext context(config, SnmpRequestOperation::Walk,
                                     environment);
    s->MainUI()->actionStop->setEnabled(true);
    emit StartWalk(true);
    if (!tableRunner->start(context, plan,
                            std::make_unique<SnmpPlusTransport>(config)))
    {
        s->MainUI()->actionStop->setEnabled(false);
        emit StartWalk(false);
    }
}

void Agent::PresentTableResult(const SnmpTableResult &result)
{
    QString output = tr("<table border=\"1\"><tr bgcolor=yellow><td>Instance</td>");
    for (const SnmpTableColumn &column : result.columns)
        output += QString("<td>%1</td>").arg(column.name);
    output += QString("</tr>");
    for (const SnmpTableRow &row : result.rows)
    {
        output += QString("<tr><td bgcolor=pink>%1</td>").arg(row.instance);
        for (int i = 0; i < row.cells.size(); ++i)
        {
            if (!row.cells[i].available)
            {
                output += tr("<td>not available</td>");
                continue;
            }
            Vb vb = row.cells[i].varbind;
            Oid oid = result.columns[i].oid;
            Oid instanceOid;
            vb.get_oid(instanceOid);
            if (instanceOid.len() == 0) instanceOid = oid;
            appendStructuredResult(s, ResolveMibObject(result.environment, instanceOid),
                                   instanceOid, vb);
            output += QString("<td>%1</td>").arg(
                RenderMibValue(ResolveMibObject(result.environment,oid),vb).toHtmlEscaped());
        }
        output += QString("</tr>");
    }
    output += QString("</table>");
    if (result.status == SnmpOperationStatus::Cancelled)
        output += tr("<font color=red>Operation cancelled</font><br>");
    else if (result.status == SnmpOperationStatus::Timeout)
        output += tr("<font color=red>Timeout</font><br>");
    else if (result.status == SnmpOperationStatus::SnmpError)
        output += tr("<font color=red>SNMP error</font><br>");
    else if (result.status == SnmpOperationStatus::TransportFailure)
        output += tr("<font color=red>No response received</font><br>");
    output += tr("-----SNMP query finished-----<br>");
    output += QString("<font color=#009000>Total # of rows = %1<br>")
                  .arg(result.rows.size());
    s->MainUI()->Query->append(output);
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
    vbui->GetBulkOp->setEnabled(s->MainUI()->AgentProtoV1->isChecked()!=true);
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
        vbui->GetBulkOp->setEnabled(s->MainUI()->AgentProtoV1->isChecked()!=true);
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
            vbui->GetBulkOp->setEnabled(s->MainUI()->AgentProtoV1->isChecked()!=true);
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
                QMessageBox::Ok, Qt::NoButton);
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
    s->MainUI()->actionStop->setEnabled(false);
    emit StartWalk(false);
    if (result.status != SnmpOperationStatus::Complete || result.instances.isEmpty())
        return;

    // Build the instance selection dialog and show it ...
    QDialog dlist(s->MainUI()->MIBTree, Qt::WindowTitleHint);
    dlist.resize(220, 250);
    QGridLayout gl1(&dlist);
    QGridLayout gl2;
    QLabel label(tr("Please select table instance to query"), &dlist);
    gl2.addWidget(&label, 0, 0, 1, 1);
    QListWidget ilist(&dlist);
    gl2.addWidget(&ilist, 1, 0, 1, 1);
    QDialogButtonBox box(QDialogButtonBox::Ok, Qt::Horizontal, &dlist);
    gl2.addWidget(&box, 2, 0, 1, 1);
    gl1.addLayout(&gl2, 0, 0, 1, 1);
    dlist.setWindowTitle(tr("Select Instance"));
    QMetaObject::connectSlotsByName(&dlist);
    connect(&ilist, SIGNAL(itemDoubleClicked(QListWidgetItem *)), 
            &dlist, SLOT(accept()));
    connect(&box, SIGNAL(accepted()), &dlist, SLOT(accept()));
    dlist.setModal(true);
    dlist.show();
    dlist.raise();
    dlist.activateWindow();

    ilist.addItems(result.instances);
    dlist.exec();

    if (ilist.selectedItems().isEmpty()) return;
    const QString requestOid = pendingInstanceOid + "." +
                               ilist.selectedItems().at(0)->text();
    if (pendingInstanceOperation == 0) Get(requestOid);
    else if (pendingInstanceOperation == 1) GetNext(requestOid);
    else if (pendingInstanceOperation == 2) GetBulk(requestOid);
}

int Agent::SelectTableInstance(const QString &oid, QString &outinstance)
{
    AgentRequestSelection selection;
    if (ResolveCurrentSelection(&selection) != AgentSelectionError::None) return 0;
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
    QDialog dialog(s->MainUI()->MIBTree, Qt::WindowTitleHint);
    QVBoxLayout layout(&dialog);
    QListWidget list(&dialog);
    list.addItems(result.instances);
    QDialogButtonBox buttons(QDialogButtonBox::Ok, &dialog);
    layout.addWidget(&list);
    layout.addWidget(&buttons);
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(&list, &QListWidget::itemDoubleClicked, &dialog, &QDialog::accept);
    if (dialog.exec() != QDialog::Accepted || list.selectedItems().isEmpty()) return 0;
    outinstance = list.selectedItems().first()->text();
    return 1;
}

void Agent::GetFromSelectInstance(const QString& oid, int op)
{
    if (instanceRunner->isRunning()) return;
    AgentRequestSelection selection;
    if (ResolveCurrentSelection(&selection) != AgentSelectionError::None) return;
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
    instanceRunner->start(SnmpRequestContext(config, SnmpRequestOperation::Walk,
                                             environment),
                          root, std::make_unique<SnmpPlusTransport>(config));
}

// Callback when the linedit edition is finished in the prompt dialog.
void Agent::GetTypedTableInstance(void)
{
    tinstresult = le->text();
}

void Agent::GetFromPromptInstance(const QString& oid, int op)
{
    int res;

    QDialog dprompt(s->MainUI()->MIBTree, Qt::WindowTitleHint);
    dprompt.resize(370, 60);
    QGridLayout gl(&dprompt);
    QLabel label(tr("Please type table instance to query"), &dprompt);
    gl.addWidget(&label, 0, 0, 1, 1);
    QDialogButtonBox box(QDialogButtonBox::Ok, Qt::Vertical, &dprompt);
    gl.addWidget(&box, 0, 1, 2, 1);
    le = new QLineEdit(&dprompt);
    le->setFocus(Qt::OtherFocusReason);
    gl.addWidget(le, 1, 0, 1, 1);
    dprompt.setWindowTitle(tr("Type Instance"));
    QObject::connect(&box, SIGNAL(accepted()), &dprompt, SLOT(accept()));
    QMetaObject::connectSlotsByName(&dprompt);
    connect(le, SIGNAL(editingFinished(void)), 
            this, SLOT(GetTypedTableInstance(void)));

    // Wait for the result and then query the proper instance
    res = dprompt.exec();
    switch(op)
    {
    case 0:
        Get(oid + (res?("." + tinstresult):".0"));
        break;
    case 1:
        GetNext(oid + (res?("." + tinstresult):".0"));
        break;
    case 2:
        GetBulk(oid + (res?("." + tinstresult):".0"));
        break;
    default:
        break;
    }

    delete le;
}

