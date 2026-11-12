#include "snmpcommandtext.h"

#include "snmp_pp/usm_v3.h"

#include <QObject>
#include <QRegularExpression>

namespace {
// POSIX shell quoting that leaves ordinary words alone.
QString quoted(const QString &text)
{
    static const QRegularExpression plain(QStringLiteral("^[A-Za-z0-9_@%+=:,./-]+$"));
    if (plain.match(text).hasMatch()) return text;
    QString escaped = text;
    escaped.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
    return QLatin1Char('\'') + escaped + QLatin1Char('\'');
}

QString authName(long protocol)
{
    switch (protocol) {
    case SNMP_AUTHPROTOCOL_HMACMD5: return QStringLiteral("MD5");
    case SNMP_AUTHPROTOCOL_HMACSHA: return QStringLiteral("SHA");
    case SNMP_AUTHPROTOCOL_HMAC128SHA224: return QStringLiteral("SHA-224");
    case SNMP_AUTHPROTOCOL_HMAC192SHA256: return QStringLiteral("SHA-256");
    case SNMP_AUTHPROTOCOL_HMAC256SHA384: return QStringLiteral("SHA-384");
    case SNMP_AUTHPROTOCOL_HMAC384SHA512: return QStringLiteral("SHA-512");
    }
    return {};
}

QString privName(long protocol)
{
    switch (protocol) {
    case SNMP_PRIVPROTOCOL_DES: return QStringLiteral("DES");
    case SNMP_PRIVPROTOCOL_AES128: return QStringLiteral("AES");
    case SNMP_PRIVPROTOCOL_AES192: return QStringLiteral("AES-192");
    case SNMP_PRIVPROTOCOL_AES256: return QStringLiteral("AES-256");
    }
    return {};
}
}

SnmpCommandText BuildSnmpCommand(const SnmpRequestConfig &config, SnmpCommandKind kind, const QString &oid)
{
    SnmpCommandText out;
    if (oid.trimmed().isEmpty() || config.address.trimmed().isEmpty()) return out;
    const bool v1 = config.version == SnmpRequestVersion::V1;
    const bool v3 = config.version == SnmpRequestVersion::V3;
    if (v1 && kind == SnmpCommandKind::GetBulk) {
        out.note = QObject::tr("Get Bulk does not exist in SNMPv1, so there is no command for it.");
        return out;
    }

    QString tool;
    switch (kind) {
    case SnmpCommandKind::Get: tool = QStringLiteral("snmpget"); break;
    case SnmpCommandKind::GetNext: tool = QStringLiteral("snmpgetnext"); break;
    case SnmpCommandKind::GetBulk: tool = QStringLiteral("snmpbulkget"); break;
    case SnmpCommandKind::Walk: tool = QStringLiteral("snmpwalk"); break;
    }

    QStringList words{tool};
    words << (v1 ? QStringLiteral("-v1") : v3 ? QStringLiteral("-v3") : QStringLiteral("-v2c"));
    QStringList notes;
    if (v3) {
        // securityLevel: 0 noAuthNoPriv, 1 authNoPriv, 2 authPriv.
        const bool auth = config.securityLevel >= 1, priv = config.securityLevel >= 2;
        words << QStringLiteral("-l")
              << (priv ? QStringLiteral("authPriv") : auth ? QStringLiteral("authNoPriv") : QStringLiteral("noAuthNoPriv"))
              << QStringLiteral("-u") << quoted(config.securityName);
        if (auth && config.usmCredential) {
            const QString a = authName(config.usmCredential->authProtocol);
            if (!a.isEmpty()) words << QStringLiteral("-a") << a;
            else notes << QObject::tr("This authentication protocol has no Net-SNMP name; add -a yourself.");
        }
        // Net-SNMP does not prompt: without -A/-X it fails with "USM generic error". The
        // passphrases are therefore written as placeholders for the user to replace.
        if (auth) words << QStringLiteral("-A") << quoted(QStringLiteral("<authentication passphrase>"));
        if (priv && config.usmCredential) {
            const QString p = privName(config.usmCredential->privacyProtocol);
            if (!p.isEmpty()) words << QStringLiteral("-x") << p;
            else notes << QObject::tr("This privacy protocol has no Net-SNMP name; add -x yourself.");
        }
        if (priv) words << QStringLiteral("-X") << quoted(QStringLiteral("<privacy passphrase>"));
        if (!config.contextName.isEmpty()) words << QStringLiteral("-n") << quoted(config.contextName);
        if (!config.contextEngineId.isEmpty()) words << QStringLiteral("-E") << quoted(config.contextEngineId);
        notes.prepend(auth ? QObject::tr("Replace %1 before you run it; passphrases are never copied.")
                                 .arg(priv ? QObject::tr("the two passphrase placeholders")
                                           : QObject::tr("the passphrase placeholder"))
                           : QObject::tr("This security level needs no passphrase."));
    } else {
        words << QStringLiteral("-c") << quoted(QStringLiteral("<community>"));
        notes << QObject::tr("Replace <community> with the community string; it is never copied.");
    }
    if (config.timeout > 0) words << QStringLiteral("-t") << QString::number(config.timeout);
    if (config.retries > 0) words << QStringLiteral("-r") << QString::number(config.retries);
    if (kind == SnmpCommandKind::GetBulk) {
        if (config.nonRepeaters > 0) words << QStringLiteral("-Cn%1").arg(config.nonRepeaters);
        if (config.maxRepetitions > 0) words << QStringLiteral("-Cr%1").arg(config.maxRepetitions);
    }

    const QString port = config.port.isEmpty() ? QStringLiteral("161") : config.port;
    const bool ipv6 = config.address.contains(QLatin1Char(':'));
    QString target = ipv6 ? QStringLiteral("udp6:[%1]:%2").arg(config.address, port)
                          : (port == QLatin1String("161") ? config.address
                                                          : QStringLiteral("%1:%2").arg(config.address, port));
    words << quoted(target);
    QString numeric = oid.trimmed();
    if (!numeric.startsWith(QLatin1Char('.'))) numeric.prepend(QLatin1Char('.'));
    words << quoted(numeric);

    out.command = words.join(QLatin1Char(' '));
    out.note = notes.join(QLatin1Char(' '));
    return out;
}
