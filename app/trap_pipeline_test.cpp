#include "trapdecoder.h"
#include "traphistorystore.h"
#include "trappresenter.h"
#include "mibenvironmentextractor.h"
#include "mibenvironmentregistry.h"
#include "trapservice.h"
#include "smi.h"

#include <QCoreApplication>
#include <QDebug>
#include <QTextStream>

namespace {
int failures = 0;
void check(bool condition, const char *message)
{
    if (!condition) { QTextStream(stderr) << "FAIL: " << message << Qt::endl; ++failures; }
}
void publishEnvironment(){MibEffectivePlan p;p.sha256="trap-test";for(SmiModule*m=smiGetFirstModule();m;m=smiGetNextModule(m)){MibEffectivePlanMember x;x.identity=QString::fromLatin1(m->name);x.provider.canonicalPath=QString::fromLocal8Bit(m->path);p.members<<x;p.effectiveModules<<x.identity;}MibEnvironmentRegistry::publish(MibEnvironmentExtractor().extract(p));}

Pdu notification(unsigned short type, const char *oid, const char *value = "one")
{
    Pdu pdu;
    pdu.set_type(type);
    pdu.set_notify_id(Oid(oid));
    pdu.set_notify_enterprise(Oid("1.3.6.1.4.1.8072"));
    pdu.set_notify_timestamp(TimeTicks(1234));
    Vb vb(Oid("1.3.6.1.2.1.1.5.0"));
    vb.set_value(value);
    pdu += vb;
    return pdu;
}

class FakeReceiver : public ITrapReceiver
{
public:
    bool shouldStart = true;
    bool running = false;
    int stops = 0;
    bool start() override { running = shouldStart; return running; }
    void stop() override { running = false; ++stops; }
    bool isRunning() const override { return running; }
};
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    smiInit("snmpb-trap-pipeline-test");
    TrapEndpoint v2{QStringLiteral("192.0.2.10"), 9162,
                    TrapSnmpVersion::V2c, QStringLiteral("public"), {}};
    TrapDecoder decoder;
    const QDateTime received(QDate(2026, 1, 2), QTime(3, 4, 5), Qt::UTC);
    TrapRecord record = decoder.decode(notification(sNMP_PDU_TRAP,
        "1.3.6.1.6.3.1.1.5.1"), v2, received);
    check(record.isValid(), "v2 notification decodes");
    check(record.sourceAddress == "192.0.2.10" && record.sourcePort == 9162,
          "endpoint is captured");
    check(record.notificationOid == "1.3.6.1.6.3.1.1.5.1",
          "numeric notification OID retained");
    check(record.varbinds.size() == 1 && record.varbinds.first().printableValue == "one",
          "varbind order and value retained");
    TrapRecord copy = record;
    copy.varbinds[0].printableValue = "copy";
    check(record.varbinds[0].printableValue == "one", "records have value semantics");

    {
        TrapRecord r; r.sourceAddress = "192.0.2.10"; r.snmpVersion = TrapSnmpVersion::V2c;
        check(TrapPresenter::sourceToCopy(r) == "192.0.2.10", "v2c copies the packet source");
        r.agentAddress = "198.51.100.7";
        check(TrapPresenter::sourceToCopy(r) == "192.0.2.10", "agent-addr is ignored outside SNMPv1");
        r.snmpVersion = TrapSnmpVersion::V1;
        check(TrapPresenter::sourceToCopy(r) == "198.51.100.7", "v1 prefers an agent-addr that differs from the source");
        r.agentAddress = "192.0.2.10";
        check(TrapPresenter::sourceToCopy(r) == "192.0.2.10", "v1 with identical agent-addr copies the source");
        r.agentAddress = "0.0.0.0";
        check(TrapPresenter::sourceToCopy(r) == "192.0.2.10", "v1 with an unset agent-addr copies the source");
        r.agentAddress = "  ";
        check(TrapPresenter::sourceToCopy(r) == "192.0.2.10", "v1 with a blank agent-addr copies the source");
    }
    TrapEndpoint v1 = v2;
    v1.version = TrapSnmpVersion::V1;
    TrapRecord v1Record = decoder.decode(notification(sNMP_PDU_V1TRAP,
        "1.3.6.1.6.3.1.1.5.3"), v1, received);
    {
        Pdu relayed = notification(sNMP_PDU_V1TRAP, "1.3.6.1.6.3.1.1.5.3");
        relayed.set_v1_trap_address(IpAddress("198.51.100.7"));
        const TrapRecord relayedRecord = decoder.decode(relayed, v1, received);
        check(relayedRecord.agentAddress == "198.51.100.7" && relayedRecord.sourceAddress == "192.0.2.10" &&
              TrapPresenter::sourceToCopy(relayedRecord) == "198.51.100.7",
              "decoder captures the v1 agent-addr and Copy source IP prefers it");
        check(v1Record.agentAddress.isEmpty(), "a v1 trap without agent-addr leaves it empty");
    }
    check(v1Record.isValid() && v1Record.messageType == "Trap(v1)" &&
          v1Record.enterpriseOid == "1.3.6.1.4.1.8072", "v1 normalization");

