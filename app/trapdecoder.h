#ifndef TRAPDECODER_H
#define TRAPDECODER_H

#include "traprecord.h"
#include "snmp_pp/snmp_pp.h"

struct TrapEndpoint
{
    QString address;
    quint16 port = 0;
    TrapSnmpVersion version = TrapSnmpVersion::Unknown;
    QString community;
    QString securityName;
};

class TrapDecoder
{
public:
    TrapRecord decode(const Pdu &pdu, const TrapEndpoint &endpoint,
                      const QDateTime &received = QDateTime::currentDateTime()) const;
};

#endif
