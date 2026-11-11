#include "snmpreport.h"

#include "snmprequestconfig.h"

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
    // snmpTargetMIB: snmpUnavailableContexts (.4) and snmpUnknownContexts (.5)
    if (hasVarbind && under(oid, "1.3.6.1.6.3.12.1", &leaf) && (leaf == 4 || leaf == 5)) {
        return QObject::tr("The device does not have the SNMPv3 context this request named (%1). The context name "
            "comes from Preferences; leave it empty unless the device needs a specific one, and if one is set, check "
            "its spelling against the device.")
            .arg(leaf == 4 ? QObject::tr("snmpUnavailableContexts") : QObject::tr("snmpUnknownContexts"));
    }
    if (pdu.get_type() == sNMP_PDU_REPORT)
        return QObject::tr("The device sent an SNMPv3 report instead of an answer%1. The request was not carried out.")
            .arg(hasVarbind ? QObject::tr(" (%1)").arg(QString::fromLatin1(oid.get_printable())) : QString());
    return {};
}

QString ExplainSnmpTimeout(const SnmpRequestConfig &config)
{
    if (config.version != SnmpRequestVersion::V3) return {};
    const QString user = config.securityName.isEmpty() ? QObject::tr("(none)") : QObject::tr("'%1'").arg(config.securityName);
    QString level;
    switch (config.securityLevel) {
    case SNMP_SECURITY_LEVEL_AUTH_PRIV: level = QObject::tr("authentication and privacy"); break;
    case SNMP_SECURITY_LEVEL_AUTH_NOPRIV: level = QObject::tr("authentication without privacy"); break;
    default: level = QObject::tr("no authentication or privacy"); break;
    }
    QString where = config.address;
    if (!config.port.isEmpty()) where += QStringLiteral(":") + config.port;
    QString text = QObject::tr("Timeout: no response from the device (%1, SNMPv3, user %2, %3). "
        "The device sent no report at all, so the exact cause cannot be known. Most likely the address or port is wrong "
        "or blocked by a firewall or access list, SNMPv3 is not enabled on the device, or the user does not exist there "
        "(many devices silently ignore unknown users).")
        .arg(where, user, level);
    if (!config.contextName.isEmpty())
        text += QObject::tr(" The SNMPv3 context name '%1' from Preferences is also in use; if the device does not "
                            "know it, clear it.").arg(config.contextName);
    text += QObject::tr(" If the device had recognised the user but rejected the passwords, it would normally answer "
                        "with a report, which this application explains.");
    return text;
}

bool IsSnmpErrorStatusCode(int code)
{
    return code >= SNMP_ERROR_TOO_BIG && code <= SNMP_ERROR_INCONSIS_NAME;
}

QString ExplainSnmpTransportFailure(int transportStatus, const QString &securityName)
{
    const QString user = securityName.isEmpty() ? QObject::tr("the user") : QObject::tr("'%1'").arg(securityName);
    switch (transportStatus) {
    case SNMPv3_USM_DECRYPTION_ERROR:
        return QObject::tr("The device's reply could not be decrypted (SNMPv3 decryption error). The privacy "
            "passphrase or algorithm (AES, DES) for %1 likely does not match the device.").arg(user);
    case SNMPv3_USM_UNKNOWN_SECURITY_NAME:
        return QObject::tr("The SNMPv3 user %1 is not set up in this application's credentials "
            "(unknown security name). Check the user name in the Credential.").arg(user);
    case SNMPv3_USM_AUTHENTICATION_FAILURE:
    case SNMPv3_USM_AUTHENTICATION_ERROR:
        return QObject::tr("SNMPv3 authentication failed. The authentication passphrase or algorithm "
            "(MD5, SHA-1, SHA-2) for %1 likely does not match the device. Check both on the device and "
            "in the Credential.").arg(user);
    default:
        return QObject::tr("The request could not be completed: %1 (SNMP++ code %2).")
            .arg(QString::fromLatin1(Snmp::error_msg(transportStatus))).arg(transportStatus);
    }
}

QString ExplainSnmpErrorStatus(int errorStatus)
{
    QString text;
    switch (errorStatus) {
    case SNMP_ERROR_TOO_BIG: text = QObject::tr("tooBig: the answer would not fit in one message"); break;
    case SNMP_ERROR_NO_SUCH_NAME: text = QObject::tr("noSuchName: the device does not have this object"); break;
    case SNMP_ERROR_BAD_VALUE: text = QObject::tr("badValue: the device does not accept this value (SNMPv1 reports every rejected value this way)"); break;
    case SNMP_ERROR_READ_ONLY: text = QObject::tr("readOnly: this object cannot be changed"); break;
    case SNMP_ERROR_GENERAL_VB_ERR: text = QObject::tr("genErr: the device failed to carry out the request"); break;
    case SNMP_ERROR_NO_ACCESS: text = QObject::tr("noAccess: this community or user is not allowed to change this object"); break;
    case SNMP_ERROR_WRONG_TYPE: text = QObject::tr("wrongType: the value has the wrong data type for this object"); break;
    case SNMP_ERROR_WRONG_LENGTH: text = QObject::tr("wrongLength: the value has the wrong length for this object"); break;
    case SNMP_ERROR_WRONG_ENCODING: text = QObject::tr("wrongEncoding: the value was encoded incorrectly"); break;
    case SNMP_ERROR_WRONG_VALUE: text = QObject::tr("wrongValue: the device does not accept this value for this object"); break;
    case SNMP_ERROR_NO_CREATION: text = QObject::tr("noCreation: the device cannot create this object"); break;
    case SNMP_ERROR_INCONSIST_VAL: text = QObject::tr("inconsistentValue: this value conflicts with the device's current settings"); break;
    case SNMP_ERROR_RESOURCE_UNAVAIL: text = QObject::tr("resourceUnavailable: the device has no resources to carry this out right now"); break;
    case SNMP_ERROR_COMITFAIL: text = QObject::tr("commitFailed: the device could not apply the change"); break;
    case SNMP_ERROR_UNDO_FAIL: text = QObject::tr("undoFailed: the device could not undo a partly applied change"); break;
    case SNMP_ERROR_AUTH_ERR: text = QObject::tr("authorizationError: the credential is not authorised to change this object"); break;
    case SNMP_ERROR_NOT_WRITEABLE: text = QObject::tr("notWritable: this object cannot be changed"); break;
    case SNMP_ERROR_INCONSIS_NAME: text = QObject::tr("inconsistentName: this object cannot be created or changed in the device's current state"); break;
    default: return QObject::tr("The device reported an error (SNMP error %1)").arg(errorStatus);
    }
    return QObject::tr("%1 (SNMP error %2)").arg(text).arg(errorStatus);
}
