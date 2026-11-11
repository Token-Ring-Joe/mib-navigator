#include "snmpsingleoperation.h"
#include "snmpreport.h"

SnmpSingleOperation::SnmpSingleOperation(const SnmpRequestContext &context,
                                         const QVector<Vb> &varbinds)
    : requestContext(context), requestVarbinds(varbinds) {}

SnmpSingleResult SnmpSingleOperation::execute(ISnmpTransport &transport) const
{
    SnmpTransportRequest request;
    switch (requestContext.operation()) {
    case SnmpRequestOperation::GetNext: request.operation = SnmpTransportOperation::GetNext; break;
    case SnmpRequestOperation::GetBulk: request.operation = SnmpTransportOperation::GetBulk; break;
    case SnmpRequestOperation::Set: request.operation = SnmpTransportOperation::Set; break;
    default: request.operation = SnmpTransportOperation::Get; break;
    }
    for (const Vb &vb : requestVarbinds) {
        Vb copy = vb;
        request.pdu += copy;
    }
    request.nonRepeaters = requestContext.nonRepeaters();
    request.maxRepetitions = requestContext.maxRepetitions();

    const SnmpTransportResult response = transport.execute(request);
    SnmpSingleResult result;
    result.status = response.status;
    result.pdu = response.pdu;
    result.transportStatus = response.transportStatus;
    result.snmpErrorStatus = response.snmpErrorStatus;
    result.snmpErrorIndex = response.snmpErrorIndex;

    if (response.status == SnmpOperationStatus::Success) {
        // An SNMPv3 device that refuses the request answers with a REPORT (an error counter);
        // never present it as data.
        const QString report = ExplainSnmpReport(response.pdu, requestContext.config().securityName);
        if (!report.isEmpty()) {
            result.problem = report;
            result.status = SnmpOperationStatus::SnmpError;
        }
    } else if (response.status == SnmpOperationStatus::TransportFailure && response.transportStatus != 0) {
        result.problem = ExplainSnmpTransportFailure(response.transportStatus,
                                                     requestContext.config().securityName);
    } else if (response.status == SnmpOperationStatus::Timeout) {
        result.problem = ExplainSnmpTimeout(requestContext.config());
    }
    return result;
}
