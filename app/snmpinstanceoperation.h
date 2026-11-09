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
};

class SnmpInstanceOperation
{
public:
    SnmpInstanceOperation(const SnmpRequestContext &context, const Oid &root);
    SnmpInstanceResult execute(ISnmpTransport &transport,
                               const SnmpCancellationToken &cancellation) const;
private:
    SnmpRequestContext requestContext;
    Oid rootOid;
};

Q_DECLARE_METATYPE(SnmpInstanceResult)

#endif
