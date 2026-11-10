#include "credentialvault.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageAuthenticationCode>
#include <QPasswordDigestor>
#include <QRandomGenerator>
#include <QSaveFile>

#include "tomcrypt.h"

namespace {
constexpr int SaltBytes = 16, IvBytes = 16, KeyBytes = 32;
const QByteArray Magic = "MIBNAV-VAULT-1";

QByteArray randomBytes(int count)
{
    QByteArray bytes(count, Qt::Uninitialized);
    QRandomGenerator::system()->fillRange(reinterpret_cast<quint32 *>(bytes.data()), count / 4);
    return bytes;
}

// 64 bytes: the first half encrypts, the second half authenticates.
QByteArray deriveKeys(const QString &password, const QByteArray &salt, int iterations)
{
    return QPasswordDigestor::deriveKeyPbkdf2(QCryptographicHash::Sha256, password.toUtf8(), salt,
                                              iterations, KeyBytes * 2);
}

QByteArray macOf(const QByteArray &macKey, int iterations, const QByteArray &salt,
                 const QByteArray &iv, const QByteArray &cipher)
{
    QMessageAuthenticationCode mac(QCryptographicHash::Sha256, macKey);
    mac.addData(Magic);
    mac.addData(QByteArray::number(iterations));
    mac.addData(salt); mac.addData(iv); mac.addData(cipher);
    return mac.result();
}

bool sameBytes(const QByteArray &a, const QByteArray &b)
{
    if (a.size() != b.size()) return false;
    unsigned char difference = 0;
    for (int i = 0; i < a.size(); ++i) difference |= static_cast<unsigned char>(a[i] ^ b[i]);
    return difference == 0;
}

bool aesCbc(bool encrypt, const QByteArray &key, const QByteArray &iv, const QByteArray &input, QByteArray *output)
{
    if (register_cipher(&aes_desc) == -1) return false;
    const int cipher = find_cipher("aes");
    if (cipher < 0 || input.size() % 16 != 0) return false;
    symmetric_CBC cbc;
    if (cbc_start(cipher, reinterpret_cast<const unsigned char *>(iv.constData()),
                  reinterpret_cast<const unsigned char *>(key.constData()), key.size(), 0, &cbc) != CRYPT_OK)
        return false;
    output->resize(input.size());
    const int status = encrypt
        ? cbc_encrypt(reinterpret_cast<const unsigned char *>(input.constData()),
                      reinterpret_cast<unsigned char *>(output->data()), input.size(), &cbc)
        : cbc_decrypt(reinterpret_cast<const unsigned char *>(input.constData()),
                      reinterpret_cast<unsigned char *>(output->data()), input.size(), &cbc);
    cbc_done(&cbc);
    return status == CRYPT_OK;
}

QByteArray pad(QByteArray data)
{
    const int fill = 16 - data.size() % 16;
    return data.append(QByteArray(fill, static_cast<char>(fill)));
}

bool unpad(QByteArray *data)
{
    if (data->isEmpty()) return false;
    const int fill = static_cast<unsigned char>(data->back());
    if (fill < 1 || fill > 16 || fill > data->size()) return false;
    data->chop(fill);
    return true;
}

QByteArray encode(const QHash<QString, QByteArray> &secrets)
{
    QJsonObject object;
    for (auto it = secrets.cbegin(); it != secrets.cend(); ++it)
        object.insert(it.key(), QString::fromLatin1(it.value().toBase64()));
    return QJsonDocument(QJsonObject{{"secrets", object}}).toJson(QJsonDocument::Compact);
}

bool decode(const QByteArray &json, QHash<QString, QByteArray> *secrets)
{
    QJsonParseError parse;
    const auto document = QJsonDocument::fromJson(json, &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject()) return false;
    const auto object = document.object().value("secrets").toObject();
    secrets->clear();
    for (auto it = object.begin(); it != object.end(); ++it) {
        const QByteArray encoded = it.value().toString().toLatin1();
        const QByteArray bytes = QByteArray::fromBase64(encoded);
        if (bytes.toBase64() != encoded) return false;
        secrets->insert(it.key(), bytes);
    }
    return true;
}
}

CredentialVault::CredentialVault(const QString &name, int rounds) : fileName(name), iterations(rounds) {}

bool CredentialVault::exists() const { return QFile::exists(fileName); }

void CredentialVault::lock()
{
    for (auto it = values.begin(); it != values.end(); ++it) it->fill('\0');
    values.clear(); sessionPassword.fill(QChar(0)); sessionPassword.clear(); unlocked = false;
}

