#ifndef TABLEINDEX_H
#define TABLEINDEX_H

#include "mibenvironment.h"

#include <QString>
#include <QVector>

// What is needed to read a table row's INDEX values back out of the instance part of its OIDs
// (RFC 2578 section 7.7). Built from the MIB by TableIndexSpecsForRow, or by hand in tests.
struct TableIndexSpec
{
    QString name;
    MibEnvironmentBaseType baseType = MibEnvironmentBaseType::Unknown;
    int fixedSize = -1;      // OCTET STRING (SIZE (n)): no length prefix is encoded
    bool ipAddress = false;  // IpAddress: four sub-identifiers, shown as a dotted address
    bool implied = false;    // IMPLIED: the last index takes whatever sub-identifiers remain
};

// Turns index sub-identifiers into text such as `ifDescr="eth0", ifIndex=3`. Returns false
// (leaving *text untouched) when the instance does not fit the declared indexes, for example
// a vendor table whose agent encodes it differently; callers then show the raw instance only.
bool DecodeTableInstance(const QVector<TableIndexSpec> &specs,
                         const QVector<quint32> &suffix, QString *text);

// The index objects that identify rows of `row`, following AUGMENTS/SPARSE/EXPAND to the row
// that really declares them (libsmi records no index list on an augmenting row).
QVector<TableIndexSpec> TableIndexSpecsForRow(const MibEnvironment &environment,
                                              const MibEnvironmentNodeRecord &row);

#endif
