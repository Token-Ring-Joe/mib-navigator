#include "usmprofile.h"
#include "snmpb.h"
#include "agent.h"
#include "usmcredentialservice.h"
#include "credentialprotection.h"
#include "usmcredentialruntime.h"
#include <QSet>

USMProfileManager::USMProfileManager(Snmpb *snmpb)
    : QObject(snmpb), application(snmpb)
{
    const auto legacy = UsmCredentialRuntimeRepository::snapshot(snmpb->AgentObj()->GetUSMObj());
    UsmCredentialRepository repository(snmpb->GetCredentialIdentitiesConfigFile());
    if (snmpb->Protection()) repository.setSecretStore(snmpb->Protection()->activeSecretStore());
    credentialService = new UsmCredentialService(legacy, repository, this);
    // Even a corrupt canonical store must not leave legacy users silently active.
    RefreshRuntime();
    connect(credentialService, &UsmCredentialService::credentialsChanged, this, [this]() {
        RefreshRuntime(); emit CredentialsChanged();
    });
}

QStringList USMProfileManager::GetUsersList()
{
    QStringList names;
    for (const auto &r : credentialService->records()) names << r.displayName;
    return names;
}

bool USMProfileManager::RefreshRuntime()
{
    // Unsolicited v3 notifications need a default per wire username. Preserve the first saved
    // variant; successful queries additionally install exact per-engine localized credentials.
    // A credential with a device engine ID is exact for that one sender: it is installed as the
    // localized entry for (engine ID, user) below and is neither the per-name default nor a
    // variant tried for other devices. Credentials without an engine ID behave as before.
    QList<UsmCredentialRecord> defaults, securityRecords;
    QSet<QString> names;
    for (auto r : credentialService->records()) {
        r.displayName.clear(); r.identity.credentialId.clear();
        securityRecords.append(r);
    }
    for (const auto &r : securityRecords) {
        if (!r.engineId.isEmpty() || names.contains(r.securityName)) continue;
        names.insert(r.securityName); defaults.append(r);
    }
    if (runtimeInitialized && securityRecords == registeredRecords) return true;
    USM *usm = application->AgentObj()->GetUSMObj();
    if (!usm) return false;
    const auto engineOctets = [](const UsmCredentialRecord &r) {
        return OctetStr(reinterpret_cast<const unsigned char *>(r.engineId.constData()), r.engineId.size());
    };
    // A device-bound user may have no entry in the per-name table, so remove its previous
    // localized entry explicitly.
    for (const auto &r : registeredRecords)
        if (!r.engineId.isEmpty())
            usm->delete_localized_user(engineOctets(r), OctetStr(r.securityName.toLatin1().constData()));
    auto entry = usm->peek_first_user();
    while (entry) {
        const OctetStr name = entry->usmUserSecurityName;
        usm->delete_usm_user(name); entry = usm->peek_first_user();
    }
    const auto secret = [](const CredentialSecret &s) {
        return OctetStr(reinterpret_cast<const unsigned char *>(s.bytes().constData()), s.bytes().size());
    };
    for (const auto &r : defaults)
        if (usm->add_usm_user(OctetStr(r.securityName.toLatin1().constData()), r.authProtocol,
                r.privacyProtocol, secret(r.authSecret), secret(r.privacySecret)) != SNMPv3_USM_OK) return false;
    // The user table keeps one entry per user name. Every saved variant is also registered so an
    // incoming notification that fails with the first one is checked against all of them.
    usm->clear_notification_variants();
    for (const auto &r : securityRecords) {
        if (!r.engineId.isEmpty()) {
            if (usm->add_usm_user(OctetStr(r.securityName.toLatin1().constData()),
                    OctetStr(r.securityName.toLatin1().constData()), r.authProtocol, r.privacyProtocol,
                    secret(r.authSecret), secret(r.privacySecret), engineOctets(r)) != SNMPv3_USM_OK) return false;
            continue;
        }
        usm->add_notification_variant(OctetStr(r.securityName.toLatin1().constData()), r.authProtocol,
                                      r.privacyProtocol, secret(r.authSecret), secret(r.privacySecret));
    }
    registeredRecords = securityRecords; runtimeInitialized = true;
    return true;
}
