#include "snmptableoperation.h"
#include "snmpreport.h"
#include "tabletraversal.h"

#include <algorithm>
#include <QObject>
#include <QStringList>
#include <QList>

namespace
{
// A table is read column by column in lock step: every column is walked with GETNEXT (or
// GETBULK), the smallest instance among the columns' next values is the next row, and a column
// that has no value for that instance leaves a gap. Walking every column, not only the first,
// is what keeps rows whose first column is missing (sparse tables) from being lost.
constexpr int kMaxColumnsPerRequest = 8;
constexpr int kMaxRows = 250000;
constexpr int kMaxRequests = 2000000;

struct Entry
{
    QVector<quint32> instance;
    Vb varbind;
};

struct ColumnState
{
    Oid cursor;               // where the next request for this column starts
    QList<Entry> queue;       // answers received but not yet placed in a row
    bool exhausted = false;
};

bool lessInstance(const QVector<quint32> &a, const QVector<quint32> &b)
{
    return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end());
}

QString dottedInstance(const QVector<quint32> &instance)
{
    QStringList parts;
    for (const quint32 part : instance) parts.append(QString::number(part));
    return parts.join(QLatin1Char('.'));
}

// Read only the requested row. No successor operations are used here: a
// disappeared instance or a missing cell must never turn into a different row.
SnmpOperationStatus readRow(const SnmpRequestContext &context, const SnmpTablePlan &plan,
                           ISnmpTransport &transport, const SnmpCancellationToken &cancel,
                           SnmpTableResult &result)
{
    QVector<quint32> suffix;
    for (const auto &part : plan.instance.split('.')) {
        bool ok = false;
        const auto value = part.toULongLong(&ok);
        if (!ok || value > 0xffffffffULL || part.isEmpty() ||
            std::any_of(part.begin(), part.end(), [](QChar c) { return c < '0' || c > '9'; })) {
            result.problem = QObject::tr("Invalid table instance.");
            return SnmpOperationStatus::TransportFailure;
        }
        suffix.append(static_cast<quint32>(value));
    }
    QList<int> pending;
    for (int i = 0; i < plan.columns.size(); ++i) {
        if (plan.columns[i].oid.len() + suffix.size() > 128) {
            result.problem = QObject::tr("The table instance makes an OID too long.");
            return SnmpOperationStatus::TransportFailure;
        }
        pending.append(i);
    }
    SnmpTableRow row;
    row.instance = dottedInstance(suffix);
    DecodeTableInstance(plan.indexSpecs, suffix, &row.indexText);
    row.cells.resize(plan.columns.size());
    int chunkSize = kMaxColumnsPerRequest;
    SnmpOperationStatus status = SnmpOperationStatus::Complete;
    while (!pending.isEmpty()) {
        if (cancel.isCancelled()) { status = SnmpOperationStatus::Cancelled; break; }
        const auto chunk = pending.mid(0, chunkSize);
        SnmpTransportRequest request;
        request.operation = SnmpTransportOperation::Get;
        QVector<Oid> expected;
        for (int column : chunk) {
            Oid oid = plan.columns[column].oid;
            oid += row.instance.toLatin1().constData();
            expected.append(oid); request.pdu += Vb(oid);
        }
        const auto response = transport.execute(request);
        ++result.requests;
        if (response.status == SnmpOperationStatus::SnmpError) {
            if (response.snmpErrorStatus == SNMP_ERROR_TOO_BIG && chunk.size() > 1) {
                chunkSize = std::max(1, int(chunk.size()) / 2); continue;
            }
            if (context.config().version == SnmpRequestVersion::V1 &&
                response.snmpErrorStatus == SNMP_ERROR_NO_SUCH_NAME &&
                response.snmpErrorIndex >= 1 && response.snmpErrorIndex <= chunk.size()) {
                pending.removeAll(chunk[response.snmpErrorIndex - 1]); continue;
            }
        }
        if (response.status != SnmpOperationStatus::Success) {
            status = response.status;
            result.transportStatus = response.transportStatus;
            result.snmpErrorStatus = response.snmpErrorStatus;
            if (status == SnmpOperationStatus::TransportFailure && response.transportStatus)
                result.problem = ExplainSnmpTransportFailure(response.transportStatus, context.config().securityName);
            break;
        }
        result.problem = ExplainSnmpReport(response.pdu, context.config().securityName);
        if (!result.problem.isEmpty()) { status = SnmpOperationStatus::SnmpError; break; }
        if (response.pdu.get_vb_count() != chunk.size()) {
            result.problem = QObject::tr("The device returned an incomplete row response.");
            status = SnmpOperationStatus::TransportFailure; break;
        }
        bool invalid = false;
        for (int i = 0; i < chunk.size(); ++i) {
            Vb vb; response.pdu.get_vb(vb, i);
            if (vb.get_oid() != expected[i]) { invalid = true; break; }
        }
        if (invalid) {
            result.problem = QObject::tr("The device returned a different instance than the one requested.");
            status = SnmpOperationStatus::TransportFailure; break;
        }
        for (int i = 0; i < chunk.size(); ++i) {
            auto &cell = row.cells[chunk[i]];
            response.pdu.get_vb(cell.varbind, i);
            const auto syntax = cell.varbind.get_syntax();
            cell.available = syntax != sNMP_SYNTAX_NOSUCHOBJECT && syntax != sNMP_SYNTAX_NOSUCHINSTANCE &&
                syntax != sNMP_SYNTAX_ENDOFMIBVIEW;
            pending.removeAll(chunk[i]);
        }
    }
    if (cancel.isCancelled()) status = SnmpOperationStatus::Cancelled;
    if (std::any_of(row.cells.begin(), row.cells.end(), [](const auto &cell) { return cell.available; }))
        result.rows.append(row);
    else if (status == SnmpOperationStatus::Complete)
        result.problem = QObject::tr("The selected instance is no longer available, or none of its columns can be read.");
    return status;
}

