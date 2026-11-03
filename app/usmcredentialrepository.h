#ifndef USMCREDENTIALREPOSITORY_H
#define USMCREDENTIALREPOSITORY_H

#include <QList>
#include <QString>
#include "credentialrecords.h"

struct UsmCredentialIdentityRecord
{
    QString credentialId;
    QString securityName;
    QString displayName;
};

class UsmCredentialRepository
{
public:
    static constexpr int CurrentVersion = 1;
    explicit UsmCredentialRepository(const QString &fileName);

    QList<UsmCredentialIdentityRecord> load() const;
    bool save(const QList<UsmCredentialIdentityRecord> &records) const;
    QList<UsmCredentialRecord> loadRecords(QString *error) const;
    bool saveRecords(const QList<UsmCredentialRecord> &records) const;
    bool hasRecordStore() const;
    QString recordPath() const { return fileName + QStringLiteral(".v2.json"); }
    QString path() const { return fileName; }

private:
    QString fileName;
};

#endif
