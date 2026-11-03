#ifndef SNMPTABLEOPERATION_H
#define SNMPTABLEOPERATION_H

#include "snmprequestcontext.h"
#include "snmptransport.h"
#include "tableindex.h"

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
    QString instance; // empty: full table; otherwise exact GETs for this one row
    Oid rowOid;
    QVector<SnmpTableColumn> columns;
    QVector<TableIndexSpec> indexSpecs;   // optional; lets rows show their decoded INDEX values
    bool instancesOnly = false;          // discovery: keep suffixes, discard cell values
};

struct SnmpTableCell
{
    bool available = false;
    Vb varbind;
};

struct SnmpTableRow
{
    QString instance;                 // the raw sub-identifiers, e.g. "4.101.116.104.48"
    QString indexText;                // decoded INDEX values, empty if they could not be decoded
    QVector<SnmpTableCell> cells;
};

struct SnmpTableResult
{
    QString requestedInstance; // preserved even if the row disappeared
    SnmpOperationStatus status = SnmpOperationStatus::TransportFailure;
    QVector<SnmpTableColumn> columns;
    QVector<SnmpTableRow> rows;
    int requests = 0;
    int transportStatus = 0;
    int snmpErrorStatus = 0;
    QString problem;                  // set when the table is incomplete for a reason worth telling
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
