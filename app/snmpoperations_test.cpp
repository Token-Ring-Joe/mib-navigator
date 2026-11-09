#include "snmpinstanceasyncrunner.h"
#include "snmptableasyncrunner.h"
#include "snmpinstanceoperation.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>
#include <chrono>
#include <iostream>
#include <thread>

namespace
{
bool check(bool value, const char *message)
{
    if (!value) std::cerr << "FAIL: " << message << '\n';
    return value;
}

SnmpRequestConfig config()
{
    SnmpRequestConfig value;
    value.version = SnmpRequestVersion::V2c;
    value.address = "192.0.2.55";
    value.port = "1161";
    value.retries = 3;
    value.timeout = 7;
    value.readCommunity = "captured-read";
    value.writeCommunity = "captured-write";
    value.maxRepetitions = 19;
    value.nonRepeaters = 2;
    return value;
}

SnmpTransportResult response(const char *oid, int syntax = sNMP_SYNTAX_INT32)
{
    SnmpTransportResult result;
    result.status = SnmpOperationStatus::Success;
    result.transportStatus = SNMP_CLASS_SUCCESS;
    Vb vb{Oid(oid)};
    if (syntax == sNMP_SYNTAX_INT32) vb.set_value(7);
    else vb.set_exception_status(syntax);
    result.pdu += vb;
    return result;
}

SnmpTransportResult status(SnmpOperationStatus value, int code = -1)
{
    SnmpTransportResult result;
    result.status = value;
    result.transportStatus = code;
    if (value == SnmpOperationStatus::SnmpError)
    {
        result.snmpErrorStatus = SNMP_ERROR_GENERAL_VB_ERR;
        result.pdu.set_error_status(SNMP_ERROR_GENERAL_VB_ERR);
    }
    return result;
}

SnmpTablePlan plan(bool twoColumns = false)
{
    SnmpTablePlan value;
    value.rowOid = Oid("1.3.6.1.4.1.999.1");
    value.columns.append({"first", Oid("1.3.6.1.4.1.999.1.1")});
    if (twoColumns)
        value.columns.append({"second", Oid("1.3.6.1.4.1.999.1.2")});
    return value;
}

class SlowTransport final : public ISnmpTransport
{
public:
    explicit SlowTransport(const SnmpRequestConfig &value) : captured(value) {}
    SnmpTransportResult execute(const SnmpTransportRequest &) override
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
        return response("1.3.6.1.4.1.999.1.1.10");
    }
    const SnmpRequestConfig &config() const override { return captured; }
private:
    SnmpRequestConfig captured;
};

bool transportCoverage()
{
    ScriptedSnmpTransport fake(config());
    fake.append(response("1.2.3"));
    fake.append(response("1.2.4"));
    fake.append(response("1.2.5"));
    fake.append(response("1.2.6"));
    fake.append(status(SnmpOperationStatus::Timeout, SNMP_CLASS_TIMEOUT));
    fake.append(status(SnmpOperationStatus::SnmpError));
    fake.append(status(SnmpOperationStatus::TransportFailure));
    const SnmpTransportOperation operations[] = {
        SnmpTransportOperation::Get, SnmpTransportOperation::GetNext,
        SnmpTransportOperation::GetBulk, SnmpTransportOperation::Set};
    bool ok = true;
    for (SnmpTransportOperation operation : operations)
    {
        SnmpTransportRequest request;
        request.operation = operation;
        request.nonRepeaters = 2; request.maxRepetitions = 19;
        ok &= check(fake.execute(request).status == SnmpOperationStatus::Success,
                    "scripted success operation");
    }
    SnmpTransportRequest request;
    ok &= check(fake.execute(request).status == SnmpOperationStatus::Timeout,
                "timeout distinction");
    ok &= check(fake.execute(request).status == SnmpOperationStatus::SnmpError,
                "SNMP error distinction");
    ok &= check(fake.execute(request).status == SnmpOperationStatus::TransportFailure,
                "transport failure distinction");
    return ok;
}

