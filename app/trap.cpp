#include "trap.h"
#include "preferences.h"
#include "snmpb.h"
#include "mibmodule.h"
#include "trapview.h"

Trap::Trap(Snmpb *snmpb)
    : s(snmpb), trapService(snmpb->PreferencesObj()->GetTrapHistoryLimit(), this)
{
    connect(s->PreferencesObj(), &Preferences::trapHistoryLimitChanged,
        &trapService, &TrapService::setMaximumRecords);
    auto *view = new TrapView(&trapService, s->MainUI()->TrapLog,
        s->MainUI()->TrapContent, s->MainUI()->TrapInfo,
        [snmpb] { return snmpb->PreferencesObj()->GetExpandTrapBinding(); }, this);
    connect(s->MibModuleObj(), &MibModule::profileRuntimeReady, view,
        [view] { view->refreshEnvironment(); });
}

Trap::~Trap() { trapService.stop(); }

TrapService *Trap::service() { return &trapService; }

bool Trap::Receive(const Pdu &pdu, const TrapEndpoint &endpoint,
                   const QDateTime &received)
{
    return trapService.receive(pdu, endpoint, received);
}
