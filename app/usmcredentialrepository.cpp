#include "usmcredentialrepository.h"

#include <QFile>
#include <QSettings>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>

UsmCredentialRepository::UsmCredentialRepository(const QString &name)
    : fileName(name) {}

QList<UsmCredentialIdentityRecord> UsmCredentialRepository::load() const
{
    QList<UsmCredentialIdentityRecord> result;
    if (!QFile::exists(fileName)) return result;
    QSettings settings(fileName, QSettings::IniFormat);
    if (settings.value("schema/version").toInt() != CurrentVersion) return result;
    const int size = settings.beginReadArray("credentials");
    for (int i = 0; i < size; ++i)
    {
        settings.setArrayIndex(i);
        UsmCredentialIdentityRecord record{settings.value("credentialId").toString(),
                                           settings.value("securityName").toString(), settings.value("displayName").toString()};
        if (!record.credentialId.isEmpty() && !record.securityName.isEmpty())
            result.append(record);
    }
    settings.endArray();
    return result;
}

bool UsmCredentialRepository::save(
    const QList<UsmCredentialIdentityRecord> &records) const
{
    QSettings settings(fileName, QSettings::IniFormat);
    settings.clear();
    settings.setValue("schema/version", CurrentVersion);
    settings.beginWriteArray("credentials");
    for (int i = 0; i < records.size(); ++i)
    {
        settings.setArrayIndex(i);
        settings.setValue("credentialId", records[i].credentialId);
        settings.setValue("securityName", records[i].securityName);
        settings.setValue("displayName", records[i].displayName);
    }
    settings.endArray();
    settings.sync();
    return settings.status() == QSettings::NoError;
}

bool UsmCredentialRepository::hasRecordStore() const { return QFile::exists(recordPath()); }

QList<UsmCredentialRecord> UsmCredentialRepository::loadRecords(QString *error) const
{
    QList<UsmCredentialRecord> result;
    if (error) error->clear();
    const auto fail = [error]() {
        if (error) *error = QObject::tr("Saved SNMPv3 credentials could not be read. The file has been left untouched.");
        return QList<UsmCredentialRecord>{};
    };
    QFile file(recordPath());
    if (!file.open(QIODevice::ReadOnly)) return fail();
    QJsonParseError parse;
    const auto doc = QJsonDocument::fromJson(file.readAll(), &parse);
    if (parse.error != QJsonParseError::NoError || !doc.isObject()) return fail();
    const auto root = doc.object();
    if (root.value("schema").toInt() != 2 || !root.value("credentials").isArray()) return fail();
    QSet<QString> ids;
    for (const auto &value : root.value("credentials").toArray()) {
        const auto o = value.toObject();
        UsmCredentialRecord record;
        record.identity = {o.value("id").toString(), CredentialKind::Usm};
        record.displayName = o.value("name").toString();
        record.securityName = o.value("user").toString();
        record.authProtocol = o.value("authProtocol").toInt(-1);
        record.privacyProtocol = o.value("privacyProtocol").toInt(-1);
        const QByteArray auth = o.value("authSecret").toString().toLatin1();
        const QByteArray priv = o.value("privacySecret").toString().toLatin1();
        const auto authBytes = QByteArray::fromBase64(auth), privBytes = QByteArray::fromBase64(priv);
        if (record.identity.credentialId.isEmpty() || ids.contains(record.identity.credentialId) ||
            record.displayName.trimmed().isEmpty() || record.securityName.trimmed().isEmpty() ||
            record.authProtocol < 0 || record.privacyProtocol < 0 ||
            authBytes.toBase64() != auth || privBytes.toBase64() != priv) return fail();
        ids.insert(record.identity.credentialId);
        record.authSecret = CredentialSecret(authBytes); record.privacySecret = CredentialSecret(privBytes);
        result.append(record);
    }
    return result;
}

bool UsmCredentialRepository::saveRecords(const QList<UsmCredentialRecord> &records) const
{
    QJsonArray entries;
    for (const auto &r : records) entries.append(QJsonObject{
        {"id", r.identity.credentialId}, {"name", r.displayName}, {"user", r.securityName},
        {"authProtocol", r.authProtocol}, {"privacyProtocol", r.privacyProtocol},
        {"authSecret", QString::fromLatin1(r.authSecret.bytes().toBase64())},
        {"privacySecret", QString::fromLatin1(r.privacySecret.bytes().toBase64())}});
    const QByteArray data = QJsonDocument(QJsonObject{{"schema", 2}, {"credentials", entries}}).toJson();
    QSaveFile file(recordPath());
    if (!file.open(QIODevice::WriteOnly)) return false;
#ifndef Q_OS_WIN
    // Owner-only access. Windows has no such mode bits; the profile folder's ACL applies there.
    if (!file.setPermissions(QFile::ReadOwner | QFile::WriteOwner)) return false;
#endif
    return file.write(data) == data.size() && file.commit();
}
