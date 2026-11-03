#ifndef SNMPSINGLEOPERATION_H
#define SNMPSINGLEOPERATION_H

#include "snmprequestcontext.h"
#include "snmptransport.h"

#include <QMetaType>
#include <QString>
#include <QVector>

struct SnmpSingleResult
{
    SnmpOperationStatus status = SnmpOperationStatus::TransportFailure;
    Pdu pdu;                     // the response, also when the device answered with an error
    int transportStatus = 0;
    int snmpErrorStatus = 0;
    int snmpErrorIndex = 0;
    QString problem;             // why the request failed on this side, or an SNMPv3 REPORT explained
};

// One Get, GetNext, GetBulk or Set (the context's operation) for a list of varbinds. No widgets.
// GetBulk uses the global non-repeaters and max-repetitions captured in the context.
class SnmpSingleOperation
{
public:
    SnmpSingleOperation(const SnmpRequestContext &context, const QVector<Vb> &varbinds);
    SnmpSingleResult execute(ISnmpTransport &transport) const;

private:
    SnmpRequestContext requestContext;
    QVector<Vb> requestVarbinds;
};

Q_DECLARE_METATYPE(SnmpSingleResult)

#endif
