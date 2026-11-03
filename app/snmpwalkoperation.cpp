#include "snmpwalkoperation.h"
#include "snmpreport.h"
#include "tabletraversal.h"

#include <algorithm>
#include <QObject>

SnmpWalkOperation::SnmpWalkOperation(const SnmpRequestContext &context, const Oid &root,
                                     const SnmpWalkLimits &limits)
    : requestContext(context), rootOid(root), walkLimits(limits) {}

SnmpWalkResult SnmpWalkOperation::execute(ISnmpTransport &transport,
                                          const SnmpCancellationToken &cancellation,
                                          const Sink &sink) const
{
    SnmpWalkResult result;
    const bool bulk = requestContext.config().version != SnmpRequestVersion::V1;
    int repetitions = std::max(1, requestContext.maxRepetitions());
    Oid cursor = rootOid;       // the last OID received; the next request asks for what follows it

    while (true) {
        if (cancellation.isCancelled()) {
            result.status = SnmpOperationStatus::Cancelled;
            return result;
        }
        if (result.requests >= walkLimits.maxRequests) {
            result.problem = QObject::tr("Stopped after %1 requests.").arg(walkLimits.maxRequests);
            result.status = SnmpOperationStatus::Complete;
            return result;
        }

        SnmpTransportRequest request;
        request.operation = bulk ? SnmpTransportOperation::GetBulk : SnmpTransportOperation::GetNext;
        Vb seed;
        seed.set_oid(cursor);
        request.pdu += seed;
        request.nonRepeaters = 0;
        request.maxRepetitions = bulk ? repetitions : 0;
        const SnmpTransportResult response = transport.execute(request);

        if (response.status == SnmpOperationStatus::SnmpError) {
            const int code = response.snmpErrorStatus;
            if (code == SNMP_ERROR_NO_SUCH_NAME) {   // SNMPv1 ends a walk this way
                result.status = SnmpOperationStatus::Complete;
                return result;
            }
            if (code == SNMP_ERROR_TOO_BIG && bulk && repetitions > 1) {
                repetitions = std::max(1, repetitions / 2);
                continue;
            }
            result.status = response.status;
            result.snmpErrorStatus = code;
            result.snmpErrorIndex = response.snmpErrorIndex;
            return result;
        }
        if (response.status != SnmpOperationStatus::Success) {
            result.status = response.status;
            result.transportStatus = response.transportStatus;
            result.snmpErrorStatus = response.snmpErrorStatus;
            if (response.status == SnmpOperationStatus::TransportFailure && response.transportStatus != 0)
                result.problem = ExplainSnmpTransportFailure(response.transportStatus,
                                                             requestContext.config().securityName);
            return result;
        }

        // An SNMPv3 device that refuses the request answers with a REPORT (an error counter),
        // which would otherwise be listed as if it were data.
        const QString report = ExplainSnmpReport(response.pdu, requestContext.config().securityName);
        if (!report.isEmpty()) {
            result.problem = report;
            result.status = SnmpOperationStatus::SnmpError;
            return result;
        }

        const int varbinds = response.pdu.get_vb_count();
        if (varbinds == 0) {
            result.status = SnmpOperationStatus::Complete;
            return result;
        }
        ++result.requests;

        QVector<Vb> found;
        bool finished = false;
        for (int i = 0; i < varbinds && !finished; ++i) {
            Vb vb;
            response.pdu.get_vb(vb, i);
            const unsigned int syntax = vb.get_syntax();
            const Oid returned = vb.get_oid();
            if (syntax == sNMP_SYNTAX_ENDOFMIBVIEW || syntax == sNMP_SYNTAX_NOSUCHOBJECT ||
                syntax == sNMP_SYNTAX_NOSUCHINSTANCE || !IsOidInSubtree(returned, rootOid)) {
                finished = true;
            } else if (!(returned > cursor)) {
                // An agent that answers with an OID that is not after the one asked about would
                // make the walk loop until the user pressed Stop.
                result.problem = QObject::tr("The device returned %1 after %2, which is not in "
                                             "increasing order, so the walk was not read further.")
                    .arg(QString::fromLatin1(returned.get_printable()),
                         QString::fromLatin1(cursor.get_printable()));
                finished = true;
            } else {
                found.append(vb);
                cursor = returned;
                if (++result.objects >= walkLimits.maxObjects) {
                    result.problem = QObject::tr("Stopped after %1 objects.").arg(walkLimits.maxObjects);
                    finished = true;
                }
            }
        }
        if (!found.isEmpty()) {
            if (sink) sink(found);
            else result.varbinds += found;
        }
        if (finished) {
            result.status = SnmpOperationStatus::Complete;
            return result;
        }
    }
}
