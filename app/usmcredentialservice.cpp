#include "usmcredentialservice.h"

#include <QHash>
#include <QSet>
#include <QUuid>

UsmCredentialService::UsmCredentialService(
    const QList<UsmCredentialRecord> &legacyRecords,
    const UsmCredentialRepository &identityRepository, QObject *parent)
    : QObject(parent), credentials(legacyRecords), repository(identityRepository)
{
    const QList<UsmCredentialIdentityRecord> stored = repository.load();
    QHash<QString, QList<QString>> idsByName;
    QHash<QString, int> runtimeCounts;
    for (const auto &entry : stored) idsByName[entry.securityName].append(entry.credentialId);
    for (const auto &record : credentials) ++runtimeCounts[record.securityName];
    for (UsmCredentialRecord &record : credentials)
    {
        record.identity.kind = CredentialKind::Usm;
        const QList<QString> ids = idsByName.value(record.securityName);
        if (runtimeCounts.value(record.securityName) == 1 && ids.size() == 1)
            record.identity.credentialId = ids.first();
        else
            record.identity.credentialId = createId();
        if (record.displayName.isEmpty()) record.displayName = record.securityName;
    }
}

UsmCredentialRecord UsmCredentialService::createWorkingRecord(
    const QString &securityName) const
{
    UsmCredentialRecord record;
    record.identity = {createId(), CredentialKind::Usm};
    record.securityName = securityName;
    record.displayName = securityName;
    return record;
}

bool UsmCredentialService::validateWorkingCopy(
    const QList<UsmCredentialRecord> &records) const
{
    QSet<QString> ids, names;
    for (const UsmCredentialRecord &record : records)
    {
        const QString name = record.securityName.trimmed();
        if (record.identity.credentialId.isEmpty() || name.isEmpty() ||
            !requirementsSatisfied(record) ||
            ids.contains(record.identity.credentialId) || names.contains(name))
            return false;
        ids.insert(record.identity.credentialId);
        names.insert(name);
    }
    return true;
}

int UsmCredentialService::securityLevel(const UsmCredentialRecord &record)
{
    return record.privacyProtocol != 0 ? 2 : (record.authProtocol != 0 ? 1 : 0);
}

bool UsmCredentialService::requirementsSatisfied(const UsmCredentialRecord &record)
{
    if (record.securityName.trimmed().isEmpty()) return false;
    const int level = securityLevel(record);
    if (level >= 1 && (record.authProtocol == 0 || record.authSecret.isEmpty()))
        return false;
    if (level == 2 && (record.privacyProtocol == 0 ||
                       record.privacySecret.isEmpty()))
        return false;
    return true;
}

void UsmCredentialService::applyCommitted(
    const QList<UsmCredentialRecord> &records)
{
    QHash<QString, UsmCredentialRecord> oldById, newById;
    for (const auto &record : credentials)
        oldById.insert(record.identity.credentialId, record);
    for (const auto &record : records)
        newById.insert(record.identity.credentialId, record);
    credentials = records;
    for (const auto &record : records)
    {
        const QString id = record.identity.credentialId;
        if (!oldById.contains(id)) emit credentialCreated(id);
        else
        {
            const UsmCredentialRecord old = oldById.value(id);
            if (old.securityName != record.securityName)
                emit credentialRenamed(id, old.securityName, record.securityName);
            if (old.securityName != record.securityName ||
                old.displayName != record.displayName ||
                old.authProtocol != record.authProtocol ||
                old.privacyProtocol != record.privacyProtocol ||
                old.authSecret.bytes() != record.authSecret.bytes() ||
                old.privacySecret.bytes() != record.privacySecret.bytes())
                emit credentialUpdated(id);
        }
    }
    for (const auto &record : oldById)
        if (!newById.contains(record.identity.credentialId))
            emit credentialDeleted(record.identity.credentialId);
    emit credentialsChanged();
}

bool UsmCredentialService::isSecurityNameUnambiguous(
    const QString &securityName) const
{
    int matches = 0;
    for (const auto &record : credentials)
        if (record.securityName == securityName) ++matches;
    return matches == 1;
}

const QList<UsmCredentialRecord> &UsmCredentialService::records() const
{
    return credentials;
}

bool UsmCredentialService::saveIdentities() const
{
    QList<UsmCredentialIdentityRecord> identities;
    for (const auto &record : credentials)
        identities.append({record.identity.credentialId, record.securityName});
    return repository.save(identities);
}

bool UsmCredentialService::rename(const QString &id, const QString &name)
{
    const int index = indexOfId(id);
    if (index < 0 || name.trimmed().isEmpty()) return false;
    credentials[index].securityName = name.trimmed();
    credentials[index].displayName = name.trimmed();
    return true;
}

bool UsmCredentialService::remove(const QString &id)
{
    const int index = indexOfId(id);
    if (index < 0) return false;
    credentials.removeAt(index);
    return true;
}

const UsmCredentialRecord *UsmCredentialService::find(const QString &id) const
{
    const int index = indexOfId(id);
    return index < 0 ? nullptr : &credentials[index];
}

QString UsmCredentialService::createId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

int UsmCredentialService::indexOfId(const QString &id) const
{
    for (int i = 0; i < credentials.size(); ++i)
        if (credentials[i].identity.credentialId == id) return i;
    return -1;
}
