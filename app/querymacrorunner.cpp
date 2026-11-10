#include "querymacrorunner.h"
#include "snmpreport.h"
#include "mibvaluesemantics.h"
#include <QTimer>

namespace {
QString readError(int code) {
    if (code == SNMP_ERROR_NO_ACCESS) return QObject::tr("noAccess: this Credential cannot read the requested object.");
    if (code == SNMP_ERROR_AUTH_ERR) return QObject::tr("authorizationError: the device refused this Credential's read request.");
    return ExplainSnmpErrorStatus(code);
}
}

int QueryMacroRunSnapshot::valueCount() const {
    int count = 0; for (const auto &group : groups) for (const auto &read : group.reads) count += read.value.has_value();
    return count;
}
int QueryMacroRunSnapshot::problemCount() const {
    int count = 0; for (const auto &group : groups) {
        count += !group.problem.isEmpty();
        for (const auto &read : group.reads) count += !read.problem.isEmpty();
    }
    return count;
}
QString QueryMacroRunSnapshot::summary() const {
    QString stateText;
    switch (state) {
    case QueryMacroRunState::Running: stateText = QObject::tr("Running"); break;
    case QueryMacroRunState::Complete: stateText = QObject::tr("Finished"); break;
    case QueryMacroRunState::Partial: stateText = QObject::tr("Partial"); break;
    case QueryMacroRunState::Failed: stateText = QObject::tr("Failed"); break;
    case QueryMacroRunState::Stopped: stateText = QObject::tr("Stopped"); break;
    case QueryMacroRunState::Refused: stateText = QObject::tr("Refused by device"); break;
    }
    QString text = QObject::tr("%1: %2 value(s), %3 problem(s), %4 request(s).")
        .arg(stateText).arg(valueCount()).arg(problemCount()).arg(requests);
    if (!detail.isEmpty()) text += ' ' + detail;
    // Distinct causes, in first-seen order, so the headline says why entries were not read.
    QStringList causes; QList<int> counts;
    const auto note = [&](const QString &cause) {
        const int at = causes.indexOf(cause);
        if (at >= 0) ++counts[at]; else { causes.append(cause); counts.append(1); }
    };
    for (const auto &group : groups) {
        if (!group.problem.isEmpty()) note(group.problem);
        for (const auto &read : group.reads) if (!read.problem.isEmpty()) note(read.problem);
    }
    constexpr int shown = 3;
    QStringList lines;
    for (int i = 0; i < causes.size() && i < shown; ++i)
        lines << (counts[i] > 1 ? QObject::tr("%1 \u00d7 %2").arg(counts[i]).arg(causes[i]) : causes[i]);
    if (causes.size() > shown) lines << QObject::tr("and %n more kind(s) of problem; see Results.", nullptr, int(causes.size()) - shown);
    if (!lines.isEmpty()) text += QObject::tr(" Problems: ") + lines.join(QStringLiteral(" | "));
    return text;
}

QueryMacroRunner::QueryMacroRunner(QObject *parent) : QObject(parent) {
    connect(&worker, &SnmpSingleAsyncRunner::completed, this, &QueryMacroRunner::receive);
}
QueryMacroRunner::~QueryMacroRunner() { shutdown(); }
void QueryMacroRunner::shutdown() {
    shuttingDown = true; stopped = true; ++generation;
    disconnect(&worker, nullptr, this, nullptr);
    pending.clear(); factory = {}; context.reset(); busy = false;
}
void QueryMacroRunner::cancel() {
    if (!busy || stopped) return;
    stopped = true;
    result.detail = tr("Stop requested. Waiting for the current request; no further requests will be sent.");
    emit changed();
}

