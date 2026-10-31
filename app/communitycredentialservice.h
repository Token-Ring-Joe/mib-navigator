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
    const QList<CommunityCredentialRecord> &records() const;
    const CommunityCredentialRecord *find(const QString &credentialId) const;
    QString create(const CommunityCredentialRecord &draft);
    bool update(const CommunityCredentialRecord &record);
    QString duplicate(const QString &credentialId);
    bool remove(const QString &credentialId);

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
    QList<CommunityCredentialRecord> credentials;
};

#endif
