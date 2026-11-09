#ifndef SNMPREPORT_H
#define SNMPREPORT_H

#include <QString>

#include "snmp_pp/snmp_pp.h"

// An SNMPv3 agent that rejects a request does not return a normal answer: it returns a REPORT
// whose single varbind is an error counter (usmStats..., snmpMPDStats...). Shown as a result,
// that counter looks like a successful Get. This turns it into a plain-English explanation.
// Returns an empty string when the PDU is an ordinary response.
QString ExplainSnmpReport(const Pdu &pdu, const QString &securityName);

#endif
