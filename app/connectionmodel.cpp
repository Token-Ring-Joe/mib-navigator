#include "connectionmodel.h"

#include "communitycredentialservice.h"
#include "usmcredentialservice.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace {
const char *const kHistoryKey = "connection/target-history";

QString communityLabel(const CommunityCredentialRecord &record, int protocol)
{
    const QString name = record.displayName.trimmed().isEmpty()
        ? QObject::tr("(unnamed)") : record.displayName.trimmed();
    return QObject::tr("%1 - SNMP%2").arg(name, protocol == 0 ? QStringLiteral("v1")
                                                              : QStringLiteral("v2c"));
}
}

QList<ConnectionHistoryEntry> ConnectionHistory::load(QSettings &settings)
{
    QList<ConnectionHistoryEntry> entries;
    const QJsonDocument document = QJsonDocument::fromJson(
        settings.value(kHistoryKey).toString().toUtf8());
    for (const QJsonValue &value : document.array()) {
        const QJsonObject object = value.toObject();
        const QString target = object.value("target").toString().trimmed();
        if (target.isEmpty()) continue;
        entries.append({target, object.value("credential").toString()});
        if (entries.size() >= kMaxEntries) break;
    }
    return entries;
}

void ConnectionHistory::save(QSettings &settings, const QList<ConnectionHistoryEntry> &entries)
{
    QJsonArray array;
    for (const ConnectionHistoryEntry &entry : entries)
        array.append(QJsonObject{{"target", entry.target}, {"credential", entry.credentialKey}});
    settings.setValue(kHistoryKey, QString::fromUtf8(
        QJsonDocument(array).toJson(QJsonDocument::Compact)));
}

QList<ConnectionHistoryEntry> ConnectionHistory::remembered(
    QList<ConnectionHistoryEntry> entries, const QString &target, const QString &credentialKey)
{
    const QString trimmed = target.trimmed();
    if (trimmed.isEmpty()) return entries;
    for (qsizetype i = entries.size() - 1; i >= 0; --i)
        if (entries.at(i).target.compare(trimmed, Qt::CaseInsensitive) == 0)
            entries.removeAt(i);
    entries.prepend({trimmed, credentialKey});
    while (entries.size() > kMaxEntries) entries.removeLast();
    return entries;
}

QList<ConnectionHistoryEntry> ConnectionHistory::removed(
    QList<ConnectionHistoryEntry> entries, const QString &target)
{
    for (qsizetype i = entries.size() - 1; i >= 0; --i)
        if (entries.at(i).target.compare(target.trimmed(), Qt::CaseInsensitive) == 0)
            entries.removeAt(i);
    return entries;
}

ConnectionTarget ConnectionTarget::parse(const QString &text, int defaultPort)
{
    ConnectionTarget result;
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty() || trimmed.contains(QLatin1Char(' ')) ||
        trimmed.contains(QLatin1Char('/')))
        return result;
    QString host = trimmed;
    QString port = QString::number(defaultPort);
    if (trimmed.startsWith(QLatin1Char('['))) {
        const qsizetype close = trimmed.indexOf(QLatin1Char(']'));
        if (close < 2) return result;
        host = trimmed.mid(1, close - 1);
        const QString rest = trimmed.mid(close + 1);
        if (!rest.isEmpty()) {
            if (!rest.startsWith(QLatin1Char(':'))) return result;
            port = rest.mid(1);
        }
    } else if (trimmed.count(QLatin1Char(':')) == 1) {
        host = trimmed.section(QLatin1Char(':'), 0, 0);
        port = trimmed.section(QLatin1Char(':'), 1);
    }
    bool portOk = false;
    const int portNumber = port.toInt(&portOk);
    if (host.isEmpty() || !portOk || portNumber < 1 || portNumber > 65535) return result;
    result.host = host;
    result.port = QString::number(portNumber);
    result.valid = true;
    return result;
}

QString ConnectionModel::communityKey(const QString &credentialId, int protocol)
{
    return QStringLiteral("community:%1:%2").arg(credentialId,
        protocol == 0 ? QStringLiteral("v1") : QStringLiteral("v2c"));
}

QString ConnectionModel::usmKey(const QString &credentialId)
{
    return QStringLiteral("usm:%1").arg(credentialId);
}

