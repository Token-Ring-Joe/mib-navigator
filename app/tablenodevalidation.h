#ifndef TABLENODEVALIDATION_H
#define TABLENODEVALIDATION_H

#include "mibenvironment.h"

enum class TableNodeValidation
{
    Valid,
    MissingNode,
    WrongNodeKind,
    MissingRowNode
};

TableNodeValidation ResolveTableRowNode(const MibEnvironmentNodeRecord *node,
    const MibEnvironmentNodeRecord *tableRow,const MibEnvironmentNodeRecord **rowNode);
bool IsValidTableColumnNode(const MibEnvironmentNodeRecord *node);
bool IsTableQueryCapableNodeKind(MibEnvironmentNodeKind kind);

#endif
