#include "snmpinstanceoperation.h"
#include "tabletraversal.h"

SnmpInstanceOperation::SnmpInstanceOperation(const SnmpRequestContext &context,
                                             const Oid &root)
    : requestContext(context), rootOid(root) {}

SnmpInstanceResult SnmpInstanceOperation::execute(
    ISnmpTransport &transport, const SnmpCancellationToken &cancellation) const
{
    SnmpInstanceResult result;
    Oid nextOid = rootOid;
    while (true)
    {
        if (cancellation.isCancelled())
        {
            result.status = SnmpOperationStatus::Cancelled;
            return result;
        }
        Vb vb(nextOid);
        SnmpTransportRequest request;
        request.operation = SnmpTransportOperation::GetNext;
        request.pdu += vb;
        request.nonRepeaters = requestContext.nonRepeaters();
        request.maxRepetitions = requestContext.maxRepetitions();
        const SnmpTransportResult response = transport.execute(request);
        ++result.requests;
        if (response.status != SnmpOperationStatus::Success)
        {
            result.status = response.status;
            result.transportStatus = response.transportStatus;
            result.snmpErrorStatus = response.snmpErrorStatus;
            return result;
        }
        if (!HasVarbindAt(response.pdu.get_vb_count(), 0))
        {
            result.status = SnmpOperationStatus::Complete;
            return result;
        }
        response.pdu.get_vb(vb, 0);
        const Oid returned = vb.get_oid();
        if (vb.get_syntax() == sNMP_SYNTAX_ENDOFMIBVIEW ||
            !IsOidInSubtree(returned, rootOid))
        {
            result.status = SnmpOperationStatus::Complete;
            return result;
        }
        QString instance;
        if (!ExtractOidSuffix(rootOid, returned, &instance) || instance.isEmpty())
        {
            result.status = SnmpOperationStatus::Complete;
            return result;
        }
        result.instances.append(instance);
        nextOid = returned;
    }
}
