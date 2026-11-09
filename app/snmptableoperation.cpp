#include "snmptableoperation.h"
#include "tabletraversal.h"

namespace
{
Pdu pduForOid(const Oid &oid)
{
    Vb vb;
    vb.set_oid(oid);
    Pdu pdu;
    pdu += vb;
    return pdu;
}

bool unavailable(const SnmpTransportResult &response, const Vb &vb)
{
    return response.status != SnmpOperationStatus::Success ||
           response.snmpErrorStatus == SNMP_ERROR_NO_SUCH_NAME ||
           vb.get_syntax() == sNMP_SYNTAX_NOSUCHOBJECT ||
           vb.get_syntax() == sNMP_SYNTAX_NOSUCHINSTANCE;
}
}

void SnmpCancellationToken::cancel() { cancelled.storeRelease(true); }
bool SnmpCancellationToken::isCancelled() const { return cancelled.loadAcquire(); }

SnmpTableOperation::SnmpTableOperation(const SnmpRequestContext &context,
                                       const SnmpTablePlan &plan)
    : requestContext(context), tablePlan(plan) {}

SnmpTableResult SnmpTableOperation::execute(
    ISnmpTransport &transport,
    const SnmpCancellationToken &cancellation) const
{
    SnmpTableResult result;
    result.columns = tablePlan.columns;
    result.environment = requestContext.environment();
    Oid nextOid = tablePlan.rowOid;
    Oid firstColumnRoot;
    bool haveFirstColumn = false;

    while (true)
    {
        if (cancellation.isCancelled())
        {
            result.status = SnmpOperationStatus::Cancelled;
            return result;
        }
        SnmpTransportRequest next;
        next.operation = SnmpTransportOperation::GetNext;
        next.pdu = pduForOid(nextOid);
        next.nonRepeaters = requestContext.nonRepeaters();
        next.maxRepetitions = requestContext.maxRepetitions();
        const SnmpTransportResult response = transport.execute(next);
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
        Vb walked;
        response.pdu.get_vb(walked, 0);
        const Oid returned = walked.get_oid();
        if (walked.get_syntax() == sNMP_SYNTAX_ENDOFMIBVIEW)
        {
            result.status = SnmpOperationStatus::Complete;
            return result;
        }
        if (!haveFirstColumn)
        {
            if (!BuildFirstColumnRoot(tablePlan.rowOid, returned,
                                      &firstColumnRoot))
            {
                result.status = SnmpOperationStatus::Complete;
                return result;
            }
            haveFirstColumn = true;
        }
        if (!IsOidInSubtree(returned, firstColumnRoot))
        {
            result.status = SnmpOperationStatus::Complete;
            return result;
        }

        SnmpTableRow row;
        if (!ExtractOidSuffix(firstColumnRoot, returned, &row.instance) ||
            row.instance.isEmpty())
        {
            result.status = SnmpOperationStatus::Complete;
            return result;
        }

        for (const SnmpTableColumn &column : tablePlan.columns)
        {
            if (cancellation.isCancelled())
            {
                result.status = SnmpOperationStatus::Cancelled;
                return result;
            }
            Oid cellOid = column.oid;
            cellOid += row.instance.toLatin1().constData();
            SnmpTransportRequest get;
            get.operation = SnmpTransportOperation::Get;
            get.pdu = pduForOid(cellOid);
            const SnmpTransportResult cellResponse = transport.execute(get);
            ++result.requests;
            SnmpTableCell cell;
            if (HasVarbindAt(cellResponse.pdu.get_vb_count(), 0))
            {
                cellResponse.pdu.get_vb(cell.varbind, 0);
                cell.available = !unavailable(cellResponse, cell.varbind);
            }
            row.cells.append(cell);
        }
        result.rows.append(row);
        nextOid = returned;
    }
}
