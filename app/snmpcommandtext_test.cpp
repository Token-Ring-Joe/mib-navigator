#include "snmpcommandtext.h"

#include "snmp_pp/usm_v3.h"

#include <iostream>

namespace {
int failures = 0;
void check(bool value, const char *message)
{ if (!value) { ++failures; std::cerr << "FAIL: " << message << '\n'; } }

SnmpRequestConfig v3(int level, long auth, long priv)
{
    SnmpRequestConfig c;
    c.version = SnmpRequestVersion::V3; c.address = "192.0.2.10"; c.port = "161";
    c.securityName = "lab-user"; c.securityLevel = level; c.timeout = 3; c.retries = 1;
    UsmCredentialRecord r; r.authProtocol = int(auth); r.privacyProtocol = int(priv);
    r.authSecret = CredentialSecret("SECRET-AUTH-PASSPHRASE"); r.privacySecret = CredentialSecret("SECRET-PRIV-PASSPHRASE");
    c.usmCredential = r;
    return c;
}
}

int main()
{
    const QString oid = QStringLiteral("1.3.6.1.2.1.1.1.0");

    auto priv = BuildSnmpCommand(v3(2, SNMP_AUTHPROTOCOL_HMACSHA, SNMP_PRIVPROTOCOL_AES128), SnmpCommandKind::Get, oid);
    check(priv.command == "snmpget -v3 -l authPriv -u lab-user -a SHA -A '<authentication passphrase>' -x AES -X '<privacy passphrase>' -t 3 -r 1 192.0.2.10 .1.3.6.1.2.1.1.1.0",
          "authPriv Get names user, level and protocols and uses passphrase placeholders");
    check(!priv.command.contains("SECRET"), "an SNMPv3 command never carries the real passphrases");
    check(priv.note.contains("placeholder"), "the note says to replace the passphrase placeholders");

    auto authOnly = BuildSnmpCommand(v3(1, SNMP_AUTHPROTOCOL_HMAC256SHA384, SNMP_PRIVPROTOCOL_NONE), SnmpCommandKind::Walk, "1.3.6.1.2.1.2");
    check(authOnly.command.startsWith("snmpwalk -v3 -l authNoPriv -u lab-user -a SHA-384 -A '<authentication passphrase>' ") &&
          !authOnly.command.contains(" -x ") && !authOnly.command.contains(" -X "),
          "authNoPriv has -a and -A but no -x or -X");
    auto open = BuildSnmpCommand(v3(0, SNMP_AUTHPROTOCOL_NONE, SNMP_PRIVPROTOCOL_NONE), SnmpCommandKind::GetNext, oid);
    check(open.command.contains("-l noAuthNoPriv") && !open.command.contains(" -a ") && !open.command.contains(" -A ") && open.note.contains("no passphrase"),
          "noAuthNoPriv has neither");

    auto context = v3(2, SNMP_AUTHPROTOCOL_HMACSHA, SNMP_PRIVPROTOCOL_AES256);
    context.contextName = "vrf blue"; context.port = "1161";
    auto withContext = BuildSnmpCommand(context, SnmpCommandKind::Get, oid);
    check(withContext.command.contains("-x AES-256") && withContext.command.contains("-n 'vrf blue'") &&
          withContext.command.contains("192.0.2.10:1161"), "context, odd port and AES-256 are written and quoted");

    SnmpRequestConfig v2; v2.version = SnmpRequestVersion::V2c; v2.address = "2001:db8::1"; v2.port = "161";
    v2.readCommunity = "VERY-SECRET-COMMUNITY"; v2.maxRepetitions = 20; v2.nonRepeaters = 1;
    auto bulk = BuildSnmpCommand(v2, SnmpCommandKind::GetBulk, oid);
    check(bulk.command == "snmpbulkget -v2c -c '<community>' -Cn1 -Cr20 'udp6:[2001:db8::1]:161' .1.3.6.1.2.1.1.1.0",
          "v2c bulk uses a community placeholder, IPv6 transport syntax and repetition counts");
    check(!bulk.command.contains("VERY-SECRET") && bulk.note.contains("<community>"), "the community string is never copied");

    SnmpRequestConfig v1; v1.version = SnmpRequestVersion::V1; v1.address = "host.example"; v1.port = "161";
    check(BuildSnmpCommand(v1, SnmpCommandKind::GetBulk, oid).command.isEmpty(), "SNMPv1 has no bulk command");
    check(BuildSnmpCommand(v1, SnmpCommandKind::Get, oid).command == "snmpget -v1 -c '<community>' host.example .1.3.6.1.2.1.1.1.0",
          "v1 Get is written plainly");
    SnmpRequestConfig hostile = v1; hostile.address = "host; rm -rf /";
    check(BuildSnmpCommand(hostile, SnmpCommandKind::Get, oid).command.contains("'host; rm -rf /'"),
          "unusual text is shell-quoted");
    check(BuildSnmpCommand(v1, SnmpCommandKind::Get, QString()).command.isEmpty(), "an empty OID gives no command");
    return failures ? 1 : 0;
}
