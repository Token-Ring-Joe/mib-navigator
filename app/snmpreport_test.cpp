#include "snmpreport.h"
#include "snmprequestconfig.h"

#include <QCoreApplication>
#include <iostream>

namespace {
bool check(bool value, const char *message)
{ if (!value) std::cerr << "FAIL: " << message << '\n'; return value; }

Pdu reportWith(const char *oid, unsigned short type = sNMP_PDU_REPORT)
{
    Pdu pdu; pdu.set_type(type);
    Vb vb{Oid(oid)}; vb.set_value(Counter32(22)); pdu += vb;
    return pdu;
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    bool ok = true;
    const QString user = "synoptics";

    const QString wrong = ExplainSnmpReport(reportWith("1.3.6.1.6.3.15.1.1.5.0"), user);
    ok &= check(wrong.contains("rejected the authentication") && wrong.contains("'synoptics'") &&
                wrong.contains("passphrase or algorithm") && wrong.contains("usmStatsWrongDigests"),
                "wrong digest names the cause, the user and what to check");
    ok &= check(ExplainSnmpReport(reportWith("1.3.6.1.6.3.15.1.1.6.0"), user).contains("privacy passphrase"),
                "decryption error points at the privacy settings");
    ok &= check(ExplainSnmpReport(reportWith("1.3.6.1.6.3.15.1.1.3.0"), user).contains("does not know the SNMPv3 user 'synoptics'"),
                "unknown user");
    ok &= check(ExplainSnmpReport(reportWith("1.3.6.1.6.3.15.1.1.1.0"), user).contains("security level"), "unsupported security level");
    ok &= check(ExplainSnmpReport(reportWith("1.3.6.1.6.3.15.1.1.2.0"), user).contains("time window"), "not in time window");
    ok &= check(ExplainSnmpReport(reportWith("1.3.6.1.6.3.15.1.1.4.0"), user).contains("engine ID"), "unknown engine ID");
    ok &= check(ExplainSnmpReport(reportWith("1.3.6.1.6.3.11.2.1.3.0"), user).contains("no handler"), "snmpMPD counter");
    ok &= check(ExplainSnmpReport(reportWith("1.3.6.1.4.1.9.9.9.0"), user).contains("report instead of an answer"),
                "an unrecognised report is still explained, not shown as data");
    ok &= check(ExplainSnmpReport(reportWith("1.3.6.1.6.3.15.1.1.5.0"), {}).contains("the user"),
                "works without a user name");
    // The error counter can arrive in a PDU not typed as REPORT; it must still be recognised.
    ok &= check(!ExplainSnmpReport(reportWith("1.3.6.1.6.3.15.1.1.5.0", sNMP_PDU_RESPONSE), user).isEmpty(),
                "a usmStats counter in a response PDU is still an error");
    ok &= check(ExplainSnmpReport(reportWith("1.3.6.1.2.1.1.1.0", sNMP_PDU_RESPONSE), user).isEmpty(),
                "an ordinary response is not touched");
    Pdu empty; empty.set_type(sNMP_PDU_RESPONSE);
    ok &= check(ExplainSnmpReport(empty, user).isEmpty(), "an empty response is not a report");

    // ---- failures on this side of the wire (SNMP++ return codes) -----------------------------
    const QString decrypt = ExplainSnmpTransportFailure(SNMPv3_USM_DECRYPTION_ERROR, user);
    ok &= check(decrypt.contains("privacy passphrase or algorithm") && decrypt.contains("'synoptics'") &&
                !decrypt.contains("SNMP error"), "decryption error points at the privacy settings");
    const QString unknownUser = ExplainSnmpTransportFailure(SNMPv3_USM_UNKNOWN_SECURITY_NAME, user);
    ok &= check(unknownUser.contains("not set up in this application's credentials") && unknownUser.contains("'synoptics'"),
                "unknown security name says the user is not in the application's credentials");
    for (const int code : {SNMPv3_USM_AUTHENTICATION_FAILURE, SNMPv3_USM_AUTHENTICATION_ERROR})
        ok &= check(ExplainSnmpTransportFailure(code, user).contains("authentication passphrase or algorithm"),
                    "authentication failure and error point at the authentication settings");
    const QString other = ExplainSnmpTransportFailure(SNMPv3_USM_NOT_IN_TIME_WINDOW, {});
    ok &= check(other.contains(QString::fromLatin1(Snmp::error_msg(SNMPv3_USM_NOT_IN_TIME_WINDOW))) &&
                other.contains("1411") && other.contains("the user") == false,
                "any other code gets SNMP++'s own text and the code");
    ok &= check(!ExplainSnmpTransportFailure(0, {}).contains("SNMP error 0"), "never says 'SNMP error 0'");
    ok &= check(IsSnmpErrorStatusCode(1) && IsSnmpErrorStatusCode(2) && IsSnmpErrorStatusCode(18) &&
                !IsSnmpErrorStatusCode(0) && !IsSnmpErrorStatusCode(19) && !IsSnmpErrorStatusCode(-1) &&
                !IsSnmpErrorStatusCode(SNMPv3_USM_DECRYPTION_ERROR), "the SNMP error-status range is 1-18");
    // ---- the device's SNMP error-status in plain English -------------------------------------
    struct Status { int code; const char *name; const char *meaning; };
    for (const Status &st : {Status{SNMP_ERROR_TOO_BIG, "tooBig", "would not fit"},
                             Status{SNMP_ERROR_NO_SUCH_NAME, "noSuchName", "does not have this object"},
                             Status{SNMP_ERROR_BAD_VALUE, "badValue", "does not accept this value"},
                             Status{SNMP_ERROR_READ_ONLY, "readOnly", "cannot be changed"},
                             Status{SNMP_ERROR_GENERAL_VB_ERR, "genErr", "failed to carry out"},
                             Status{SNMP_ERROR_NO_ACCESS, "noAccess", "not allowed to change"},
                             Status{SNMP_ERROR_WRONG_TYPE, "wrongType", "wrong data type"},
                             Status{SNMP_ERROR_WRONG_LENGTH, "wrongLength", "wrong length"},
                             Status{SNMP_ERROR_WRONG_ENCODING, "wrongEncoding", "encoded incorrectly"},
                             Status{SNMP_ERROR_WRONG_VALUE, "wrongValue", "does not accept this value"},
                             Status{SNMP_ERROR_NO_CREATION, "noCreation", "cannot create"},
                             Status{SNMP_ERROR_INCONSIST_VAL, "inconsistentValue", "conflicts with"},
                             Status{SNMP_ERROR_RESOURCE_UNAVAIL, "resourceUnavailable", "no resources"},
                             Status{SNMP_ERROR_COMITFAIL, "commitFailed", "could not apply"},
                             Status{SNMP_ERROR_UNDO_FAIL, "undoFailed", "could not undo"},
                             Status{SNMP_ERROR_AUTH_ERR, "authorizationError", "not authorised"},
                             Status{SNMP_ERROR_NOT_WRITEABLE, "notWritable", "cannot be changed"},
                             Status{SNMP_ERROR_INCONSIS_NAME, "inconsistentName", "current state"}}) {
        const QString text = ExplainSnmpErrorStatus(st.code);
        ok &= check(text.startsWith(st.name) && text.contains(st.meaning) &&
                    text.endsWith(QString("(SNMP error %1)").arg(st.code)), "every error-status is explained");
    }
    ok &= check(ExplainSnmpErrorStatus(99).contains("99") && !ExplainSnmpErrorStatus(0).isEmpty(),
                "an unknown error-status still gives text");
    // Context reports (snmpTargetMIB)
    ok &= check(ExplainSnmpReport(reportWith("1.3.6.1.6.3.12.1.5.0"), user).contains("snmpUnknownContexts") &&
                ExplainSnmpReport(reportWith("1.3.6.1.6.3.12.1.5.0"), user).contains("Preferences"),
                "an unknown context points at the context name in Preferences");
    ok &= check(ExplainSnmpReport(reportWith("1.3.6.1.6.3.12.1.4.0"), user).contains("snmpUnavailableContexts"),
                "an unavailable context is named");

    // SNMPv3 timeout: says what was used and what is likely, never claims a certain cause.
    SnmpRequestConfig v3; v3.version = SnmpRequestVersion::V3; v3.address = "10.1.1.5"; v3.port = "161";
    v3.securityName = "lab"; v3.securityLevel = SNMP_SECURITY_LEVEL_AUTH_PRIV;
    const QString timeout = ExplainSnmpTimeout(v3);
    ok &= check(timeout.startsWith("Timeout: no response from the device") && timeout.contains("10.1.1.5:161") &&
                timeout.contains("'lab'") && timeout.contains("authentication and privacy") &&
                timeout.contains("cannot be known") && timeout.contains("firewall") &&
                timeout.contains("silently ignore unknown users") && !timeout.contains("context name"),
                "a v3 timeout names the target, user and level and the likely causes");
    v3.securityLevel = SNMP_SECURITY_LEVEL_AUTH_NOPRIV;
    ok &= check(ExplainSnmpTimeout(v3).contains("authentication without privacy"), "authNoPriv is described");
    v3.securityLevel = SNMP_SECURITY_LEVEL_NOAUTH_NOPRIV; v3.contextName = "vrf-red"; v3.port.clear();
    const QString noAuth = ExplainSnmpTimeout(v3);
    ok &= check(noAuth.contains("no authentication or privacy") && noAuth.contains("'vrf-red'") &&
                noAuth.contains("(10.1.1.5,"), "a context name is mentioned only when one is set; a missing port is left out");
    SnmpRequestConfig v2c; v2c.version = SnmpRequestVersion::V2c;
    ok &= check(ExplainSnmpTimeout(v2c).isEmpty(), "SNMPv1/v2c timeouts keep the short generic text");
    return ok ? 0 : 1;
}
