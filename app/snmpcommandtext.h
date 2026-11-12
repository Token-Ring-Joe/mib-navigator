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
//
// Lab option: with includeKeys the SNMPv3 passphrases are replaced by their one-way
// master keys (-3m / -3M). A master key is the passphrase hashed per RFC 3414; it needs no
// engine ID, so the command works against any device, and it is as good as the passphrase
// for logging in, so the caller must warn that the text now holds a usable secret.
enum class SnmpCommandKind { Get, GetNext, GetBulk, Walk };

struct SnmpCommandText
{
    QString command;   // empty when the request cannot be written as a command
    QString note;      // one-line explanation for the user (prompts, placeholders, limits)
    bool carriesSecret = false;   // true when the command holds usable key material
};

// RFC 3414 password-to-key (the 1 MiB expansion) with the hash of the authentication
// protocol. Empty for an unknown protocol or an empty passphrase.
QByteArray SnmpMasterKey(const QByteArray &passphrase, long authProtocol);

SnmpCommandText BuildSnmpCommand(const SnmpRequestConfig &config, SnmpCommandKind kind, const QString &oid,
                                bool includeKeys = false);

#endif
