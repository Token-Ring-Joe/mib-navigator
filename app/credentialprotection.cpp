#include "credentialprotection.h"
#include "communitycredentialservice.h"
#include "usmcredentialservice.h"

CredentialProtection::CredentialProtection(const QString &vaultFile, int iterations, QObject *parent)
    : QObject(parent), vault(std::make_shared<CredentialVault>(vaultFile, iterations)) {}

bool CredentialProtection::fail(const QString &message)
{
    lastError = message;
    return false;
}

bool CredentialProtection::unlockVault(const QString &password)
{
    lastError.clear();
    if (!vault->unlock(password)) return fail(vault->error());
    emit stateChanged();
    return true;
}

bool CredentialProtection::unlock(const QString &password, CommunityCredentialService *communities,
                                  UsmCredentialService *users)
{
    if (!vault->exists()) return fail(tr("Password protection is not turned on."));
    if (!unlockVault(password)) return false;
    communities->reload();
    users->reload();
    return true;
}

bool CredentialProtection::enable(const QString &password, CommunityCredentialService *communities,
                                  UsmCredentialService *users)
{
    lastError.clear();
    if (vault->exists()) return fail(tr("Password protection is already turned on."));
    if (password.isEmpty()) return fail(tr("Enter a master password."));
    if (!communities->storageError().isEmpty() || !users->storageError().isEmpty())
        return fail(tr("Saved credentials cannot be read, so they were not protected."));

    QHash<QString, QByteArray> secrets;
    for (const auto &r : communities->records()) {
        secrets.insert(CredentialSecretKeys::community(r.identity.credentialId, QStringLiteral("read")), r.readCommunity.bytes());
        secrets.insert(CredentialSecretKeys::community(r.identity.credentialId, QStringLiteral("write")), r.writeCommunity.bytes());
    }
    for (const auto &r : users->records()) {
        secrets.insert(CredentialSecretKeys::usm(r.identity.credentialId, QStringLiteral("auth")), r.authSecret.bytes());
        secrets.insert(CredentialSecretKeys::usm(r.identity.credentialId, QStringLiteral("priv")), r.privacySecret.bytes());
    }
    // 1. The vault first: until it exists nothing has changed.
    if (!vault->create(password, secrets)) return fail(vault->error());

    // 2. Rewrite both credential files without secrets.
    communities->attachSecretStore(vault);
    users->attachSecretStore(vault);
    if (communities->rewriteStorage() && users->rewriteStorage()) {
        emit stateChanged();
        return true;
    }

    // 3. Roll back: plain files again, then drop the new vault. If a plain file cannot be
    // restored, the vault stays, because it may hold the only copy of those secrets.
    communities->attachSecretStore(nullptr);
    users->attachSecretStore(nullptr);
    const bool restored = communities->rewriteStorage() && users->rewriteStorage();
    if (restored) {
        vault->removeFile();
        vault->lock();
        return fail(tr("Could not update the credential files. Password protection was not turned on and nothing was changed."));
    }
    communities->attachSecretStore(vault);
    users->attachSecretStore(vault);
    communities->rewriteStorage();
    users->rewriteStorage();
    emit stateChanged();
    return fail(tr("Could not update the credential files completely. Password protection stays on so no secret is lost; check that the settings folder is writable, then turn protection off and on again."));
}

bool CredentialProtection::disable(const QString &password, CommunityCredentialService *communities,
                                   UsmCredentialService *users)
{
    lastError.clear();
    if (!vault->exists()) return fail(tr("Password protection is not turned on."));
    if (!vault->isUnlocked()) return fail(tr("Unlock the credentials first."));
    if (!vault->verifyPassword(password)) return fail(tr("The master password is incorrect. Password protection is still on."));
    communities->attachSecretStore(nullptr);
    users->attachSecretStore(nullptr);
    if (!communities->rewriteStorage() || !users->rewriteStorage()) {
        communities->attachSecretStore(vault);
        users->attachSecretStore(vault);
        communities->rewriteStorage();
        users->rewriteStorage();
        return fail(tr("Could not write the credential files. Password protection is still on and nothing was lost."));
    }
    // The plain files are in place; only now is the vault removed.
    if (!vault->removeFile()) {
        return fail(tr("Credentials are no longer protected, but the vault file could not be removed: %1").arg(vault->error()));
    }
    vault->lock();
    emit stateChanged();
    return true;
}

bool CredentialProtection::changePassword(const QString &current, const QString &replacement)
{
    lastError.clear();
    if (replacement.isEmpty()) return fail(tr("Enter a new master password."));
    if (!vault->changePassword(current, replacement)) return fail(vault->error());
    return true;
}
