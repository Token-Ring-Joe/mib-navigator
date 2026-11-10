#include "communitycredentialrepository.h"
#include <QFile>
#include <QSettings>
#include <QSaveFile>
#include <QFileInfo>
#include <QSet>
#include <QUuid>

CommunityCredentialRepository::CommunityCredentialRepository(const QString &name)
    : fileName(name) {}

QString CommunityCredentialRepository::lockedMessage()
{
    return QObject::tr("Saved credentials are locked. Enter the master password to unlock them.");
}

bool CommunityCredentialRepository::isFileProtected() const
{
    if (!QFile::exists(fileName)) return false;
    QSettings settings(fileName, QSettings::IniFormat);
    return settings.value("schema/protected", false).toBool();
}

QList<CommunityCredentialRecord> CommunityCredentialRepository::load(QString *error) const
{
    if (error) error->clear();
    QList<CommunityCredentialRecord> result;
    if (!QFile::exists(fileName)) return result;
    QSettings settings(fileName, QSettings::IniFormat);
    if (settings.status() != QSettings::NoError || settings.value("schema/version").toInt() != CurrentVersion) {
        if (error) *error = QObject::tr("Saved community credentials could not be read. The file has been left untouched.");
        return {};
    }
    const bool protectedFile = settings.value("schema/protected", false).toBool();
    if (protectedFile && (!secretStore || !secretStore->isUnlocked())) {
        if (error) *error = lockedMessage();
        return {};
    }
    QSet<QString> ids;
    const int size = settings.beginReadArray("credentials");
    for (int i = 0; i < size; ++i)
    {
        settings.setArrayIndex(i);
        CommunityCredentialRecord record;
        record.identity = {settings.value("credentialId").toString(),
                           CredentialKind::Community};
        record.displayName = settings.value("displayName").toString();
        record.protocol = settings.value("protocol", -1).toInt();
        if (protectedFile) {
            // Every protected record has both entries (an empty community is stored as empty).
            const auto &all = secretStore->secrets();
            const QString readKey = CredentialSecretKeys::community(record.identity.credentialId, QStringLiteral("read"));
            const QString writeKey = CredentialSecretKeys::community(record.identity.credentialId, QStringLiteral("write"));
            if (!all.contains(readKey) || !all.contains(writeKey)) {
                if (error) *error = QObject::tr("The saved credentials do not match the protected secrets. The file has been left untouched.");
                return {};
            }
            record.readCommunity = CredentialSecret(all.value(readKey));
            record.writeCommunity = CredentialSecret(all.value(writeKey));
        } else {
            record.readCommunity = CredentialSecret(settings.value("readCommunity").toByteArray());
            record.writeCommunity = CredentialSecret(settings.value("writeCommunity").toByteArray());
        }
        if (record.identity.credentialId.isEmpty() || ids.contains(record.identity.credentialId) ||
            record.protocol < -1 || record.protocol > 1) {
            if (error) *error = QObject::tr("Saved community credentials are invalid. The file has been left untouched.");
            return {};
        }
        ids.insert(record.identity.credentialId); result.append(record);
    }
    settings.endArray();
    return result;
}

bool CommunityCredentialRepository::save(
    const QList<CommunityCredentialRecord> &records) const
{
    if (!secretStore) return writeFile(records, false);
    // Secrets go to the store first; if the file then cannot be written the store is put back,
    // so the two never describe different credentials.
    QHash<QString, QByteArray> mine, previous;
    for (const auto &r : records) {
        mine.insert(CredentialSecretKeys::community(r.identity.credentialId, QStringLiteral("read")), r.readCommunity.bytes());
        mine.insert(CredentialSecretKeys::community(r.identity.credentialId, QStringLiteral("write")), r.writeCommunity.bytes());
    }
    if (!credentialStoreReplace(*secretStore, CredentialSecretKeys::communityPrefix(), mine, &previous)) return false;
    if (writeFile(records, true)) return true;
    secretStore->store(previous);
    return false;
}

bool CommunityCredentialRepository::writeFile(
    const QList<CommunityCredentialRecord> &records, bool withoutSecrets) const
{
    // QSettings would quietly create a missing folder; a store whose folder is gone is an error.
    if (!QFileInfo(QFileInfo(fileName).absolutePath()).isDir()) return false;
    // QSettings writes a scratch file next to the store (a name nothing else holds open); the
    // bytes are then committed atomically over the real file.
    const QString temporaryName = QFileInfo(fileName).absolutePath() + QStringLiteral("/.credentials-") +
        QUuid::createUuid().toString(QUuid::Id128) + QStringLiteral(".tmp");
    struct Scratch { QString name; ~Scratch() { QFile::remove(name); } } scratch{temporaryName};
    {
        QSettings settings(temporaryName, QSettings::IniFormat);
        settings.setValue("schema/version", CurrentVersion);
        if (withoutSecrets) settings.setValue("schema/protected", true);
        settings.beginWriteArray("credentials");
        for (int i = 0; i < records.size(); ++i) {
            settings.setArrayIndex(i);
            settings.setValue("credentialId", records[i].identity.credentialId);
            settings.setValue("displayName", records[i].displayName);
            settings.setValue("protocol", records[i].protocol);
            if (!withoutSecrets) {
                settings.setValue("readCommunity", records[i].readCommunity.bytes());
                settings.setValue("writeCommunity", records[i].writeCommunity.bytes());
            }
        }
        settings.endArray(); settings.sync();
        if (settings.status() != QSettings::NoError) return false;
    }
    QFile source(temporaryName);
    if (!source.open(QIODevice::ReadOnly)) return false;
    const auto bytes = source.readAll();
    QSaveFile destination(fileName);
    if (!destination.open(QIODevice::WriteOnly)) return false;
#ifndef Q_OS_WIN
    // Owner-only access. Windows has no such mode bits; the profile folder's ACL applies there.
    if (!destination.setPermissions(QFile::ReadOwner | QFile::WriteOwner)) return false;
#endif
    return destination.write(bytes) == bytes.size() && destination.commit();
}
