#include "communitycredentialservice.h"
#include <QUuid>

CommunityCredentialService::CommunityCredentialService(
    const QString &credentialFile, QObject *parent)
    : QObject(parent), credentialRepository(credentialFile),
      credentials(credentialRepository.load()) {}

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
bool CommunityCredentialService::saveCredentials() { return credentialRepository.save(credentials); }