class Walker
{
public:
    Walker(const SnmpRequestContext &context, const SnmpTablePlan &plan,
           ISnmpTransport &transport, const SnmpCancellationToken &cancellation,
           SnmpTableResult &result)
        : context(context), plan(plan), transport(transport), cancellation(cancellation),
          result(result), states(plan.columns.size()),
          bulkRepetitions(context.config().version == SnmpRequestVersion::V1
                              ? 1 : std::max(1, context.maxRepetitions()))
    {
        for (int i = 0; i < states.size(); ++i) states[i].cursor = plan.columns.at(i).oid;
    }

    SnmpOperationStatus run();

private:
    // Fetches more values for every column in `need`. Returns Success to carry on.
    SnmpOperationStatus refill(QList<int> need);
    bool absorb(int column, const Vb &vb);
    void fail(const QString &text) { result.problem = text; }

    const SnmpRequestContext &context;
    const SnmpTablePlan &plan;
    ISnmpTransport &transport;
    const SnmpCancellationToken &cancellation;
    SnmpTableResult &result;
    QVector<ColumnState> states;
    int bulkRepetitions;
    int chunkSize = kMaxColumnsPerRequest;
    bool misordered = false;
};

// Records one answer for a column. Returns false when the column has ended.
bool Walker::absorb(int column, const Vb &vb)
{
    ColumnState &state = states[column];
    const Oid &columnOid = plan.columns.at(column).oid;
    const unsigned int syntax = vb.get_syntax();
    const Oid returned = vb.get_oid();
    if (syntax == sNMP_SYNTAX_ENDOFMIBVIEW || syntax == sNMP_SYNTAX_NOSUCHOBJECT ||
        syntax == sNMP_SYNTAX_NOSUCHINSTANCE || returned.len() <= columnOid.len() ||
        !IsOidInSubtree(returned, columnOid)) {
        state.exhausted = true;
        return false;
    }
    if (!(returned > state.cursor)) {
        // The agent answered with an OID that is not after the one asked about. Following it
        // would loop forever, so stop with a clear reason instead.
        misordered = true;
        fail(QObject::tr("The device returned %1 after %2, which is not in increasing order, "
                         "so the table was not read further.")
                 .arg(QString::fromLatin1(returned.get_printable()),
                      QString::fromLatin1(state.cursor.get_printable())));
        return false;
    }
    Entry entry;
    for (unsigned long i = columnOid.len(); i < returned.len(); ++i)
        entry.instance.append(static_cast<quint32>(returned[i]));
    entry.varbind = vb;
    state.queue.append(entry);
    state.cursor = returned;
    return true;
}

