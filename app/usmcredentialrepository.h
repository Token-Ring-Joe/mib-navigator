#ifndef USMCREDENTIALREPOSITORY_H
#define USMCREDENTIALREPOSITORY_H

#include <QList>
#include <QString>
#include <memory>
#include "credentialsecretstore.h"
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
    // With a store attached, SNMPv3 secrets live there and the record file holds only names,
    // users and protocols. Reading such a file while the store is locked is refused.
    void setSecretStore(std::shared_ptr<CredentialSecretStore> store) { secretStore = std::move(store); }
    static QString lockedMessage();

private:
    bool writeRecords(const QList<UsmCredentialRecord> &records, bool withoutSecrets) const;
    QString fileName;
    std::shared_ptr<CredentialSecretStore> secretStore;
};

#endif
