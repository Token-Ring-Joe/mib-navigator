#include "mibservice.h"

#include "mibdiagnosticcollector.h"
#include "mibengine.h"
#include "mibservice_internal.h"

#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <algorithm>
#include <cstdlib>

namespace {
QString safe(const char *text) { return QString::fromLocal8Bit(text ? text : ""); }

QString accessName(SmiAccess value)
{
    switch (value) {
    case SMI_ACCESS_NOT_ACCESSIBLE: return QStringLiteral("not-accessible");
    case SMI_ACCESS_NOTIFY: return QStringLiteral("notify");
    case SMI_ACCESS_READ_ONLY: return QStringLiteral("read-only");
    case SMI_ACCESS_READ_WRITE: return QStringLiteral("read-write");
    case SMI_ACCESS_INSTALL: return QStringLiteral("install");
    case SMI_ACCESS_INSTALL_NOTIFY: return QStringLiteral("install-notify");
    case SMI_ACCESS_REPORT_ONLY: return QStringLiteral("report-only");
    default: return {};
    }
}

QString statusName(SmiStatus value)
{
    switch (value) {
    case SMI_STATUS_CURRENT: return QStringLiteral("current");
    case SMI_STATUS_DEPRECATED: return QStringLiteral("deprecated");
    case SMI_STATUS_MANDATORY: return QStringLiteral("mandatory");
    case SMI_STATUS_OPTIONAL: return QStringLiteral("optional");
    case SMI_STATUS_OBSOLETE: return QStringLiteral("obsolete");
    default: return {};
    }
}

QString baseTypeName(SmiBasetype value)
{
    switch (value) {
    case SMI_BASETYPE_UNSIGNED32: return QStringLiteral("UNSIGNED32");
    case SMI_BASETYPE_INTEGER32: return QStringLiteral("INTEGER");
    case SMI_BASETYPE_ENUM: return QStringLiteral("ENUM");
    case SMI_BASETYPE_OBJECTIDENTIFIER: return QStringLiteral("OBJECT IDENTIFIER");
    case SMI_BASETYPE_OCTETSTRING: return QStringLiteral("OCTET STRING");
    case SMI_BASETYPE_BITS: return QStringLiteral("BITS");
    case SMI_BASETYPE_UNSIGNED64: return QStringLiteral("UNSIGNED64");
    default: return {};
    }
}
}

MibService::MibService() = default;

void MibService::setSearchPaths(const QStringList &paths)
{
    auto operation=MibEngine::instance().beginOperation(QStringLiteral("set-search-paths"));
    smiSetPath(paths.join(QDir::listSeparator()).toLocal8Bit().constData());
}

QStringList MibService::searchPaths() const
{
    auto operation=MibEngine::instance().beginOperation(QStringLiteral("get-search-paths"));
    std::unique_ptr<char, decltype(&std::free)> path{smiGetPath(), std::free};
    return path ? QString::fromLocal8Bit(path.get()).split(QDir::listSeparator(), Qt::KeepEmptyParts)
                : QStringList();
}

MibLoadResult MibService::loadModules(const QStringList &modules, int errorLevel)
{
    auto operation=MibEngine::instance().beginOperation(QStringLiteral("load-modules"));
    MibLoadResult result;
    result.operationId = nextOperationId++;
    result.requestedModules = modules;
    for (const QString &requested : modules) {
        if (smiIsLoaded(requested.toLocal8Bit().constData())) {
            result.alreadyLoadedModules.append(requested);
            continue;
        }
        MibDiagnosticCollector collector(result.operationId, requested);
        collector.install(errorLevel);
        char *canonical = smiLoadModule(requested.toLocal8Bit().constData());
        collector.finish(nullptr, 0);
        result.diagnostics.append(collector.diagnostics());
        if (canonical)
            result.loadedModules.append(QString::fromLocal8Bit(canonical));
        else
            result.unavailableModules.append(requested);
    }
    if (!result.unavailableModules.isEmpty())
        result.status = result.loadedModules.isEmpty() && result.alreadyLoadedModules.isEmpty()
            ? MibLoadStatus::Failure : MibLoadStatus::Partial;
    return result;
}

MibLoadResult MibService::loadPreloads(const QStringList &preloads, int errorLevel)
{
    return loadModules(preloads, errorLevel);
}

QList<MibModuleRecord> MibService::modulesFromFile(const QString &path) const
{
    auto operation=MibEngine::instance().beginOperation(QStringLiteral("modules-from-file"));
    QList<MibModuleRecord> result;
    const QString canonicalPath = QFileInfo(path).canonicalFilePath();
    if (canonicalPath.isEmpty()) return result;
    for (SmiModule *module = smiGetFirstModule(); module; module = smiGetNextModule(module)) {
        const MibModuleRecord record = SnapshotMibModule(module);
        if (QFileInfo(record.path).canonicalFilePath().compare(
                canonicalPath, Qt::CaseInsensitive) == 0)
            result.append(record);
    }
    return result;
}