bool QueryMacroRunner::start(const QueryMacroDefinition &definition, const SnmpRequestContext &captured,
    const QString &target, const QString &credential, const QString &profile,
    TransportFactory transportFactory, QString *error)
{
    if (error) error->clear();
    if (busy || shuttingDown || worker.isBusy() || !transportFactory) {
        if (error) *error = tr("Another query is running or the application is closing."); return false;
    }
    if (!ValidateQueryMacros({definition}, error)) return false;
    if (definition.entries.isEmpty()) { if (error) *error = tr("Add an object or exact instance before running this macro."); return false; }
    result = {}; result.name = definition.name; result.target = target;
    result.credential = credential; result.profile = profile; result.started = QDateTime::currentDateTime();
    result.environment = captured.environment();
    QueryMacroRunGroup values; values.id = "values"; values.name = tr("Objects and values");
    QList<QueryMacroRunGroup> tables;
    for (const auto &entry : definition.entries) {
        if (entry.kind == QueryMacroKind::Table) {
            tables.append({entry.id, entry.label.isEmpty() ? entry.oid : entry.label,
                tr("Table retrieval is not available yet, so this table entry was skipped. Scalar and exact-instance entries still run."), {}});
            continue;
        }
        QueryMacroRead read; read.id = entry.id; read.label = entry.label;
        read.oid = entry.oid + (entry.kind == QueryMacroKind::Scalar ? QStringLiteral(".0") : QString());
        const auto *node = result.environment ? result.environment->nodeByOid(entry.oid) : nullptr;
        if (entry.kind == QueryMacroKind::Scalar && node &&
            (node->kind != MibEnvironmentNodeKind::Scalar ||
             (node->access != MibEnvironmentAccess::ReadOnly && node->access != MibEnvironmentAccess::ReadWrite &&
              node->access != MibEnvironmentAccess::Install && node->access != MibEnvironmentAccess::InstallNotify))) {
            read.problem = tr("The captured MIB Profile no longer defines a readable scalar here. Review this entry.");
            read.finished = true;
        }
        values.reads.append(read);
    }
    // A table-only macro opens its explanation, rather than an unrelated empty
    // object/value group. Whenever requests exist, group zero is the read group.
    if (!values.reads.isEmpty()) result.groups.append(values);
    result.groups.append(tables);
    pending.clear(); inFlight.clear(); QVector<int> batch;
    for (int i = 0; i < values.reads.size(); ++i) if (!values.reads[i].finished) {
        batch.append(i); if (batch.size() == 8) { pending.append(batch); batch.clear(); }
    }
    if (!batch.isEmpty()) pending.append(batch);
    context.emplace(captured.config(), SnmpRequestOperation::Get, captured.environment());
    factory = std::move(transportFactory); batchLimit = 8;
    stopped = false; busy = true; ++generation;
    emit started(); emit changed();
    const auto current = generation;
    QTimer::singleShot(0, this, [this, current] { if (current == generation) advance(); });
    return true;
}
void QueryMacroRunner::failReads(const QVector<int> &indexes, const QString &problem) {
    for (int i : indexes) { auto &read = result.groups[0].reads[i]; read.problem = problem; read.finished = true; }
}
void QueryMacroRunner::advance()
{
    if (!busy || shuttingDown) return;
    if (stopped) { finish(QueryMacroRunState::Stopped, tr("Completed values have been kept.")); return; }
    if (pending.isEmpty()) {
        finish(result.problemCount() ? (result.valueCount() ? QueryMacroRunState::Partial : QueryMacroRunState::Failed)
                                     : QueryMacroRunState::Complete); return;
    }
    if (result.requests >= 4096) {
        finish(result.valueCount() ? QueryMacroRunState::Partial : QueryMacroRunState::Failed,
               tr("The macro request limit was reached.")); return;
    }
    auto next = pending.takeFirst();
    if (next.size() > batchLimit) { pending.prepend(next.mid(batchLimit)); next.resize(batchLimit); }
    inFlight = next;
    QVector<Vb> varbinds; QStringList oids;
    for (int i : inFlight) {
        const auto &oid = result.groups[0].reads[i].oid;
        varbinds.append(Vb(Oid(oid.toLatin1().constData()))); oids.append(oid);
    }
    auto transport = factory(context->config());
    if (!transport || !worker.start(*context, varbinds, std::move(transport))) {
        failReads(inFlight, tr("The GET batch could not be started.")); inFlight.clear();
        const auto current = generation;
        QTimer::singleShot(0, this, [this, current] { if (current == generation) advance(); });
        emit changed(); return;
    }
    ++result.requests; emit requestStarted(oids); emit changed();
}
void QueryMacroRunner::receive(const SnmpSingleResult &response)
{
    if (!busy || shuttingDown) return;
    const QVector<int> batch = inFlight; inFlight.clear();
    const int error = response.snmpErrorStatus ? response.snmpErrorStatus : response.pdu.get_error_status();
    const int errorIndex = response.snmpErrorIndex ? response.snmpErrorIndex : response.pdu.get_error_index();
    const QString report = ExplainSnmpReport(response.pdu, context->config().securityName);
    const bool securityFailure = response.transportStatus >= SNMPv3_USM_UNSUPPORTED_SECURITY_LEVEL &&
        response.transportStatus <= SNMPv3_USM_UNSUPPORTED_PRIVPROTOCOL;
    const bool refused = !report.isEmpty() || securityFailure || error == SNMP_ERROR_AUTH_ERR;
    const bool timeout = response.status == SnmpOperationStatus::Timeout;
    QString detail; int received = 0; bool failed = false;
    if (refused) {
        detail = !report.isEmpty() ? report : !response.problem.isEmpty() ? response.problem : readError(error);
        failReads(batch, detail); failed = true;
    } else if (error == SNMP_ERROR_TOO_BIG && batch.size() > 1) {
        batchLimit = qMax(1, int(batch.size()) / 2);
        pending.prepend(batch); detail = tr("tooBig: retrying a smaller GET batch."); failed = true;
    } else if (error && errorIndex > 0 && errorIndex <= batch.size()) {
        auto remaining = batch; const int bad = remaining.takeAt(errorIndex - 1);
        failReads({bad}, readError(error));
        if (!remaining.isEmpty()) pending.prepend(remaining);
        detail = readError(error); failed = true;
    } else if (error || response.status != SnmpOperationStatus::Success) {
        detail = timeout ? tr("Timeout: no response from %1 (timeout %2 s, %3 retries). Check the Target address and port, that SNMP is enabled on the device, and that the Credential is accepted.")
                .arg(context->config().endpoint()).arg(context->config().timeout).arg(context->config().retries)
            : !response.problem.isEmpty() ? response.problem
            : error ? readError(error) : tr("The GET batch failed without a response.");
        failReads(batch, detail); failed = true;
    } else {
        for (int position = 0; position < batch.size(); ++position) {
            auto &read = result.groups[0].reads[batch[position]]; Vb vb;
            if (position >= response.pdu.get_vb_count() || !response.pdu.get_vb(vb, position))
                read.problem = tr("The response omitted this requested instance.");
            else if (QString::fromLatin1(vb.get_printable_oid()) != read.oid)
                read.problem = tr("The response returned a different OID (%1 instead of %2). The requested exact instance was not replaced.")
                    .arg(QString::fromLatin1(vb.get_printable_oid()), read.oid);
            else if (vb.get_syntax() == sNMP_SYNTAX_NOSUCHOBJECT || vb.get_syntax() == sNMP_SYNTAX_NOSUCHINSTANCE ||
                     vb.get_syntax() == sNMP_SYNTAX_ENDOFMIBVIEW || vb.get_syntax() == sNMP_SYNTAX_NULL)
                read.problem = tr("The device has no value for this exact instance (%1).").arg(SnmpSyntaxName(vb.get_syntax()));
            else { read.value = vb; read.received = QDateTime::currentDateTime(); ++received; }
            read.finished = true; failed |= !read.problem.isEmpty();
        }
        if (response.pdu.get_vb_count() > batch.size()) {
            result.groups[0].problem = tr("The device returned extra values that were not requested; they were ignored."); failed = true;
        }
        detail = failed ? tr("GET batch returned %1 value(s); unavailable or unexpected values are explained in Results.").arg(received)
                        : tr("GET batch returned %1 value(s).").arg(received);
    }
    emit requestFinished(detail, received, failed, timeout, refused);
    emit changed();
    if (refused) { finish(QueryMacroRunState::Refused, detail); return; }
    const auto current = generation;
    QTimer::singleShot(0, this, [this, current] { if (current == generation) advance(); });
}
void QueryMacroRunner::finish(QueryMacroRunState state, const QString &detail)
{
    result.state = state; result.detail = detail;
    for (auto &read : result.groups[0].reads) if (!read.finished) {
        read.finished = true;
        read.problem = state == QueryMacroRunState::Stopped ? tr("Not read: the macro was stopped.")
            : state == QueryMacroRunState::Refused ? tr("Not read: the device refused the credential or security settings.")
            : tr("Not read: the macro ended before this entry was requested.");
    }
    pending.clear(); factory = {}; context.reset();
    emit changed(); busy = false; emit completed();
}
