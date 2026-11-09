#include "tabletraversal.h"

#include <QStringList>

bool HasVarbindAt(int varbindCount, int index)
{
    return index >= 0 && index < varbindCount;
}

bool IsOidInSubtree(const Oid &oid, const Oid &root)
{
    return oid.len() >= root.len() && oid.nCompare(root.len(), root) == 0;
}

bool BuildFirstColumnRoot(const Oid &tableRow, const Oid &returned,
                          Oid *columnRoot)
{
    if (!columnRoot || returned.len() <= tableRow.len() ||
        !IsOidInSubtree(returned, tableRow))
        return false;

    *columnRoot = tableRow;
    *columnRoot += returned[tableRow.len()];
    return true;
}

bool ExtractOidSuffix(const Oid &root, const Oid &oid, QString *suffix)
{
    if (!suffix || !IsOidInSubtree(oid, root))
        return false;

    QStringList parts;
    for (unsigned long i = root.len(); i < oid.len(); ++i)
        parts.append(QString::number(oid[i]));
    *suffix = parts.join(QLatin1Char('.'));
    return true;
}

bool HasValidColumnInfo(const MibEnvironmentNodeRecord *node)
{
    return node && node->kind == MibEnvironmentNodeKind::Column && !node->name.isEmpty() && !node->oid.isEmpty();
}

bool RenderEnvironmentNodeOid(const MibEnvironmentNodeRecord *node, Oid *oid)
{
    if (!oid || !node || node->oid.isEmpty()) return false;
    *oid = Oid(node->oid.toLatin1().constData());
    return oid->valid();
}
