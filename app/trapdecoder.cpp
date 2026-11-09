#include "trapdecoder.h"

namespace {
QString printable(const SnmpSyntax &value)
{
    const char *text = value.get_printable();
    return text ? QString::fromLocal8Bit(text) : QString();
}
}

TrapRecord TrapDecoder::decode(const Pdu &pdu, const TrapEndpoint &endpoint,
                               const QDateTime &received) const
{
    TrapRecord record;
    record.receivedTimestamp = received;
    record.sourceAddress = endpoint.address;
    record.sourcePort = endpoint.port;
    record.snmpVersion = endpoint.version;
    record.community = endpoint.community;
    record.securityName = endpoint.securityName;

    switch (pdu.get_type()) {
    case sNMP_PDU_V1TRAP: record.messageType = QStringLiteral("Trap(v1)"); break;
    case sNMP_PDU_TRAP: record.messageType = QStringLiteral("Trap(v2)"); break;
    case sNMP_PDU_INFORM: record.messageType = QStringLiteral("Inform"); break;
    case sNMP_PDU_REPORT: record.messageType = QStringLiteral("Report"); break;
    default:
        record.messageType = QStringLiteral("Unknown");
        record.decodeError = QStringLiteral("Unsupported notification PDU type");
        return record;
    }

    if (pdu.get_type() == sNMP_PDU_V1TRAP) {
        GenAddress agent;
        if (pdu.get_v1_trap_address(agent))
            record.agentAddress = printable(agent);
    }

    Oid notification;
    if (!pdu.get_notify_id(notification) || notification.len() == 0) {
        record.decodeError = QStringLiteral("Notification OID is missing");
        return record;
    }
    record.notificationOid = printable(notification);

    Oid enterprise;
    if (pdu.get_notify_enterprise(enterprise) && enterprise.len() > 0)
        record.enterpriseOid = printable(enterprise);

    TimeTicks ticks;
    pdu.get_notify_timestamp(ticks);
    record.notificationTicks = ticks;

    record.contextName = printable(pdu.get_context_name());
    record.contextEngineId = printable(pdu.get_context_engine_id());
    record.messageId = pdu.get_message_id();
    switch (pdu.get_security_level()) {
    case SNMP_SECURITY_LEVEL_NOAUTH_NOPRIV: record.securityLevel = QStringLiteral("NoAuthNoPriv"); break;
    case SNMP_SECURITY_LEVEL_AUTH_NOPRIV: record.securityLevel = QStringLiteral("AuthNoPriv"); break;
    case SNMP_SECURITY_LEVEL_AUTH_PRIV: record.securityLevel = QStringLiteral("AuthPriv"); break;
    default: record.securityLevel = QStringLiteral("Unknown"); break;
    }

    for (int i = 0; i < pdu.get_vb_count(); ++i) {
        Vb vb;
        TrapVarbind item;
        if (!pdu.get_vb(vb, i)) {
            item.valid = false;
            record.varbinds.append(item);
            continue;
        }
        Oid oid;
        vb.get_oid(oid);
        item.oid = printable(oid);
        item.syntax = vb.get_syntax();
        const char *value = vb.get_printable_value();
        item.printableValue = value ? QString::fromLocal8Bit(value) : QString();
        item.valid = oid.len() > 0;
        record.varbinds.append(item);
    }
    return record;
}