    TrapEndpoint v3 = v2;
    v3.version = TrapSnmpVersion::V3;
    v3.community.clear(); v3.securityName = "operator";
    Pdu v3Pdu = notification(sNMP_PDU_INFORM, "1.3.6.1.6.3.1.1.5.4");
    v3Pdu.set_context_name("ctx"); v3Pdu.set_message_id(77);
    TrapRecord v3Record = decoder.decode(v3Pdu, v3, received);
    check(v3Record.securityName == "operator" && v3Record.contextName == "ctx" &&
          v3Record.messageId == 77, "v3 non-secret metadata retained");

    Pdu malformed;
    malformed.set_type(sNMP_PDU_TRAP);
    check(!decoder.decode(malformed, v2, received).isValid(), "missing notification OID rejected");
    Pdu unsupported;
    unsupported.set_type(sNMP_PDU_GET);
    unsupported.set_notify_id(Oid("1.3.6.1"));
    check(!decoder.decode(unsupported, v2, received).isValid(), "unsupported PDU rejected");

    TrapHistoryStore history(2);
    history.append(record); history.append(record); history.append(v1Record);
    check(history.count() == 2 && history.records().first().recordId == 2,
          "bounded history evicts oldest and preserves duplicates");
    history.setMaximumRecords(1);
    check(history.count() == 1 && history.records().first().messageType == "Trap(v1)",
          "limit change trims oldest");
    history.clear(); check(history.count() == 0, "history clears");

    TrapService service(2);
    int added = 0, reset = 0, failed = 0;
    QObject::connect(&service, &TrapService::recordAdded, [&added](quint64){ ++added; });
    QObject::connect(&service, &TrapService::historyReset, [&reset](){ ++reset; });
    QObject::connect(&service, &TrapService::receiverFailed, [&failed](){ ++failed; });
    check(service.receive(notification(sNMP_PDU_TRAP, "1.3.6.1.6.3.1.1.5.1"), v2, received),
          "service accepts valid trap");
    check(!service.receive(malformed, v2, received) && service.history().count() == 1,
          "malformed trap cannot corrupt history");
    service.receive(notification(sNMP_PDU_TRAP, "1.3.6.1.6.3.1.1.5.1"), v2, received);
    service.receive(notification(sNMP_PDU_TRAP, "1.3.6.1.6.3.1.1.5.1"), v2, received);
    check(service.history().count() == 2 && added == 3, "service signals and capacity");
    service.clear(); check(service.history().count() == 0 && reset == 1, "service clear signal");

    FakeReceiver receiver;
    check(service.start(&receiver) && service.isRunning(), "receiver starts");
    service.stop(); check(!service.isRunning() && receiver.stops == 1, "receiver stops");
    receiver.shouldStart = false;
    check(!service.start(&receiver) && failed == 1, "receiver failure reported");
    receiver.shouldStart = true;
    service.start(&receiver); service.start(&receiver);
    check(receiver.stops == 2 && service.isRunning(), "repeated lifecycle stops previous receiver");
    // The service does not own the receiver, and `receiver` is destroyed before
    // `service`. Stop it here so ~TrapService() never touches a dead receiver.
    service.stop();

    TrapPresentation view = TrapPresenter().present(record);
    check(view.summaryColumns.size() == 9 && view.varbindLines.size() == 1,
          "full pipeline produces presentation data");
    check(TrapPresenter::symbolicOid("9.9.9.9") == "9.9.9.9",
          "missing MIB uses numeric fallback");
    const QByteArray mibPath = QByteArray(SNMPB_SOURCE_DIR) + "/libsmi/mibs/ietf";
    smiSetPath(mibPath.constData());
    check(smiLoadModule("SNMPv2-MIB") != nullptr, "bundled symbolic MIB loads");
    check(smiLoadModule("IF-MIB") != nullptr, "bundled formatting MIB loads");
    publishEnvironment();
    check(TrapPresenter::symbolicOid("1.3.6.1.6.3.1.1.5.1") == "coldStart",
          "loaded MIB enriches numeric notification OID");

