#include "snmpreport.h"

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
    return ok ? 0 : 1;
}
