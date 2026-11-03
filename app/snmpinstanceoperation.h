#ifndef SNMPINSTANCEOPERATION_H
#define SNMPINSTANCEOPERATION_H

#include "snmptableoperation.h"

struct SnmpInstanceResult
{
    SnmpOperationStatus status = SnmpOperationStatus::TransportFailure;
    QStringList instances;
    int requests = 0;
    int transportStatus = 0;
    int snmpErrorStatus = 0;
    QString problem;   // why the device refused, when it did (e.g. an SNMPv3 REPORT)
};

class SnmpInstanceOperation
{
public:
    SnmpInstanceOperation(const SnmpRequestContext &context, const Oid &root);
    SnmpInstanceOperation(const SnmpRequestContext &context, const QVector<Oid> &roots);
    SnmpInstanceResult execute(ISnmpTransport &transport,
                               const SnmpCancellationToken &cancellation) const;
private:
    SnmpRequestContext requestContext;
    QVector<Oid> rootOids;
    bool tableDiscovery = false;
};

Q_DECLARE_METATYPE(SnmpInstanceResult)

#endif
