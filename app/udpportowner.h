#ifndef UDPPORTOWNER_H
#define UDPPORTOWNER_H

#include <QString>

struct UdpPortOwner
{
    bool found = false;
    quint32 processId = 0;
    QString processName;
    QString executablePath;
};

class UdpPortOwnerLookup
{
public:
    static UdpPortOwner lookup(quint16 port, bool ipv6);
    static QString conflictDescription(quint16 port, const UdpPortOwner &owner);
};

#endif