bool tableCoverage()
{
    SnmpRequestConfig mutableSelection = config();
    const SnmpRequestContext context(mutableSelection, SnmpRequestOperation::Walk);
    const SnmpRequestConfig transportConfig = mutableSelection;
    mutableSelection.readCommunity = "changed-after-capture";
    mutableSelection.maxRepetitions = 1;
    ScriptedSnmpTransport fake(transportConfig);
    fake.append(response("1.3.6.1.4.1.999.1.1.10"));
    fake.append(response("1.3.6.1.4.1.999.1.1.10"));
    fake.append(response("1.3.6.1.4.1.999.1.2.10"));
    fake.append(response("1.3.6.1.4.1.999.1.1.11"));
    fake.append(response("1.3.6.1.4.1.999.1.1.11"));
    fake.append(SnmpTransportResult{}); // short cell response
    fake.append(response("1.3.6.1.4.1.999.2.1.0")); // subtree exit
    SnmpCancellationToken cancellation;
    const SnmpTableResult result = SnmpTableOperation(context, plan(true)).execute(
        fake, cancellation);
    bool ok = check(result.status == SnmpOperationStatus::Complete &&
                    result.rows.size() == 2 && result.rows[0].cells.size() == 2,
                    "normal multi-column traversal") &&
              check(result.rows[0].instance == "10" &&
                    result.rows[1].instance == "11",
                    "instance extraction/order") &&
              check(result.rows[0].cells[0].available &&
                    !result.rows[1].cells[1].available,
                    "available and short-response cells") &&
              check(fake.requests().size() == 7 &&
                    fake.requests().first().operation ==
                        SnmpTransportOperation::GetNext &&
                    fake.requests()[1].operation == SnmpTransportOperation::Get,
                    "request ordering") &&
              check(fake.requests().first().maxRepetitions == 19 &&
                    fake.requests().first().nonRepeaters == 2 &&
                    fake.config().readCommunity == "captured-read",
                    "captured bulk/config values");

    ScriptedSnmpTransport malformed(config());
    malformed.append(response("1.3.6.1.4.1.998.1.1.10"));
    ok &= check(SnmpTableOperation(context, plan()).execute(
                    malformed, cancellation).status == SnmpOperationStatus::Complete,
                "malformed/out-of-subtree lexical completion");

    ScriptedSnmpTransport unavailableCell(config());
    unavailableCell.append(response("1.3.6.1.4.1.999.1.1.10"));
    unavailableCell.append(response("1.3.6.1.4.1.999.1.1.10",
                                    sNMP_SYNTAX_NOSUCHINSTANCE));
    unavailableCell.append(response("1.3.6.1.4.1.999.2.1.0"));
    const SnmpTableResult unavailableResult = SnmpTableOperation(
        context, plan()).execute(unavailableCell, cancellation);
    ok &= check(unavailableResult.rows.size() == 1 &&
                !unavailableResult.rows.first().cells.first().available,
                "per-column unavailable exception");

    ScriptedSnmpTransport empty(transportConfig);
    SnmpTransportResult zero; zero.status = SnmpOperationStatus::Success;
    zero.transportStatus = SNMP_CLASS_SUCCESS; empty.append(zero);
    ok &= check(SnmpTableOperation(context, plan()).execute(empty, cancellation).status ==
                    SnmpOperationStatus::Complete,
                "zero-varbind completion");
    ScriptedSnmpTransport timed(transportConfig);
    timed.append(status(SnmpOperationStatus::Timeout, SNMP_CLASS_TIMEOUT));
    ok &= check(SnmpTableOperation(context, plan()).execute(timed, cancellation).status ==
                    SnmpOperationStatus::Timeout,
                "table timeout");
    ScriptedSnmpTransport errored(transportConfig);
    errored.append(status(SnmpOperationStatus::SnmpError));
    ok &= check(SnmpTableOperation(context, plan()).execute(errored, cancellation).status ==
                    SnmpOperationStatus::SnmpError,
                "table SNMP error");
    return ok;
}

