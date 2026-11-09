#ifndef MIBSERVICE_INTERNAL_H
#define MIBSERVICE_INTERNAL_H
// Engine-internal adapter. Never include from UI/application public headers.
#include "mibrecords.h"
#include "smi.h"
MibModuleRecord SnapshotMibModule(SmiModule *module);
MibTreeNodeRecord SnapshotMibNode(SmiNode *node);
#endif
