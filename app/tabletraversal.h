#ifndef TABLETRAVERSAL_H
#define TABLETRAVERSAL_H

#include <QString>

#include "mibenvironment.h"
#include "snmp_pp/oid.h"

bool HasVarbindAt(int varbindCount, int index);
bool IsOidInSubtree(const Oid &oid, const Oid &root);
bool BuildFirstColumnRoot(const Oid &tableRow, const Oid &returned,
                          Oid *columnRoot);
bool ExtractOidSuffix(const Oid &root, const Oid &oid, QString *suffix);
bool RenderEnvironmentNodeOid(const MibEnvironmentNodeRecord *node, Oid *oid);
bool HasValidColumnInfo(const MibEnvironmentNodeRecord *node);

#endif
