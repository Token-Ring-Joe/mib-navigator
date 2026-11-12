#include "snmpcommandtext.h"

#include "snmp_pp/usm_v3.h"

#include <QByteArray>
#include <QCryptographicHash>
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

QByteArray SnmpMasterKey(const QByteArray &passphrase, long authProtocol)
{
    QCryptographicHash::Algorithm algorithm;
    switch (authProtocol) {
    case SNMP_AUTHPROTOCOL_HMACMD5: algorithm = QCryptographicHash::Md5; break;
    case SNMP_AUTHPROTOCOL_HMACSHA: algorithm = QCryptographicHash::Sha1; break;
    case SNMP_AUTHPROTOCOL_HMAC128SHA224: algorithm = QCryptographicHash::Sha224; break;
    case SNMP_AUTHPROTOCOL_HMAC192SHA256: algorithm = QCryptographicHash::Sha256; break;
    case SNMP_AUTHPROTOCOL_HMAC256SHA384: algorithm = QCryptographicHash::Sha384; break;
    case SNMP_AUTHPROTOCOL_HMAC384SHA512: algorithm = QCryptographicHash::Sha512; break;
    default: return {};
    }
    if (passphrase.isEmpty()) return {};
    constexpr int expansion = 1048576;
    QByteArray buffer;
    buffer.reserve(expansion);
    while (buffer.size() < expansion) buffer.append(passphrase);
    buffer.truncate(expansion);
    return QCryptographicHash::hash(buffer, algorithm);
}

SnmpCommandText BuildSnmpCommand(const SnmpRequestConfig &config, SnmpCommandKind kind, const QString &oid,
                                 bool includeKeys)
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
        // Net-SNMP does not prompt: without -A/-X (or -3m/-3M) it fails with "USM generic
        // error". Secrets are therefore placeholders unless the lab key option is used.
        QString authKey, privKey;
        if (includeKeys && auth && config.usmCredential) {
            authKey = QString::fromLatin1(SnmpMasterKey(config.usmCredential->authSecret.bytes(),
                                                        config.usmCredential->authProtocol).toHex());
            // AES-192/256 need a key longer than the hash gives; Net-SNMP only extends it for
            // localized keys, so those keep the placeholder.
            const long pp = config.usmCredential->privacyProtocol;
            if (priv && (pp == SNMP_PRIVPROTOCOL_DES || pp == SNMP_PRIVPROTOCOL_AES128))
                privKey = QString::fromLatin1(SnmpMasterKey(config.usmCredential->privacySecret.bytes(),
                                                            config.usmCredential->authProtocol).toHex());
        }
        if (auth && !authKey.isEmpty()) words << QStringLiteral("-3m") << authKey;
        else if (auth) words << QStringLiteral("-A") << quoted(QStringLiteral("<authentication passphrase>"));
        if (priv && config.usmCredential) {
            const QString p = privName(config.usmCredential->privacyProtocol);
            if (!p.isEmpty()) words << QStringLiteral("-x") << p;
            else notes << QObject::tr("This privacy protocol has no Net-SNMP name; add -x yourself.");
        }
        if (priv && !privKey.isEmpty()) words << QStringLiteral("-3M") << privKey;
        else if (priv) words << QStringLiteral("-X") << quoted(QStringLiteral("<privacy passphrase>"));
        out.carriesSecret = !authKey.isEmpty() || !privKey.isEmpty();
        if (!config.contextName.isEmpty()) words << QStringLiteral("-n") << quoted(config.contextName);
        if (!config.contextEngineId.isEmpty()) words << QStringLiteral("-E") << quoted(config.contextEngineId);
        const bool placeholders = (auth && authKey.isEmpty()) || (priv && privKey.isEmpty());
        if (out.carriesSecret)
            notes.prepend(QObject::tr("This command contains key material that logs in to this user; treat it like a password.")
                          + (placeholders ? QObject::tr(" The remaining placeholder must still be replaced.") : QString()));
        else
            notes.prepend(auth ? QObject::tr("Replace %1 before you run it; passphrases are never copied.")
                                     .arg(priv ? QObject::tr("the two passphrase placeholders")
                                               : QObject::tr("the passphrase placeholder"))
                               : QObject::tr("This security level needs no passphrase."));
        if (includeKeys && auth && !out.carriesSecret)
            notes << QObject::tr("No key could be derived (empty passphrase or unknown protocol).");
    } else {
        words << QStringLiteral("-c") << quoted(QStringLiteral("<community>"));
        notes << QObject::tr("Replace <community> with the community string; it is never copied.");
        if (includeKeys) notes << QObject::tr("Keys apply to SNMPv3 only.");
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
