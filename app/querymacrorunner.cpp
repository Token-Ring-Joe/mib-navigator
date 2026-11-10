#include "querymacrorunner.h"
#include "snmpreport.h"
#include "tableindex.h"
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
    int count = 0;
    for (const auto &group : groups) {
        for (const auto &read : group.reads) count += read.value.has_value();
        for (const auto &row : group.table.rows) for (const auto &cell : row.cells) count += cell.available;
    }
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
    connect(&tableWorker, &SnmpTableAsyncRunner::completed, this, &QueryMacroRunner::receiveTable);
    connect(&instanceWorker, &SnmpInstanceAsyncRunner::completed, this, &QueryMacroRunner::receiveInstances);
}
QueryMacroRunner::~QueryMacroRunner() { shutdown(); }
void QueryMacroRunner::shutdown() {
    shuttingDown = true; stopped = true; ++generation;
    disconnect(&worker, nullptr, this, nullptr);
    disconnect(&tableWorker, nullptr, this, nullptr); disconnect(&instanceWorker, nullptr, this, nullptr);
    tableWorker.cancel(); instanceWorker.cancel();
    pending.clear(); tableJobs.clear(); factory = {}; context.reset(); tableContext.reset(); busy = false;
}
void QueryMacroRunner::cancel() {
    if (!busy || stopped) return;
    stopped = true;
    tableWorker.cancel(); instanceWorker.cancel();
    if (selecting) return;   // nothing has replaced the earlier results yet; abandon() reports it
    result.detail = tr("Stop requested. Waiting for the current request; no further requests will be sent.");
    emit changed();
}

