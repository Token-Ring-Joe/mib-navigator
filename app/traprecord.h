#ifndef TRAPRECORD_H
#define TRAPRECORD_H

#include <QDateTime>
#include <QList>
#include <QString>

enum class TrapSnmpVersion { Unknown, V1, V2c, V3 };

struct TrapVarbind
{
    QString oid;
    int syntax = 0;
    QString printableValue;
    bool valid = true;
};

struct TrapRecord
{
    quint64 recordId = 0;
    QDateTime receivedTimestamp;
    QString sourceAddress;
    QString agentAddress;   // SNMPv1 "agent-addr" field when the trap carried one
    quint16 sourcePort = 0;
    TrapSnmpVersion snmpVersion = TrapSnmpVersion::Unknown;
    QString securityName;
    QString community;
    QString notificationOid;
    QString enterpriseOid;
    int genericTrap = -1;
    int specificTrap = -1;
    quint64 notificationTicks = 0;
    QString messageType;
    QString securityLevel;
    QString contextName;
    QString contextEngineId;
    quint32 messageId = 0;
    QList<TrapVarbind> varbinds;
    QString decodeError;

    bool isValid() const { return decodeError.isEmpty() && !notificationOid.isEmpty(); }
};

#endif
