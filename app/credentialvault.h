#ifndef CREDENTIALVAULT_H
#define CREDENTIALVAULT_H

#include <QByteArray>
#include <QHash>
#include <QString>

// Password-protected storage for saved credential secrets (communities and SNMPv3 passwords).
//
// The vault file holds a salt, an IV, the ciphertext and a MAC. The key comes from the master
// password with PBKDF2-HMAC-SHA256; the payload is encrypted with AES-256-CBC and
// authenticated with HMAC-SHA256 (encrypt-then-MAC), so a wrong password and a damaged file
// are both detected before anything is decrypted. Nothing here touches widgets, SNMP state or
// the credential records themselves: callers hand it a flat map of "<credentialId>/<field>"
// to secret bytes and get the same map back after unlocking.
//
// The master password is never stored. If it is lost the secrets cannot be recovered; the
// callers keep the non-secret credential details (IDs, names, users, protocols) in their own
// files so the user can re-enter the secrets.
class CredentialVault
{
public:
    static constexpr int DefaultIterations = 600000;
    explicit CredentialVault(const QString &fileName, int iterations = DefaultIterations);

    bool exists() const;                       // a vault file is present (protection is on)
    bool isUnlocked() const { return unlocked; }
    QString error() const { return lastError; }
    QString path() const { return fileName; }

    // Turns protection on: encrypts `secrets` under `password`. Fails (changing nothing) if a
    // vault already exists or the file cannot be written.
    bool create(const QString &password, const QHash<QString, QByteArray> &secrets);
    // Decrypts the vault. A wrong password or a damaged/unsupported file returns false and
    // leaves the vault locked and the file untouched.
    bool unlock(const QString &password);
    void lock();
    // Replaces every stored secret and rewrites the file; requires an unlocked vault.
    bool store(const QHash<QString, QByteArray> &secrets);
    bool changePassword(const QString &currentPassword, const QString &newPassword);
    const QHash<QString, QByteArray> &secrets() const { return values; }
    // Turns protection off: after the caller has written the secrets back to its own files.
    bool removeFile();

private:
    bool write(const QString &password, const QHash<QString, QByteArray> &secrets);
    QString fileName;
    int iterations;
    bool unlocked = false;
    QString lastError;
    QHash<QString, QByteArray> values;
    QString sessionPassword;   // kept only while unlocked so store() can re-encrypt
};

#endif
