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
#ifndef AGENT_H
#define AGENT_H
#include <memory>
#include <qtimer.h>

#include "snmpb.h"
#include "mibview.h"
#include "trap.h"
#include "mibselection.h"
#include "agentrequestselection.h"
#include "connectionmodel.h"
#include "snmprequestconfig.h"
#include "snmprequestcontext.h"
#include "snmptableasyncrunner.h"
#include "snmpinstanceasyncrunner.h"
#include "snmpwalkasyncrunner.h"
#include "snmpsingleasyncrunner.h"
#include "ui_varbinds.h"
#include "snmp_pp/snmp_pp.h"

class ConnectionBar;

class Agent: public QObject
{
    Q_OBJECT
    
public:
    // The last Query Table as header + one record per row; empty when the last request was not a table.
    const QList<QStringList> &LastTableRecords() const { return lastTable; }
    Agent(Snmpb *snmpb, bool offline = false);
    ~Agent() override;
    bool GetStartupResult(QString &Err);
    void StartTrapTimer(void);
    void Init(void);
    void AsyncCallbackTrap(int reason, Pdu &pdu, SnmpTarget &target);
    
    void ConfigTargetFromSettings(const SnmpRequestConfig& config,
                                  SnmpTarget *target);
    Oid ConfigPduFromSettings(const SnmpRequestConfig& config,
                              const QString& oid, Pdu *p,
                              bool usevblist = false);

    int SelectTableInstance(const QString& oid, QString& outinstance);

    // The connection bar (Target, Credential, MIB Profile) is the authority for who a
    // request goes to and how it authenticates.
    void SetConnectionBar(ConnectionBar *bar);
    // Every request is recorded here, with how it ended.
    void SetRequestLog(class RequestLog *log) { requestLog = log; }

    inline USM *GetUSMObj(void) { return v3mp->get_usm(); }

protected:
    int SetupFromCurrentSelection(const QString& oid, SnmpTarget **t, Pdu **p,
              bool usevblist = false,
              SnmpRequestConfig *resolvedConfig = nullptr);
    int Setup(const AgentRequestSelection &selection, const QString& oid,
              SnmpTarget **t, Pdu **p, bool usevblist = false,
              SnmpRequestConfig *resolvedConfig = nullptr);
    AgentSelectionError ResolveCurrentSelection(
        AgentRequestSelection *selection) const;
    // Same as above for a request the user just asked for: explains what is missing.
    bool ResolveForRequest(AgentRequestSelection *selection);
    void BeginRequest(const SnmpRequestConfig &config,
                      SnmpRequestOperation operation);

private:
    void ConnectionSelectionChanged();
    void UpdateTableQueryAvailability();
    ConnectionBar *connectionBar = nullptr;
    class RequestLog *requestLog = nullptr;
    quint64 activeLogId = 0;
    void finishLog(const QString &resultHtml, int objectCount);
    quint64 startLog(const QString &name, const SnmpRequestConfig &config, const QString &oid);
    mutable ConnectionError lastConnectionError = ConnectionError::None;
    QString GetValueString(MibSelection &ms, Vb* vb);
    void VarbindsBuildList(void);
    void QueryTableFrom(const QString &oid, bool selectInstance);
    void StartTableQuery(const SnmpRequestContext &context, const SnmpTablePlan &plan, const QString &oid);
    void PresentTableResult(const SnmpTableResult &result);
    void PresentInstanceResult(const SnmpInstanceResult &result);
    void RunSingleRequest(const QString& oid, bool usevblist, SnmpRequestOperation operation);
    void PresentSingleResult(const SnmpSingleResult &result);
    void FinishQuery(const struct SnmpQueryText &text, bool restoreControls = true);
    void HoldResultSorting();
    void ReleaseResultSorting();
    bool resultSortingHeld = false;
    bool resultSortingWasOn = false;
    void PresentWalkBatch(const QVector<Vb> &batch);
    void PresentWalkResult(const SnmpWalkResult &result);

public slots:
    void WalkFrom(const QString& oid);
    void Get(const QString& oid, bool usevblist = false);
    void GetNext(const QString& oid, bool usevblist = false);
    void GetBulk(const QString& oid, bool usevblist = false);
    void GetFrom(const QString& oid, int op);
    void GetFromPromptInstance(const QString& oid, int op);
    void GetFromSelectInstance(const QString& oid, int op);
    void Set(const QString& oid, bool usevblist = false);
    void SetVarbinds(const QVector<Vb> &varbinds, const QString& oid = QString());
    void SetFrom(const QString& oid);
    void Stop(void);
    void TableViewFrom(const QString& oid);
    void TableRowSelectFrom(const QString& oid);
    void Varbinds(void);
    void VarbindsFrom(const QString& oid);
    void GetTypedTableInstance(void);
    void StopTimer(void);
    void Shutdown();

protected slots:
    void TimerExpired(void);    
    void VarbindsNew(void);
    void VarbindsEdit(void);
    void VarbindsDelete(void);
    void VarbindsDeleteAll(void);
    void VarbindsMoveUp(void);
    void VarbindsMoveDown(void);
    void VarbindsQuit(void);
    void VarbindsGet(void);
    void VarbindsGetNext(void);
    void VarbindsGetBulk(void);
    void VarbindsSet(void);
    void VarbindsSelected(void);

signals:
    void StartWalk(bool);

private:
    Snmpb *s;
    bool start_result;
    bool offline;
    bool shutdownComplete = false;
    QString start_err;
 
    Snmp *snmp;
    v3MP *v3mp;
    QTimer timer;
    
    int requests;
    int objects;
    QString msg;
    Oid theoid;

    QLineEdit *le;
    QString tinstresult;
 
    bool stop;

    QVector<Vb> vblist;

    std::unique_ptr<SnmpRequestContext> activeRequestContext;
    SnmpTableAsyncRunner *tableRunner = nullptr;
    QList<QStringList> lastTable;
    bool tableQueryPending = false;
    SnmpInstanceAsyncRunner *instanceRunner = nullptr;
    SnmpWalkAsyncRunner *walkRunner = nullptr;
    SnmpSingleAsyncRunner *singleRunner = nullptr;
    std::unique_ptr<SnmpRequestContext> pendingTableRowContext;
    SnmpTablePlan pendingTableRowPlan;
    QString pendingInstanceOid;
    int pendingInstanceOperation = 0;

    Ui_Varbinds *vbui;
    QDialog *vbd;
};

#endif /* AGENT_H */
