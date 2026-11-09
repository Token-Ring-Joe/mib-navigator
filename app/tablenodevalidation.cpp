#include "tablenodevalidation.h"

TableNodeValidation ResolveTableRowNode(const MibEnvironmentNodeRecord *node,
 const MibEnvironmentNodeRecord *tableRow,const MibEnvironmentNodeRecord **rowNode)
{
    if (rowNode)
        *rowNode = nullptr;
    if (!node)
        return TableNodeValidation::MissingNode;
    if (node->kind == MibEnvironmentNodeKind::Row)
    {
        if (rowNode)
            *rowNode = node;
        return TableNodeValidation::Valid;
    }
    if (node->kind != MibEnvironmentNodeKind::Table)
        return TableNodeValidation::WrongNodeKind;
    const auto *firstChild=tableRow;
    if (!firstChild || firstChild->kind != MibEnvironmentNodeKind::Row)
        return TableNodeValidation::MissingRowNode;

    if (rowNode)
        *rowNode = firstChild;
    return TableNodeValidation::Valid;
}

bool IsValidTableColumnNode(const MibEnvironmentNodeRecord *node)
{
    return node && node->kind == MibEnvironmentNodeKind::Column;
}

bool IsTableQueryCapableNodeKind(MibEnvironmentNodeKind kind)
{
    return kind == MibEnvironmentNodeKind::Table || kind == MibEnvironmentNodeKind::Row;
}
