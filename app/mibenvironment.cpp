#include "mibenvironment.h"

const MibEnvironmentModuleRecord *MibEnvironment::module(const QString &identity) const
{
    const auto it = moduleIndex.constFind(identity);
    return it == moduleIndex.cend() ? nullptr : &moduleRecords[*it];
}

const MibEnvironmentNodeRecord *MibEnvironment::nodeByOid(const QString &oid) const
{
    const auto it = oidIndex.constFind(oid);
    return it == oidIndex.cend() || it->isEmpty() ? nullptr : &nodeRecords[it->first()];
}

QList<const MibEnvironmentNodeRecord *> MibEnvironment::nodesByOid(const QString &oid) const
{
    QList<const MibEnvironmentNodeRecord *> result;
    const auto it = oidIndex.constFind(oid);
    if (it != oidIndex.cend()) for (qsizetype index : *it) result.append(&nodeRecords[index]);
    return result;
}

const MibEnvironmentNodeRecord *MibEnvironment::longestPrefixNode(
    const QString &numericOid, QStringList *suffix) const
{
    QStringList parts = numericOid.split(u'.', Qt::SkipEmptyParts);
    QStringList removed;
    while (!parts.isEmpty()) {
        if (const auto *node = nodeByOid(parts.join(u'.'))) {
            if (suffix) *suffix = removed;
            return node;
        }
        removed.prepend(parts.takeLast());
    }
    if (suffix) suffix->clear();
    return nullptr;
}

const MibEnvironmentNodeRecord *MibEnvironment::nodeByQualifiedName(const QString &name) const
{
    const auto it = qualifiedNameIndex.constFind(name);
    return it == qualifiedNameIndex.cend() ? nullptr : &nodeRecords[*it];
}

QList<const MibEnvironmentNodeRecord *> MibEnvironment::nodesByName(const QString &name) const
{
    QList<const MibEnvironmentNodeRecord *> result;
    const auto it = unqualifiedNameIndex.constFind(name);
    if (it != unqualifiedNameIndex.cend())
        for (qsizetype index : *it) result.append(&nodeRecords[index]);
    return result;
}

const MibEnvironmentTypeRecord *MibEnvironment::type(const QString &id) const
{
    const auto it = typeIndex.constFind(id);
    return it == typeIndex.cend() ? nullptr : &typeRecords[*it];
}
