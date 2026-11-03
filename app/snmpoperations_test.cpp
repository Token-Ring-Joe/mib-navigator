#include "fakesnmpagent.h"
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

// Walk correctness (sparse rows, SNMPv1, GETBULK, bad agents) is covered against a fake agent
// in snmptable_test.cpp; here only the request context capture matters.
bool tableCoverage()
{
    SnmpRequestConfig mutableSelection = config();
    const SnmpRequestContext context(mutableSelection, SnmpRequestOperation::Walk);
    const SnmpRequestConfig transportConfig = mutableSelection;
    mutableSelection.readCommunity = "changed-after-capture";
    mutableSelection.maxRepetitions = 1;
    FakeSnmpAgent agent(transportConfig);
    agent.addInteger("1.3.6.1.4.1.999.1.1.10", 1);
    agent.addInteger("1.3.6.1.4.1.999.1.2.10", 2);
    agent.addInteger("1.3.6.1.4.1.999.1.1.11", 3);
    SnmpCancellationToken cancellation;
    const SnmpTableResult result = SnmpTableOperation(context, plan(true)).execute(
        agent, cancellation);
    return check(result.status == SnmpOperationStatus::Complete && result.rows.size() == 2 &&
                     result.rows[0].instance == "10" && result.rows[1].instance == "11",
                 "table walk uses the captured request context") &&
           check(agent.config().readCommunity == "captured-read",
                 "transport keeps the captured configuration") &&
           check(agent.countOf(SnmpTransportOperation::GetBulk) > 0,
                 "captured GETBULK repetitions are used");
}

bool instanceReport()
{
    SnmpRequestConfig v3 = config();
    v3.version = SnmpRequestVersion::V3;
    v3.securityName = "operator";
    FakeSnmpAgent agent(v3);
    agent.setFailure([](int, SnmpTransportResult *response) {
        response->status = SnmpOperationStatus::Success;
        response->transportStatus = SNMP_CLASS_SUCCESS;
        Vb report{Oid("1.3.6.1.6.3.15.1.1.3.0")};   // usmStatsUnknownUserNames
        report.set_value(1UL);
        response->pdu += report;
        return true;
    });
    SnmpCancellationToken token;
    const SnmpInstanceResult result = SnmpInstanceOperation(
        SnmpRequestContext(v3, SnmpRequestOperation::Walk), Oid("1.3.6.1.4.1.999.1.1")).execute(agent, token);
    return check(result.status == SnmpOperationStatus::SnmpError && result.instances.isEmpty() &&
                     result.problem.contains("operator"),
                 "instance listing reports an SNMPv3 REPORT as a refusal");
}

bool classification()
{
    // A real device error: the code and the PDU agree.
    Pdu noSuchName; noSuchName.set_error_status(SNMP_ERROR_NO_SUCH_NAME); noSuchName.set_error_index(1);
    Pdu tooBig; tooBig.set_error_status(SNMP_ERROR_TOO_BIG);
    Pdu clean;
    bool ok = check(ClassifySnmpTransportStatus(SNMP_ERROR_NO_SUCH_NAME, noSuchName) == SnmpOperationStatus::SnmpError &&
                        ClassifySnmpTransportStatus(SNMP_ERROR_TOO_BIG, tooBig) == SnmpOperationStatus::SnmpError &&
                        ClassifySnmpTransportStatus(SNMP_ERROR_INCONSIS_NAME, [] { Pdu p; p.set_error_status(SNMP_ERROR_INCONSIS_NAME); return p; }()) ==
                            SnmpOperationStatus::SnmpError,
                    "device errors are still SNMP errors");
    ok &= check(ClassifySnmpTransportStatus(SNMP_CLASS_SUCCESS, clean) == SnmpOperationStatus::Success &&
                    ClassifySnmpTransportStatus(SNMP_CLASS_SUCCESS, noSuchName) == SnmpOperationStatus::SnmpError &&
                    ClassifySnmpTransportStatus(SNMP_CLASS_TIMEOUT, clean) == SnmpOperationStatus::Timeout,
                "success, error-status in the PDU, and timeout are unchanged");
    ok &= check(ClassifySnmpTransportStatus(SNMP_CLASS_TL_FAILED, clean) == SnmpOperationStatus::TransportFailure &&
                    ClassifySnmpTransportStatus(SNMPv3_MP_ERROR, clean) == SnmpOperationStatus::TransportFailure,
                "negative codes (transport, SNMPv3 MP) are failures");
    // SNMPv3 USM codes are positive but are not the device's error-status.
    for (const int code : {SNMPv3_USM_DECRYPTION_ERROR, SNMPv3_USM_UNKNOWN_SECURITY_NAME,
                           SNMPv3_USM_AUTHENTICATION_FAILURE, SNMPv3_USM_AUTHENTICATION_ERROR,
                           SNMPv3_USM_NOT_IN_TIME_WINDOW, SNMPv3_USM_FILEREAD_ERROR})
        ok &= check(ClassifySnmpTransportStatus(code, clean) == SnmpOperationStatus::TransportFailure,
                    "an SNMPv3 USM code is a failure on this side, not a device error");
    ok &= check(ClassifySnmpTransportStatus(SNMP_ERROR_NO_SUCH_NAME, clean) == SnmpOperationStatus::TransportFailure &&
                    ClassifySnmpTransportStatus(19, clean) == SnmpOperationStatus::TransportFailure,
                "a positive code the PDU does not back up is not reported as the device's error");
    return ok;
}