bool instanceCoverage()
{
    const SnmpRequestContext context(config(), SnmpRequestOperation::Walk);
    const Oid root("1.3.6.1.4.1.999.1.1");
    ScriptedSnmpTransport fake(config());
    fake.append(response("1.3.6.1.4.1.999.1.1.10"));
    fake.append(response("1.3.6.1.4.1.999.1.1.11"));
    fake.append(response("1.3.6.1.4.1.999.2.1"));
    SnmpCancellationToken token;
    const SnmpInstanceResult normal = SnmpInstanceOperation(context, root).execute(
        fake, token);
    bool ok = check(normal.status == SnmpOperationStatus::Complete &&
                    normal.instances == QStringList({"10", "11"}) &&
                    fake.requests().size() == 3,
                    "instance enumeration/order/subtree exit") &&
              check(fake.requests().first().maxRepetitions == 19 &&
                    fake.config().readCommunity == "captured-read",
                    "instance captured context");
    ScriptedSnmpTransport empty(config());
    SnmpTransportResult zero; zero.status = SnmpOperationStatus::Success;
    zero.transportStatus = SNMP_CLASS_SUCCESS; empty.append(zero);
    ok &= check(SnmpInstanceOperation(context, root).execute(empty, token).instances.isEmpty(),
                "empty instance response");
    ScriptedSnmpTransport malformed(config());
    malformed.append(response("1.3.6.1.4.1.998.1"));
    ok &= check(SnmpInstanceOperation(context, root).execute(malformed, token).status ==
                    SnmpOperationStatus::Complete,
                "malformed instance response");
    ScriptedSnmpTransport timed(config());
    timed.append(status(SnmpOperationStatus::Timeout, SNMP_CLASS_TIMEOUT));
    ok &= check(SnmpInstanceOperation(context, root).execute(timed, token).status ==
                    SnmpOperationStatus::Timeout,
                "instance timeout");
    ScriptedSnmpTransport errored(config());
    errored.append(status(SnmpOperationStatus::SnmpError));
    ok &= check(SnmpInstanceOperation(context, root).execute(errored, token).status ==
                    SnmpOperationStatus::SnmpError,
                "instance SNMP error");
    SnmpCancellationToken cancelled; cancelled.cancel();
    ScriptedSnmpTransport unused(config());
    ok &= check(SnmpInstanceOperation(context, root).execute(unused, cancelled).status ==
                    SnmpOperationStatus::Cancelled && unused.requests().isEmpty(),
                "instance cancellation stops requests");
    return ok;
}

// Regression: a runner used to deadlock when it was waited on or destroyed by a thread that was
// not running its event loop. The worker asks its thread to quit through a queued call into the
// owner's thread, which can never be delivered while the owner is blocked in wait(). These cases
// block the owner on purpose, so they fail deterministically (not just under load) if it returns.
bool blockedOwnerCoverage()
{
    bool ok = true;
    const SnmpRequestContext context(config(), SnmpRequestOperation::Walk);
    const auto scriptedTable = [] {
        auto fake = std::make_unique<ScriptedSnmpTransport>(config());
        fake->append(response("1.3.6.1.4.1.999.1.1.10"));
        fake->append(response("1.3.6.1.4.1.999.1.1.10"));
        fake->append(response("1.3.6.1.4.1.999.2.1.0"));
        return fake;
    };
    const auto scriptedInstance = [] {
        auto fake = std::make_unique<ScriptedSnmpTransport>(config());
        fake->append(response("1.3.6.1.4.1.999.1.1.10"));
        fake->append(response("1.3.6.1.4.1.999.2.1"));
        return fake;
    };
    for (int iteration = 0; iteration < 5; ++iteration) {
        {
            SnmpTableAsyncRunner runner;
            ok &= check(runner.start(context, plan(), scriptedTable()), "blocked-owner table start");
            ok &= check(runner.wait(5000),
                        "table wait() returns although this thread never runs an event loop");
        }
        {
            SnmpInstanceAsyncRunner runner;
            ok &= check(runner.start(context, Oid("1.3.6.1.4.1.999.1.1"), scriptedInstance()),
                        "blocked-owner instance start");
            ok &= check(runner.wait(5000),
                        "instance wait() returns although this thread never runs an event loop");
        }
        { SnmpTableAsyncRunner runner; runner.start(context, plan(), scriptedTable()); }
        { SnmpInstanceAsyncRunner runner;
          runner.start(context, Oid("1.3.6.1.4.1.999.1.1"), scriptedInstance()); }
    }
    return ok;
}

