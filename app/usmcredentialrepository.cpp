#include "usmcredentialrepository.h"

#include <QFile>
#include <QSettings>

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
                                           settings.value("securityName").toString()};
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
    }
    settings.endArray();
    settings.sync();
    return settings.status() == QSettings::NoError;
}
