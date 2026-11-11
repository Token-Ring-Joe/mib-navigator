#include "fakesnmpagent.h"
#include "snmpwalkasyncrunner.h"
#include "snmpwalkoperation.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QSet>
#include <QThread>
#include <QTimer>
#include <atomic>
#include <iostream>
#include <string>

namespace
{
int failures = 0;
void check(bool value, const char *message)
{
    if (!value) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}

const char *const kRoot = "1.3.6.1.4.1.999.1";

SnmpRequestConfig config(SnmpRequestVersion version = SnmpRequestVersion::V2c, int maxRepetitions = 3)
{
    SnmpRequestConfig value;
    value.version = version;
    value.address = "192.0.2.55";
    value.port = "161";
    value.readCommunity = "walk-test";
    value.maxRepetitions = maxRepetitions;
    value.nonRepeaters = 2;      // the global setting; a walk must ignore it
    return value;
}

SnmpWalkResult walk(ISnmpTransport &transport, const SnmpCancellationToken &token = {},
                    const SnmpWalkLimits &limits = {}, const SnmpWalkOperation::Sink &sink = {})
{
    return SnmpWalkOperation(SnmpRequestContext(transport.config(), SnmpRequestOperation::Walk),
                             Oid(kRoot), limits).execute(transport, token, sink);
}

void populate(FakeSnmpAgent &agent, int count)
{
    for (int i = 1; i <= count; ++i)
        agent.addInteger((std::string(kRoot) + "." + std::to_string(i)).c_str(), i);
    agent.addInteger("1.3.6.1.4.1.999.2.1", 99);   // outside the subtree
}

SnmpTransportResult answer(std::initializer_list<const char *> oids)
{
    SnmpTransportResult result;
    result.status = SnmpOperationStatus::Success;
    result.transportStatus = SNMP_CLASS_SUCCESS;
    for (const char *oid : oids) {
        Vb vb{Oid(oid)};
        vb.set_value(1);
        result.pdu += vb;
    }
    return result;
}

SnmpTransportResult failure(SnmpOperationStatus status, int code = 0)
{
    SnmpTransportResult result;
    result.status = status;
    result.transportStatus = code;
    return result;
}

SnmpTransportResult snmpError(int code)
{
    SnmpTransportResult result = failure(SnmpOperationStatus::SnmpError, code);
    result.snmpErrorStatus = code;
    result.pdu.set_error_status(code);
    return result;
}

QStringList oids(const SnmpWalkResult &result)
{
    QStringList list;
    for (const Vb &vb : result.varbinds)
        list << QString::fromLatin1(vb.get_oid().get_printable());
    return list;
}

void normalWalks()
{
    FakeSnmpAgent v2(config());
    populate(v2, 8);
    const SnmpWalkResult result = walk(v2);
    check(result.status == SnmpOperationStatus::Complete && result.objects == 8 &&
              result.varbinds.size() == 8 && result.problem.isEmpty(),
          "v2c walk lists the whole subtree");
    check(oids(result).first() == QString(kRoot) + ".1" && oids(result).last() == QString(kRoot) + ".8",
          "walk results are in order and stop before the object outside the subtree");
    check(v2.countOf(SnmpTransportOperation::GetBulk) == v2.requestCount() && v2.requestCount() == 3,
          "v2c walk uses GETBULK, three objects at a time (8 objects, then the one outside)");
    check(result.requests == 3, "requests counts the answers received");

    // The captured request context is used; non-repeaters are always 0.
    ScriptedSnmpTransport scripted(config(SnmpRequestVersion::V2c, 17));
    scripted.append(answer({"1.3.6.1.4.1.999.1.1"}));
    scripted.append(answer({"1.3.6.1.4.1.999.2.1"}));
    walk(scripted);
    check(scripted.requests().size() == 2 && scripted.requests().first().nonRepeaters == 0 &&
              scripted.requests().first().maxRepetitions == 17 &&
              scripted.requests().first().operation == SnmpTransportOperation::GetBulk,
          "a walk asks for the configured max-repetitions and never for non-repeaters");
    Vb seed;
    scripted.requests().last().pdu.get_vb(seed, 0);
    check(seed.get_oid() == Oid("1.3.6.1.4.1.999.1.1"), "the last object read seeds the next request");

    FakeSnmpAgent v1(config(SnmpRequestVersion::V1));
    v1.addInteger("1.3.6.1.4.1.999.1.1", 1);
    v1.addInteger("1.3.6.1.4.1.999.1.2", 2);
    const SnmpWalkResult one = walk(v1);
    check(one.status == SnmpOperationStatus::Complete && one.objects == 2 && one.problem.isEmpty() &&
              v1.countOf(SnmpTransportOperation::GetNext) == v1.requestCount() && v1.requestCount() == 3,
          "SNMPv1 walk uses GETNEXT and ends on noSuchName");

    FakeSnmpAgent view(config());
    view.addInteger("1.3.6.1.4.1.999.1.1", 1);
    const SnmpWalkResult ended = walk(view);
    check(ended.status == SnmpOperationStatus::Complete && ended.objects == 1,
          "endOfMibView ends the walk");

    FakeSnmpAgent empty(config());
    empty.addInteger("1.3.6.1.4.1.999.2.1", 1);
    const SnmpWalkResult none = walk(empty);
    check(none.status == SnmpOperationStatus::Complete && none.objects == 0 && none.problem.isEmpty() &&
              none.varbinds.isEmpty(),
          "a subtree with no objects completes with nothing");

    ScriptedSnmpTransport exception(config());
    Vb gone{Oid("1.3.6.1.4.1.999.1.2")};
    gone.set_exception_status(sNMP_SYNTAX_NOSUCHINSTANCE);
    SnmpTransportResult mixed = answer({"1.3.6.1.4.1.999.1.1"});
    mixed.pdu += gone;
    exception.append(mixed);
    const SnmpWalkResult withException = walk(exception);
    check(withException.status == SnmpOperationStatus::Complete && withException.objects == 1,
          "noSuchInstance ends the walk");

    ScriptedSnmpTransport nothing(config());
    SnmpTransportResult emptyAnswer;
    emptyAnswer.status = SnmpOperationStatus::Success;
    nothing.append(emptyAnswer);
    check(walk(nothing).status == SnmpOperationStatus::Complete, "an empty response ends the walk");
}

void sinkDelivery()
{
    FakeSnmpAgent agent(config());
    populate(agent, 8);
    QVector<Vb> received;
    int batches = 0;
    const SnmpWalkResult result = walk(agent, {}, {}, [&](const QVector<Vb> &found) {
        received += found;
        ++batches;
    });
    check(received.size() == 8 && result.varbinds.isEmpty() && result.objects == 8 && batches == 3,
          "with a sink the objects are delivered as each answer arrives, not kept in the result");
}

void misbehavingAgents()
{
    // The same OID again.
    FakeSnmpAgent stuck(config());
    populate(stuck, 3);
    stuck.setStuck(true);
    SnmpWalkResult result = walk(stuck);
    check(result.status == SnmpOperationStatus::Complete && !result.problem.isEmpty() &&
              result.problem.contains("increasing order") && result.objects == 1 && stuck.requestCount() < 10,
          "an agent repeating an OID ends the walk with a reason and keeps what was read");

    FakeSnmpAgent stuck1(config(SnmpRequestVersion::V1));
    populate(stuck1, 3);
    stuck1.setStuck(true);
    result = walk(stuck1);
    check(!result.problem.isEmpty() && stuck1.requestCount() < 10, "the same applies to SNMPv1");

    // A lower OID that is still inside the subtree.
    ScriptedSnmpTransport lower(config());
    lower.append(answer({"1.3.6.1.4.1.999.1.10", "1.3.6.1.4.1.999.1.20"}));
    lower.append(answer({"1.3.6.1.4.1.999.1.15", "1.3.6.1.4.1.999.1.30"}));
    result = walk(lower);
    check(result.status == SnmpOperationStatus::Complete && result.problem.contains("1.3.6.1.4.1.999.1.15") &&
              result.problem.contains("1.3.6.1.4.1.999.1.20") && result.objects == 2 &&
              lower.requests().size() == 2,
          "a lower OID inside the subtree stops the walk with a reason");

    // Out of order inside one answer.
    ScriptedSnmpTransport within(config());
    within.append(answer({"1.3.6.1.4.1.999.1.5", "1.3.6.1.4.1.999.1.3", "1.3.6.1.4.1.999.1.9"}));
    result = walk(within);
    check(!result.problem.isEmpty() && result.objects == 1, "out of order within one answer is caught");

    // The root itself returned.
    ScriptedSnmpTransport same(config());
    same.append(answer({kRoot}));
    result = walk(same);
    check(!result.problem.isEmpty() && result.objects == 0, "returning the starting OID is not progress");
}

void failures_()
{
    SnmpRequestConfig v3 = config();
    v3.version = SnmpRequestVersion::V3;
    v3.securityName = "operator";
    ScriptedSnmpTransport report(v3);
    SnmpTransportResult refused = answer({"1.3.6.1.6.3.15.1.1.3.0"});   // usmStatsUnknownUserNames
    refused.pdu.set_type(sNMP_PDU_REPORT);
    report.append(refused);
    SnmpWalkResult result = walk(report);
    check(result.status == SnmpOperationStatus::SnmpError && result.objects == 0 &&
              result.problem.contains("operator"),
          "an SNMPv3 REPORT is explained, not listed");

    ScriptedSnmpTransport local(v3);
    local.append(failure(SnmpOperationStatus::TransportFailure, SNMPv3_USM_DECRYPTION_ERROR));
    result = walk(local);
    check(result.status == SnmpOperationStatus::TransportFailure && result.problem.contains("privacy") &&
              result.transportStatus == SNMPv3_USM_DECRYPTION_ERROR,
          "a local SNMPv3 failure names its cause");

    // A timeout in the middle keeps the partial results.
    FakeSnmpAgent quiet(config());
    populate(quiet, 9);
    quiet.setFailure([](int number, SnmpTransportResult *failed) {
        if (number < 3) return false;
        failed->status = SnmpOperationStatus::Timeout;
        failed->transportStatus = SNMP_CLASS_TIMEOUT;
        return true;
    });
    result = walk(quiet);
    check(result.status == SnmpOperationStatus::Timeout && result.objects == 6 && result.varbinds.size() == 6,
          "a timeout mid-walk keeps the objects already read");

    {
        // A silent SNMPv3 device (nothing read yet) is explained; a device that answered first is not.
        SnmpRequestConfig v3Quiet = config(SnmpRequestVersion::V3);
        v3Quiet.securityName = "lab"; v3Quiet.securityLevel = SNMP_SECURITY_LEVEL_AUTH_NOPRIV;
        ScriptedSnmpTransport silent(v3Quiet);
        silent.append(failure(SnmpOperationStatus::Timeout, SNMP_CLASS_TIMEOUT));
        const SnmpWalkResult silentResult = walk(silent);
        check(silentResult.status == SnmpOperationStatus::Timeout && silentResult.problem.contains("user 'lab'") &&
                  silentResult.problem.contains("authentication without privacy") &&
                  silentResult.problem.contains("cannot be known"),
              "an SNMPv3 walk that never gets an answer explains the likely causes");
        check(result.problem.isEmpty(), "a timeout after objects were read keeps the plain message");
    }

    ScriptedSnmpTransport denied(config());
    denied.append(snmpError(SNMP_ERROR_GENERAL_VB_ERR));
    result = walk(denied);
    check(result.status == SnmpOperationStatus::SnmpError && result.snmpErrorStatus == SNMP_ERROR_GENERAL_VB_ERR,
          "another device error is reported with its code");
}

void tooBig()
{
    FakeSnmpAgent smallLimit(config(SnmpRequestVersion::V2c, 20));
    populate(smallLimit, 30);
    smallLimit.setMaxVarbinds(2);
    SnmpWalkResult result = walk(smallLimit);
    check(result.status == SnmpOperationStatus::Complete && result.objects == 30 && result.problem.isEmpty(),
          "tooBig: the walk asks for less and completes");

    FakeSnmpAgent tiny(config());
    populate(tiny, 3);
    tiny.setFailure([](int, SnmpTransportResult *response) {
        response->status = SnmpOperationStatus::SnmpError;
        response->snmpErrorStatus = SNMP_ERROR_TOO_BIG;
        response->pdu.set_error_status(SNMP_ERROR_TOO_BIG);
        return true;
    });
    result = walk(tiny);
    check(result.status == SnmpOperationStatus::SnmpError && result.snmpErrorStatus == SNMP_ERROR_TOO_BIG &&
              tiny.requestCount() <= 4,
          "tooBig that cannot be fixed fails cleanly instead of retrying forever");

    FakeSnmpAgent v1(config(SnmpRequestVersion::V1));
    v1.setFailure([](int, SnmpTransportResult *response) {
        response->status = SnmpOperationStatus::SnmpError;
        response->snmpErrorStatus = SNMP_ERROR_TOO_BIG;
        response->pdu.set_error_status(SNMP_ERROR_TOO_BIG);
        return true;
    });
    result = walk(v1);
    check(result.status == SnmpOperationStatus::SnmpError && v1.requestCount() == 1,
          "SNMPv1 has nothing to shrink: tooBig fails at once");
}

void cancellationAndCaps()
{
    SnmpCancellationToken cancelled;
    cancelled.cancel();
    FakeSnmpAgent never(config());
    populate(never, 3);
    check(walk(never, cancelled).status == SnmpOperationStatus::Cancelled && never.requestCount() == 0,
          "cancelled before the first request sends nothing");

    SnmpCancellationToken midway;
    FakeSnmpAgent agent(config());
    populate(agent, 12);
    const SnmpWalkResult result = walk(agent, midway, {}, [&](const QVector<Vb> &) { midway.cancel(); });
    check(result.status == SnmpOperationStatus::Cancelled && result.objects == 3 && agent.requestCount() == 1,
          "cancelling between requests stops the walk and keeps what was read");

    FakeSnmpAgent capped(config());
    populate(capped, 50);
    SnmpWalkLimits limits;
    limits.maxRequests = 4;
    SnmpWalkResult limited = walk(capped, {}, limits);
    check(limited.status == SnmpOperationStatus::Complete && limited.problem.contains("4 requests") &&
              limited.objects == 12 && capped.requestCount() == 4,
          "the request cap stops a runaway walk with a reason");
    limits = {};
    limits.maxObjects = 7;
    limited = walk(capped, {}, limits);
    check(limited.status == SnmpOperationStatus::Complete && limited.problem.contains("7 objects") &&
              limited.objects == 7 && limited.varbinds.size() == 7,
          "the object cap stops a runaway walk with a reason");
}

// An agent that takes a while to answer each request, so a walk is visibly in progress.
class SlowAgent final : public ISnmpTransport
{
public:
    SlowAgent(const SnmpRequestConfig &config, int delayMs) : agent(config), delay(delayMs) {}
    FakeSnmpAgent &inner() { return agent; }
    SnmpTransportResult execute(const SnmpTransportRequest &request) override
    {
        QThread::msleep(delay);
        return agent.execute(request);
    }
    const SnmpRequestConfig &config() const override { return agent.config(); }
private:
    FakeSnmpAgent agent;
    unsigned long delay;
};

// Blocks inside one request, like a device that never answers.
class BlockedTransport final : public ISnmpTransport
{
public:
    BlockedTransport(const SnmpRequestConfig &config, std::shared_ptr<std::atomic<bool>> started,
                     std::shared_ptr<std::atomic<bool>> done, unsigned long blockMs)
        : requestConfig(config), started(std::move(started)), done(std::move(done)), blockMs(blockMs) {}
    SnmpTransportResult execute(const SnmpTransportRequest &) override
    {
        *started = true;
        QThread::msleep(blockMs);
        *done = true;
        return failure(SnmpOperationStatus::Timeout, SNMP_CLASS_TIMEOUT);
    }
    const SnmpRequestConfig &config() const override { return requestConfig; }
private:
    SnmpRequestConfig requestConfig;
    std::shared_ptr<std::atomic<bool>> started, done;
    unsigned long blockMs;
};

void spin(const std::function<bool()> &until, int limitMs)
{
    QEventLoop loop;
    QTimer poll; QObject::connect(&poll, &QTimer::timeout, &loop, [&]() { if (until()) loop.quit(); });
    QTimer limit; limit.setSingleShot(true); QObject::connect(&limit, &QTimer::timeout, &loop, &QEventLoop::quit);
    poll.start(5); limit.start(limitMs);
    if (!until()) loop.exec();
}

void asyncWalks()
{
    // Large and fast enough to need more than the runner's backlog: all of it must arrive, in
    // order, and `completed` must come after the last batch.
    {
        auto *agent = new FakeSnmpAgent(config(SnmpRequestVersion::V2c, 50));
        for (int i = 1; i <= 6000; ++i) agent->addInteger((std::string(kRoot) + "." + std::to_string(i)).c_str(), i);
        SnmpWalkAsyncRunner runner;
        QVector<Vb> received;
        bool completedSeen = false, progressAfterCompleted = false;
        SnmpWalkResult final;
        QObject::connect(&runner, &SnmpWalkAsyncRunner::progress, [&](const QVector<Vb> &batch) {
            if (completedSeen) progressAfterCompleted = true;
            received += batch;
        });
        QObject::connect(&runner, &SnmpWalkAsyncRunner::completed, [&](const SnmpWalkResult &result) {
            completedSeen = true; final = result;
        });
        check(runner.start(SnmpRequestContext(agent->config(), SnmpRequestOperation::Walk), Oid(kRoot),
                           std::unique_ptr<ISnmpTransport>(agent)), "the runner starts");
        spin([&]() { return completedSeen; }, 20000);
        check(completedSeen && final.status == SnmpOperationStatus::Complete && final.objects == 6000 &&
                  final.varbinds.isEmpty(),
              "the runner reports a finished walk");
        bool ordered = received.size() == 6000;
        for (int i = 0; ordered && i < received.size(); ++i)
            ordered = received[i].get_oid() == Oid((std::string(kRoot) + "." + std::to_string(i + 1)).c_str());
        check(ordered && !progressAfterCompleted, "every object arrives, in order, before the result");
    }

    // A slow consumer must yield to other owner-thread work between delivery chunks.
    // Observe queued heartbeats instead of bounding wall time: a VM can be descheduled at any
    // point, and a scheduling pause says nothing about whether the event loop is blocked.
    {
        auto *agent = new FakeSnmpAgent(config(SnmpRequestVersion::V2c, 50));
        for (int i = 1; i <= 3000; ++i) agent->addInteger((std::string(kRoot) + "." + std::to_string(i)).c_str(), i);
        SnmpWalkAsyncRunner runner;
        int objects = 0, batches = 0, largest = 0;
        bool completedSeen = false;
        QSet<int> heartbeatPositions;
        QObject::connect(&runner, &SnmpWalkAsyncRunner::progress, [&](const QVector<Vb> &batch) {
            ++batches; objects += batch.size(); largest = qMax(largest, int(batch.size()));
            QMetaObject::invokeMethod(&runner, [&]() {
                if (!completedSeen && objects > 0 && objects < 3000)
                    heartbeatPositions.insert(objects);
            }, Qt::QueuedConnection);
            QThread::msleep(20);   // a consumer that is slow per chunk
        });
        QObject::connect(&runner, &SnmpWalkAsyncRunner::completed, [&](const SnmpWalkResult &) { completedSeen = true; });
        runner.start(SnmpRequestContext(agent->config(), SnmpRequestOperation::Walk), Oid(kRoot),
                     std::unique_ptr<ISnmpTransport>(agent));
        spin([&]() { return completedSeen; }, 20000);
        check(completedSeen && objects == 3000, "a slow consumer still gets every object");
        check(largest <= SnmpWalkAsyncRunner::kDeliveryChunk && batches >= 3000 / SnmpWalkAsyncRunner::kDeliveryChunk,
              "objects are delivered in smallLimit chunks");
        check(heartbeatPositions.size() >= 2,
              "the owner's event loop runs queued heartbeats between delivery chunks");
    }

    // Objects are shown while the walk is still running, and Stop ends it.
    {
        auto *agent = new SlowAgent(config(SnmpRequestVersion::V2c, 5), 30);
        for (int i = 1; i <= 400; ++i) agent->inner().addInteger((std::string(kRoot) + "." + std::to_string(i)).c_str(), i);
        SnmpWalkAsyncRunner runner;
        int received = 0;
        bool firstWhileRunning = false, completedSeen = false;
        SnmpWalkResult final;
        QObject::connect(&runner, &SnmpWalkAsyncRunner::progress, [&](const QVector<Vb> &batch) {
            if (received == 0) firstWhileRunning = runner.isRunning() && !completedSeen;
            received += batch.size();
            if (received >= 15) runner.cancel();
        });
        QObject::connect(&runner, &SnmpWalkAsyncRunner::completed, [&](const SnmpWalkResult &result) {
            completedSeen = true; final = result;
        });
        runner.start(SnmpRequestContext(agent->config(), SnmpRequestOperation::Walk), Oid(kRoot),
                     std::unique_ptr<ISnmpTransport>(agent));
        spin([&]() { return completedSeen; }, 20000);
        check(firstWhileRunning, "objects appear while the walk is still running");
        check(completedSeen && final.status == SnmpOperationStatus::Cancelled && received >= 15 &&
                  received == final.objects && received < 400,
              "Stop cancels the walk between requests and keeps what was read");
        spin([&]() { return !runner.isRunning(); }, 5000);
        check(!runner.isRunning(), "the worker thread finishes after Stop");
    }

    // A second walk may start the moment the first reports its result, even though the first
    // worker thread has not quite exited; a walk already in progress refuses a second one.
    {
        SnmpWalkAsyncRunner runner;
        int completions = 0, objects = 0;
        bool refusedWhileBusy = false, secondStarted = false;
        QObject::connect(&runner, &SnmpWalkAsyncRunner::progress,
                         [&](const QVector<Vb> &batch) { objects += batch.size(); });
        QObject::connect(&runner, &SnmpWalkAsyncRunner::completed, [&](const SnmpWalkResult &) {
            if (++completions == 1) {
                auto *second = new FakeSnmpAgent(config());
                populate(*second, 4);
                secondStarted = runner.start(SnmpRequestContext(second->config(), SnmpRequestOperation::Walk),
                                             Oid(kRoot), std::unique_ptr<ISnmpTransport>(second));
            }
        });
        auto *first = new FakeSnmpAgent(config());
        populate(*first, 5);
        runner.start(SnmpRequestContext(first->config(), SnmpRequestOperation::Walk), Oid(kRoot),
                     std::unique_ptr<ISnmpTransport>(first));
        auto *extra = new FakeSnmpAgent(config());
        refusedWhileBusy = !runner.start(SnmpRequestContext(extra->config(), SnmpRequestOperation::Walk),
                                         Oid(kRoot), std::unique_ptr<ISnmpTransport>(extra));
        spin([&]() { return completions >= 2; }, 10000);
        spin([&]() { return !runner.isRunning(); }, 5000);
        check(refusedWhileBusy, "a walk already in progress refuses a second");
        check(secondStarted && completions == 2 && objects == 9,
              "a walk can start in the handler of the previous result, and both complete");
        check(!runner.isRunning() && !runner.isBusy(), "the runner is idle afterwards");
    }

    // Shutting down during a request that does not return must not hang.
    {
        auto started = std::make_shared<std::atomic<bool>>(false);
        auto done = std::make_shared<std::atomic<bool>>(false);
        QElapsedTimer timer;
        {
            SnmpWalkAsyncRunner runner;
            runner.setShutdownWaitMs(200);
            runner.start(SnmpRequestContext(config(), SnmpRequestOperation::Walk), Oid(kRoot),
                         std::make_unique<BlockedTransport>(config(), started, done, 1500));
            spin([&]() { return started->load(); }, 5000);
            timer.start();
        }
        check(timer.elapsed() < 1200 && !done->load(),
              "destroying a runner whose request is stuck returns after the bounded wait");
        spin([&]() { return done->load(); }, 5000);   // let the abandoned worker finish before exit
        QThread::msleep(200);
    }
}
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    normalWalks();
    sinkDelivery();
    misbehavingAgents();
    failures_();
    tooBig();
    cancellationAndCaps();
    asyncWalks();
    if (failures) { std::cerr << failures << " check(s) failed\n"; return 1; }
    std::cout << "walk tests passed\n";
    return 0;
}
