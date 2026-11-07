#ifndef TRAPPRESENTER_H
#define TRAPPRESENTER_H

#include "traprecord.h"
#include "mibenvironment.h"

#include <QStringList>

struct TrapBindingPresentation
{
    QString oid;
    QString object;
    QString receivedType;
    QString rawValue;
    QString interpretedValue;
    QString units;
    QStringList warnings;
};

struct TrapPresentation
{
    QStringList summaryColumns;
    QString notificationHtml;
    QString communityText;
    QList<TrapBindingPresentation> bindings;
};

class TrapPresenter
{
public:
    TrapPresentation present(const TrapRecord &record) const;
    QStringList summary(const TrapRecord &record) const;
    static QString symbolicOid(const QString &numericOid);
    static QString formattedValue(const TrapVarbind &varbind);
    // The address to copy for "Copy source IP": the sender of the UDP packet, except for
    // SNMPv1 traps whose agent-address field names a different (real) originator, which
    // is typical when a trap was relayed or sent through NAT. 0.0.0.0 means "not set".
    static QString sourceToCopy(const TrapRecord &record);
};

#endif
