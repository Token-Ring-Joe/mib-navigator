#include "credentialvault.h"

#include <QCoreApplication>
#include "tomcrypt.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <cstdio>

namespace {
int failures = 0;
void check(bool ok, const char *why) { if (!ok) { std::fprintf(stderr, "FAIL: %s\n", why); ++failures; } }
QByteArray slurp(const QString &path) { QFile f(path); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray(); }
void spit(const QString &path, const QByteArray &bytes)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate) || f.write(bytes) != bytes.size()) check(false, "test fixture could not be written");
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    // SNMP++ registers libtomcrypt's "rijndael" cipher before any credential is touched; the
    // vault must work in that state, which is the state of the real application.
    register_cipher(&rijndael_desc);
    QTemporaryDir dir; const QString path = dir.filePath("vault.json");
    const QHash<QString, QByteArray> secrets{{"c1/read", "public"}, {"c1/write", "private"},
        {"u1/auth", QByteArray("pass\0word", 9)}, {"u1/priv", QByteArray("\xc3\xa9t\xc3\xa9", 4)}, {"empty/read", QByteArray()}};
    {
        CredentialVault vault(path, 2000);
        check(!vault.exists() && !vault.isUnlocked(), "a new vault starts absent and locked");
        check(!vault.create("", secrets) && !vault.exists(), "an empty master password is refused");
        check(vault.create("correct horse", secrets) && vault.exists() && vault.isUnlocked(), "create protects and unlocks");
        const QByteArray file = slurp(path);
        check(!file.contains("public") && !file.contains("private") && !file.contains("cHVibGlj") && !file.contains("word"),
              "no secret appears in the file, in plain text or base64");
#ifndef Q_OS_WIN
        check(!(QFile::permissions(path) & (QFile::ReadGroup | QFile::ReadOther | QFile::WriteGroup | QFile::WriteOther)),
              "the vault file is readable by its owner only");
#endif
        check(!vault.create("again", secrets), "protection cannot be created twice");
    }
    {
        CredentialVault vault(path);
        check(vault.exists() && !vault.isUnlocked() && vault.secrets().isEmpty(), "a reopened vault is locked and exposes nothing");
        const QByteArray before = slurp(path);
        check(!vault.unlock("wrong") && !vault.isUnlocked() && vault.secrets().isEmpty(), "a wrong password does not unlock");
        check(vault.error().contains("incorrect"), "a wrong password is explained");
        check(slurp(path) == before, "a failed unlock leaves the file untouched");
        check(vault.unlock("correct horse") && vault.secrets() == secrets, "the right password returns every secret byte-for-byte");
        check(vault.secrets().value("u1/auth") == QByteArray("pass\0word", 9), "binary secrets survive");
    }
    {   // Damage and tampering are detected, never half-accepted.
        QByteArray bytes = slurp(path); auto doc = QJsonDocument::fromJson(bytes).object();
        auto tamper = [&](const char *field) {
            auto copy = doc; QByteArray raw = QByteArray::fromBase64(copy.value(field).toString().toLatin1());
            raw[raw.size() / 2] = raw[raw.size() / 2] ^ 0x01; copy[field] = QString::fromLatin1(raw.toBase64());
            spit(dir.filePath("t.json"), QJsonDocument(copy).toJson());
            CredentialVault v(dir.filePath("t.json")); return v.unlock("correct horse");
        };
        check(!tamper("data") && !tamper("iv") && !tamper("salt") && !tamper("mac"), "a modified file never unlocks");
        spit(dir.filePath("garbage.json"), "not json");
        CredentialVault garbage(dir.filePath("garbage.json"));
        check(!garbage.unlock("x") && garbage.error().contains("damaged"), "a damaged file is reported, not crashed on");
        auto newer = doc; newer["format"] = "MIBNAV-VAULT-9";
        spit(dir.filePath("newer.json"), QJsonDocument(newer).toJson());
        CredentialVault future(dir.filePath("newer.json"));
        check(!future.unlock("correct horse") && future.error().contains("newer version"), "an unknown format is refused untouched");
    }
    {   // Storing changes, and a fresh salt/IV each time.
        CredentialVault vault(path);
        check(!vault.store(secrets), "a locked vault cannot be written");
        check(vault.unlock("correct horse"), "unlock for update");
        const QByteArray one = slurp(path);
        auto changed = secrets; changed["c1/read"] = "new-community"; changed.remove("c1/write");
        check(vault.store(changed) && vault.secrets() == changed, "store replaces the secret set");
        check(slurp(path) != one, "the file is rewritten with a new salt and IV");
        vault.lock();
        check(!vault.isUnlocked() && vault.secrets().isEmpty(), "lock forgets every secret");
        CredentialVault again(path); check(again.unlock("correct horse") && again.secrets() == changed, "stored changes persist");
    }
    {   // Changing the password.
        CredentialVault vault(path); check(vault.unlock("correct horse"), "unlock for password change");
        check(!vault.changePassword("nope", "new password") && !vault.changePassword("correct horse", ""),
              "a wrong current password or an empty new one is refused");
        const auto before = vault.secrets();
        check(vault.changePassword("correct horse", "new password"), "password change succeeds");
        CredentialVault old(path), fresh(path);
        check(!old.unlock("correct horse") && fresh.unlock("new password") && fresh.secrets() == before,
              "the old password stops working and the secrets are unchanged");
    }
    {   // Turning protection off.
        CredentialVault vault(path); check(vault.unlock("new password"), "unlock to remove");
        check(vault.removeFile() && !QFile::exists(path) && !vault.isUnlocked(), "removing protection deletes the vault and locks");
        CredentialVault locked(path); check(!locked.removeFile(), "a locked vault cannot be removed");
    }
    {   // An unwritable location leaves nothing half-written.
        CredentialVault vault(dir.filePath("missing/dir/vault.json"), 2000);
        check(!vault.create("pw", secrets) && !vault.exists() && !vault.isUnlocked(), "an unwritable vault is not created");
    }
    return failures ? 1 : 0;
}
