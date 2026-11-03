#ifndef SNMPWALKOPERATION_H
#define SNMPWALKOPERATION_H

#include "snmptableoperation.h"

#include <QMetaType>
#include <QString>
#include <QVector>
#include <functional>

struct SnmpWalkLimits
{
    int maxRequests = 2000000;   // a runaway walk stops here with a reason
    int maxObjects = 2000000;
};

struct SnmpWalkResult
{
    // Complete: the walk reached its natural end (or stopped for a reason in `problem`).
    // Cancelled, Timeout, SnmpError and TransportFailure keep what was read before them.
    SnmpOperationStatus status = SnmpOperationStatus::TransportFailure;
    QVector<Vb> varbinds;        // every object found, unless the caller took them through a sink
    int objects = 0;
    int requests = 0;            // answers received that carried varbinds
    int transportStatus = 0;
    int snmpErrorStatus = 0;
    int snmpErrorIndex = 0;
    QString problem;             // why the walk ended early, or why the device refused it
};

// Walks the subtree under `root` with GETBULK (SNMPv2c/v3) or GETNEXT (SNMPv1). No widgets.
// Non-repeaters are always 0 for a walk; max-repetitions come from the request context.
class SnmpWalkOperation
{
public:
    // Receives each answer's objects as they arrive (for progressive display).
    using Sink = std::function<void(const QVector<Vb> &)>;

    SnmpWalkOperation(const SnmpRequestContext &context, const Oid &root,
                      const SnmpWalkLimits &limits = {});
    SnmpWalkResult execute(ISnmpTransport &transport,
                           const SnmpCancellationToken &cancellation,
                           const Sink &sink = {}) const;

private:
    SnmpRequestContext requestContext;
    Oid rootOid;
    SnmpWalkLimits walkLimits;
};

Q_DECLARE_METATYPE(SnmpWalkResult)

#endif
