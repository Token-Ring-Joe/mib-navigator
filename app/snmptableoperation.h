#ifndef SNMPTABLEOPERATION_H
#define SNMPTABLEOPERATION_H

#include "snmprequestcontext.h"
#include "snmptransport.h"

#include <QAtomicInteger>
#include <QMetaType>
#include <QString>
#include <QVector>

struct SnmpTableColumn
{
    QString name;
    Oid oid;
};

struct SnmpTablePlan
{
    Oid rowOid;
    QVector<SnmpTableColumn> columns;
};

struct SnmpTableCell
{
    bool available = false;
    Vb varbind;
};

struct SnmpTableRow
{
    QString instance;
    QVector<SnmpTableCell> cells;
};

struct SnmpTableResult
{
    SnmpOperationStatus status = SnmpOperationStatus::TransportFailure;
    QVector<SnmpTableColumn> columns;
    QVector<SnmpTableRow> rows;
    int requests = 0;
    int transportStatus = 0;
    int snmpErrorStatus = 0;
    MibEnvironmentPtr environment;
};

class SnmpCancellationToken
{
public:
    void cancel();
    bool isCancelled() const;
private:
    QAtomicInteger<bool> cancelled = false;
};

class SnmpTableOperation
{
public:
    SnmpTableOperation(const SnmpRequestContext &context,
                       const SnmpTablePlan &plan);
    SnmpTableResult execute(ISnmpTransport &transport,
                            const SnmpCancellationToken &cancellation) const;

private:
    SnmpRequestContext requestContext;
    SnmpTablePlan tablePlan;
};

Q_DECLARE_METATYPE(SnmpTableResult)

#endif
