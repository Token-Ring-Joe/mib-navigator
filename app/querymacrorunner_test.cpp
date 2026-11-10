#include "querymacrorunner.h"
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
};
class Transport final : public ISnmpTransport {
public:
    Transport(Script &script, SnmpRequestConfig config) : script(script), captured(std::move(config)) {}
    const SnmpRequestConfig &config() const override { return captured; }
    SnmpTransportResult execute(const SnmpTransportRequest &request) override {
        check(request.operation == SnmpTransportOperation::Get, "macros send exact GETs only");
        int number;
        { QMutexLocker lock(&script.mutex); QStringList oids;
          for (int i = 0; i < request.pdu.get_vb_count(); ++i) { Vb vb; request.pdu.get_vb(vb, i); oids.append(QString::fromLatin1(vb.get_printable_oid())); }
          script.requests.append(oids); script.configs.append(captured); number = script.requests.size(); }
        return script.answer(request, number);
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
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    {
        Script script; script.answer = [](const auto &request, int) { return success(request); };
        QueryMacroRunner runner; auto macro = definition(0);
        QueryMacroEntry table; table.id = QueryMacroNewId(); table.kind = QueryMacroKind::Table;
        table.oid = "1.3.6.1.2.1.2.2.1"; table.columns = {"1.3.6.1.2.1.2.2.1.2"}; macro.entries.append(table);
        check(start(runner, macro, script), "table-only macro starts an explanatory result"); wait(runner);
        check(script.requests.isEmpty() && runner.snapshot().groups.size() == 1 &&
            runner.snapshot().groups[0].id == table.id && !runner.snapshot().groups[0].problem.isEmpty(),
            "table-only macro shows its deferred group directly and never sends network requests");
    }
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
        table.oid = "1.3.6.1.2.1.2.2.1"; table.columns = {"1.3.6.1.2.1.2.2.1.2"}; macro.entries.append(table);
        const auto environment = std::make_shared<MibEnvironment>(); int starts = 0, finishes = 0;
        QObject::connect(&runner, &QueryMacroRunner::requestStarted, [&](const QStringList &) { ++starts; });
        QObject::connect(&runner, &QueryMacroRunner::requestFinished, [&](const QString &, int, bool, bool, bool) { ++finishes; });
        check(start(runner, macro, script, settings, environment), "mixed scalar/exact/table run starts explicitly");
        check(!start(runner, definition(), script), "concurrent run cannot replace captured state");
        macro.name = "Later edit"; macro.entries.clear(); settings.address = "192.0.2.99"; settings.readCommunity = "later-secret";
        wait(runner); const auto &run = runner.snapshot();
        check(run.name == "Captured essentials" && run.environment == environment && run.credential == "Lab read credential" &&
            run.profile == "Captured Profile" && run.started.isValid(), "definition, environment and display identity are captured values");
        check(run.state == QueryMacroRunState::Partial && run.valueCount() == 20 && run.groups.size() == 2 &&
            !run.groups[1].problem.isEmpty(), "table groups are explained while scalar/exact reads complete");
        check(starts == 3 && finishes == starts && script.requests.size() == 3, "one lifecycle pair per actual bounded batch");
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
        auto macro = definition(1); QueryMacroEntry table; table.id = QueryMacroNewId(); table.kind = QueryMacroKind::Table;
        table.oid = "1.3.6.1.2.1.2.2.1"; table.columns = {"1.3.6.1.2.1.2.2.1.2"}; macro.entries.append(table);
        QueryMacroRunner runner; check(start(runner, macro, script), "table summary run starts"); wait(runner);
        const QString text = runner.snapshot().summary();
        check(text.contains("Problems:") && text.contains("table entry was skipped") && !text.contains("\u00d7"),
            "a skipped table entry is explained in the headline");
    }
    return failures ? 1 : 0;
}
