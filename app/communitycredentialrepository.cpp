#include "communitycredentialrepository.h"
#include <QFile>
#include <QSettings>

CommunityCredentialRepository::CommunityCredentialRepository(const QString &name)
    : fileName(name) {}

QList<CommunityCredentialRecord> CommunityCredentialRepository::load() const
{
    QList<CommunityCredentialRecord> result;
    if (!QFile::exists(fileName)) return result;
    QSettings settings(fileName, QSettings::IniFormat);
    if (settings.value("schema/version").toInt() != CurrentVersion) return result;
    const int size = settings.beginReadArray("credentials");
    for (int i = 0; i < size; ++i)
    {
        settings.setArrayIndex(i);
        CommunityCredentialRecord record;
        record.identity = {settings.value("credentialId").toString(),
                           CredentialKind::Community};
        record.displayName = settings.value("displayName").toString();
        record.readCommunity = CredentialSecret(
            settings.value("readCommunity").toByteArray());
        record.writeCommunity = CredentialSecret(
            settings.value("writeCommunity").toByteArray());
        if (!record.identity.credentialId.isEmpty()) result.append(record);
    }
    settings.endArray();
    return result;
}

bool CommunityCredentialRepository::save(
    const QList<CommunityCredentialRecord> &records) const
{
    QSettings settings(fileName, QSettings::IniFormat);
    settings.clear(); settings.setValue("schema/version", CurrentVersion);
    settings.beginWriteArray("credentials");
    for (int i = 0; i < records.size(); ++i)
    {
        settings.setArrayIndex(i);
        settings.setValue("credentialId", records[i].identity.credentialId);
        settings.setValue("displayName", records[i].displayName);
        settings.setValue("readCommunity", records[i].readCommunity.bytes());
        settings.setValue("writeCommunity", records[i].writeCommunity.bytes());
    }
    settings.endArray(); settings.sync();
    return settings.status() == QSettings::NoError;
}