SnmpOperationStatus Walker::refill(QList<int> need)
{
    while (!need.isEmpty()) {
        if (cancellation.isCancelled()) return SnmpOperationStatus::Cancelled;
        if (result.requests >= kMaxRequests) {
            fail(QObject::tr("Stopped after %1 requests.").arg(kMaxRequests));
            return SnmpOperationStatus::Complete;
        }
        const int count = std::min<int>(chunkSize, need.size());
        const QList<int> chunk = need.mid(0, count);
        const bool bulk = bulkRepetitions > 1;
        SnmpTransportRequest request;
        request.operation = bulk ? SnmpTransportOperation::GetBulk
                                 : SnmpTransportOperation::GetNext;
        for (const int column : chunk) {
            Vb vb;
            vb.set_oid(states[column].cursor);
            request.pdu += vb;
        }
        request.nonRepeaters = 0;
        request.maxRepetitions = bulk ? bulkRepetitions : 0;
        const SnmpTransportResult response = transport.execute(request);
        ++result.requests;

        if (response.status == SnmpOperationStatus::SnmpError) {
            const int code = response.snmpErrorStatus;
            if (code == SNMP_ERROR_TOO_BIG) {
                if (bulk && bulkRepetitions > 1) bulkRepetitions = std::max(1, bulkRepetitions / 2);
                else if (chunkSize > 1) chunkSize = std::max(1, chunkSize / 2);
                else {
                    result.status = response.status;
                    result.snmpErrorStatus = code;
                    return response.status;
                }
                continue;
            }
            const int index = response.snmpErrorIndex;
            if (code == SNMP_ERROR_NO_SUCH_NAME && index >= 1 && index <= chunk.size()) {
                // SNMPv1 ends a column by failing the whole request and naming the culprit.
                states[chunk.at(index - 1)].exhausted = true;
                need.removeAll(chunk.at(index - 1));
                continue;
            }
            result.status = response.status;
            result.snmpErrorStatus = code;
            return response.status;
        }
        if (response.status != SnmpOperationStatus::Success) {
            result.status = response.status;
            result.transportStatus = response.transportStatus;
            result.snmpErrorStatus = response.snmpErrorStatus;
            if (response.status == SnmpOperationStatus::TransportFailure && response.transportStatus != 0)
                fail(ExplainSnmpTransportFailure(response.transportStatus, context.config().securityName));
            return response.status;
        }

        // An SNMPv3 device that refuses the request answers with a REPORT (an error counter)
        // that otherwise looks like a table with nothing in it.
        const QString report = ExplainSnmpReport(response.pdu, context.config().securityName);
        if (!report.isEmpty()) {
            fail(report);
            result.status = SnmpOperationStatus::SnmpError;
            return SnmpOperationStatus::SnmpError;
        }

        const int varbinds = response.pdu.get_vb_count();
        if (varbinds == 0) {
            for (const int column : chunk) states[column].exhausted = true;
        }
        bool progress = varbinds == 0;
        for (int i = 0; i < varbinds; ++i) {
            const int column = chunk.at(i % chunk.size());
            if (states[column].exhausted) continue;
            Vb vb;
            response.pdu.get_vb(vb, i);
            if (absorb(column, vb)) progress = true;
            else if (states[column].exhausted) progress = true;
            if (misordered) return SnmpOperationStatus::Complete;
        }
        if (!progress) {
            fail(QObject::tr("The device did not return any new values, so the table was not "
                             "read further."));
            return SnmpOperationStatus::Complete;
        }
        for (const int column : chunk)
            if (states[column].exhausted || !states[column].queue.isEmpty())
                need.removeAll(column);
    }
    return SnmpOperationStatus::Success;
}

SnmpOperationStatus Walker::run()
{
    const int columns = plan.columns.size();
    while (true) {
        if (cancellation.isCancelled()) return SnmpOperationStatus::Cancelled;
        QList<int> need;
        for (int i = 0; i < columns; ++i)
            if (!states[i].exhausted && states[i].queue.isEmpty()) need.append(i);
        if (!need.isEmpty()) {
            const SnmpOperationStatus status = refill(need);
            if (status != SnmpOperationStatus::Success) return status;
        }
        const QVector<quint32> *smallest = nullptr;
        for (int i = 0; i < columns; ++i) {
            if (states[i].queue.isEmpty()) continue;
            const QVector<quint32> &candidate = states[i].queue.first().instance;
            if (!smallest || lessInstance(candidate, *smallest)) smallest = &candidate;
        }
        if (!smallest) return SnmpOperationStatus::Complete;

        const QVector<quint32> instance = *smallest;
        SnmpTableRow row;
        row.instance = dottedInstance(instance);
        QString decoded;
        if (!plan.indexSpecs.isEmpty() && DecodeTableInstance(plan.indexSpecs, instance, &decoded))
            row.indexText = decoded;
        for (int i = 0; i < columns; ++i) {
            SnmpTableCell cell;
            if (!states[i].queue.isEmpty() && states[i].queue.first().instance == instance) {
                if (!plan.instancesOnly) {
                    cell.varbind = states[i].queue.first().varbind;
                    cell.available = true;
                }
                states[i].queue.removeFirst();
            }
            if (!plan.instancesOnly) row.cells.append(cell);
        }
        result.rows.append(row);
        if (result.rows.size() >= kMaxRows) {
            fail(QObject::tr("Stopped after %1 rows.").arg(kMaxRows));
            return SnmpOperationStatus::Complete;
        }
    }
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
    result.requestedInstance = tablePlan.instance;
    result.columns = tablePlan.columns;
    result.environment = requestContext.environment();
    if (!tablePlan.instance.isEmpty()) {
        result.status = readRow(requestContext, tablePlan, transport, cancellation, result);
        return result;
    }
    Walker walker(requestContext, tablePlan, transport, cancellation, result);
    result.status = walker.run();
    return result;
}
