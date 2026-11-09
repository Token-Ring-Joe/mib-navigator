#include "requestlog.h"

#include <QCoreApplication>
#include <iostream>

namespace {
bool check(bool value, const char *message)
{ if (!value) std::cerr << "FAIL: " << message << '\n'; return value; }
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    bool ok = true;

    // ---- classification -------------------------------------------------------------------------
    ok &= check(ClassifyRequestOutcome("-----SNMP query finished-----<br>Total # of Requests = 1") == RequestOutcome::Succeeded, "finished is OK");
    ok &= check(ClassifyRequestOutcome("<font color=red>Timeout: no response from the device.</font>") == RequestOutcome::TimedOut, "timeout");
    ok &= check(ClassifyRequestOutcome("<font color=red>The device rejected the authentication (usmStatsWrongDigests). x</font>") == RequestOutcome::Refused,
                "an SNMPv3 report is 'refused by device'");
    ok &= check(ClassifyRequestOutcome("<font color=red>Could not send GETBULK request: No such host</font>") == RequestOutcome::Failed, "send failure");
    ok &= check(ClassifyRequestOutcome("<font color=red>-----SNMP query stopped-----</font><br>") == RequestOutcome::Stopped, "stopped by the user");
    ok &= check(ClassifyRequestOutcome("<font color=red>No Such Object</font>") == RequestOutcome::Failed, "an error in the data is a failure");

    // ---- lifecycle ----------------------------------------------------------------------------------
    RequestLog log(nullptr, 3);
    int added = 0, changed = 0, cleared = 0;
    QObject::connect(&log, &RequestLog::entryAdded, [&](quint64) { ++added; });
    QObject::connect(&log, &RequestLog::entryChanged, [&](quint64) { ++changed; });
    QObject::connect(&log, &RequestLog::cleared, [&] { ++cleared; });

    const quint64 first = log.begin("Get", "10.1.1.20:161", "SNMPv2c", "1.3.6.1.2.1.1.1.0");
    ok &= check(added == 1 && log.entry(first) && log.entry(first)->outcome == RequestOutcome::Running &&
                log.entry(first)->operation == "Get" && log.entry(first)->target == "10.1.1.20:161", "a request is logged as running when it starts");
    log.finish(first, "1: sysDescr <font color=blue>Linux</font><br>-----SNMP query finished-----<br>Total # of Objects = 1", 1);
    const RequestLogEntry *done = log.entry(first);
    ok &= check(changed == 1 && done->outcome == RequestOutcome::Succeeded && done->objects == 1 && done->durationMs >= 0 &&
                !done->detail.contains("<") && !done->detail.contains("-----"), "finishing records outcome, object count and clean text");
    log.finish(first, "<font color=red>Timeout</font>", 0);
    ok &= check(log.entry(first)->outcome == RequestOutcome::Succeeded && changed == 1, "a finished request is not changed again");

    const quint64 second = log.begin("Walk", "[2001:db8::1]:161", "SNMPv3 operator (authPriv)", "1.3.6.1.2.1.2");
    log.finish(second, "<font color=red>Timeout: no response from the device.</font>", 0);
    ok &= check(log.entry(second)->outcome == RequestOutcome::TimedOut, "a timeout is recorded");

    // limit: the oldest entries go first
    log.begin("Get", "a", "x", "1"); log.begin("Get", "b", "x", "1");
    ok &= check(log.entries().size() == 3 && !log.entry(first) && log.entry(second), "the log keeps only the newest entries");
    log.finish(9999, "<font color=red>x</font>", 0);   // an unknown id must not crash or change anything
    ok &= check(changed == 2, "finishing an unknown request does nothing");

    // ---- export is plain text and carries no markup -----------------------------------------------------
    const QString text = RequestLogAsText(log.entries());
    ok &= check(text.startsWith("Time\t") && text.contains("Walk") && text.contains("Timeout") && text.count('\n') == 4,
                "the log exports as tab-separated text, one line per request");
    log.clear();
    ok &= check(log.entries().isEmpty() && cleared == 1, "clearing empties the log");
    return ok ? 0 : 1;
}
