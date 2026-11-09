#include "profiledetector.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QMutex>
#include <QThread>
#include <iostream>

namespace {
bool check(bool value, const char *message)
{ if (!value) std::cerr << "FAIL: " << message << '\n'; return value; }

SnmpTransportResult answer(const char *objectId, const char *descr)
{
    SnmpTransportResult r; r.status = SnmpOperationStatus::Success;
    Vb a{Oid("1.3.6.1.2.1.1.2.0")}; a.set_value(Oid(objectId)); r.pdu += a;
    Vb b{Oid("1.3.6.1.2.1.1.1.0")}; b.set_value(descr); r.pdu += b;
    return r;
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    bool ok = true;
    SnmpRequestConfig config; config.securityName = "operator";

    // ---- reading the identity from an answer ----------------------------------------------------
    {
        ScriptedSnmpTransport transport(config);
        transport.append(answer("1.3.6.1.4.1.100.1.5", "Alpha Switch 9000 v7.1"));
        const DeviceIdentity id = ProfileDetector::read(transport, config);
        ok &= check(id.ok && id.sysObjectId == "1.3.6.1.4.1.100.1.5" && id.sysDescr == "Alpha Switch 9000 v7.1", "a good answer is read");
        ok &= check(transport.requests().size() == 1 && transport.requests().first().pdu.get_vb_count() == 2 &&
                    transport.requests().first().operation == SnmpTransportOperation::Get,
                    "exactly one GET for two objects is sent");
    }
    {   ScriptedSnmpTransport t(config); SnmpTransportResult r; r.status = SnmpOperationStatus::Timeout; t.append(r);
        const DeviceIdentity id = ProfileDetector::read(t, config);
        ok &= check(!id.ok && id.error.contains("Timeout"), "a timeout is explained"); }
    {   ScriptedSnmpTransport t(config); SnmpTransportResult r; r.status = SnmpOperationStatus::Success;
        r.pdu.set_type(sNMP_PDU_REPORT);
        Vb c{Oid("1.3.6.1.6.3.15.1.1.5.0")}; c.set_value(Counter32(9)); r.pdu += c; t.append(r);
        const DeviceIdentity id = ProfileDetector::read(t, config);
        ok &= check(!id.ok && id.error.contains("rejected the authentication") && id.error.contains("'operator'"),
                    "a refused SNMPv3 login is explained, not treated as an identity"); }
    {   ScriptedSnmpTransport t(config); SnmpTransportResult r; r.status = SnmpOperationStatus::Success;
        Vb a{Oid("1.3.6.1.2.1.1.2.0")}; a.set_exception_status(sNMP_SYNTAX_NOSUCHOBJECT); r.pdu += a; t.append(r);
        const DeviceIdentity id = ProfileDetector::read(t, config);
        ok &= check(!id.ok && id.error.contains("sysObjectID"), "a device with no sysObjectID is reported"); }
    {   ScriptedSnmpTransport t(config); t.append(SnmpTransportResult{});
        ok &= check(!ProfileDetector::read(t, config).ok, "a failed send is not an identity"); }

    // ---- the background read ------------------------------------------------------------------------------
    QMutex lock; int sessions = 0; QString lastTarget;
    ProfileDetector detector(
        [&](const QString &target, const QString &key, SnmpRequestConfig *c, QString *error) {
            if (key.isEmpty()) { *error = "Choose a Credential."; return false; }
            c->address = target; c->securityName = "operator"; return true; },
        [&](const SnmpRequestConfig &c) {
            QMutexLocker l(&lock); ++sessions; lastTarget = c.address;
            auto t = std::make_unique<ScriptedSnmpTransport>(c); t->append(answer("1.3.6.1.4.1.200.1", "Beta 1")); return t; });
    QString error;
    ok &= check(!detector.detect("10.0.0.1", "", &error) && error.contains("Credential") && !detector.isRunning() && sessions == 0,
                "an unusable credential is refused before anything is sent");
    DeviceIdentity received; QString receivedTarget; int finishedCount = 0;
    QObject::connect(&detector, &ProfileDetector::finished, [&](const QString &t, const DeviceIdentity &id) { receivedTarget = t; received = id; ++finishedCount; });
    ok &= check(detector.detect("10.0.0.7", "community:x:v2c", &error) && detector.isRunning(), "a read starts");
    ok &= check(!detector.detect("10.0.0.8", "community:x:v2c", &error) && error.contains("already"), "a second read is refused while one runs");
    QElapsedTimer waited; waited.start();
    while (finishedCount == 0 && waited.elapsed() < 10000) { QCoreApplication::processEvents(QEventLoop::AllEvents, 20); QThread::msleep(5); }
    ok &= check(finishedCount == 1 && receivedTarget == "10.0.0.7" && received.ok && received.sysObjectId == "1.3.6.1.4.1.200.1" &&
                !detector.isRunning() && sessions == 1 && lastTarget == "10.0.0.7",
                "the answer arrives once, for the target that was asked, and the device is contacted once");
    return ok ? 0 : 1;
}
