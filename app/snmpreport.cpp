#include "snmpreport.h"

#include <QObject>

namespace {
// usmStats (1.3.6.1.6.3.15.1.1.N.0) and snmpMPDStats (1.3.6.1.6.3.11.2.1.N.0)
bool under(const Oid &oid, const char *prefix, int *leaf)
{
    const Oid base(prefix);
    if (oid.len() < base.len() + 1 || oid.nCompare(base.len(), base) != 0) return false;
    *leaf = int(oid[base.len()]);
    return true;
}
}

QString ExplainSnmpReport(const Pdu &pdu, const QString &securityName)
{
    Vb first;
    const bool hasVarbind = pdu.get_vb_count() > 0 && pdu.get_vb(first, 0);
    Oid oid;
    if (hasVarbind) first.get_oid(oid);
    const QString user = securityName.isEmpty() ? QObject::tr("the user") : QObject::tr("'%1'").arg(securityName);
    int leaf = 0;

    if (hasVarbind && under(oid, "1.3.6.1.6.3.15.1.1", &leaf)) {
        switch (leaf) {
        case 1:
            return QObject::tr("The device refused the security level (usmStatsUnsupportedSecLevels). "
                "%1 is not allowed to use this level on the device. Check that the Credential's "
                "authentication and privacy settings match how the user is set up on the device.").arg(user);
        case 2:
            return QObject::tr("The device says the request was outside its time window (usmStatsNotInTimeWindows). "
                "This usually clears when you try again; if it keeps happening, the device's SNMP engine clock restarted.");
        case 3:
            return QObject::tr("The device does not know the SNMPv3 user %1 (usmStatsUnknownUserNames). "
                "Check the user name in the Credential.").arg(user);
        case 4:
            return QObject::tr("The device did not recognise the SNMPv3 engine ID (usmStatsUnknownEngineIDs). "
                "Try again; if it persists, the device may have been reset or replaced.");
        case 5:
            return QObject::tr("The device rejected the authentication (usmStatsWrongDigests). The user %1 may exist, "
                "but the authentication passphrase or algorithm (MD5, SHA-1, SHA-2) does not match what is configured "
                "on the device. Check both on the device and in the Credential. The number shown for this error "
                "counts every rejected request, so it rises each time you try.").arg(user);
        case 6:
            return QObject::tr("The device could not decrypt the request (usmStatsDecryptionErrors). Authentication was "
                "accepted, but the privacy passphrase or algorithm (AES, DES) for %1 does not match the device.").arg(user);
        default: break;
        }
    }
    if (hasVarbind && under(oid, "1.3.6.1.6.3.11.2.1", &leaf)) {
        switch (leaf) {
        case 1: return QObject::tr("The device does not support this SNMPv3 security model (snmpUnknownSecurityModels).");
        case 2: return QObject::tr("The device could not parse the request (snmpInvalidMessages).");
        case 3: return QObject::tr("The device has no handler for this kind of request (snmpUnknownPDUHandlers).");
        default: break;
        }
    }
    if (pdu.get_type() == sNMP_PDU_REPORT)
        return QObject::tr("The device sent an SNMPv3 report instead of an answer%1. The request was not carried out.")
            .arg(hasVarbind ? QObject::tr(" (%1)").arg(QString::fromLatin1(oid.get_printable())) : QString());
    return {};
}
