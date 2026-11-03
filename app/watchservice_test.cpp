#include "watchservice.h"

#include <QCoreApplication>
#include <QMutex>
#include <QMutexLocker>
#include <iostream>
#include <memory>

namespace {
bool check(bool value, const char *message)
{ if (!value) std::cerr << "FAIL: " << message << '\n'; return value; }

SnmpTransportResult counterAnswer(unsigned long value, unsigned long uptime, bool withUptime = true)
{
    SnmpTransportResult r;
    r.status = SnmpOperationStatus::Success;
    Vb first(Oid("1.3.6.1.2.1.2.2.1.10.1")); first.set_value(Counter32(value)); r.pdu += first;
    if (withUptime) { Vb up(Oid("1.3.6.1.2.1.1.3.0")); up.set_value(TimeTicks(uptime)); r.pdu += up; }
    return r;
}
SnmpTransportResult failure(SnmpOperationStatus status)
{ SnmpTransportResult r; r.status = status; return r; }

// Answers handed to the next polls, in order, whatever thread asks.
struct Script
{
    QMutex lock;
    QList<SnmpTransportResult> answers;
    QList<QString> targetsSeen;
    void push(const SnmpTransportResult &r) { QMutexLocker l(&lock); answers.append(r); }
};

class ScriptedForPoll final : public ISnmpTransport
{
public:
    ScriptedForPoll(Script *script, const SnmpRequestConfig &config) : script(script), requestConfig(config) {}
    SnmpTransportResult execute(const SnmpTransportRequest &) override
    {
        QMutexLocker l(&script->lock);
        script->targetsSeen.append(requestConfig.address);
        return script->answers.isEmpty() ? SnmpTransportResult{} : script->answers.takeFirst();
    }
    const SnmpRequestConfig &config() const override { return requestConfig; }
private:
    Script *script; SnmpRequestConfig requestConfig;
};
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    bool ok = true;
    Script script;
    QString barTarget = "192.0.2.1";   // stands in for whatever the connection bar currently says
    bool credentialExists = true;

    WatchService service(
        [&](const QString &target, const QString &key, SnmpRequestConfig *config, QString *error) {
            if (!credentialExists) { *error = "The saved credential no longer exists"; return false; }
            config->version = SnmpRequestVersion::V2c;
            config->address = target; config->port = "161"; config->readCommunity = key;
            return true;
        },
        [&](const SnmpRequestConfig &config) { return std::make_unique<ScriptedForPoll>(&script, config); },
        [] { return MibEnvironmentPtr{}; });

    WatchSpec spec; spec.target = "198.51.100.7"; spec.credentialKey = "community:lab:v2c";
    spec.oid = "1.3.6.1.2.1.2.2.1.10.1"; spec.intervalSeconds = 5;
    int addedCount = 0, removedCount = 0;
    QObject::connect(&service, &WatchService::rowAdded, [&](quint64) { ++addedCount; });
    QObject::connect(&service, &WatchService::rowRemoved, [&](quint64) { ++removedCount; });
    const quint64 id = service.add(spec);
    ok &= check(id != 0 && addedCount == 1 && service.row(id) && service.ids() == QList<quint64>{id},
                "a watch is added and announced");

    // ---- baseline, then change and rate ------------------------------------------------------
    script.push(counterAnswer(1000, 100000));
    service.pollNow(id);
    ok &= check(service.waitForPolls(5000), "first poll completes");
    ok &= check(service.row(id)->status == "OK" && service.row(id)->value == "1000" &&
                service.row(id)->received == "Counter32: 1000" && service.row(id)->change.change.isEmpty(),
                "first answer sets the baseline and shows the value");
    script.push(counterAnswer(6000, 100500));
    service.pollNow(id);
    ok &= check(service.waitForPolls(5000), "second poll completes");
    ok &= check(service.row(id)->change.change == "+5000" && service.row(id)->change.rate == "1.00 k/s",
                "change and rate use the device's own clock (5000 in 5.00 s)");

    // ---- the connection bar changes: the row keeps its own target --------------------------------
    barTarget = "203.0.113.99";
    script.push(counterAnswer(7000, 101000));
    service.pollNow(id);
    ok &= check(service.waitForPolls(5000), "poll after the bar changed completes");
    ok &= check(!script.targetsSeen.isEmpty() && script.targetsSeen.last() == "198.51.100.7" &&
                !script.targetsSeen.contains("203.0.113.99"),
                "changing the connection bar never retargets an existing watch");