bool instanceLocalFailures()
{
    struct Case { int code; const char *cause; };
    const Case cases[] = {
        {SNMPv3_USM_DECRYPTION_ERROR, "privacy"},
        {SNMPv3_USM_UNKNOWN_SECURITY_NAME, "credentials"},
        {SNMPv3_USM_AUTHENTICATION_FAILURE, "authentication"},
    };
    bool ok = true;
    for (const Case &c : cases)
    {
        SnmpRequestConfig v3 = config();
        v3.version = SnmpRequestVersion::V3;
        v3.securityName = "operator";
        ScriptedSnmpTransport fake(v3);
        fake.append(status(SnmpOperationStatus::TransportFailure, c.code));
        SnmpCancellationToken token;
        const SnmpInstanceResult result = SnmpInstanceOperation(
            SnmpRequestContext(v3, SnmpRequestOperation::Walk), Oid("1.3.6.1.4.1.999.1.1")).execute(fake, token);
        ok &= check(result.status == SnmpOperationStatus::TransportFailure &&
                        result.transportStatus == c.code && result.problem.contains(c.cause) &&
                        !result.problem.contains("error 0"),
                    "a local SNMPv3 failure in the instance listing names its cause");
    }
    return ok;
}

bool tableInstanceBatching()
{
    bool ok = true;
    for (auto version : {SnmpRequestVersion::V1, SnmpRequestVersion::V2c, SnmpRequestVersion::V3}) {
        auto c = config(); c.version = version; c.maxRepetitions = 25;
        FakeSnmpAgent agent(c);
        QVector<Oid> roots;
        for (int column = 1; column <= 24; ++column) {
            const QString root = QString("1.3.6.1.4.1.999.1.1.%1").arg(column);
            roots.append(Oid(root.toLatin1().constData()));
            for (int row = 1; row <= 240; ++row)
                if (column != 1 || row % 2 == 0)
                    agent.addInteger((root + "." + QString::number(row)).toLatin1().constData(), row);
        }
        SnmpCancellationToken token;
        const auto result = SnmpInstanceOperation(SnmpRequestContext(c,SnmpRequestOperation::Walk),roots).execute(agent,token);
        std::cout << "DISCOVERY version=" << int(version) << " requests=" << agent.requestCount()
                  << " instances=" << result.instances.size() << '\n';
        ok &= check(result.status == SnmpOperationStatus::Complete && result.instances.size()==240 &&
                    result.instances.first()=="1" && result.instances.last()=="240",
                    "large discovery: all sparse instances in numeric order");
        ok &= check(agent.requestCount() <= (version == SnmpRequestVersion::V1 ? 800 : 100),
                    "large discovery: batched requests avoid one network round trip per cell");
        ok &= check(version == SnmpRequestVersion::V1
                        ? agent.countOf(SnmpTransportOperation::GetBulk)==0
                        : agent.countOf(SnmpTransportOperation::GetBulk)>0,
                    "large discovery: GETBULK for v2c/v3 only");
    }
    return ok;
}

bool tableInstanceUnion()
{
    bool ok = true;
    for (auto version : {SnmpRequestVersion::V1, SnmpRequestVersion::V2c, SnmpRequestVersion::V3}) {
        auto c = config(); c.version = version;
        FakeSnmpAgent agent(c);
        agent.addInteger("1.3.6.1.4.1.999.1.1.1.10",10);
        agent.addInteger("1.3.6.1.4.1.999.1.1.2.2",2);
        agent.addInteger("1.3.6.1.4.1.999.1.1.2.10",10);
        const QVector<Oid> roots{Oid("1.3.6.1.4.1.999.1.1.1"),Oid("1.3.6.1.4.1.999.1.1.2")};
        const SnmpRequestContext context(c, SnmpRequestOperation::Walk);
        SnmpCancellationToken token;
        const auto found = SnmpInstanceOperation(context,roots).execute(agent, token);
        ok &= check(found.status == SnmpOperationStatus::Complete && found.instances == QStringList({"2","10"}),
                    "table instances: sparse-column union, numeric order and deduplication across v1/v2c/v3");
        FakeSnmpAgent stuck(c); stuck.addInteger("1.3.6.1.4.1.999.1.1.1.10",10); stuck.setStuck(true);
        const auto invalid = SnmpInstanceOperation(context,roots).execute(stuck,token);
        // v1 retries the batch after noSuchName for its absent second column;
        // that adds one request before the repeated first-column OID is detected.
        ok &= check(invalid.status == SnmpOperationStatus::TransportFailure && stuck.requestCount()>0 && stuck.requestCount()<=(version == SnmpRequestVersion::V1 ? 3 : 2) && !invalid.problem.isEmpty(),
                    "table instances: out-of-order device cannot loop indefinitely");
    }
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
        auto fake = std::make_unique<FakeSnmpAgent>(config());
        fake->addInteger("1.3.6.1.4.1.999.1.1.10", 7);
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
            ok &= check(!runner.start(context, plan(), scriptedTable()),
                        "table cannot replace a thread before queued cleanup");
        }
        {
            SnmpInstanceAsyncRunner runner;
            ok &= check(runner.start(context, Oid("1.3.6.1.4.1.999.1.1"), scriptedInstance()),
                        "blocked-owner instance start");
            ok &= check(runner.wait(5000),
                        "instance wait() returns although this thread never runs an event loop");
            ok &= check(!runner.start(context, Oid("1.3.6.1.4.1.999.1.1"), scriptedInstance()),
                        "instance discovery cannot replace a thread before queued cleanup");
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
        auto fake = std::make_unique<FakeSnmpAgent>(config());
        fake->addInteger("1.3.6.1.4.1.999.1.1.10", 7);
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
    return tableInstanceBatching() && tableInstanceUnion() && transportCoverage() && tableCoverage() && instanceCoverage() && instanceReport() && instanceLocalFailures() && classification() &&
                   asyncCoverage(application)
               ? 0 : 1;
}