bool asyncCoverage(QCoreApplication &application)
{
    Q_UNUSED(application)
    bool ok = blockedOwnerCoverage();
    for (int iteration = 0; iteration < 2; ++iteration)
    {
        SnmpTableAsyncRunner runner;
        auto fake = std::make_unique<ScriptedSnmpTransport>(config());
        fake->append(response("1.3.6.1.4.1.999.1.1.10"));
        fake->append(response("1.3.6.1.4.1.999.1.1.10"));
        fake->append(response("1.3.6.1.4.1.999.2.1.0"));
        QEventLoop loop;
        SnmpTableResult delivered;
        QObject::connect(&runner, &SnmpTableAsyncRunner::completed, &loop,
                         [&](const SnmpTableResult &value) {
                             delivered = value; loop.quit();
                         });
        ok &= check(runner.start(SnmpRequestContext(config(), SnmpRequestOperation::Walk),
                                 plan(), std::move(fake)), "async start");
        QTimer::singleShot(2000, &loop, &QEventLoop::quit);
        loop.exec();
        ok &= check(delivered.status == SnmpOperationStatus::Complete &&
                    delivered.rows.size() == 1 && runner.wait(2000),
                    "async completion and cleanup");
    }

    SnmpTableAsyncRunner cancelled;
    QEventLoop cancelLoop;
    SnmpTableResult cancelledResult;
    QObject::connect(&cancelled, &SnmpTableAsyncRunner::completed, &cancelLoop,
                     [&](const SnmpTableResult &value) {
                         cancelledResult = value; cancelLoop.quit();
                     });
    cancelled.start(SnmpRequestContext(config(), SnmpRequestOperation::Walk),
                    plan(), std::make_unique<SlowTransport>(config()));
    QTimer::singleShot(5, &cancelled, &SnmpTableAsyncRunner::cancel);
    QTimer::singleShot(2000, &cancelLoop, &QEventLoop::quit);
    cancelLoop.exec();
    ok &= check(cancelledResult.status == SnmpOperationStatus::Cancelled &&
                cancelled.wait(2000), "cooperative cancellation and cleanup");

    SnmpTableAsyncRunner failed;
    auto timeoutTransport = std::make_unique<ScriptedSnmpTransport>(config());
    timeoutTransport->append(status(SnmpOperationStatus::Timeout,
                                    SNMP_CLASS_TIMEOUT));
    QEventLoop failureLoop;
    SnmpTableResult failedResult;
    QObject::connect(&failed, &SnmpTableAsyncRunner::completed, &failureLoop,
                     [&](const SnmpTableResult &value) {
                         failedResult = value; failureLoop.quit();
                     });
    failed.start(SnmpRequestContext(config(), SnmpRequestOperation::Walk),
                 plan(), std::move(timeoutTransport));
    QTimer::singleShot(2000, &failureLoop, &QEventLoop::quit);
    failureLoop.exec();
    ok &= check(failedResult.status == SnmpOperationStatus::Timeout &&
                failed.wait(2000), "async error cleanup and timeout distinction");
    return ok;
}
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    return transportCoverage() && tableCoverage() && instanceCoverage() &&
                   asyncCoverage(application)
               ? 0 : 1;
}