bool CredentialVault::write(const QString &password, const QHash<QString, QByteArray> &secrets)
{
    const QByteArray salt = randomBytes(SaltBytes), iv = randomBytes(IvBytes);
    const QByteArray keys = deriveKeys(password, salt, iterations);
    QByteArray cipher;
    if (keys.size() != KeyBytes * 2 || !aesCbc(true, keys.left(KeyBytes), iv, pad(encode(secrets)), &cipher)) {
        lastError = QObject::tr("The credential vault could not be encrypted."); return false;
    }
    const QByteArray mac = macOf(keys.mid(KeyBytes), iterations, salt, iv, cipher);
    const QJsonObject envelope{
        {"format", QString::fromLatin1(Magic)}, {"kdf", "pbkdf2-hmac-sha256"}, {"cipher", "aes-256-cbc+hmac-sha256"},
        {"iterations", iterations}, {"salt", QString::fromLatin1(salt.toBase64())},
        {"iv", QString::fromLatin1(iv.toBase64())}, {"data", QString::fromLatin1(cipher.toBase64())},
        {"mac", QString::fromLatin1(mac.toBase64())}};
    QSaveFile file(fileName);
    if (!file.open(QIODevice::WriteOnly) || !file.setPermissions(QFile::ReadOwner | QFile::WriteOwner)) {
        lastError = QObject::tr("The credential vault file could not be written."); return false;
    }
    const QByteArray bytes = QJsonDocument(envelope).toJson();
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        lastError = QObject::tr("The credential vault file could not be written."); return false;
    }
    return true;
}

bool CredentialVault::create(const QString &password, const QHash<QString, QByteArray> &secrets)
{
    lastError.clear();
    if (password.isEmpty()) { lastError = QObject::tr("Choose a master password."); return false; }
    if (exists()) { lastError = QObject::tr("Credentials are already protected."); return false; }
    if (!write(password, secrets)) return false;
    values = secrets; sessionPassword = password; unlocked = true;
    return true;
}

bool CredentialVault::unlock(const QString &password)
{
    lastError.clear();
    if (unlocked) return true;
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly)) { lastError = QObject::tr("The credential vault could not be opened."); return false; }
    const auto document = QJsonDocument::fromJson(file.readAll());
    const auto envelope = document.object();
    const int rounds = envelope.value("iterations").toInt();
    const QByteArray salt = QByteArray::fromBase64(envelope.value("salt").toString().toLatin1());
    const QByteArray iv = QByteArray::fromBase64(envelope.value("iv").toString().toLatin1());
    const QByteArray cipher = QByteArray::fromBase64(envelope.value("data").toString().toLatin1());
    const QByteArray mac = QByteArray::fromBase64(envelope.value("mac").toString().toLatin1());
    if (envelope.value("format").toString().toLatin1() != Magic || rounds < 1000 || rounds > 50000000 ||
        salt.size() != SaltBytes || iv.size() != IvBytes || cipher.isEmpty() || cipher.size() % 16 != 0) {
        lastError = QObject::tr("The credential vault is damaged or from a newer version. It was left untouched.");
        return false;
    }
    const QByteArray keys = deriveKeys(password, salt, rounds);
    if (keys.size() != KeyBytes * 2 || !sameBytes(mac, macOf(keys.mid(KeyBytes), rounds, salt, iv, cipher))) {
        lastError = QObject::tr("The master password is incorrect, or the vault is damaged."); return false;
    }
    QByteArray plain; QHash<QString, QByteArray> decoded;
    if (!aesCbc(false, keys.left(KeyBytes), iv, cipher, &plain) || !unpad(&plain) || !decode(plain, &decoded)) {
        lastError = QObject::tr("The credential vault is damaged. It was left untouched."); return false;
    }
    plain.fill('\0');
    values = decoded; sessionPassword = password; iterations = rounds; unlocked = true;
    return true;
}

bool CredentialVault::store(const QHash<QString, QByteArray> &secrets)
{
    lastError.clear();
    if (!unlocked) { lastError = QObject::tr("The credential vault is locked."); return false; }
    if (!write(sessionPassword, secrets)) return false;   // the old file stays if this fails
    values = secrets;
    return true;
}

bool CredentialVault::changePassword(const QString &currentPassword, const QString &newPassword)
{
    lastError.clear();
    if (newPassword.isEmpty()) { lastError = QObject::tr("Choose a new master password."); return false; }
    if (!unlocked || currentPassword != sessionPassword) {
        lastError = QObject::tr("The current master password is incorrect."); return false;
    }
    if (!write(newPassword, values)) return false;
    sessionPassword = newPassword;
    return true;
}

bool CredentialVault::removeFile()
{
    lastError.clear();
    if (!unlocked) { lastError = QObject::tr("The credential vault is locked."); return false; }
    if (QFile::exists(fileName) && !QFile::remove(fileName)) {
        lastError = QObject::tr("The credential vault file could not be removed."); return false;
    }
    lock();
    return true;
}
