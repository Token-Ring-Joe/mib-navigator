#include "usmcredentialruntime.h"

#include "snmp_pp/usm_v3.h"

QList<UsmCredentialRecord> UsmCredentialRuntimeRepository::snapshot(
    USM *usm)
{
    QList<UsmCredentialRecord> result;
    if (!usm) return result;
    const UsmUserNameTableEntry *entry = usm->peek_first_user();
    while (entry)
    {
        UsmCredentialRecord record;
        record.identity.kind = CredentialKind::Usm;
        record.securityName = QString::fromLatin1(
            entry->usmUserSecurityName.get_printable());
        record.displayName = record.securityName;
        record.authProtocol = static_cast<int>(entry->usmUserAuthProtocol);
        record.authSecret = CredentialSecret(QByteArray(
            reinterpret_cast<const char *>(entry->authPassword),
            static_cast<qsizetype>(entry->authPasswordLength)));
        record.privacyProtocol = static_cast<int>(entry->usmUserPrivProtocol);
        record.privacySecret = CredentialSecret(QByteArray(
            reinterpret_cast<const char *>(entry->privPassword),
            static_cast<qsizetype>(entry->privPasswordLength)));
        result.append(record);
        entry = usm->peek_next_user(entry);
    }
    return result;
}

int UsmCredentialRuntimeRepository::replaceAndSave(
    USM *usm, const QList<UsmCredentialRecord> &records,
    const QString &fileName)
{
    if (!usm) return SNMPv3_USM_ERROR;
    const UsmUserNameTableEntry *entry = usm->peek_first_user();
    while (entry)
    {
        const UsmUserNameTableEntry *current = entry;
        entry = usm->peek_next_user(current);
        usm->delete_usm_user(current->usmUserSecurityName);
    }
    for (const UsmCredentialRecord &record : records)
    {
        const QByteArray name = record.securityName.toLatin1();
        usm->add_usm_user(OctetStr(name.constData()), record.authProtocol,
                          record.privacyProtocol,
                          OctetStr(reinterpret_cast<const unsigned char *>(
                                       record.authSecret.bytes().constData()),
                                   record.authSecret.bytes().size()),
                          OctetStr(reinterpret_cast<const unsigned char *>(
                                       record.privacySecret.bytes().constData()),
                                   record.privacySecret.bytes().size()));
    }
    return usm->save_users(fileName.toLatin1().constData());
}
