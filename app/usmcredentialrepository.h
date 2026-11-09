#ifndef USMCREDENTIALREPOSITORY_H
#define USMCREDENTIALREPOSITORY_H

#include <QList>
#include <QString>

struct UsmCredentialIdentityRecord
{
    QString credentialId;
    QString securityName;
};

class UsmCredentialRepository
{
public:
    static constexpr int CurrentVersion = 1;
    explicit UsmCredentialRepository(const QString &fileName);

    QList<UsmCredentialIdentityRecord> load() const;
    bool save(const QList<UsmCredentialIdentityRecord> &records) const;
    QString path() const { return fileName; }

private:
    QString fileName;
};

#endif