bool QueryMacroRunner::start(const QueryMacroDefinition &definition, const SnmpRequestContext &captured,
    const QString &target, const QString &credential, const QString &profile,
    TransportFactory transportFactory, QString *error)
{
    if (error) error->clear();
    if (busy || shuttingDown || worker.isBusy() || tableWorker.isBusy() || instanceWorker.isBusy() || !transportFactory) {
        if (error) *error = tr("Another query is running or the application is closing."); return false;
    }
    if (!ValidateQueryMacros({definition}, error)) return false;
    if (definition.entries.isEmpty()) { if (error) *error = tr("Add an object or exact instance before running this macro."); return false; }
    result = {}; result.name = definition.name; result.target = target;
    result.credential = credential; result.profile = profile; result.started = QDateTime::currentDateTime();
    result.environment = captured.environment();
    QueryMacroRunGroup values; values.id = "values"; values.name = tr("Objects and values");
    QList<QueryMacroRunGroup> tables;
    QList<TableJob> jobs;
    tableJobs.clear();
    const auto &environment = result.environment;
    for (const auto &entry : definition.entries) {
        if (entry.kind == QueryMacroKind::Table) {
            QueryMacroRunGroup group; group.id = entry.id; group.isTable = true;
            group.name = entry.label.isEmpty() ? entry.oid : entry.label;
            group.table.environment = environment;
            TableJob job; job.label = group.name; job.plan.rowOid = Oid(entry.oid.toLatin1().constData());
            const auto *row = environment ? environment->nodeByOid(entry.oid) : nullptr;
            if (row && row->kind != MibEnvironmentNodeKind::Row) {
                group.problem = tr("The captured MIB Profile no longer defines a table row here. Review this entry.");
            } else {
                if (row) job.plan.indexSpecs = TableIndexSpecsForRow(*environment, *row);
                for (const QString &column : entry.columns) {
                    const auto *node = environment ? environment->nodeByOid(column) : nullptr;
                    if (node && node->kind != MibEnvironmentNodeKind::Column) {
                        group.problem = tr("The captured MIB Profile no longer defines %1 as a table column. Review this entry.").arg(column);
                        break;
                    }
                    job.plan.columns.append({node ? node->name : column, Oid(column.toLatin1().constData())});
                    group.table.columns.append(job.plan.columns.last());
                }
                if (group.problem.isEmpty() && job.plan.columns.isEmpty()) group.problem = tr("This table group has no columns.");
            }
            if (group.problem.isEmpty()) {
                job.choose = entry.instancePolicy == QueryMacroInstancePolicy::ChooseWhenRun;
                if (entry.instancePolicy == QueryMacroInstancePolicy::Fixed) job.instances = entry.fixedInstances;
                job.group = int(tables.size());   // made absolute below
                jobs.append(job);
            }
            tables.append(group);
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
    const int tableBase = values.reads.isEmpty() ? 0 : 1;
    for (auto &job : jobs) { job.group += tableBase; tableJobs.append(job); }
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
    tableContext.emplace(captured.config(), SnmpRequestOperation::Walk, captured.environment());
    factory = std::move(transportFactory); batchLimit = 8; requestLabel.clear();
    stopped = false; busy = true; ++generation;
    selecting = std::any_of(tableJobs.cbegin(), tableJobs.cend(), [](const TableJob &job) { return job.choose; });
    if (selecting) {
        // Earlier results stay on screen until every picker is answered.
        emit selectionStarted();
        const auto current = generation;
        QTimer::singleShot(0, this, [this, current] { if (current == generation) selectNext(); });
    } else beginRun();
    return true;
}
void QueryMacroRunner::beginRun() {
    selecting = false;
    emit started(); emit changed();
    const auto current = generation;
    QTimer::singleShot(0, this, [this, current] { if (current == generation) advance(); });
}
void QueryMacroRunner::abandon() {
    selecting = false; busy = false; ++generation;
    pending.clear(); tableJobs.clear(); factory = {}; context.reset(); tableContext.reset();
    emit abandoned();
}
void QueryMacroRunner::selectNext() {
    if (!busy || shuttingDown || !selecting) return;
    if (stopped) { abandon(); return; }
    int at = -1;
    for (int i = 0; i < tableJobs.size(); ++i) if (tableJobs[i].choose) { at = i; break; }
    if (at < 0) { beginRun(); return; }
    const auto &job = tableJobs[at];
    QVector<Oid> roots; for (const auto &column : job.plan.columns) roots.append(column.oid);
    requestLabel = tr("Macro %1 / %2 / Find instances").arg(result.name, job.label);
    auto transport = factory(tableContext->config());
    if (!transport || !instanceWorker.start(*tableContext, roots, std::move(transport))) {
        result.groups[job.group].problem = tr("Instance discovery could not be started.");
        tableJobs.removeAt(at);
        const auto current = generation;
        QTimer::singleShot(0, this, [this, current] { if (current == generation) selectNext(); });
        return;
    }
    emit requestStarted({QString::fromLatin1(job.plan.rowOid.get_printable())});
}
void QueryMacroRunner::receiveInstances(const SnmpInstanceResult &found) {
    if (!busy || shuttingDown || !selecting) return;
    int at = -1;
    for (int i = 0; i < tableJobs.size(); ++i) if (tableJobs[i].choose) { at = i; break; }
    if (at < 0) return;
    auto &job = tableJobs[at];
    const bool timeout = found.status == SnmpOperationStatus::Timeout;
    const bool refused = (found.transportStatus >= SNMPv3_USM_UNSUPPORTED_SECURITY_LEVEL &&
        found.transportStatus <= SNMPv3_USM_UNSUPPORTED_PRIVPROTOCOL) || found.snmpErrorStatus == SNMP_ERROR_AUTH_ERR ||
        (found.status == SnmpOperationStatus::SnmpError && !found.problem.isEmpty());
    QString detail;
    if (found.status == SnmpOperationStatus::Cancelled || stopped) { emit requestFinished(tr("Stopped."), 0, true, false, false); abandon(); return; }
    if (timeout) detail = tr("Timeout: the device did not answer while looking for instances.");
    else if (!found.problem.isEmpty()) detail = found.problem;
    else if (found.status == SnmpOperationStatus::SnmpError) detail = tr("SNMP error %1: the device refused to list instances.").arg(found.snmpErrorStatus);
    else if (found.status != SnmpOperationStatus::Complete) detail = tr("No response received while looking for instances.");
    else if (found.instances.isEmpty()) detail = tr("The device has no instances of this table.");
    result.requests += qMax(1, found.requests);
    if (!detail.isEmpty()) {
        emit requestFinished(detail, found.instances.size(), true, timeout, refused);
        result.groups[job.group].problem = detail;
        if (refused) {
            // The same refused login is not repeated for every remaining table.
            selecting = false; emit started(); emit changed(); finish(QueryMacroRunState::Refused, detail); return;
        }
        tableJobs.removeAt(at);
    } else {
        emit requestFinished(tr("%1 instance(s) found.").arg(found.instances.size()), found.instances.size(), false, false, false);
        QStringList decoded;
        for (const QString &instance : found.instances) {
            QVector<quint32> suffix; for (const QString &part : instance.split('.')) suffix.append(part.toUInt());
            QString text; DecodeTableInstance(job.plan.indexSpecs, suffix, &text); decoded.append(text);
        }
        const auto picked = instanceChooser ? instanceChooser(job.label, found.instances, decoded) : std::nullopt;
        if (!picked || picked->isEmpty()) { abandon(); return; }
        job.instances.clear();
        for (const QString &instance : found.instances) if (picked->contains(instance)) job.instances.append(instance);
        if (job.instances.isEmpty()) { abandon(); return; }
        job.choose = false;
    }
    const auto current = generation;
    QTimer::singleShot(0, this, [this, current] { if (current == generation) selectNext(); });
}
void QueryMacroRunner::startTableRequest() {
    auto &job = tableJobs.first();
    SnmpTablePlan plan = job.plan;
    if (!job.instances.isEmpty()) plan.instance = job.instances.value(job.next);
    requestLabel = plan.instance.isEmpty() ? tr("Macro %1 / %2 / Table").arg(result.name, job.label)
        : tr("Macro %1 / %2 / Table row %3").arg(result.name, job.label, plan.instance);
    auto transport = factory(tableContext->config());
    if (!transport || !tableWorker.start(*tableContext, plan, std::move(transport))) {
        result.groups[job.group].problem = tr("The table request could not be started.");
        tableJobs.removeFirst();
        const auto current = generation;
        QTimer::singleShot(0, this, [this, current] { if (current == generation) advance(); });
        emit changed(); return;
    }
    emit requestStarted({QString::fromLatin1(plan.rowOid.get_printable())});
    emit changed();
}
void QueryMacroRunner::receiveTable(const SnmpTableResult &table) {
    if (!busy || shuttingDown || selecting || tableJobs.isEmpty()) return;
    auto &job = tableJobs.first();
    auto &group = result.groups[job.group];
    const QString instance = job.instances.isEmpty() ? QString() : job.instances.value(job.next);
    result.requests += qMax(1, table.requests);
    for (const auto &row : table.rows) group.table.rows.append(row);
    const bool timeout = table.status == SnmpOperationStatus::Timeout;
    const bool securityFailure = table.transportStatus >= SNMPv3_USM_UNSUPPORTED_SECURITY_LEVEL &&
        table.transportStatus <= SNMPv3_USM_UNSUPPORTED_PRIVPROTOCOL;
    const bool refused = securityFailure || table.snmpErrorStatus == SNMP_ERROR_AUTH_ERR ||
        (table.status == SnmpOperationStatus::SnmpError && !table.problem.isEmpty());
    QString detail; bool failed = false;
    if (table.status == SnmpOperationStatus::Cancelled) detail = tr("Stopped. Rows received so far were kept.");
    else if (timeout) { detail = tr("Timeout: the device stopped answering, so the table may be incomplete."); failed = true; }
    else if (table.status == SnmpOperationStatus::SnmpError)
        { detail = !table.problem.isEmpty() ? table.problem : readError(table.snmpErrorStatus); failed = true; }
    else if (table.status == SnmpOperationStatus::TransportFailure)
        { detail = !table.problem.isEmpty() ? table.problem : tr("No response received."); failed = true; }
    else if (!table.problem.isEmpty()) { detail = table.problem; failed = true; }
    if (failed || table.status == SnmpOperationStatus::Cancelled) {
        const QString line = instance.isEmpty() ? detail : tr("Instance %1: %2").arg(instance, detail);
        group.problem = group.problem.isEmpty() ? line : group.problem + QLatin1Char(' ') + line;
    }
    emit requestFinished(detail.isEmpty() ? tr("Table returned %n row(s).", nullptr, int(table.rows.size())) : detail,
        int(table.rows.size()), failed, timeout, refused);
    emit changed();
    if (refused) { finish(QueryMacroRunState::Refused, detail); return; }
    // A silent or failing device will not answer the rest of this group's rows either.
    const bool deviceGone = timeout || table.status == SnmpOperationStatus::TransportFailure;
    if (!job.instances.isEmpty() && !deviceGone && ++job.next < job.instances.size()) {}
    else tableJobs.removeFirst();
    const auto current = generation;
    QTimer::singleShot(0, this, [this, current] { if (current == generation) advance(); });
}
void QueryMacroRunner::failReads(const QVector<int> &indexes, const QString &problem) {
    for (int i : indexes) { auto &read = result.groups[0].reads[i]; read.problem = problem; read.finished = true; }
}
void QueryMacroRunner::advance()
{
    if (!busy || shuttingDown) return;
    if (stopped) { finish(QueryMacroRunState::Stopped, tr("Completed values have been kept.")); return; }
    if (pending.isEmpty() && !tableJobs.isEmpty()) {
        if (result.requests >= 4096) {
            finish(result.valueCount() ? QueryMacroRunState::Partial : QueryMacroRunState::Failed,
                   tr("The macro request limit was reached.")); return;
        }
        startTableRequest(); return;
    }
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
    for (const auto &job : std::as_const(tableJobs)) {
        auto &group = result.groups[job.group];
        if (group.problem.isEmpty())
            group.problem = state == QueryMacroRunState::Stopped ? tr("Not read: the macro was stopped.")
                : state == QueryMacroRunState::Refused ? tr("Not read: the device refused the credential or security settings.")
                : tr("Not read: the macro ended before this table was requested.");
    }
    if (!result.groups.isEmpty()) for (auto &read : result.groups[0].reads) if (!read.finished) {
        read.finished = true;
        read.problem = state == QueryMacroRunState::Stopped ? tr("Not read: the macro was stopped.")
            : state == QueryMacroRunState::Refused ? tr("Not read: the device refused the credential or security settings.")
            : tr("Not read: the macro ended before this entry was requested.");
    }
    pending.clear(); tableJobs.clear(); factory = {}; context.reset(); tableContext.reset();
    emit changed(); busy = false; emit completed();
}
