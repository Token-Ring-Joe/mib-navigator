#ifndef CREDENTIALSECRETSTORE_H
#define CREDENTIALSECRETSTORE_H

#include <QByteArray>
#include <QHash>
#include <QString>

// Where credential secrets live while password protection is on. The repositories only know
// this interface, so they compile without the cryptography; CredentialVault implements it.
class CredentialSecretStore
{
public:
    virtual ~CredentialSecretStore() = default;
    virtual bool isUnlocked() const = 0;
    virtual const QHash<QString, QByteArray> &secrets() const = 0;
    // Replaces every stored secret (and persists them); false leaves the stored set unchanged.
    virtual bool store(const QHash<QString, QByteArray> &all) = 0;
};

namespace CredentialSecretKeys
{
inline QString community(const QString &id, const QString &field) { return QStringLiteral("community/") + id + QLatin1Char('/') + field; }
inline QString usm(const QString &id, const QString &field) { return QStringLiteral("usm/") + id + QLatin1Char('/') + field; }
inline QString communityPrefix() { return QStringLiteral("community/"); }
inline QString usmPrefix() { return QStringLiteral("usm/"); }
}

// Replaces the secrets under `prefix` with `mine`, leaving the other repository's secrets alone.
// `previous` receives the full set before the change so a failed file write can undo it.
inline bool credentialStoreReplace(CredentialSecretStore &store, const QString &prefix,
                                   const QHash<QString, QByteArray> &mine,
                                   QHash<QString, QByteArray> *previous)
{
    if (!store.isUnlocked()) return false;
    *previous = store.secrets();
    QHash<QString, QByteArray> merged;
    for (auto it = previous->cbegin(); it != previous->cend(); ++it)
        if (!it.key().startsWith(prefix)) merged.insert(it.key(), it.value());
    for (auto it = mine.cbegin(); it != mine.cend(); ++it) merged.insert(it.key(), it.value());
    return store.store(merged);
}

#endif
