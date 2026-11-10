#ifndef COMMUNITYCREDENTIALSERVICE_H
#define COMMUNITYCREDENTIALSERVICE_H

#include "communitycredentialrepository.h"
#include <QObject>

// Saved, reusable SNMPv1/v2c communities. A credential is chosen in the connection bar for each
// request; nothing else holds a reference to it, so deleting one is always safe (the bar then
// shows "choose a credential"). The old profile-to-credential binding file
// (credential-bindings.conf) is no longer read; it is left on disk untouched.
class CommunityCredentialService : public QObject
{
    Q_OBJECT
public:
    CommunityCredentialService(const QString &credentialFile, QObject *parent = nullptr);
    CommunityCredentialService(const QString &credentialFile,
                               std::shared_ptr<CredentialSecretStore> secretStore, QObject *parent = nullptr);
    const QList<CommunityCredentialRecord> &records() const;
    const CommunityCredentialRecord *find(const QString &credentialId) const;
    QString create(const CommunityCredentialRecord &draft);
    bool update(const CommunityCredentialRecord &record);
    QString duplicate(const QString &credentialId);
    bool remove(const QString &credentialId);
    bool saveWorkingCopy(const QList<CommunityCredentialRecord> &records) const;
    void applyCommitted(const QList<CommunityCredentialRecord> &records);
    QString storageError() const { return loadError; }
    // Password protection: the secrets live in `store` instead of the credentials file.
    // attach changes where the next save writes; it does not touch the file or the records.
    void attachSecretStore(std::shared_ptr<CredentialSecretStore> store);
    bool rewriteStorage();                  // saves the records in memory with the current setting
    void reload();                          // re-reads the file (after an unlock); announces changes
    bool isLocked() const { return locked; }

signals:
    void credentialCreated(const QString &credentialId);
    void credentialUpdated(const QString &credentialId);
    void credentialRenamed(const QString &credentialId);
    void credentialDeleted(const QString &credentialId);
    void credentialsChanged();

private:
    static QString newId();
    bool saveCredentials();
    CommunityCredentialRepository credentialRepository;
    QString loadError;
    bool locked = false;
    QList<CommunityCredentialRecord> credentials;
    std::shared_ptr<CredentialSecretStore> secretStore;
};

#endif
