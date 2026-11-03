#include "usmprofile.h"
#include "snmpb.h"
#include "agent.h"
#include "usmcredentialservice.h"
#include "usmcredentialruntime.h"
#include <QSet>

USMProfileManager::USMProfileManager(Snmpb *snmpb)
    : QObject(snmpb), application(snmpb)
{
    const auto legacy = UsmCredentialRuntimeRepository::snapshot(snmpb->AgentObj()->GetUSMObj());
    credentialService = new UsmCredentialService(legacy,
        UsmCredentialRepository(snmpb->GetCredentialIdentitiesConfigFile()), this);
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
    QList<UsmCredentialRecord> defaults, securityRecords;
    QSet<QString> names;
    for (auto r : credentialService->records()) {
        r.displayName.clear(); r.identity.credentialId.clear();
        securityRecords.append(r);
        if (names.contains(r.securityName)) continue;
        names.insert(r.securityName); defaults.append(r);
    }
    if (runtimeInitialized && securityRecords == registeredRecords) return true;
    USM *usm = application->AgentObj()->GetUSMObj();
    if (!usm) return false;
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
    registeredRecords = securityRecords; runtimeInitialized = true;
    return true;
}
