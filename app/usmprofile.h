#ifndef USMPROFILE_H
#define USMPROFILE_H

#include <QObject>
#include <QStringList>
#include "credentialrecords.h"

class Snmpb;
class UsmCredentialService;

// Compatibility entry point for the existing application. Credential data belongs to the
// service; the unified CredentialsDialog owns its editable working copies.
class USMProfileManager : public QObject
{
    Q_OBJECT
public:
    explicit USMProfileManager(Snmpb *snmpb);
    UsmCredentialService *Credentials() const { return credentialService; }
    QStringList GetUsersList();
    bool RefreshRuntime();
signals:
    void CredentialsChanged();
private:
    Snmpb *application;
    UsmCredentialService *credentialService;
    QList<UsmCredentialRecord> registeredRecords;
    bool runtimeInitialized = false;
};
#endif
