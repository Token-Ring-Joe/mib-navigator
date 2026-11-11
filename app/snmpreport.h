#ifndef SNMPREPORT_H
#define SNMPREPORT_H

#include <QString>

#include "snmp_pp/snmp_pp.h"

// An SNMPv3 agent that rejects a request does not return a normal answer: it returns a REPORT
// whose single varbind is an error counter (usmStats..., snmpMPDStats...). Shown as a result,
// that counter looks like a successful Get. This turns it into a plain-English explanation.
// Returns an empty string when the PDU is an ordinary response.
QString ExplainSnmpReport(const Pdu &pdu, const QString &securityName);

struct SnmpRequestConfig;

// A request that got no answer at all. For SNMPv3 this says what the request used (user, security
// level, context) and the likely causes, and is honest that a silent device gives no way to tell
// them apart. Returns an empty string for SNMPv1 and SNMPv2c, which keep the short generic text.
QString ExplainSnmpTimeout(const SnmpRequestConfig &config);

// SNMP++ returns a positive code for two different things: the device's own SNMP error-status
// (tooBig = 1 ... inconsistentName = 18) and its own SNMPv3 USM failures (1400 and up), which
// happen on this side of the wire. Only the first is an error the device reported.
bool IsSnmpErrorStatusCode(int code);

// Plain-English reason for a request that failed on this side of the wire, from the SNMP++
// return code (for example SNMPv3_USM_DECRYPTION_ERROR). Always returns text, never empty.
QString ExplainSnmpTransportFailure(int transportStatus, const QString &securityName);

// Plain-English meaning of a device's SNMP error-status (1-18), for example
// "notWritable: this object cannot be changed (SNMP error 17)". Always returns text.
QString ExplainSnmpErrorStatus(int errorStatus);

#endif
