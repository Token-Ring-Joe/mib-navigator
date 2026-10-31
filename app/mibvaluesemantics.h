#ifndef MIBVALUESEMANTICS_H
#define MIBVALUESEMANTICS_H

#include "mibenvironment.h"
#include "snmp_pp/snmp_pp.h"

struct MibResolvedObject
{
    MibEnvironmentPtr environment;
    const MibEnvironmentNodeRecord *node = nullptr;
    QStringList instanceSuffix;
};

MibResolvedObject ResolveMibObject(const MibEnvironmentPtr &environment,
                                   const Oid &oid);
QString RenderMibOid(const MibEnvironmentPtr &environment, const Oid &oid);
QString RenderMibValue(const MibResolvedObject &object, const Vb &varbind);
int SnmpSyntaxForMibNode(const MibEnvironmentNodeRecord *node);
bool IsWritableMibNode(const MibEnvironmentNodeRecord *node);
// What the device actually sent, next to what the MIB says it should be.
struct MibValueCheck
{
    QString receivedType;     // SMI name of the type on the wire, e.g. "Counter32"
    QString rawValue;         // the value exactly as received, without MIB interpretation
    QStringList warnings;     // plain-English mismatches between the value and the MIB
};
QString SnmpSyntaxName(int syntax);
MibValueCheck CheckMibValue(const MibResolvedObject &object, const Vb &varbind);
bool ValidateMibSetValue(const MibEnvironmentNodeRecord *node, int syntax,
                         const QString &value, QString *error = nullptr);

#endif