    auto formatted = [](const char *oid, int syntax, const QString &value) {
        TrapVarbind vb;
        vb.oid = QString::fromLatin1(oid);
        vb.syntax = syntax;
        vb.printableValue = value;
        return TrapPresenter::formattedValue(vb);
    };
    check(formatted("1.3.6.1.2.1.2.2.1.8.1", sNMP_SYNTAX_INT32, "1") == "up(1)",
          "enumerated INTEGER uses MIB label and value");
    const QString physical = formatted("1.3.6.1.2.1.2.2.1.6.1", sNMP_SYNTAX_OCTETS,
                    "  00 11 22 33 44 55                      ..\"3DU\n");
    check(physical == "00:11:22:33:44:55",
          "display hint formats a binary physical address");
    check(formatted("1.3.6.1.2.1.1.2.0", sNMP_SYNTAX_OID,
                    "1.3.6.1.6.3.1.1.5.1").contains("coldStart"),
          "OBJECT IDENTIFIER value is rendered symbolically");
    check(formatted("9.9.9.9", sNMP_SYNTAX_OID, "9.8.7") == "9.8.7",
          "OBJECT IDENTIFIER value falls back when MIB is missing");
    check(formatted("1.3.6.1.2.1.1.3.0", sNMP_SYNTAX_TIMETICKS,
                    "0:00:12.34") == "0:00:12.34", "TimeTicks retain SNMP++ formatting");
    check(formatted("1.3.6.1.2.1.2.2.1.10.1", sNMP_SYNTAX_CNTR32, "42") == "42",
          "Counter32 formatting retained");
    check(formatted("1.3.6.1.2.1.2.2.1.5.1", sNMP_SYNTAX_GAUGE32, "1000") == "1000",
          "Gauge32 formatting retained");
    const QString counter64 = formatted("1.3.6.1.2.1.31.1.1.1.6.1", sNMP_SYNTAX_CNTR64,
                    "4294967297");
    check(counter64 == "4294967297", "Counter64 formatting retained");
    const QString display = formatted("1.3.6.1.2.1.1.1.0", sNMP_SYNTAX_OCTETS, "router");
    check(display == "router", "printable DisplayString uses legacy rendering");
    check(formatted("9.9.9.9", sNMP_SYNTAX_OCTETS,
                    "  00 FF                                         ..\n").contains("00 FF"),
          "untyped binary OCTET STRING retains SNMP++ hex fallback");
    check(formatted("1.3.6.1.2.1.1.1.0", sNMP_SYNTAX_NOSUCHOBJECT,
                    "noSuchObject") == "noSuchObject" &&
          formatted("1.3.6.1.2.1.1.1.0", sNMP_SYNTAX_NOSUCHINSTANCE,
                    "noSuchInstance") == "noSuchInstance" &&
          formatted("1.3.6.1.2.1.1.1.0", sNMP_SYNTAX_ENDOFMIBVIEW,
                    "endOfMibView") == "endOfMibView", "exception syntax text retained");
    check(formatted("9.9.9.9", sNMP_SYNTAX_INT32, "-7") == "-7",
          "INTEGER without MIB enumeration falls back to SNMP++ text");
    check(formatted("9.9.9.9", sNMP_SYNTAX_IPADDR, "192.0.2.9") == "192.0.2.9",
          "IpAddress fallback retained");
    check(formatted("9.9.9.9", sNMP_SYNTAX_OPAQUE, "opaque-value") == "opaque-value",
          "Opaque fallback retained");
    check(formatted("9.9.9.9", sNMP_SYNTAX_NULL, "NULL") == "NULL",
          "NULL fallback retained");
    check(smiLoadModule("ACCOUNTING-CONTROL-MIB") != nullptr,
          "bundled BITS fixture MIB loads");
    publishEnvironment();
    const QString bits = formatted("acctngSelectionType", sNMP_SYNTAX_OCTETS,
                                   "  C0                                           .\n");
    check(bits.contains("svcIncoming") && bits.contains("svcOutgoing"),
          "BITS uses MIB bit labels");
    check(TrapPresenter::symbolicOid("1.3.6.1.2.1.2.2.1.8.27") ==
              "ifOperStatus.27", "symbolic varbind OID retains instance suffix");

    TrapRecord presentationRecord = record;
    presentationRecord.recordId = 12;
    presentationRecord.varbinds = {
        {"1.3.6.1.2.1.2.2.1.8.1", sNMP_SYNTAX_INT32, "1", true},
        {"1.3.6.1.2.1.1.3.0", sNMP_SYNTAX_TIMETICKS, "0:00:12.34", true}
    };
    const TrapPresentation parity = TrapPresenter().present(presentationRecord);
    check(parity.varbindLines == QStringList{
              "#0 ifOperStatus.1: up(1)", "#1 sysUpTime.0: 0:00:12.34"},
          "multiple varbinds preserve formatting and order");
    check(parity.summaryColumns.size() == 9 && parity.summaryColumns.at(0) == "0012" &&
          parity.summaryColumns.at(4) == "coldStart" &&
          parity.summaryColumns.at(5) == "Trap(v2)" &&
          parity.summaryColumns.at(6) == "SNMPv2c",
          "notification summary formatting retained");
    smiExit();
    return failures == 0 ? 0 : 1;
}
