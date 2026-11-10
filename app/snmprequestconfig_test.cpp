#include "snmprequestconfig.h"
#include "snmprequestconfigadapter.h"

#include <QCoreApplication>
#include <QTextStream>

#include "snmp_pp/address.h"

namespace {
int failures = 0;

void check(bool condition, const char *description)
{
    if (!condition)
    {
        QTextStream(stderr) << "FAIL: " << description << Qt::endl;
        ++failures;
    }
}

SnmpRequestSettings profile()
{
    SnmpRequestSettings record{};
    record.name = "lab-router";
    record.v1 = true;
    record.v2 = true;
    record.v3 = true;
    record.address = "192.0.2.10";
    record.port = "10161";
    record.retries = 4;
    record.timeout = 17;
    record.readcomm = "read-only";
    record.writecomm = "write-private";
    record.maxrepetitions = 42;
    record.nonrepeaters = 3;
    record.secname = "test-user";
    record.seclevel = 1;
    record.contextname = "test-context";
    record.contextengineid = "engine-id";
    return record;
}

QString octets(const OctetStr &value)
{
    return QString::fromLatin1(
        reinterpret_cast<const char *>(value.data()), value.len());
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const SnmpRequestSettings record = profile();

    SnmpRequestConfig v1;
    check(SnmpRequestConfig::FromProfile(record, 0, &v1),
          "SNMPv1 configuration is accepted");
    check(v1.version == SnmpRequestVersion::V1, "SNMPv1 version selected");
    check(v1.address == "192.0.2.10" && v1.port == "10161" &&
          v1.endpoint() == "192.0.2.10/10161", "IPv4 endpoint and port copied");
    check(v1.retries == 4 && v1.timeout == 17,
          "retries and timeout copied");
    check(v1.readCommunity == "read-only" &&
          v1.writeCommunity == "write-private", "communities copied");
    check(v1.maxRepetitions == 42 && v1.nonRepeaters == 3,
          "GET-BULK settings copied");

    UdpAddress v1Address(v1.endpoint().toLatin1().data());
    check(v1Address.valid() && v1Address.get_port() == 10161,
          "fixed IPv4 endpoint and non-default port construct without lookup");
    CTarget v1Target(v1Address);
    ApplySnmpRequestConfig(v1, v1Target);
    check(v1Target.get_version() == version1 && v1Target.get_retry() == 4 &&
          v1Target.get_timeout() == 1700, "SNMPv1 target configured");
    check(QString::fromLatin1(v1Target.get_readcommunity()) == "read-only" &&
          QString::fromLatin1(v1Target.get_writecommunity()) == "write-private",
          "community target configured");

    SnmpRequestConfig v2;
    check(SnmpRequestConfig::FromProfile(record, 1, &v2),
          "SNMPv2c configuration is accepted");
    CTarget v2Target(v1Address);
    ApplySnmpRequestConfig(v2, v2Target);
    check(v2.version == SnmpRequestVersion::V2c &&
          v2Target.get_version() == version2c, "SNMPv2c target configured");

    SnmpRequestConfig v3;
    check(SnmpRequestConfig::FromProfile(record, 2, &v3),
          "SNMPv3 configuration is accepted");
    check(v3.securityName == "test-user" && v3.securityLevel == 1 &&
          v3.contextName == "test-context" &&
          v3.contextEngineId == "engine-id", "SNMPv3 fields copied");
    UTarget v3Target(v1Address);
    ApplySnmpRequestConfig(v3, v3Target);
    check(v3Target.get_version() == version3 &&
          v3Target.get_security_model() == SNMP_SECURITY_MODEL_USM &&
          octets(v3Target.get_security_name()) == "test-user",
          "SNMPv3 target configured");
    Pdu v3Pdu;
    ApplySnmpV3PduConfig(v3, v3Pdu);
    check(v3Pdu.get_security_level() == SNMP_SECURITY_LEVEL_AUTH_NOPRIV,
          "SNMPv3 security level mapped");
    check(octets(v3Pdu.get_context_name()) == "test-context" &&
          octets(v3Pdu.get_context_engine_id()) == "engine-id",
          "SNMPv3 context configured");
    v3.contextName = QString::fromUtf8("Lab réseau");
    ApplySnmpV3PduConfig(v3, v3Pdu);
    const auto context = v3Pdu.get_context_name();
    check(QByteArray(reinterpret_cast<const char *>(context.data()), int(context.len())) == v3.contextName.toUtf8(),
          "SNMPv3 context reaches the actual PDU as UTF-8 without loss");
    v3.securityLevel = 0;
    ApplySnmpV3PduConfig(v3, v3Pdu);
    check(v3Pdu.get_security_level() == SNMP_SECURITY_LEVEL_NOAUTH_NOPRIV,
          "noAuthNoPriv security level mapped");
    v3.securityLevel = 2;
    ApplySnmpV3PduConfig(v3, v3Pdu);
    check(v3Pdu.get_security_level() == SNMP_SECURITY_LEVEL_AUTH_PRIV,
          "authPriv security level mapped");

    SnmpRequestSettings ipv6Record = record;
    ipv6Record.address = "2001:db8::10";
    SnmpRequestConfig ipv6;
    check(SnmpRequestConfig::FromProfile(ipv6Record, 1, &ipv6) &&
          ipv6.endpoint() == "2001:db8::10/10161", "IPv6 endpoint preserved");
    UdpAddress ipv6Address(ipv6.endpoint().toLatin1().data());
    check(ipv6Address.valid() && ipv6Address.get_port() == 10161,
          "fixed IPv6 endpoint constructs without lookup");

    SnmpRequestConfig invalid;
    check(!SnmpRequestConfig::FromProfile(record, -1, &invalid) &&
          !SnmpRequestConfig::FromProfile(record, 3, &invalid),
          "invalid protocol selections rejected");
    SnmpRequestSettings unsupported = record;
    unsupported.v2 = false;
    check(SnmpRequestConfig::FromProfile(unsupported, 1, &invalid) &&
          invalid.version == SnmpRequestVersion::V2c,
          "resolved selection preserves existing Setup semantics");
    check(!SnmpRequestConfig::FromProfile(record, 0, nullptr),
          "null output is rejected");

    if (failures == 0)
        QTextStream(stdout) << "All SNMP request configuration tests passed."
                            << Qt::endl;
    return failures == 0 ? 0 : 1;
}
