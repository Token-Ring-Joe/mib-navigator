#ifndef USMCREDENTIALSERVICE_H
#define USMCREDENTIALSERVICE_H

#include "credentialrecords.h"
#include "usmcredentialrepository.h"
#include <QObject>

class UsmCredentialService : public QObject
{
    Q_OBJECT
public:
    UsmCredentialService(const QList<UsmCredentialRecord> &legacyRecords,
                         const UsmCredentialRepository &repository,
                         QObject *parent = nullptr);

    const QList<UsmCredentialRecord> &records() const;
    bool saveIdentities() const;
    bool rename(const QString &credentialId, const QString &securityName);
    bool remove(const QString &credentialId);
    const UsmCredentialRecord *find(const QString &credentialId) const;
    UsmCredentialRecord createWorkingRecord(const QString &securityName) const;
    bool validateWorkingCopy(const QList<UsmCredentialRecord> &records) const;
    static int securityLevel(const UsmCredentialRecord &record);
    static bool requirementsSatisfied(const UsmCredentialRecord &record);
    void applyCommitted(const QList<UsmCredentialRecord> &records);
    bool isSecurityNameUnambiguous(const QString &securityName) const;

signals:
    void credentialCreated(const QString &credentialId);
    void credentialUpdated(const QString &credentialId);
    void credentialRenamed(const QString &credentialId,
                           const QString &oldName, const QString &newName);
    void credentialDeleted(const QString &credentialId);
    void credentialsReloaded();
    void credentialsChanged();

private:
    static QString createId();
    int indexOfId(const QString &credentialId) const;
    QList<UsmCredentialRecord> credentials;
    UsmCredentialRepository repository;
};

#endif
