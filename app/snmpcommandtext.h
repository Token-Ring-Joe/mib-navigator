#ifndef SNMPCOMMANDTEXT_H
#define SNMPCOMMANDTEXT_H

#include "snmprequestconfig.h"

#include <QString>

// "Copy as command": the same request as the application would send, written as a Net-SNMP
// command line for a terminal or a ticket.
//
// No secret is ever written. SNMPv3 commands carry the user name, security level and
// protocols, with the placeholders <authentication passphrase> and <privacy passphrase>
// after -A and -X (Net-SNMP does not prompt: without them it fails with "USM generic
// error"). SNMPv1/v2c commands carry the placeholder <community>.
enum class SnmpCommandKind { Get, GetNext, GetBulk, Walk };

struct SnmpCommandText
{
    QString command;   // empty when the request cannot be written as a command
    QString note;      // one-line explanation for the user (prompts, placeholders, limits)
};

SnmpCommandText BuildSnmpCommand(const SnmpRequestConfig &config, SnmpCommandKind kind, const QString &oid);

#endif