QList<ConnectionCredentialChoice> ConnectionModel::credentialChoices(
    const CommunityCredentialService *community, const UsmCredentialService *usm)
{
    QList<ConnectionCredentialChoice> choices;
    if (community) {
        for (const CommunityCredentialRecord &record : community->records()) {
            const QString id = record.identity.credentialId;
            choices.append({communityKey(id, 1), communityLabel(record, 1), 1});
            choices.append({communityKey(id, 0), communityLabel(record, 0), 0});
        }
    }
    if (usm) {
        for (const UsmCredentialRecord &record : usm->records()) {
            const int level = UsmCredentialService::securityLevel(record);
            const QString levelText = level == 2 ? QObject::tr("authPriv")
                : level == 1 ? QObject::tr("authNoPriv") : QObject::tr("noAuthNoPriv");
            choices.append({usmKey(record.identity.credentialId),
                            QObject::tr("%1 - SNMPv3 %2").arg(record.securityName, levelText), 2});
        }
    }
    return choices;
}

ConnectionError ConnectionModel::build(
    const QString &target, const QString &credentialKey,
    const CommunityCredentialService *community, const UsmCredentialService *usm,
    const PreferencesSettings &preferences, AgentRequestSelection *selection)
{
    if (!selection) return ConnectionError::NoTarget;
    if (target.trimmed().isEmpty()) return ConnectionError::NoTarget;
    const ConnectionTarget parsed = ConnectionTarget::parse(target, preferences.requestPort);
    if (!parsed.valid) return ConnectionError::BadTarget;
    if (credentialKey.isEmpty()) return ConnectionError::NoCredential;

    SnmpRequestSettings profile{};
    profile.profileId = QStringLiteral("connection-bar");
    profile.name = parsed.host;
    profile.address = parsed.host;
    profile.port = parsed.port;
    profile.retries = preferences.requestRetries;
    profile.timeout = preferences.requestTimeout;
    profile.maxrepetitions = preferences.bulkMaxRepetitions;
    profile.nonrepeaters = preferences.bulkNonRepeaters;

    EffectiveCredentialValues values;
    int protocol = 1;
    const QStringList parts = credentialKey.split(QLatin1Char(':'));
    if (parts.size() == 3 && parts.at(0) == QLatin1String("community")) {
        const CommunityCredentialRecord *record = community ? community->find(parts.at(1)) : nullptr;
        if (!record) return ConnectionError::CredentialMissing;
        protocol = parts.at(2) == QLatin1String("v1") ? 0 : 1;
        values = CredentialResolver::communityValues(*record);
        if (values.readCommunity.isEmpty()) return ConnectionError::CredentialIncomplete;
    } else if (parts.size() == 2 && parts.at(0) == QLatin1String("usm")) {
        const UsmCredentialRecord *record = usm ? usm->find(parts.at(1)) : nullptr;
        if (!record) return ConnectionError::CredentialMissing;
        if (!UsmCredentialService::requirementsSatisfied(*record))
            return ConnectionError::CredentialIncomplete;
        // The SNMPv3 engine looks the user up by security name, so two saved users with the
        // same name cannot be told apart; refuse rather than guess which one is meant.
        if (!usm->isSecurityNameUnambiguous(record->securityName))
            return ConnectionError::CredentialAmbiguous;
        protocol = 2;
        values.securityName = record->securityName;
        values.securityLevel = UsmCredentialService::securityLevel(*record);
    } else {
        return ConnectionError::CredentialMissing;
    }
    profile.v1 = protocol == 0;
    profile.v2 = protocol == 1;
    profile.v3 = protocol == 2;

    selection->profile = profile;
    selection->selectedProtocol = protocol;
    selection->credentials = values;
    selection->hasResolvedCredentials = true;
    return ConnectionError::None;
}

QString ConnectionModel::errorText(ConnectionError error)
{
    switch (error) {
    case ConnectionError::None: return {};
    case ConnectionError::NoTarget:
        return QObject::tr("Enter a Target: an IP address or host name.");
    case ConnectionError::BadTarget:
        return QObject::tr("The Target is not valid. Use an IP address or host name, optionally "
                           "followed by :port (for example 10.1.1.20 or 10.1.1.20:1161).");
    case ConnectionError::NoCredential:
        return QObject::tr("Choose a Credential. Add one with the Manage… button next to the Credential list.");
    case ConnectionError::CredentialMissing:
        return QObject::tr("The chosen Credential no longer exists. Choose another one.");
    case ConnectionError::CredentialIncomplete:
        return QObject::tr("The chosen Credential is incomplete (missing community or "
                           "SNMPv3 passphrase). Edit it with the Manage… button next to the Credential list.");
    case ConnectionError::CredentialAmbiguous:
        return QObject::tr("Two saved SNMPv3 users have the same name, so the right one cannot be "
                           "chosen. Rename or delete one with the Manage… button.");
    }
    return {};
}
