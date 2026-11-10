#include "communitycredentialservice.h"
#include <QUuid>
#include <algorithm>

CommunityCredentialService::CommunityCredentialService(
    const QString &credentialFile, QObject *parent)
    : CommunityCredentialService(credentialFile, nullptr, parent) {}

CommunityCredentialService::CommunityCredentialService(
    const QString &credentialFile, std::shared_ptr<CredentialSecretStore> store, QObject *parent)
    : QObject(parent), credentialRepository(credentialFile), secretStore(store)
{
    credentialRepository.setSecretStore(store);
    credentials = credentialRepository.load(&loadError);
    locked = !loadError.isEmpty() && loadError == CommunityCredentialRepository::lockedMessage();
}

void CommunityCredentialService::attachSecretStore(std::shared_ptr<CredentialSecretStore> store)
{
    secretStore = store;
    credentialRepository.setSecretStore(std::move(store));
}

bool CommunityCredentialService::rewriteStorage() { return saveCredentials(); }

void CommunityCredentialService::reload()
{
    QString error;
    const auto loaded = credentialRepository.load(&error);
    loadError = error;
    locked = !error.isEmpty() && error == CommunityCredentialRepository::lockedMessage();
    if (!error.isEmpty()) return;
    applyCommitted(loaded);
}

const QList<CommunityCredentialRecord> &CommunityCredentialService::records() const { return credentials; }
const CommunityCredentialRecord *CommunityCredentialService::find(const QString &id) const
{ for (const auto &r : credentials) if (r.identity.credentialId == id) return &r; return nullptr; }

QString CommunityCredentialService::create(const CommunityCredentialRecord &draft)
{
    CommunityCredentialRecord record = draft;
    record.identity.kind = CredentialKind::Community;
    if (record.identity.credentialId.isEmpty() || find(record.identity.credentialId))
        record.identity.credentialId = newId();
    credentials.append(record); if (!saveCredentials()) { credentials.removeLast(); return {}; }
    emit credentialCreated(record.identity.credentialId); emit credentialsChanged();
    return record.identity.credentialId;
}

bool CommunityCredentialService::update(const CommunityCredentialRecord &record)
{
    for (auto &stored : credentials) if (stored.identity.credentialId == record.identity.credentialId)
    {
        const QString oldName = stored.displayName; const auto old = stored; stored = record;
        if (!saveCredentials()) { stored = old; return false; }
        if (oldName != stored.displayName) emit credentialRenamed(stored.identity.credentialId);
        emit credentialUpdated(stored.identity.credentialId); emit credentialsChanged(); return true;
    }
    return false;
}

QString CommunityCredentialService::duplicate(const QString &id)
{ const auto *source = find(id); if (!source) return {}; CommunityCredentialRecord copy = *source;
  copy.displayName += tr(" copy"); return create(copy); }

bool CommunityCredentialService::remove(const QString &id)
{
    for (int i = 0; i < credentials.size(); ++i) if (credentials[i].identity.credentialId == id)
    { const auto old = credentials.takeAt(i); if (!saveCredentials()) { credentials.insert(i, old); return false; }
      emit credentialDeleted(id); emit credentialsChanged(); return true; }
    return false;
}

QString CommunityCredentialService::newId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
bool CommunityCredentialService::saveCredentials() { return loadError.isEmpty() && credentialRepository.save(credentials); }

bool CommunityCredentialService::saveWorkingCopy(const QList<CommunityCredentialRecord> &records) const
{
    return loadError.isEmpty() && credentialRepository.save(records);
}
void CommunityCredentialService::applyCommitted(const QList<CommunityCredentialRecord> &records)
{
    if (credentials == records) return;
    const auto before = credentials;
    credentials = records;
    for (const auto &old : before) if (!find(old.identity.credentialId)) emit credentialDeleted(old.identity.credentialId);
    for (const auto &r : records) {
        const auto it = std::find_if(before.cbegin(), before.cend(), [&](const auto &old) {
            return old.identity.credentialId == r.identity.credentialId;
        });
        if (it == before.cend()) emit credentialCreated(r.identity.credentialId);
        else if (!(*it == r)) {
            if (it->displayName != r.displayName) emit credentialRenamed(r.identity.credentialId);
            emit credentialUpdated(r.identity.credentialId);
        }
    }
    emit credentialsChanged();
}
