#include "querymacrorunner.h"
#include "fakesnmpagent.h"
#include "mibenvironmentregistry.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QMutex>
#include <QMutexLocker>
#include <QThread>
#include <QTimer>
#include <atomic>
#include <cstdio>
#include <thread>

namespace {
std::atomic<int> failures{0};
void check(bool ok, const char *why) { if (!ok) { std::fprintf(stderr, "FAIL: %s\n", why); ++failures; } }
struct Script {
    QMutex mutex;
    QList<QStringList> requests;
    QList<SnmpRequestConfig> configs;
    std::function<SnmpTransportResult(const SnmpTransportRequest &, int)> answer;
    // Table traffic (GETNEXT/GETBULK, and exact GETs when agentGets) is answered by this agent.
    FakeSnmpAgent *agent = nullptr;
    bool agentGets = false;
};
class Transport final : public ISnmpTransport {
public:
    Transport(Script &script, SnmpRequestConfig config) : script(script), captured(std::move(config)) {}
    const SnmpRequestConfig &config() const override { return captured; }
    SnmpTransportResult execute(const SnmpTransportRequest &request) override {
        const bool viaAgent = script.agent && (request.operation != SnmpTransportOperation::Get || script.agentGets);
        check(viaAgent || request.operation == SnmpTransportOperation::Get, "scalar and exact entries send exact GETs only");
        int number;
        { QMutexLocker lock(&script.mutex); QStringList oids;
          for (int i = 0; i < request.pdu.get_vb_count(); ++i) { Vb vb; request.pdu.get_vb(vb, i); oids.append(QString::fromLatin1(vb.get_printable_oid())); }
          script.requests.append(oids); script.configs.append(captured); number = script.requests.size(); }
        return viaAgent ? script.agent->execute(request) : script.answer(request, number);
    }
private:
    Script &script; SnmpRequestConfig captured;
};
SnmpTransportResult success(const SnmpTransportRequest &request) {
    SnmpTransportResult result; result.status = SnmpOperationStatus::Success;
    for (int i = 0; i < request.pdu.get_vb_count(); ++i) {
        Vb vb; request.pdu.get_vb(vb, i); vb.set_value(SnmpInt32(42)); result.pdu += vb;
    }
    return result;
}
SnmpTransportResult error(int code, int index = 0) {
    SnmpTransportResult result; result.status = SnmpOperationStatus::SnmpError;
    result.snmpErrorStatus = code; result.snmpErrorIndex = index;
    result.pdu.set_error_status(code); result.pdu.set_error_index(index); return result;
}
QueryMacroDefinition definition(int count = 20) {
    QueryMacroDefinition macro; macro.id = QueryMacroNewId(); macro.name = "Captured essentials";
    for (int i = 0; i < count; ++i) {
        QueryMacroEntry e; e.id = QueryMacroNewId(); e.oid = QString("1.3.6.1.4.1.64994.%1.0").arg(i + 1); macro.entries.append(e);
    }
    return macro;
}
SnmpRequestConfig config(SnmpRequestVersion version = SnmpRequestVersion::V2c) {
    SnmpRequestConfig config; config.version = version; config.address = "192.0.2.10"; config.port = "4161";
    config.readCommunity = "captured-secret"; config.contextName = QString::fromUtf8("captured réseau"); return config;
}
bool start(QueryMacroRunner &runner, const QueryMacroDefinition &macro, Script &script,
           const SnmpRequestConfig &settings = config(), MibEnvironmentPtr environment = {}) {
    QString problem;
    return runner.start(macro, SnmpRequestContext(settings, SnmpRequestOperation::Get, environment),
        "192.0.2.10:4161", "Lab read credential", "Captured Profile",
        [&script](const SnmpRequestConfig &captured) { return std::make_unique<Transport>(script, captured); }, &problem);
}
void wait(QueryMacroRunner &runner) {
    QEventLoop loop; QObject::connect(&runner, &QueryMacroRunner::completed, &loop, &QEventLoop::quit);
    QTimer::singleShot(15000, &loop, &QEventLoop::quit); if (runner.isBusy()) loop.exec();
    check(!runner.isBusy(), "run completes within its deterministic budget");
}
constexpr auto kRow = "1.3.6.1.4.1.999.1.1";
void put(FakeSnmpAgent &agent, int column, int instance, long value) {
    agent.addInteger(QString("%1.%2.%3").arg(kRow).arg(column).arg(instance).toLatin1().constData(), value);
}
// Three rows, two columns; column 2 of row 2 is missing (a sparse table).
void fillTable(FakeSnmpAgent &agent) {
    for (int row = 1; row <= 3; ++row) { put(agent, 1, row, row * 10); if (row != 2) put(agent, 2, row, row * 100); }
}
QueryMacroEntry tableEntry(QueryMacroInstancePolicy policy = QueryMacroInstancePolicy::All, QStringList fixed = {}) {
    QueryMacroEntry entry; entry.id = QueryMacroNewId(); entry.kind = QueryMacroKind::Table; entry.label = "testTable";
    entry.oid = kRow; entry.columns = {QString(kRow) + ".1", QString(kRow) + ".2"};
    entry.instancePolicy = policy; entry.fixedInstances = fixed; return entry;
}
QueryMacroDefinition tableMacro(const QList<QueryMacroEntry> &entries) {
    QueryMacroDefinition macro; macro.id = QueryMacroNewId(); macro.name = "Table macro"; macro.entries = entries; return macro;
}
void waitAbandoned(QueryMacroRunner &runner) {
    QEventLoop loop; QObject::connect(&runner, &QueryMacroRunner::abandoned, &loop, &QEventLoop::quit);
    QObject::connect(&runner, &QueryMacroRunner::completed, &loop, &QEventLoop::quit);
    QTimer::singleShot(15000, &loop, &QEventLoop::quit); if (runner.isBusy()) loop.exec();
}
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    {
        Script script; script.answer = [](const auto &request, int) { return success(request); };
        QueryMacroRunner runner; QString problem;
        check(runner.start(definition(1), SnmpRequestContext(config(), SnmpRequestOperation::Set), "target", "credential", "profile",
            [&](const auto &captured) { return std::make_unique<Transport>(script, captured); }, &problem), "caller operation cannot turn a macro into SET");
        wait(runner); check(runner.snapshot().valueCount() == 1, "macro coerces its captured context to read-only GET");
    }
    {
        Script script; script.answer = [](const auto &request, int) { return success(request); };
        QueryMacroRunner runner; auto macro = definition(); auto settings = config();
        macro.entries[0].kind = QueryMacroKind::Scalar; macro.entries[0].oid = "1.3.6.1.2.1.1.1";
        QueryMacroEntry table; table.id = QueryMacroNewId(); table.kind = QueryMacroKind::Table;
        table = tableEntry(); macro.entries.append(table);
        FakeSnmpAgent agent(config()); fillTable(agent); script.agent = &agent;
        const auto environment = std::make_shared<MibEnvironment>(); int starts = 0, finishes = 0;
        QObject::connect(&runner, &QueryMacroRunner::requestStarted, [&](const QStringList &) { ++starts; });
        QObject::connect(&runner, &QueryMacroRunner::requestFinished, [&](const QString &, int, bool, bool, bool) { ++finishes; });
        check(start(runner, macro, script, settings, environment), "mixed scalar/exact/table run starts explicitly");
        check(!start(runner, definition(), script), "concurrent run cannot replace captured state");
        macro.name = "Later edit"; macro.entries.clear(); settings.address = "192.0.2.99"; settings.readCommunity = "later-secret";
        wait(runner); const auto &run = runner.snapshot();
        check(run.name == "Captured essentials" && run.environment == environment && run.credential == "Lab read credential" &&
            run.profile == "Captured Profile" && run.started.isValid(), "definition, environment and display identity are captured values");
        check(run.state == QueryMacroRunState::Complete && run.valueCount() == 20 + 5 && run.groups.size() == 2 &&
            run.groups[1].isTable && run.groups[1].table.rows.size() == 3 && run.groups[1].problem.isEmpty(),
            "a table group runs after the scalar/exact reads and keeps its own grid");
        check(starts >= 4 && finishes == starts && int(script.requests.size()) >= starts, "one lifecycle pair per scalar batch and per table request");
        check(script.requests.first().first() == "1.3.6.1.2.1.1.1.0" &&
            script.requests.first()[1] == "1.3.6.1.4.1.64994.2.0", "scalar appends .0 and exact instance stays exact");
        for (const auto &seen : script.configs) check(seen.address == "192.0.2.10" && seen.port == "4161" &&
            seen.readCommunity == "captured-secret" && seen.contextName == QString::fromUtf8("captured réseau"), "connection and secrets remain captured across batches");
        check(!run.groups[0].reads.first().received.isNull(), "received timestamp belongs to the value snapshot");
    }
    {
        Script script; script.answer = [](const auto &request, int) { return request.pdu.get_vb_count() > 2 ? error(SNMP_ERROR_TOO_BIG) : success(request); };
        QueryMacroRunner runner; int probes = 0;
        QObject::connect(&runner, &QueryMacroRunner::requestFinished, [&](const QString &, int, bool failed, bool, bool) { probes += failed; });
        check(start(runner, definition(), script), "adaptive run starts"); wait(runner);
        check(runner.snapshot().state == QueryMacroRunState::Complete && runner.snapshot().valueCount() == 20 &&
            runner.snapshot().problemCount() == 0 && probes == 2 && script.requests.size() == 12,
            "tooBig shrinks all following batches without dropping values or manufacturing a final problem");
    }
    {
        Script script; script.answer = [](const auto &request, int n) { return n == 1 ? error(SNMP_ERROR_NO_SUCH_NAME, 2) : success(request); };
        QueryMacroRunner runner; check(start(runner, definition(), script, config(SnmpRequestVersion::V1)), "v1 run starts"); wait(runner);
        check(runner.snapshot().state == QueryMacroRunState::Partial && runner.snapshot().valueCount() == 19 &&
            runner.snapshot().groups[0].reads[1].problem.contains("noSuchName") && script.requests[1].size() == 7 &&
            !script.requests[1].contains("1.3.6.1.4.1.64994.2.0"), "v1 missing object is isolated and valid siblings retried exactly");
    }
    {
        Script script; script.answer = [](const auto &request, int n) {
            if (n != 1) return success(request);
            SnmpTransportResult timeout; timeout.status = SnmpOperationStatus::Timeout; return timeout;
        };
        QueryMacroRunner runner; check(start(runner, definition(), script), "timeout run starts"); wait(runner);
        check(runner.snapshot().state == QueryMacroRunState::Partial && runner.snapshot().valueCount() == 12 && script.requests.size() == 3,
            "one timeout does not discard later independent reads");
    }
    {
        Script script; script.answer = [](const auto &request, int) {
            SnmpTransportResult result; result.status = SnmpOperationStatus::Success;
            for (int i = 0; i < request.pdu.get_vb_count() - 1; ++i) {
                Vb vb; request.pdu.get_vb(vb, i);
                if (i == 0) vb.set_oid(Oid("1.3.6.1.4.1.64994.999.0"));
                if (i == 1) vb.set_exception_status(sNMP_SYNTAX_NOSUCHINSTANCE); else vb.set_value(SnmpInt32(3));
                result.pdu += vb;
            }
            return result;
        };
        QueryMacroRunner runner; check(start(runner, definition(8), script), "imperfect-reply run starts"); wait(runner);
        const auto &run = runner.snapshot();
        check(run.state == QueryMacroRunState::Partial && run.valueCount() == 5 && run.problemCount() == 3 &&
            run.groups[0].reads[0].problem.contains("different OID") && run.groups[0].reads[7].problem.contains("omitted"),
            "successor, exception and omitted replies cannot masquerade as exact-instance values");
    }
    for (bool wireReport : {true, false}) {
        Script script; script.answer = [wireReport](const auto &, int) {
            SnmpTransportResult result;
            if (wireReport) {
                result.status = SnmpOperationStatus::Success; result.pdu.set_type(sNMP_PDU_REPORT);
                Vb vb(Oid("1.3.6.1.6.3.15.1.1.5.0")); vb.set_value(Counter32(1)); result.pdu += vb;
            } else { result.status = SnmpOperationStatus::TransportFailure; result.transportStatus = SNMPv3_USM_AUTHENTICATION_FAILURE; }
            return result;
        };
        QueryMacroRunner runner; check(start(runner, definition(), script, config(SnmpRequestVersion::V3)), "refusal run starts"); wait(runner);
        check(runner.snapshot().state == QueryMacroRunState::Refused && script.requests.size() == 1 &&
            runner.snapshot().valueCount() == 0 && runner.snapshot().problemCount() == 20,
            "wire/local security refusals stop later batches and never present REPORTs as values");
    }
    for (int index : {0, 9}) {
        Script script; script.answer = [index](const auto &request, int n) { return n == 1 ? error(SNMP_ERROR_NO_SUCH_NAME, index) : success(request); };
        QueryMacroRunner runner; check(start(runner, definition(12), script, config(SnmpRequestVersion::V1)), "invalid error-index run starts"); wait(runner);
        check(runner.snapshot().state == QueryMacroRunState::Partial && runner.snapshot().valueCount() == 4 && script.requests.size() == 2,
            "invalid v1 error indexes fail only their batch and cannot invent an object or loop");
    }
    {
        Script script; script.answer = [](const auto &, int) { return error(SNMP_ERROR_NO_SUCH_NAME, 1); };
        QueryMacroRunner runner; check(start(runner, definition(200), script, config(SnmpRequestVersion::V1)), "all-missing bounded run starts"); wait(runner);
        check(runner.snapshot().state == QueryMacroRunState::Failed && runner.snapshot().requests == 200 && runner.snapshot().problemCount() == 200,
            "an all-missing v1 agent terminates after bounded isolation rather than endlessly retrying");
    }
    {
        Script script; script.answer = [](const auto &request, int) {
            auto reply = success(request); Vb extra(Oid("1.3.6.1.4.1.64994.999.0")); extra.set_value(SnmpInt32(999)); reply.pdu += extra; return reply;
        };
        QueryMacroRunner runner; check(start(runner, definition(2), script), "extra-response run starts"); wait(runner);
        check(runner.snapshot().state == QueryMacroRunState::Partial && runner.snapshot().valueCount() == 2 &&
            runner.snapshot().groups[0].problem.contains("extra values"), "unrequested values are ignored and explained");
    }
    {
        Script script; script.answer = [](const auto &request, int n) { return n == 1 ? error(SNMP_ERROR_NO_ACCESS, 2) : success(request); };
        QueryMacroRunner runner; check(start(runner, definition(8), script), "object-access refusal starts"); wait(runner);
        check(runner.snapshot().state == QueryMacroRunState::Partial && runner.snapshot().valueCount() == 7 &&
            runner.snapshot().groups[0].reads[1].problem.contains("read the requested object"),
            "per-object read denial isolates one object and uses read-specific guidance");
    }
    {
        Script script; script.answer = [](const auto &request, int) { return success(request); };
        QueryMacroRunner runner; check(start(runner, definition(), script), "cancel-before-request run starts"); runner.cancel();
        check(runner.snapshot().detail.contains("Stop requested"), "Stop immediately explains pending transport completion");
        wait(runner);
        check(runner.snapshot().state == QueryMacroRunState::Stopped && script.requests.isEmpty(), "immediate Stop sends no requests");
        QObject::connect(&runner, &QueryMacroRunner::requestStarted, &runner, [&] { runner.cancel(); });
        check(start(runner, definition(), script), "cancel-between-batches run starts"); wait(runner);
        check(runner.snapshot().state == QueryMacroRunState::Stopped && runner.snapshot().valueCount() == 8 && script.requests.size() == 1,
            "Stop preserves the in-flight answer and prevents all later requests");
    }
    {
        Script script; script.answer = [](const auto &request, int) { return success(request); };
        QueryMacroRunner runner; check(start(runner, definition(1024), script), "maximum-size definition starts"); wait(runner);
        check(runner.snapshot().valueCount() == 1024 && runner.snapshot().requests == 128 && runner.snapshot().state == QueryMacroRunState::Complete,
            "1024 exact entries complete with bounded batching");
        const auto prior = runner.snapshot().name; auto empty = definition(0);
        check(!start(runner, empty, script) && runner.snapshot().name == prior, "invalid empty run keeps previous snapshot");
    }
    {
        Script script; std::atomic<bool> entered{false}, release{false};
        script.answer = [&](const auto &request, int) { entered = true; while (!release) QThread::msleep(1); return success(request); };
        auto runner = std::make_unique<QueryMacroRunner>(); int changes = 0;
        QObject::connect(runner.get(), &QueryMacroRunner::changed, [&] { ++changes; });
        check(start(*runner, definition(), script), "shutdown run starts");
        while (!entered) { QCoreApplication::processEvents(); QThread::msleep(1); }
        const int before = changes;
        std::thread unblock([&] { QThread::msleep(30); release = true; });
        runner.reset(); unblock.join(); QCoreApplication::processEvents();
        check(changes == before, "destroyed runner receives no late completion or queued UI delivery");
    }
    {
        // Headline must say why: unreachable device, and a skipped table entry.
        Script script; script.answer = [](const auto &, int) { SnmpTransportResult t; t.status = SnmpOperationStatus::Timeout; return t; };
        auto settings = config(); settings.timeout = 3; settings.retries = 2;
        QueryMacroRunner runner; check(start(runner, definition(3), script, settings), "timeout summary run starts"); wait(runner);
        const QString text = runner.snapshot().summary();
        check(runner.snapshot().state == QueryMacroRunState::Failed && text.contains("Problems:") && text.contains("3 \u00d7") &&
            text.contains("no response from 192.0.2.10/4161") && text.contains("timeout 3 s, 2 retries"),
            "timeout summary names the cause, endpoint and timeout settings once with a count");
    }
    {
        Script script; script.answer = [](const auto &request, int) { return success(request); };
        auto macro = definition(1); macro.entries.append(tableEntry());
        FakeSnmpAgent agent(config()); fillTable(agent); script.agent = &agent;
        agent.setFailure([](int, SnmpTransportResult *result) { result->status = SnmpOperationStatus::Timeout; return true; });
        QueryMacroRunner runner; check(start(runner, macro, script), "table summary run starts"); wait(runner);
        const QString text = runner.snapshot().summary();
        check(runner.snapshot().state == QueryMacroRunState::Partial && text.contains("Problems:") && text.contains("Timeout"),
            "a table that stops answering is explained in the headline and keeps the scalar value");
    }
    // ---- Milestone 3: table groups ----
    {   // All instances, sparse cells, raw numeric (no MIB definition in the captured environment).
        Script script; script.answer = [](const auto &request, int) { return success(request); };
        FakeSnmpAgent agent(config()); fillTable(agent); script.agent = &agent;
        QueryMacroRunner runner;
        check(start(runner, tableMacro({tableEntry()}), script), "table-only macro starts"); wait(runner);
        const auto &run = runner.snapshot();
        check(run.state == QueryMacroRunState::Complete && run.groups.size() == 1 && run.groups[0].isTable &&
            run.groups[0].table.rows.size() == 3 && run.groups[0].table.columns.size() == 2 && run.valueCount() == 5,
            "All instances reads the whole table into one grid");
        const auto &rows = run.groups[0].table.rows;
        check(rows.size() == 3 && rows[1].instance == "2" && rows[1].cells[0].available && !rows[1].cells[1].available,
            "a sparse row keeps its missing cell instead of shifting columns");
        check(agent.countOf(SnmpTransportOperation::Get) == 0, "an All-instances table sends no exact GETs");
    }
    {   // SNMPv1 uses GETNEXT only.
        Script script; script.answer = [](const auto &request, int) { return success(request); };
        FakeSnmpAgent agent(config(SnmpRequestVersion::V1)); fillTable(agent); script.agent = &agent;
        QueryMacroRunner runner;
        check(start(runner, tableMacro({tableEntry()}), script, config(SnmpRequestVersion::V1)), "v1 table starts"); wait(runner);
        check(runner.snapshot().groups[0].table.rows.size() == 3 && agent.countOf(SnmpTransportOperation::GetBulk) == 0 &&
            agent.countOf(SnmpTransportOperation::GetNext) > 0, "SNMPv1 tables use GETNEXT, never GETBULK");
    }
    {   // Fixed instances: exact reads; a vanished row never becomes its successor.
        Script script; script.answer = [](const auto &request, int) { return success(request); };
        FakeSnmpAgent agent(config()); fillTable(agent); script.agent = &agent; script.agentGets = true;
        QueryMacroRunner runner;
        check(start(runner, tableMacro({tableEntry(QueryMacroInstancePolicy::Fixed, {"1", "7", "3"})}), script), "fixed table starts");
        wait(runner);
        const auto &run = runner.snapshot();
        check(run.state == QueryMacroRunState::Partial && run.groups[0].table.rows.size() == 2 &&
            run.groups[0].table.rows[0].instance == "1" && run.groups[0].table.rows[1].instance == "3" &&
            run.groups[0].problem.contains("Instance 7"), "Fixed instances read exactly those rows and explain the vanished one");
        check(agent.countOf(SnmpTransportOperation::GetNext) == 0 && agent.countOf(SnmpTransportOperation::GetBulk) == 0,
            "Fixed instances never walk");
    }
    {   // Choose when run: discovery, picker, then retrieval of only the chosen rows.
        Script script; script.answer = [](const auto &request, int) { return success(request); };
        FakeSnmpAgent agent(config()); fillTable(agent); script.agent = &agent; script.agentGets = true;
        QueryMacroRunner runner; QStringList order, offered; int started = 0;
        QObject::connect(&runner, &QueryMacroRunner::selectionStarted, [&] { order << "selection"; });
        QObject::connect(&runner, &QueryMacroRunner::started, [&] { order << "started"; ++started; });
        runner.setInstanceChooser([&](const QString &label, const QStringList &instances, const QStringList &) {
            order << "chooser"; offered = instances; check(label == "testTable", "picker is told which table it chooses for");
            return std::optional<QStringList>(QStringList{"3", "1"});
        });
        check(start(runner, tableMacro({tableEntry(QueryMacroInstancePolicy::ChooseWhenRun)}), script), "choose table starts");
        wait(runner);
        check(order == QStringList({"selection", "chooser", "started"}) && started == 1,
            "results are replaced only after the picker is answered");
        check(offered == QStringList({"1", "2", "3"}), "the picker is offered every discovered instance");
        const auto &run = runner.snapshot();
        check(run.groups[0].table.rows.size() == 2 && run.groups[0].table.rows[0].instance == "1" &&
            run.groups[0].table.rows[1].instance == "3", "only the chosen rows are retrieved, in table order");
    }
    {   // Cancelling the picker abandons the run before any retrieval.
        Script script; script.answer = [](const auto &request, int) { return success(request); };
        FakeSnmpAgent agent(config()); fillTable(agent); script.agent = &agent; script.agentGets = true;
        QueryMacroRunner runner; int started = 0, abandoned = 0, completed = 0;
        QObject::connect(&runner, &QueryMacroRunner::started, [&] { ++started; });
        QObject::connect(&runner, &QueryMacroRunner::abandoned, [&] { ++abandoned; });
        QObject::connect(&runner, &QueryMacroRunner::completed, [&] { ++completed; });
        runner.setInstanceChooser([](const QString &, const QStringList &, const QStringList &) { return std::optional<QStringList>(); });
        auto macro = definition(1); macro.entries.append(tableEntry(QueryMacroInstancePolicy::ChooseWhenRun));
        check(start(runner, macro, script), "cancelled picker run starts"); waitAbandoned(runner);
        check(abandoned == 1 && started == 0 && completed == 0 && !runner.isBusy(),
            "a cancelled picker leaves earlier results in place and ends the run");
        check(agent.countOf(SnmpTransportOperation::Get) == 0 && script.requests.size() == agent.requestCount(),
            "no scalar or table retrieval starts after a cancelled picker");
    }
    {   // Stop during the picker's discovery also abandons without touching results.
        Script script; script.answer = [](const auto &request, int) { return success(request); };
        FakeSnmpAgent agent(config()); fillTable(agent); script.agent = &agent;
        QueryMacroRunner runner; int started = 0, abandoned = 0;
        QObject::connect(&runner, &QueryMacroRunner::started, [&] { ++started; });
        QObject::connect(&runner, &QueryMacroRunner::abandoned, [&] { ++abandoned; });
        QObject::connect(&runner, &QueryMacroRunner::requestStarted, &runner, [&] { runner.cancel(); });
        runner.setInstanceChooser([](const QString &, const QStringList &i, const QStringList &) { return std::optional<QStringList>(i); });
        check(start(runner, tableMacro({tableEntry(QueryMacroInstancePolicy::ChooseWhenRun)}), script), "stopped discovery starts");
        waitAbandoned(runner);
        check(abandoned == 1 && started == 0 && !runner.isBusy(), "Stop during instance discovery abandons the run");
    }
    {   // A table that cannot be listed is explained; the rest of the macro still runs.
        Script script; script.answer = [](const auto &request, int) { return success(request); };
        FakeSnmpAgent agent(config()); script.agent = &agent;   // empty agent: no instances exist
        QueryMacroRunner runner; runner.setInstanceChooser([](const QString &, const QStringList &i, const QStringList &) { return std::optional<QStringList>(i); });
        auto macro = definition(2); macro.entries.append(tableEntry(QueryMacroInstancePolicy::ChooseWhenRun));
        check(start(runner, macro, script), "empty-table choose starts"); wait(runner);
        const auto &run = runner.snapshot();
        check(run.valueCount() == 2 && run.groups.size() == 2 && run.groups[1].problem.contains("no instances") &&
            run.state == QueryMacroRunState::Partial, "a table with no instances is explained and scalars still run");
    }
    {   // Stop keeps rows already received and does not start the next table.
        Script script; script.answer = [](const auto &request, int) { return success(request); };
        FakeSnmpAgent agent(config()); fillTable(agent); script.agent = &agent; script.agentGets = true;
        QueryMacroRunner runner; int requests = 0;
        QObject::connect(&runner, &QueryMacroRunner::requestStarted, &runner, [&] { if (++requests == 2) runner.cancel(); });
        auto macro = tableMacro({tableEntry(QueryMacroInstancePolicy::Fixed, {"1", "2", "3"}), tableEntry()});
        check(start(runner, macro, script), "stopped table run starts"); wait(runner);
        const auto &run = runner.snapshot();
        check(run.state == QueryMacroRunState::Stopped && run.groups[0].table.rows.size() >= 1 && run.groups[0].table.rows.size() <= 2 && run.groups.size() == 2 &&
            run.groups[1].problem.contains("stopped") && agent.countOf(SnmpTransportOperation::GetBulk) == 0,
            "Stop keeps received rows, marks unread tables, and sends nothing further");
    }
    {   // Security refusal ends the run; later tables are not attempted with the same login.
        Script script; script.answer = [](const auto &request, int) { return success(request); };
        FakeSnmpAgent agent(config(SnmpRequestVersion::V3)); fillTable(agent); script.agent = &agent;
        agent.setFailure([](int, SnmpTransportResult *result) {
            result->status = SnmpOperationStatus::TransportFailure; result->transportStatus = SNMPv3_USM_AUTHENTICATION_FAILURE; return true; });
        QueryMacroRunner runner;
        check(start(runner, tableMacro({tableEntry(), tableEntry()}), script, config(SnmpRequestVersion::V3)), "refused tables start");
        wait(runner);
        check(runner.snapshot().state == QueryMacroRunState::Refused && agent.requestCount() == 1 &&
            runner.snapshot().groups[1].problem.contains("refused"), "a refused login is not repeated for every table");
    }
    {   // Two tables run independently; a failing one does not discard the other.
        Script script; script.answer = [](const auto &request, int) { return success(request); };
        FakeSnmpAgent agent(config()); fillTable(agent); script.agent = &agent;
        auto broken = tableEntry(); broken.oid = "1.3.6.1.4.1.999.9.1";
        broken.columns = {"1.3.6.1.4.1.999.9.1.1"};
        QueryMacroRunner runner;
        check(start(runner, tableMacro({broken, tableEntry()}), script), "two-table run starts"); wait(runner);
        const auto &run = runner.snapshot();
        check(run.groups.size() == 2 && run.groups[1].table.rows.size() == 3 && run.state == QueryMacroRunState::Complete,
            "an empty table is not a failure and does not affect an independent table");
    }
    return failures ? 1 : 0;
}
