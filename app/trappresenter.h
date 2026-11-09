#ifndef TRAPPRESENTER_H
#define TRAPPRESENTER_H

#include "traprecord.h"

#include <QStringList>

struct TrapPresentation
{
    QStringList summaryColumns;
    QString notificationHtml;
    QString communityText;
    QStringList varbindLines;
};

class TrapPresenter
{
public:
    TrapPresentation present(const TrapRecord &record, bool showAgentName = false) const;
    static QString symbolicOid(const QString &numericOid);
    static QString formattedValue(const TrapVarbind &varbind);
    // The address to copy for "Copy source IP": the sender of the UDP packet, except for
    // SNMPv1 traps whose agent-address field names a different (real) originator, which
    // is typical when a trap was relayed or sent through NAT. 0.0.0.0 means "not set".
    static QString sourceToCopy(const TrapRecord &record);
};

#endif
