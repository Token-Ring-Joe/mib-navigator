#ifndef CREDENTIALPROTECTION_H
#define CREDENTIALPROTECTION_H

#include "credentialvault.h"
#include <QObject>
#include <memory>

class CommunityCredentialService;
class UsmCredentialService;

// Turns master-password protection of saved credential secrets on and off.
//
// Protection is opt-in. With it off, secrets stay in the credential files exactly as before.
// With it on, they live only in the encrypted vault file and the credential files keep the
// names, users and protocols. Every change here either completes or restores the previous
// state: the files are rewritten before the vault is removed, and a failed step puts the
// plain files back and keeps the vault if that cannot be done.
class CredentialProtection : public QObject
{
    Q_OBJECT
public:
    explicit CredentialProtection(const QString &vaultFile,
                                  int iterations = CredentialVault::DefaultIterations,
                                  QObject *parent = nullptr);

    // What the credential services should be given at startup: the vault while protection is
    // on (even if still locked), nothing while it is off, so plain saves are never refused.
    std::shared_ptr<CredentialSecretStore> activeSecretStore() const
    { return vault->exists() ? std::shared_ptr<CredentialSecretStore>(vault) : nullptr; }
    bool isEnabled() const { return vault->exists(); }
    bool isUnlocked() const { return vault->isUnlocked(); }
    QString error() const { return lastError; }
    QString vaultPath() const { return vault->path(); }

    bool enable(const QString &password, CommunityCredentialService *communities,
                UsmCredentialService *users);
    // Turning protection off re-asks for the master password even though the vault is open.
    bool disable(const QString &password, CommunityCredentialService *communities,
                 UsmCredentialService *users);
    bool changePassword(const QString &current, const QString &replacement);
    // Wrong password: false, nothing changes. Success reloads both services.
    bool unlock(const QString &password, CommunityCredentialService *communities,
                UsmCredentialService *users);
    // Startup helper: unlock the vault only; the services are created afterwards.
    bool unlockVault(const QString &password);

signals:
    void stateChanged();

private:
    bool fail(const QString &message);
    std::shared_ptr<CredentialVault> vault;
    QString lastError;
};

#endif
