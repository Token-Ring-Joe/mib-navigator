#ifndef COMMUNITYCREDENTIALREPOSITORY_H
#define COMMUNITYCREDENTIALREPOSITORY_H

#include "credentialrecords.h"
#include <QList>
#include <memory>
#include "credentialsecretstore.h"

class CommunityCredentialRepository
{
public:
    static constexpr int CurrentVersion = 1;
    explicit CommunityCredentialRepository(const QString &fileName);
    QList<CommunityCredentialRecord> load(QString *error = nullptr) const;
    bool save(const QList<CommunityCredentialRecord> &records) const;
    // With a store attached, secrets are kept there and the file holds only names and settings.
    // A file written that way is refused (never silently blanked) while the store is locked.
    void setSecretStore(std::shared_ptr<CredentialSecretStore> store) { secretStore = std::move(store); }
    bool isFileProtected() const;
    static QString lockedMessage();
private:
    bool writeFile(const QList<CommunityCredentialRecord> &records, bool withoutSecrets) const;
    QString fileName;
    std::shared_ptr<CredentialSecretStore> secretStore;
};

#endif
