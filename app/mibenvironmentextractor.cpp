#include <QTimeZone>
#include "mibenvironmentextractor.h"
#include "mibengine.h"
#include "mibparsernodesafety.h"

#include "smi.h"

#include <QElapsedTimer>
#include <QDir>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <algorithm>
#include <functional>

namespace {
QString text(const char *value) { return QString::fromLocal8Bit(value ? value : ""); }

MibEnvironmentLanguage language(SmiLanguage value)
{
    switch (value) {
    case SMI_LANGUAGE_SMIV1: return MibEnvironmentLanguage::SmiV1;
    case SMI_LANGUAGE_SMIV2: return MibEnvironmentLanguage::SmiV2;
    case SMI_LANGUAGE_SMING: return MibEnvironmentLanguage::SmiNg;
    case SMI_LANGUAGE_SPPI: return MibEnvironmentLanguage::Sppi;
    case SMI_LANGUAGE_YANG: return MibEnvironmentLanguage::Yang;
    default: return MibEnvironmentLanguage::Unknown;
    }
}

MibEnvironmentBaseType baseType(SmiBasetype value)
{
    switch (value) {
    case SMI_BASETYPE_INTEGER32: return MibEnvironmentBaseType::Integer32;
    case SMI_BASETYPE_OCTETSTRING: return MibEnvironmentBaseType::OctetString;
    case SMI_BASETYPE_OBJECTIDENTIFIER: return MibEnvironmentBaseType::ObjectIdentifier;
    case SMI_BASETYPE_UNSIGNED32: return MibEnvironmentBaseType::Unsigned32;
    case SMI_BASETYPE_INTEGER64: return MibEnvironmentBaseType::Integer64;
    case SMI_BASETYPE_UNSIGNED64: return MibEnvironmentBaseType::Unsigned64;
    case SMI_BASETYPE_FLOAT32: return MibEnvironmentBaseType::Float32;
    case SMI_BASETYPE_FLOAT64: return MibEnvironmentBaseType::Float64;
    case SMI_BASETYPE_FLOAT128: return MibEnvironmentBaseType::Float128;
    case SMI_BASETYPE_ENUM: return MibEnvironmentBaseType::Enumeration;
    case SMI_BASETYPE_BITS: return MibEnvironmentBaseType::Bits;
    case SMI_BASETYPE_POINTER: return MibEnvironmentBaseType::Pointer;
    default: return MibEnvironmentBaseType::Unknown;
    }
}

MibEnvironmentNodeKind nodeKind(SmiNodekind value)
{
    switch (value) {
    case SMI_NODEKIND_NODE: return MibEnvironmentNodeKind::Node;
    case SMI_NODEKIND_SCALAR: return MibEnvironmentNodeKind::Scalar;
    case SMI_NODEKIND_TABLE: return MibEnvironmentNodeKind::Table;
    case SMI_NODEKIND_ROW: return MibEnvironmentNodeKind::Row;
    case SMI_NODEKIND_COLUMN: return MibEnvironmentNodeKind::Column;
    case SMI_NODEKIND_NOTIFICATION: return MibEnvironmentNodeKind::Notification;
    case SMI_NODEKIND_GROUP: return MibEnvironmentNodeKind::Group;
    case SMI_NODEKIND_COMPLIANCE: return MibEnvironmentNodeKind::Compliance;
    case SMI_NODEKIND_CAPABILITIES: return MibEnvironmentNodeKind::Capabilities;
    default: return MibEnvironmentNodeKind::Unknown;
    }
}

MibEnvironmentIndexKind indexKind(SmiIndexkind value)
{
    switch (value) {
    case SMI_INDEX_INDEX: return MibEnvironmentIndexKind::Index;
    case SMI_INDEX_AUGMENT: return MibEnvironmentIndexKind::Augment;
    case SMI_INDEX_REORDER: return MibEnvironmentIndexKind::Reorder;
    case SMI_INDEX_SPARSE: return MibEnvironmentIndexKind::Sparse;
    case SMI_INDEX_EXPAND: return MibEnvironmentIndexKind::Expand;
    default: return MibEnvironmentIndexKind::None;
    }
}

QList<quint32> oidParts(const SmiNode *node)
{
    return MibParserNodeOidParts(node);
}

QString oidText(const SmiNode *node)
{
    QStringList parts;
    for (quint32 part : oidParts(node)) parts.append(QString::number(part));
    return parts.join(u'.');
}

MibEnvironmentValue value(const SmiValue &source)
{
    MibEnvironmentValue result;
    result.baseType = baseType(source.basetype);
    switch (source.basetype) {
    case SMI_BASETYPE_INTEGER32:
        result.isSigned = true; result.signedValue = source.value.integer32;
        result.canonicalText = QString::number(result.signedValue); break;
    case SMI_BASETYPE_INTEGER64:
        result.isSigned = true; result.signedValue = source.value.integer64;
        result.canonicalText = QString::number(result.signedValue); break;
    case SMI_BASETYPE_UNSIGNED32: case SMI_BASETYPE_ENUM:
        result.unsignedValue = source.value.unsigned32;
        result.canonicalText = QString::number(result.unsignedValue); break;
    case SMI_BASETYPE_UNSIGNED64:
        result.unsignedValue = source.value.unsigned64;
        result.canonicalText = QString::number(result.unsignedValue); break;
    case SMI_BASETYPE_OCTETSTRING: case SMI_BASETYPE_BITS:
        if (source.value.ptr && source.len)
            result.bytes = QByteArray(reinterpret_cast<const char *>(source.value.ptr), source.len);
        result.canonicalText = QString::fromLatin1(result.bytes.toHex()); break;
    case SMI_BASETYPE_OBJECTIDENTIFIER:
        if (source.value.oid)
            for (unsigned int i = 0; i < source.len; ++i) result.oid.append(source.value.oid[i]);
        { QStringList parts; for (quint32 part : result.oid) parts.append(QString::number(part));
          result.canonicalText = parts.join(u'.'); }
        break;
    case SMI_BASETYPE_FLOAT32:
        result.canonicalText = QString::number(source.value.float32, 'g', 9); break;
    case SMI_BASETYPE_FLOAT64:
        result.canonicalText = QString::number(source.value.float64, 'g', 17); break;
    case SMI_BASETYPE_FLOAT128:
        result.canonicalText = QString::number(static_cast<double>(source.value.float128), 'g', 17); break;
    default: break;
    }
    return result;
}

QString typeId(SmiType *type)
{
    if (!type || !type->name) return {};
    SmiModule *module = smiGetTypeModule(type);
    return QStringLiteral("%1::%2").arg(module ? text(module->name) : QString(), text(type->name));
}

MibEnvironmentTypeRecord typeRecord(SmiType *type, const QString &fallbackId = {})
{
    MibEnvironmentTypeRecord result;
    if (!type) return result;
    SmiModule *module = smiGetTypeModule(type);
    result.moduleIdentity = module ? text(module->name) : QString();
    result.name = text(type->name);
    result.id = typeId(type);
    if (result.id.isEmpty()) result.id = fallbackId;
    result.baseType = baseType(type->basetype);
    result.declaration = type->decl;
    result.displayHint = text(type->format);
    result.units = text(type->units);
    result.description = text(type->description);
    result.reference = text(type->reference);
    result.status = type->status;
    result.defaultValue = value(type->value);
    QSet<SmiType *> seen;
    for (SmiType *parent = smiGetParentType(type); parent && !seen.contains(parent);
         parent = smiGetParentType(parent)) {
        seen.insert(parent);
        const QString id = typeId(parent);
        if (!id.isEmpty()) result.ancestry.append(id);
        if (result.parentTypeId.isEmpty()) result.parentTypeId = id;
    }
    const bool size = type->basetype == SMI_BASETYPE_OCTETSTRING || type->basetype == SMI_BASETYPE_BITS;
    for (SmiRange *range = smiGetFirstRange(type); range; range = smiGetNextRange(range))
        result.constraints.append({value(range->minValue), value(range->maxValue), size});
    for (SmiNamedNumber *number = smiGetFirstNamedNumber(type); number;
         number = smiGetNextNamedNumber(number))
        result.namedValues.append({text(number->name), value(number->value)});
    return result;
}

bool oidLess(const MibEnvironmentNodeRecord &left, const MibEnvironmentNodeRecord &right)
{
    if (left.oidParts != right.oidParts)
        return std::lexicographical_compare(left.oidParts.cbegin(), left.oidParts.cend(),
                                            right.oidParts.cbegin(), right.oidParts.cend());
    return left.qualifiedName < right.qualifiedName;
}

bool oidTextLess(const QString &left, const QString &right)
{
    const QStringList leftParts = left.split(u'.');
    const QStringList rightParts = right.split(u'.');
    const qsizetype common = std::min(leftParts.size(), rightParts.size());
    for (qsizetype i = 0; i < common; ++i) {
        const quint64 a = leftParts[i].toULongLong(), b = rightParts[i].toULongLong();
        if (a != b) return a < b;
    }
    return leftParts.size() < rightParts.size();
}

QString canonicalPath(const QString &path)
{
    if (path.trimmed().isEmpty()) return {};
    const QString normalized = QDir::fromNativeSeparators(path);
    const QString canonical = QFileInfo(normalized).canonicalFilePath();
    return canonical.isEmpty() ? QFileInfo(normalized).absoluteFilePath() : canonical;
}

}