static QString languageName(SmiLanguage language)
{
    switch (language) {
    case SMI_LANGUAGE_SMIV1: return QStringLiteral("SMIv1");
    case SMI_LANGUAGE_SMIV2: return QStringLiteral("SMIv2");
    case SMI_LANGUAGE_SMING: return QStringLiteral("SMIng");
    case SMI_LANGUAGE_SPPI: return QStringLiteral("SPPI");
    default: return QStringLiteral("Unknown");
    }
}

static QString oidText(SmiNode *node)
{
    const char *text = node ? smiRenderOID(node->oidlen, node->oid, SMI_RENDER_NUMERIC) : nullptr;
    return safe(text);
}

MibModuleRecord SnapshotMibModule(SmiModule *module)
{
    auto operation=MibEngine::instance().beginOperation(QStringLiteral("snapshot-module"));
    MibModuleRecord record;
    if (!module)
        return record;
    record.name = safe(module->name);
    record.path = safe(module->path);
    record.language = languageName(module->language);
    record.organization = safe(module->organization);
    record.contactInfo = safe(module->contactinfo);
    record.description = safe(module->description);
    record.reference = safe(module->reference);
    record.loaded = true;
    for (SmiRevision *revision = smiGetFirstRevision(module); revision;
         revision = smiGetNextRevision(revision)) {
        MibRevisionRecord item;
        item.date = QDateTime::fromSecsSinceEpoch(revision->date, Qt::UTC);
        item.description = safe(revision->description);
        record.revisions.append(item);
        if (!record.lastRevision.isValid() || item.date > record.lastRevision)
            record.lastRevision = item.date;
    }
    if (SmiNode *root = smiGetModuleIdentityNode(module)) {
        record.rootName = safe(root->name);
        record.rootOid = oidText(root);
    }
    for (SmiImport *item = smiGetFirstImport(module); item; item = smiGetNextImport(item)) {
        const QString dependency = safe(item->module);
        if (!record.imports.contains(dependency))
            record.imports.append(dependency);
    }
    return record;
}

QList<MibModuleRecord> MibService::moduleInventory() const
{
    auto operation=MibEngine::instance().beginOperation(QStringLiteral("module-inventory"));
    QList<MibModuleRecord> result;
    for (SmiModule *module = smiGetFirstModule(); module; module = smiGetNextModule(module))
        result.append(SnapshotMibModule(module));
    std::sort(result.begin(), result.end(), [](const auto &a, const auto &b) {
        return a.name < b.name;
    });
    return result;
}

MibTreeNodeRecord SnapshotMibNode(SmiNode *node)
{
    auto operation=MibEngine::instance().beginOperation(QStringLiteral("snapshot-node"));
    MibTreeNodeRecord record;
    if (!node)
        return record;
    record.oid = oidText(node);
    record.name = safe(node->name);
    if (SmiModule *module = smiGetNodeModule(node))
        record.moduleName = safe(module->name);
    record.nodeKind = node->nodekind;
    record.access = accessName(node->access);
    record.status = statusName(node->status);
    record.units = safe(node->units);
    record.description = safe(node->description);
    record.reference = safe(node->reference);
    if (SmiType *type = smiGetNodeType(node)) {
        if (type->decl == SMI_DECL_IMPLICIT_TYPE)
            if (SmiType *parent = smiGetParentType(type)) type = parent;
        record.typeName = safe(type->name);
        record.baseType = baseTypeName(type->basetype);
        record.displayHint = safe(type->format);
        for (SmiRange *range = smiGetFirstRange(type); range; range = smiGetNextRange(range))
            record.ranges.append(QStringLiteral("%1 .. %2")
                .arg(range->minValue.value.unsigned64).arg(range->maxValue.value.unsigned64));
        for (SmiNamedNumber *number = smiGetFirstNamedNumber(type); number;
             number = smiGetNextNamedNumber(number))
            record.namedValues.append(QStringLiteral("%1 (%2)")
                .arg(safe(number->name)).arg(number->value.value.unsigned32));
    }
    return record;
}

static void appendTree(SmiNode *node, const QSet<QString> &included,
                       MibTreeNodeRecord *parent)
{
    if (!node || !parent)
        return;
    MibTreeNodeRecord current = SnapshotMibNode(node);
    const bool directlyIncluded = included.isEmpty() || included.contains(current.moduleName);
    for (SmiNode *child = smiGetFirstChildNode(node); child;
         child = smiGetNextChildNode(child))
        appendTree(child, included, &current);
    if (directlyIncluded || !current.children.isEmpty())
        parent->children.append(std::move(current));
}

MibTreeNodeRecord MibService::treeSnapshot(const QStringList &includedModules) const
{
    auto operation=MibEngine::instance().beginOperation(QStringLiteral("tree-snapshot"));
    MibTreeNodeRecord root;
    root.oid = QStringLiteral("1");
    root.name = QStringLiteral("MIB Tree");
    appendTree(smiGetNode(nullptr, "iso"), QSet<QString>(includedModules.begin(),
               includedModules.end()), &root);
    return root;
}
