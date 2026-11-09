#ifndef MIBSERVICE_H
#define MIBSERVICE_H

#include "mibrecords.h"
#include <QSet>

class MibService
{
public:
    MibService();
    void setSearchPaths(const QStringList &paths);
    QStringList searchPaths() const;
    MibLoadResult loadModules(const QStringList &modules, int errorLevel = 9);
    MibLoadResult loadPreloads(const QStringList &preloads, int errorLevel = 9);
    QList<MibModuleRecord> moduleInventory() const;
    QList<MibModuleRecord> modulesFromFile(const QString &path) const;
    MibTreeNodeRecord treeSnapshot(const QStringList &includedModules) const;
private:
    quint64 nextOperationId = 1;
};

#endif