    // ---- restart, then a failure that keeps the baseline ------------------------------------------
    script.push(counterAnswer(50, 300));
    service.pollNow(id);
    ok &= check(service.waitForPolls(5000) && service.row(id)->change.baselineReset &&
                service.row(id)->change.rate.isEmpty() &&
                service.row(id)->change.note.contains("restarted"),
                "a device restart shows no rate and resets the baseline");
    script.push(failure(SnmpOperationStatus::Timeout));
    service.pollNow(id);
    ok &= check(service.waitForPolls(5000) && service.row(id)->status.contains("Timeout") &&
                service.row(id)->failures == 1 && service.row(id)->change.change.isEmpty(),
                "a timeout is reported and counted");
    script.push(counterAnswer(150, 400));
    service.pollNow(id);
    ok &= check(service.waitForPolls(5000) && service.row(id)->status == "OK" &&
                service.row(id)->change.change == "+100",
                "recovery compares with the last good answer, not the failure");

    // ---- an answer without sysUpTime still works ----------------------------------------------------
    script.push(counterAnswer(250, 0, false));
    service.pollNow(id);
    ok &= check(service.waitForPolls(5000) && service.row(id)->change.change == "+100",
                "sysUpTime is optional");

    // ---- a counter cleared on a device that did not restart --------------------------------------------
    script.push(counterAnswer(1000000, 1000));
    service.pollNow(id);
    ok &= check(service.waitForPolls(5000), "baseline before a counter clear");
    script.push(counterAnswer(500, 1500));          // clear counters: sysUpTime keeps rising
    service.pollNow(id);
    ok &= check(service.waitForPolls(5000) && service.row(id)->change.baselineReset &&
                service.row(id)->change.rate.isEmpty() && service.row(id)->change.change == "-" &&
                service.row(id)->change.note.contains("Counter reset"),
                "the row after a counter clear has no rate and resets the baseline");
    script.push(counterAnswer(1500, 2000));
    service.pollNow(id);
    ok &= check(service.waitForPolls(5000) && service.row(id)->change.change == "+1000" &&
                service.row(id)->change.rate == "200 /s" && service.row(id)->change.note.isEmpty(),
                "the poll after a counter clear has a normal change and rate again");

    // ---- the credential disappears -------------------------------------------------------------------
    credentialExists = false;
    service.pollNow(id);
    ok &= check(service.row(id)->status.contains("no longer exists") && service.pollsInFlight() == 0,
                "a deleted credential is reported and nothing is sent");
    credentialExists = true;

    // ---- an SNMPv3 REPORT is an error, not a value -----------------------------------------------------
    SnmpTransportResult report; report.status = SnmpOperationStatus::Success;
    report.pdu.set_type(sNMP_PDU_REPORT);
    Vb counter(Oid("1.3.6.1.6.3.15.1.1.5.0")); counter.set_value(Counter32(22)); report.pdu += counter;
    script.push(report);
    service.pollNow(id);
    ok &= check(service.waitForPolls(5000) && service.row(id)->status.contains("rejected the authentication"),
                "a refused SNMPv3 request is explained");

    // ---- SNMPv3 failures on this side of the wire --------------------------------------------------------
    struct LocalFailure { int code; const char *cause; };
    const LocalFailure localFailures[] = {
        {SNMPv3_USM_DECRYPTION_ERROR, "privacy"},
        {SNMPv3_USM_UNKNOWN_SECURITY_NAME, "credentials"},
        {SNMPv3_USM_AUTHENTICATION_FAILURE, "authentication"},
    };
    for (const LocalFailure &f : localFailures) {
        SnmpTransportResult local = failure(SnmpOperationStatus::TransportFailure);
        local.transportStatus = f.code;
        script.push(local);
        service.pollNow(id);
        ok &= check(service.waitForPolls(5000) && service.row(id)->status.contains(f.cause) &&
                    !service.row(id)->status.contains("error 0"),
                    "a local SNMPv3 failure names its cause in the Watch status");
    }

    // ---- pause, resume ---------------------------------------------------------------------------------------
    service.setPaused(id, true);
    ok &= check(service.row(id)->paused && service.row(id)->status == "Paused", "pause");
    service.setPaused(id, false);
    ok &= check(!service.row(id)->paused, "resume");

    // ---- removing a watch while its poll is running ---------------------------------------------------------------
    script.push(counterAnswer(1, 1));
    service.pollNow(id);
    service.remove(id);
    ok &= check(removedCount == 1 && !service.row(id), "a watch can be removed");
    ok &= check(service.waitForPolls(5000), "a poll for a removed watch is absorbed without crashing");
    service.add(spec); service.add(spec);
    service.clear();
    ok &= check(service.ids().isEmpty(), "clear removes every watch");
    return ok ? 0 : 1;
}
