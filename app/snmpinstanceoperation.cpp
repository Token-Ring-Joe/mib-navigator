#include "snmpinstanceoperation.h"
#include "snmpreport.h"
#include "tabletraversal.h"
#include <map>
#include <QObject>

SnmpInstanceOperation::SnmpInstanceOperation(const SnmpRequestContext &context,
                                             const Oid &root)
    : requestContext(context), rootOids{root} {}

SnmpInstanceOperation::SnmpInstanceOperation(const SnmpRequestContext &context, const QVector<Oid> &roots)
    : requestContext(context), rootOids(roots), tableDiscovery(true) {}

SnmpInstanceResult SnmpInstanceOperation::execute(
    ISnmpTransport &transport, const SnmpCancellationToken &cancellation) const
{
    SnmpInstanceResult result;
    if (tableDiscovery) {
        // Reuse the sparse-aware table traversal and its batching, tooBig fallback,
        // version handling and bounds. Discovery retains only unique row suffixes.
        SnmpTablePlan plan; plan.instancesOnly = true;
        for (const auto &root : rootOids) plan.columns.append({QString(), root});
        const auto table = SnmpTableOperation(requestContext, plan).execute(transport, cancellation);
        result.status = cancellation.isCancelled() ? SnmpOperationStatus::Cancelled : table.status;
        result.requests = table.requests;
        result.transportStatus = table.transportStatus;
        result.snmpErrorStatus = table.snmpErrorStatus;
        result.problem = table.problem;
        if (result.status == SnmpOperationStatus::Complete && !result.problem.isEmpty())
            result.status = SnmpOperationStatus::TransportFailure;
        if (result.status == SnmpOperationStatus::Complete)
            for (const auto &row : table.rows) result.instances.append(row.instance);
        return result;
    }
    struct Less { bool operator()(const Oid &a, const Oid &b) const { return a < b; } };
    std::map<Oid, QString, Less> found;
    for (const Oid &rootOid : rootOids) {
        Oid nextOid = rootOid;
        while (true)
        {
            if (cancellation.isCancelled())
            {
                result.status = SnmpOperationStatus::Cancelled;
                return result;
            }
            if (result.requests >= 2000000 || found.size() >= 250000) {
                result.status = SnmpOperationStatus::TransportFailure;
                result.problem = QObject::tr("Stopped: too many table instances or requests.");
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
            if (requestContext.config().version == SnmpRequestVersion::V1 &&
                response.status == SnmpOperationStatus::SnmpError && response.snmpErrorStatus == SNMP_ERROR_NO_SUCH_NAME)
                break;
            if (response.status != SnmpOperationStatus::Success)
            {
                result.status = response.status;
                result.transportStatus = response.transportStatus;
                result.snmpErrorStatus = response.snmpErrorStatus;
                if (response.status == SnmpOperationStatus::TransportFailure && response.transportStatus != 0)
                    result.problem = ExplainSnmpTransportFailure(response.transportStatus,
                                                                 requestContext.config().securityName);
                else if (response.status == SnmpOperationStatus::Timeout && result.requests == 1)
                    result.problem = ExplainSnmpTimeout(requestContext.config());   // nothing answered yet
                return result;
            }
            const QString report = ExplainSnmpReport(response.pdu, requestContext.config().securityName);
            if (!report.isEmpty())
            {
                result.problem = report;
                result.status = SnmpOperationStatus::SnmpError;
                return result;
            }
            if (!HasVarbindAt(response.pdu.get_vb_count(), 0))
            {
                break;
            }
            response.pdu.get_vb(vb, 0);
            const Oid returned = vb.get_oid();
            if (vb.get_syntax() == sNMP_SYNTAX_ENDOFMIBVIEW ||
                vb.get_syntax() == sNMP_SYNTAX_NOSUCHOBJECT || vb.get_syntax() == sNMP_SYNTAX_NOSUCHINSTANCE ||
                !IsOidInSubtree(returned, rootOid))
            {
                break;
            }
            QString instance;
            if (!ExtractOidSuffix(rootOid, returned, &instance) || instance.isEmpty())
            {
                break;
            }
            if (!(returned > nextOid)) {
                result.status = SnmpOperationStatus::TransportFailure;
                result.problem = QObject::tr("The device returned instances out of order; instance discovery stopped.");
                return result;
            }
            found.emplace(Oid(instance.toLatin1().constData()), instance);
            nextOid = returned;
        }
    }
    if (cancellation.isCancelled()) { result.status = SnmpOperationStatus::Cancelled; return result; }
    for (const auto &entry : found) result.instances.append(entry.second);
    result.status = SnmpOperationStatus::Complete;
    return result;
}