MibEnvironmentPtr MibEnvironmentExtractor::extract(const MibEffectivePlan &plan,
                                                    const QStringList &failedIdentities,
                                                    const QList<MibExplicitRootLoadResult> &rootOutcomes,
                                                    const QStringList &diagnostics,
                                                    const QList<MibProblem> &problems) const
{
    auto engineOperation=MibEngine::instance().beginOperation(QStringLiteral("environment-extraction"));
    QElapsedTimer timer; timer.start();
    std::shared_ptr<MibEnvironment> environment(new MibEnvironment);
    environment->planSha256 = plan.sha256;
    environment->parserId = QStringLiteral("patched-libsmi-%1/env-extractor-%2")
        .arg(QStringLiteral(SMI_VERSION_STRING)).arg(MIB_ENVIRONMENT_BUILDER_VERSION);
    environment->plannedModuleCount = plan.effectiveModules.size();
    environment->authorityProfileId = plan.profileId;
    environment->runtimeConfigurationSha256 = plan.runtimeConfiguration.sha256();
    environment->runtimePathSha256 = plan.runtimePaths.sha256();
    environment->authorityLibraryGeneration = plan.runtimeConfiguration.libraryGeneration();
    for (const auto &entry : plan.runtimePaths.entries())
        environment->authorityPaths.append(entry.canonicalPath);
    environment->requestedRoots = plan.runtimeConfiguration.explicitRoots();
    for (const auto &file : plan.runtimeFiles)
        environment->authorityFiles.insert(canonicalPath(file.canonicalPath), file.sha256);
    environment->rootOutcomes = rootOutcomes;
    environment->authorityDiagnostics = diagnostics;
    environment->problemList = problems;
    environment->loadedProvidersAuthorized = true;

    const auto planMember = [&plan](const QString &identity) -> const MibEffectivePlanMember * {
        for (const auto &member : plan.members) if (member.identity == identity) return &member;
        return nullptr;
    };
    QSet<QString> failed(failedIdentities.cbegin(), failedIdentities.cend());
    for (const QString &identity : plan.effectiveModules)
        if (!smiGetModule(identity.toLocal8Bit().constData())) failed.insert(identity);

    for (const QString &identity : std::as_const(failed)) {
        const auto *member = planMember(identity);
        environment->materializationFindings.append({
            MibEnvironmentFindingKind::PlannedModuleFailed, identity,
            member ? member->provider.canonicalPath : QString(), {},
            QStringLiteral("planned identity did not materialize")});
    }

    for (SmiModule *module = smiGetFirstModule(); module; module = smiGetNextModule(module)) {
        MibEnvironmentModuleRecord record;
        record.identity = text(module->name);
        record.language = language(module->language);
        record.actualProviderPath = canonicalPath(text(module->path));
        const auto stagedSource = plan.runtimeConfiguration.stagedToOriginal().constFind(
#ifdef Q_OS_WIN
            QDir::fromNativeSeparators(record.actualProviderPath).toLower()
#else
            QDir::fromNativeSeparators(record.actualProviderPath)
#endif
        );
        if (stagedSource != plan.runtimeConfiguration.stagedToOriginal().cend())
            record.actualProviderPath = stagedSource.value();
        environment->actualLoadedIdentities.append(record.identity);
        environment->actualProviderPaths.insert(record.identity, record.actualProviderPath);
        const auto exactFile = std::find_if(
            plan.runtimeFiles.cbegin(), plan.runtimeFiles.cend(), [&record](const auto &file) {
#ifdef Q_OS_WIN
                return canonicalPath(file.canonicalPath).compare(
                    record.actualProviderPath, Qt::CaseInsensitive) == 0;
#else
                return canonicalPath(file.canonicalPath) == record.actualProviderPath;
#endif
            });
        bool exactAuthorized = true;
        QString exactFailure;
        if (plan.hasRuntimePaths) {
            if (exactFile == plan.runtimeFiles.cend()) {
                exactAuthorized = false;
                exactFailure = QStringLiteral("physical file is not in the Effective Plan");
            } else if (!exactFile->identities.contains(record.identity)) {
                exactAuthorized = false;
                exactFailure = QStringLiteral("Effective Plan file did not authorize this declared identity");
            } else {
                QFile provider(record.actualProviderPath);
                if (!provider.open(QIODevice::ReadOnly)) {
                    exactAuthorized = false;
                    exactFailure = QStringLiteral("Profile member file is no longer readable");
                } else {
                    const QString actualHash = QString::fromLatin1(QCryptographicHash::hash(
                        provider.readAll(), QCryptographicHash::Sha256).toHex());
                    if (actualHash != exactFile->sha256) {
                        exactAuthorized = false;
                        exactFailure = QStringLiteral("Effective Plan file content changed");
                    }
                }
            }
        }
        if (!exactAuthorized) {
            environment->loadedProvidersAuthorized = false;
            const QString detail = exactFailure.isEmpty()
                ? QStringLiteral("module loaded from unauthorized runtime path") : exactFailure;
            environment->authorityDiagnostics.append(QStringLiteral("Module '%1' loaded from unauthorized file %2: %3")
                .arg(record.identity, record.actualProviderPath, detail));
            MibEnvironmentFinding finding{MibEnvironmentFindingKind::ProviderMismatch,
                record.identity, {}, record.actualProviderPath, detail};
            record.findings.append(finding);
            environment->materializationFindings.append(finding);
        } else if (plan.hasRuntimePaths && record.actualProviderPath.isEmpty()) {
            environment->authorityDiagnostics.append(QStringLiteral(
                "Module '%1' has no physical provider path; authorization could not be independently verified")
                .arg(record.identity));
        }
        record.organization = text(module->organization);
        record.contactInfo = text(module->contactinfo);
        record.description = text(module->description);
        record.reference = text(module->reference);
        if (const auto *member = planMember(record.identity)) {
            record.plannedProviderPath = canonicalPath(member->provider.canonicalPath);
            record.rawProviderSha256 = member->provider.sha256;
            if (!record.plannedProviderPath.isEmpty() &&
                record.plannedProviderPath.compare(record.actualProviderPath, Qt::CaseInsensitive) != 0) {
                MibEnvironmentFinding finding{MibEnvironmentFindingKind::ProviderMismatch,
                    record.identity, record.plannedProviderPath, record.actualProviderPath,
                    QStringLiteral("libsmi materialized a different provider")};
                record.findings.append(finding); environment->materializationFindings.append(finding);
            }
        } else {
            MibEnvironmentFinding finding{MibEnvironmentFindingKind::UnexpectedRecursiveModule,
                record.identity, {}, record.actualProviderPath,
                QStringLiteral("module was loaded but is absent from the Effective Plan")};
            record.findings.append(finding); environment->materializationFindings.append(finding);
        }
        for (SmiImport *item = smiGetFirstImport(module); item; item = smiGetNextImport(item))
            record.imports.append({text(item->module), text(item->name)});
        if (const auto *member = planMember(record.identity)) {
            QStringList parserImports;
            for (const auto &item : record.imports)
                if (!parserImports.contains(item.moduleIdentity)) parserImports.append(item.moduleIdentity);
            parserImports.sort(Qt::CaseSensitive);
            QStringList indexedImports = member->imports; indexedImports.removeDuplicates();
            indexedImports.sort(Qt::CaseSensitive);
            if (parserImports != indexedImports) {
                MibEnvironmentFinding finding{MibEnvironmentFindingKind::ImportMismatch,
                    record.identity, record.plannedProviderPath, record.actualProviderPath,
                    QStringLiteral("indexed=[%1] parser=[%2]")
                        .arg(indexedImports.join(u','), parserImports.join(u','))};
                record.findings.append(finding); environment->materializationFindings.append(finding);
            }
        }
        for (SmiRevision *revision = smiGetFirstRevision(module); revision;
             revision = smiGetNextRevision(revision)) {
            MibEnvironmentRevisionRecord value{QDateTime::fromSecsSinceEpoch(revision->date, QTimeZone::UTC),
                                                text(revision->description)};
            record.revisions.append(value);
            if (!record.lastUpdated.isValid() || value.date > record.lastUpdated) record.lastUpdated = value.date;
        }
        if (SmiNode *root = smiGetModuleIdentityNode(module)) {
            record.rootOid = oidText(root); record.rootName = text(root->name);
        }
        environment->moduleRecords.append(std::move(record));

        for (SmiType *type = smiGetFirstType(module); type; type = smiGetNextType(type)) {
            MibEnvironmentTypeRecord record = typeRecord(type);
            if (!record.id.isEmpty()) environment->typeRecords.append(std::move(record));
        }
    }
    std::sort(environment->moduleRecords.begin(), environment->moduleRecords.end(),
              [](const auto &a, const auto &b) { return a.identity < b.identity; });
    std::sort(environment->typeRecords.begin(), environment->typeRecords.end(),
              [](const auto &a, const auto &b) { return a.id < b.id; });

    for (SmiModule *module = smiGetFirstModule(); module; module = smiGetNextModule(module)) {
        for (SmiNode *node = smiGetFirstNode(module, SMI_NODEKIND_ANY); node;
             node = smiGetNextNode(node, SMI_NODEKIND_ANY)) {
            if (!MibParserNodeHasReadableOid(node)) {
                environment->materializationFindings.append({
                    MibEnvironmentFindingKind::MalformedModule, text(module->name), {}, {},
                    QStringLiteral("parser node has nonzero OID length but no OID storage")});
                continue;
            }
            MibEnvironmentNodeRecord record;
            record.oidParts = oidParts(node); record.oid = oidText(node);
            record.name = text(node->name); record.moduleIdentity = text(module->name);
            record.qualifiedName = record.name.isEmpty() ? QString()
                : QStringLiteral("%1::%2").arg(record.moduleIdentity, record.name);
            record.kind = nodeKind(node->nodekind); record.declaration = node->decl;
            if (SmiNode *parent = smiGetParentNode(node)) record.parentOid = oidText(parent);
            for (SmiNode *child = smiGetFirstChildNode(node); child;
                 child = smiGetNextChildNode(child)) record.childOids.append(oidText(child));
            record.access = static_cast<MibEnvironmentAccess>(node->access);
            record.status = static_cast<MibEnvironmentStatusCode>(node->status);
            record.displayHint = text(node->format); record.units = text(node->units);
            record.description = text(node->description); record.reference = text(node->reference);
            record.defaultValue = value(node->value);
            if (SmiType *type = smiGetNodeType(node)) {
                record.syntaxName = text(type->name); record.typeId = typeId(type);
                if (record.typeId.isEmpty()) {
                    record.typeId = QStringLiteral("@node:%1").arg(record.qualifiedName.isEmpty()
                        ? record.oid : record.qualifiedName);
                    environment->typeRecords.append(typeRecord(type, record.typeId));
                }
                record.baseType = baseType(type->basetype);
                MibEnvironmentTypeRecord snapshot = typeRecord(type, record.typeId);
                record.constraints = snapshot.constraints; record.namedValues = snapshot.namedValues;
                record.textualConventionAncestry = snapshot.ancestry;
                for (SmiType *cursor = type; cursor; cursor = smiGetParentType(cursor)) {
                    if (cursor->decl == SMI_DECL_TEXTUALCONVENTION) {
                        record.textualConventionId = typeId(cursor); break;
                    }
                }
                if (record.displayHint.isEmpty()) {
                    for (SmiType *cursor = type; cursor && record.displayHint.isEmpty();
                         cursor = smiGetParentType(cursor)) record.displayHint = text(cursor->format);
                }
            }
            record.indexKind = indexKind(node->indexkind); record.implied = node->implied != 0;
            record.creatable = node->create != 0;
            if (SmiNode *related = smiGetRelatedNode(node)) {
                if (record.kind == MibEnvironmentNodeKind::Row) record.augmentsRowOid = oidText(related);
                else record.rowOid = oidText(related);
            }
            for (SmiElement *element = smiGetFirstElement(node); element;
                 element = smiGetNextElement(element)) {
                SmiNode *object = smiGetElementNode(element);
                if (!MibParserNodeHasReadableOid(object)) continue;
                if (record.kind == MibEnvironmentNodeKind::Notification)
                    record.notificationObjectOids.append(oidText(object));
                else {
                    SmiType *objectType = smiGetNodeType(object);
                    SmiModule *objectModule = smiGetNodeModule(object);
                    record.indexObjects.append({oidText(object),
                        QStringLiteral("%1::%2").arg(objectModule ? text(objectModule->name) : QString(),
                                                       text(object->name)),
                        typeId(objectType), objectType ? baseType(objectType->basetype)
                                                      : MibEnvironmentBaseType::Unknown});
                }
            }
            if (node->decl == SMI_DECL_TRAPTYPE && !record.oidParts.isEmpty()) {
                record.trapGeneric = 6; // enterpriseSpecific
                record.trapSpecific = static_cast<int>(record.oidParts.last());
                QList<quint32> enterprise = record.oidParts; enterprise.removeLast();
                if (!enterprise.isEmpty() && enterprise.last() == 0) enterprise.removeLast();
                QStringList parts; for (quint32 part : enterprise) parts.append(QString::number(part));
                record.trapEnterpriseOid = parts.join(u'.');
            }
            environment->nodeRecords.append(std::move(record));
        }
    }
    // libsmi's global tree also contains structural ancestors (notably iso,
    // org, dod and internet) that are not owned by any module enumeration.
    QSet<QString> extractedNodeKeys;
    for(const auto&present:environment->nodeRecords)
        extractedNodeKeys.insert(present.oid+u'\n'+present.moduleIdentity+u'\n'+present.name);
    QSet<SmiNode *> visitedStructuralNodes;
    std::function<void(SmiNode *)> appendStructural;
    appendStructural = [&environment, &extractedNodeKeys, &visitedStructuralNodes, &appendStructural](SmiNode *node) {
        if (!node || visitedStructuralNodes.contains(node)) return;
        visitedStructuralNodes.insert(node);
        {
            if (!MibParserNodeHasReadableOid(node)) {
                environment->materializationFindings.append({
                    MibEnvironmentFindingKind::MalformedModule, {}, {}, {},
                    QStringLiteral("structural parser node has nonzero OID length but no OID storage")});
            } else {
            MibEnvironmentNodeRecord record;
            record.oidParts=oidParts(node);record.oid=oidText(node);record.name=text(node->name);
            if(SmiModule*module=smiGetNodeModule(node))record.moduleIdentity=text(module->name);
            record.qualifiedName=record.moduleIdentity.isEmpty()?record.name:QStringLiteral("%1::%2").arg(record.moduleIdentity,record.name);record.kind=nodeKind(node->nodekind);record.declaration=node->decl;
            if(SmiNode*parent=smiGetParentNode(node))record.parentOid=oidText(parent);
            for(SmiNode*child=smiGetFirstChildNode(node);child;child=smiGetNextChildNode(child))record.childOids.append(oidText(child));
            record.access=static_cast<MibEnvironmentAccess>(node->access);record.status=static_cast<MibEnvironmentStatusCode>(node->status);
            record.description=text(node->description);record.reference=text(node->reference);
            const QString key=record.oid+u'\n'+record.moduleIdentity+u'\n'+record.name;
            if(!extractedNodeKeys.contains(key)){extractedNodeKeys.insert(key);environment->nodeRecords.append(std::move(record));}
            }
        }
        for(SmiNode*child=smiGetFirstChildNode(node);child;child=smiGetNextChildNode(child))appendStructural(child);
    };
    appendStructural(smiGetNode(nullptr,"iso"));
    std::sort(environment->nodeRecords.begin(), environment->nodeRecords.end(), oidLess);
    std::sort(environment->typeRecords.begin(), environment->typeRecords.end(),
              [](const auto &a, const auto &b) { return a.id < b.id; });

    for (qsizetype i = 0; i < environment->moduleRecords.size(); ++i)
        environment->moduleIndex.insert(environment->moduleRecords[i].identity, i);
    for (qsizetype i = 0; i < environment->typeRecords.size(); ++i)
        if (!environment->typeIndex.contains(environment->typeRecords[i].id))
            environment->typeIndex.insert(environment->typeRecords[i].id, i);
    for (qsizetype i = 0; i < environment->nodeRecords.size(); ++i) {
        const auto &node = environment->nodeRecords[i];
        environment->oidIndex[node.oid].append(i);
        if (!node.qualifiedName.isEmpty() && !environment->qualifiedNameIndex.contains(node.qualifiedName))
            environment->qualifiedNameIndex.insert(node.qualifiedName, i);
        if (!node.name.isEmpty()) environment->unqualifiedNameIndex[node.name].append(i);
    }
    for (auto &node : environment->nodeRecords) {
        std::sort(node.childOids.begin(), node.childOids.end(), oidTextLess);
        if (node.parentOid.isEmpty() || !environment->oidIndex.contains(node.parentOid))
            environment->roots.append(node.oid);
    }
    for (auto &node : environment->nodeRecords) {
        if (node.kind == MibEnvironmentNodeKind::Row) {
            const auto parents = environment->oidIndex.value(node.parentOid);
            if (!parents.isEmpty()) {
                node.tableOid = node.parentOid;
                for (qsizetype parent : parents)
                    if (environment->nodeRecords[parent].kind == MibEnvironmentNodeKind::Table)
                        environment->nodeRecords[parent].rowOid = node.oid;
            }
            for (const QString &child : node.childOids) {
                const auto columns = environment->oidIndex.value(child);
                bool isColumn = false;
                for (qsizetype column : columns) if (environment->nodeRecords[column].kind == MibEnvironmentNodeKind::Column) {
                    isColumn = true;
                    environment->nodeRecords[column].rowOid = node.oid;
                    environment->nodeRecords[column].tableOid = node.tableOid;
                }
                if (isColumn) {
                    node.columnOids.append(child);
                }
            }
        }
    }
    environment->roots.removeDuplicates();
    std::sort(environment->roots.begin(), environment->roots.end(), oidTextLess);

    environment->loadedModuleCount = environment->moduleRecords.size();
    environment->failedModuleCount = failed.size();
    environment->constructionStatus = failed.isEmpty() ? MibEnvironmentStatus::Complete
                                                        : MibEnvironmentStatus::Partial;
    if (plan.hasRuntimePaths) {
        QSet<QString> outcomeIdentities;
        bool rootsSucceeded = rootOutcomes.size() == environment->requestedRoots.size();
        for (const auto &outcome : rootOutcomes) {
            outcomeIdentities.insert(outcome.identity);
            if (!outcome.success) {
                rootsSucceeded = false;
                environment->authorityDiagnostics.append(outcome.diagnostic.isEmpty()
                    ? QStringLiteral("Root '%1' did not materialize").arg(outcome.identity)
                    : outcome.diagnostic);
            }
        }
        for (const QString &root : std::as_const(environment->requestedRoots))
            if (!outcomeIdentities.contains(root)) {
                rootsSucceeded = false;
                environment->authorityDiagnostics.append(
                    QStringLiteral("Root '%1' has no final load outcome").arg(root));
            }
        const bool materializedRequestedRoot = environment->requestedRoots.isEmpty() ||
            std::any_of(rootOutcomes.cbegin(), rootOutcomes.cend(),
                [](const auto &outcome) { return outcome.success; });
        // Authority is about integrity: the configuration matches the plan, the
        // loaded files are exactly the authorized ones, and at least one requested
        // root really loaded. Individual module failures do not break authority;
        // they make the Environment Partial and are reported as problems.
        environment->runtimeAuthorityComplete =
            plan.runtimeConfiguration.profileId() == plan.profileId &&
            !plan.runtimeConfiguration.sha256().isEmpty() &&
            !plan.runtimePaths.sha256().isEmpty() && plan.runtimePaths.isValid() &&
            materializedRequestedRoot && environment->loadedProvidersAuthorized;
        if (!environment->runtimeAuthorityComplete)
            environment->constructionStatus = MibEnvironmentStatus::Unusable;
        else if (!rootsSucceeded && environment->constructionStatus == MibEnvironmentStatus::Complete)
            environment->constructionStatus = MibEnvironmentStatus::Partial;
    }
    if (!failed.isEmpty()) environment->materializationFindings.append({
        MibEnvironmentFindingKind::PartialLoad, {}, {}, {},
        QStringLiteral("%1 planned module(s) failed").arg(failed.size())});
    auto &metrics = environment->metrics;
    metrics.moduleCount = environment->moduleRecords.size(); metrics.nodeCount = environment->nodeRecords.size();
    metrics.typeCount = environment->typeRecords.size();
    auto count = [&metrics](const QString &s) { metrics.ownedUtf16Characters += s.size(); };
    for (const auto &module : environment->moduleRecords) {
        count(module.identity); count(module.plannedProviderPath); count(module.actualProviderPath);
        count(module.rawProviderSha256); count(module.organization); count(module.contactInfo);
        count(module.description); count(module.reference); count(module.rootOid); count(module.rootName);
        for (const auto &item : module.imports) { count(item.moduleIdentity); count(item.symbol); }
        for (const auto &item : module.revisions) count(item.description);
    }
    for (const auto &type : environment->typeRecords) {
        count(type.id); count(type.moduleIdentity); count(type.name); count(type.parentTypeId);
        count(type.displayHint); count(type.units); count(type.description); count(type.reference);
        metrics.constraintCount += type.constraints.size(); metrics.enumRecordCount += type.namedValues.size();
    }
    for (const auto &node : environment->nodeRecords) {
        count(node.oid); count(node.name); count(node.moduleIdentity); count(node.qualifiedName);
        count(node.description); count(node.reference); count(node.displayHint); count(node.units);
        metrics.constraintCount += node.constraints.size(); metrics.enumRecordCount += node.namedValues.size();
        metrics.tableIndexRecordCount += node.indexObjects.size();
        if (node.kind == MibEnvironmentNodeKind::Notification) ++metrics.notificationCount;
    }
    metrics.approximateOwnedBytes = metrics.ownedUtf16Characters * sizeof(QChar)
        + metrics.nodeCount * sizeof(MibEnvironmentNodeRecord)
        + metrics.moduleCount * sizeof(MibEnvironmentModuleRecord)
        + metrics.typeCount * sizeof(MibEnvironmentTypeRecord)
        + metrics.enumRecordCount * sizeof(MibEnvironmentNamedValue)
        + metrics.constraintCount * sizeof(MibEnvironmentConstraint);
    metrics.extractionMilliseconds = timer.elapsed();
    return std::const_pointer_cast<const MibEnvironment>(environment);
}
