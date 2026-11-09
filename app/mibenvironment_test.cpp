#include "mibenvironmentextractor.h"
#include "mibenvironmentregistry.h"
#include "mibparsernodesafety.h"

#include "smi.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <algorithm>
#include <iostream>

namespace {
bool check(bool condition, const char *message)
{
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}

QString canonical(const QString &path)
{
    const QString value = QFileInfo(path).canonicalFilePath();
    return value.isEmpty() ? QFileInfo(path).absoluteFilePath() : value;
}

void appendLoadedToPlan(MibEffectivePlan *plan)
{
    for (SmiModule *module = smiGetFirstModule(); module; module = smiGetNextModule(module)) {
        const QString identity = QString::fromLocal8Bit(module->name ? module->name : "");
        if (identity.isEmpty() || plan->effectiveModules.contains(identity)) continue;
        MibEffectivePlanMember member;
        member.identity = identity;
        member.provider.canonicalPath = canonical(QString::fromLocal8Bit(module->path ? module->path : ""));
        member.provider.sha256 = QStringLiteral("test-provider-hash-%1").arg(identity);
        for (SmiImport *item = smiGetFirstImport(module); item; item = smiGetNextImport(item)) {
            const QString imported = QString::fromLocal8Bit(item->module ? item->module : "");
            if (!member.imports.contains(imported)) member.imports.append(imported);
        }
        plan->members.append(member); plan->effectiveModules.append(identity);
    }
    plan->effectiveModules.sort(Qt::CaseSensitive);
    plan->sha256 = QStringLiteral("environment-test-plan");
}
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    const bool largeMode = application.arguments().contains("--large");
    bool ok = true;
    SmiNode malformedNode{};
    malformedNode.oidlen = 1;
    malformedNode.oid = nullptr;
    ok &= check(!MibParserNodeHasReadableOid(&malformedNode) &&
                MibParserNodeOidParts(&malformedNode).isEmpty(),
                "malformed parser node OID storage is rejected without dereference");
    malformedNode.oid = reinterpret_cast<SmiSubid *>(std::uintptr_t{1});
    ok &= check(!MibParserNodeHasReadableOid(&malformedNode) &&
                MibParserNodeOidParts(&malformedNode).isEmpty(),
                "SPPI numeric pseudo-object OID sentinel is rejected without dereference");
    const QString root = QStringLiteral(SNMPB_SOURCE_DIR);
    QTemporaryDir vendorDirectory;
    ok &= check(vendorDirectory.isValid(), "vendor fixture directory");
    const auto fixture = [&vendorDirectory, &ok](const QString &name, const QByteArray &body) {
        QFile file(vendorDirectory.filePath(name));
        ok &= check(file.open(QIODevice::WriteOnly), "vendor fixture writable");
        file.write(body); file.close(); return file.fileName();
    };
    const QString oddProvider = fixture("different-physical-name.my",
        "ENV-VENDOR-MIB DEFINITIONS ::= BEGIN\n"
        "IMPORTS enterprises FROM RFC1155-SMI;\n"
        "envVendor OBJECT IDENTIFIER ::= { enterprises 424245 }\nEND\n");
    const QString multiProvider = fixture("environment-multi-bundle.mib",
        "ENV-MULTI-ONE-MIB DEFINITIONS ::= BEGIN\n"
        "IMPORTS enterprises FROM RFC1155-SMI;\n"
        "envMultiOne OBJECT IDENTIFIER ::= { enterprises 424246 }\nEND\n"
        "ENV-MULTI-TWO-MIB DEFINITIONS ::= BEGIN\n"
        "IMPORTS enterprises FROM RFC1155-SMI;\n"
        "envMultiTwo OBJECT IDENTIFIER ::= { enterprises 424247 }\nEND\n");
    const QStringList paths = {
        vendorDirectory.path(),
        root + "/libsmi/mibs/ietf", root + "/libsmi/mibs/iana",
        root + "/libsmi/mibs/irtf", root + "/libsmi/mibs/site", root + "/libsmi/mibs/tubs"};
    smiInit("mib-environment-test");
    smiSetPath(paths.join(QDir::listSeparator()).toLocal8Bit().constData());
    smiSetFlags((smiGetFlags() | SMI_FLAG_ERRORS) & ~SMI_FLAG_NODESCR);
    for (const char *identity : {"SNMPv2-MIB", "IF-MIB", "BRIDGE-MIB", "RMON2-MIB", "MAU-MIB"})
        ok &= check(smiLoadModule(identity) != nullptr, identity);
    if (largeMode) {
        for (const QString &directory : paths) {
            const QStringList files = QDir(directory).entryList(QDir::Files, QDir::Name);
            for (const QString &file : files)
                smiLoadModule(QDir::toNativeSeparators(QDir(directory).filePath(file)).toLocal8Bit().constData());
        }
    }
    ok &= check(smiLoadModule(QDir::toNativeSeparators(oddProvider).toLocal8Bit().constData()) != nullptr,
                "filename-different vendor fixture loads");
    ok &= check(smiLoadModule(QDir::toNativeSeparators(multiProvider).toLocal8Bit().constData()) != nullptr,
                "multiple-identity fixture loads");

    MibEffectivePlan plan;
    appendLoadedToPlan(&plan);
    SmiModule *liveIfModule = smiGetModule("IF-MIB");
    SmiNode *liveIfEntry = smiGetNode(liveIfModule, "ifEntry");
    const QString liveIfEntryOid = liveIfEntry ? QString::fromLatin1(
        smiRenderOID(liveIfEntry->oidlen, liveIfEntry->oid, SMI_RENDER_NUMERIC)) : QString();
    QStringList liveIndexes;
    for (SmiElement *item = liveIfEntry ? smiGetFirstElement(liveIfEntry) : nullptr; item;
         item = smiGetNextElement(item)) {
        SmiNode *object = smiGetElementNode(item);
        SmiModule *objectModule = object ? smiGetNodeModule(object) : nullptr;
        liveIndexes.append(QStringLiteral("%1::%2").arg(
            objectModule ? QString::fromLocal8Bit(objectModule->name) : QString(),
            object ? QString::fromLocal8Bit(object->name) : QString()));
    }
    MibEnvironmentPtr environment = MibEnvironmentExtractor().extract(plan);
    ok &= check(environment && environment->status() == MibEnvironmentStatus::Complete,
                "complete Environment extracted");
    ok &= check(environment->schemaVersion() == MIB_ENVIRONMENT_SCHEMA_VERSION &&
                environment->builderVersion() == MIB_ENVIRONMENT_BUILDER_VERSION,
                "schema and builder identity");
    ok &= check(environment->module("SNMPv2-MIB") && environment->module("IF-MIB") &&
                environment->module("BRIDGE-MIB"), "module lookup");
    bool hasSmiV1 = false, hasSmiV2 = false;
    for (const auto &module : environment->modules()) {
        hasSmiV1 |= module.language == MibEnvironmentLanguage::SmiV1;
        hasSmiV2 |= module.language == MibEnvironmentLanguage::SmiV2;
    }
    ok &= check(hasSmiV1 && hasSmiV2, "SMIv1 and SMIv2 module languages retained");
    ok &= check(environment->module("ENV-VENDOR-MIB") &&
                environment->module("ENV-VENDOR-MIB")->actualProviderPath == canonical(oddProvider),
                "filename is not confused with module identity");
    ok &= check(environment->module("ENV-MULTI-ONE-MIB") &&
                environment->module("ENV-MULTI-TWO-MIB") &&
                environment->module("ENV-MULTI-ONE-MIB")->actualProviderPath ==
                    environment->module("ENV-MULTI-TWO-MIB")->actualProviderPath,
                "multiple identities in one file remain distinct");
    const auto *sysDescr = environment->nodeByQualifiedName("SNMPv2-MIB::sysDescr");
    ok &= check(sysDescr && sysDescr->oid == "1.3.6.1.2.1.1.1" &&
                !sysDescr->description.isEmpty() && !sysDescr->typeId.isEmpty(),
                "node semantic snapshot and qualified lookup");
    const auto oidAliases = environment->nodesByOid("1.3.6.1.2.1.1.1");
    ok &= check(std::find(oidAliases.cbegin(), oidAliases.cend(), sysDescr) != oidAliases.cend(),
                "numeric OID index preserves deterministic aliases");
    ok &= check(!environment->nodesByName("sysDescr").isEmpty(),
                "deterministic unqualified lookup preserves candidates");
    QStringList instanceSuffix;
    const auto *instanceNode = environment->longestPrefixNode("1.3.6.1.2.1.2.2.1.2.27", &instanceSuffix);
    ok &= check(instanceNode && instanceNode->name == "ifDescr" && instanceSuffix == QStringList{"27"},
                "longest-prefix symbolic lookup retains instance suffix");
    const auto *ifType = environment->nodeByQualifiedName("IF-MIB::ifType");
    ok &= check(ifType && ifType->baseType == MibEnvironmentBaseType::Enumeration &&
                !ifType->namedValues.isEmpty(), "enumeration values retained");
    const auto *ifTable = environment->nodeByQualifiedName("IF-MIB::ifTable");
    const auto *ifEntry = environment->nodeByQualifiedName("IF-MIB::ifEntry");
    ok &= check(ifTable && ifEntry && ifTable->rowOid == ifEntry->oid &&
                ifEntry->tableOid == ifTable->oid && !ifEntry->columnOids.isEmpty() &&
                !ifEntry->indexObjects.isEmpty(), "table row columns and ordered indexes");
    QStringList extractedIndexes;
    if (ifEntry) for (const auto &item : ifEntry->indexObjects) extractedIndexes.append(item.qualifiedName);
    ok &= check(ifEntry && ifEntry->oid == liveIfEntryOid && extractedIndexes == liveIndexes,
                "Environment table semantics match live libsmi interpretation");
    const auto *ifXEntry = environment->nodeByQualifiedName("IF-MIB::ifXEntry");
    ok &= check(ifXEntry && !ifXEntry->augmentsRowOid.isEmpty(), "AUGMENTS retained");
    const auto *linkDown = environment->nodeByQualifiedName("IF-MIB::linkDown");
    ok &= check(linkDown && linkDown->kind == MibEnvironmentNodeKind::Notification &&
                !linkDown->notificationObjectOids.isEmpty(), "notification object order retained");
    bool hasConstraints = false, hasBits = false, hasSigned = false;
    for (const auto &type : environment->types()) {
        hasConstraints |= !type.constraints.isEmpty();
        hasBits |= type.baseType == MibEnvironmentBaseType::Bits && !type.namedValues.isEmpty();
        for (const auto &constraint : type.constraints)
            hasSigned |= constraint.minimum.isSigned;
    }
    ok &= check(hasConstraints && hasBits && hasSigned,
                "ranges/sizes, BITS and signed constraints retained");
    ok &= check(environment->telemetry().moduleCount == environment->modules().size() &&
                environment->telemetry().nodeCount == environment->nodes().size() &&
                environment->telemetry().ownedUtf16Characters > 0 &&
                environment->telemetry().approximateOwnedBytes > 0,
                "deterministic Environment telemetry");

    MibEffectivePlan mismatchPlan = plan;
    if (!mismatchPlan.members.isEmpty()) {
        mismatchPlan.members.first().provider.canonicalPath += ".different";
        mismatchPlan.members.first().imports = {"INTENTIONALLY-DIFFERENT-MIB"};
    }
    MibEnvironmentPtr mismatched = MibEnvironmentExtractor().extract(mismatchPlan);
    bool providerMismatch = false, importMismatch = false;
    for (const auto &finding : mismatched->findings()) {
        providerMismatch |= finding.kind == MibEnvironmentFindingKind::ProviderMismatch;
        importMismatch |= finding.kind == MibEnvironmentFindingKind::ImportMismatch;
    }
    ok &= check(providerMismatch && importMismatch, "provider and scanner/parser mismatches are structured");

    MibEffectivePlan partialPlan = plan;
    partialPlan.effectiveModules.append("NO-SUCH-MIB");
    MibEnvironmentPtr partial = MibEnvironmentExtractor().extract(partialPlan, {"NO-SUCH-MIB"});
    bool failedIdentityFinding = false;
    for (const auto &finding : partial->findings())
        failedIdentityFinding |= finding.kind == MibEnvironmentFindingKind::PlannedModuleFailed &&
                                 finding.moduleIdentity == QStringLiteral("NO-SUCH-MIB");
    MibEnvironmentRegistry::publish(environment);
    ok &= check(partial->status() == MibEnvironmentStatus::Partial && partial->failedCount() == 1 &&
                partial->loadedCount() > 0 && partial->module("SNMPv2-MIB") && failedIdentityFinding &&
                MibEnvironmentRegistry::publishMaterialization(partial) &&
                MibEnvironmentRegistry::active() == partial,
                "usable partial materialization publishes successful modules and failed findings");

    MibEffectivePlan fatalPlan;
    fatalPlan.sha256 = QStringLiteral("fatal-materialization");
    fatalPlan.effectiveModules = {QStringLiteral("NO-SUCH-MIB")};
    MibEnvironmentPtr fatal = MibEnvironmentExtractor().extract(fatalPlan, fatalPlan.effectiveModules);
    ok &= check(fatal->status() == MibEnvironmentStatus::Partial && fatal->failedCount() == 1 &&
                !MibEnvironmentRegistry::publishMaterialization(fatal) &&
                MibEnvironmentRegistry::active() == partial,
                "fatal materialization preserves the previous successful Environment");

    const QString savedDescription = sysDescr ? sysDescr->description : QString();
    const QString savedRevision = environment->module("SNMPv2-MIB") &&
        !environment->module("SNMPv2-MIB")->revisions.isEmpty()
        ? environment->module("SNMPv2-MIB")->revisions.first().description : QString();
    const QString savedIndex = ifEntry && !ifEntry->indexObjects.isEmpty()
        ? ifEntry->indexObjects.first().qualifiedName : QString();
    const QString savedNotification = linkDown && !linkDown->notificationObjectOids.isEmpty()
        ? linkDown->notificationObjectOids.first() : QString();
    smiExit();
    ok &= check(environment->nodeByQualifiedName("SNMPv2-MIB::sysDescr") &&
                environment->nodeByQualifiedName("SNMPv2-MIB::sysDescr")->description == savedDescription,
                "node strings survive smiExit");
    ok &= check(environment->module("SNMPv2-MIB") &&
                environment->module("SNMPv2-MIB")->revisions.first().description == savedRevision,
                "module revisions survive smiExit");
    ok &= check(environment->nodeByQualifiedName("IF-MIB::ifEntry") &&
                environment->nodeByQualifiedName("IF-MIB::ifEntry")->indexObjects.first().qualifiedName == savedIndex,
                "table indexes survive smiExit");
    ok &= check(environment->nodeByQualifiedName("IF-MIB::linkDown") &&
                environment->nodeByQualifiedName("IF-MIB::linkDown")->notificationObjectOids.first() == savedNotification,
                "notifications survive smiExit");

    QFile publicHeader(root + "/app/mibenvironment.h");
    ok &= check(publicHeader.open(QIODevice::ReadOnly), "public Environment header readable");
    const QByteArray header = publicHeader.readAll();
    ok &= check(!header.contains("smi.h") && !header.contains("SmiNode") &&
                !header.contains("SmiType") && !header.contains("SmiModule") &&
                !header.contains("QModelIndex") && !header.contains("QWidget"),
                "public Environment header exposes no parser or UI types");

    if (ok) std::cout << (largeMode ? "Large Environment" : "Environment")
                      << " planned=" << environment->plannedCount()
                      << " materialized=" << environment->loadedCount()
                      << " failed=" << environment->failedCount()
                      << " findings=" << environment->findings().size()
                      << " modules=" << environment->telemetry().moduleCount
                      << " nodes=" << environment->telemetry().nodeCount
                      << " types=" << environment->telemetry().typeCount
                      << " utf16-characters=" << environment->telemetry().ownedUtf16Characters
                      << " approximate-bytes=" << environment->telemetry().approximateOwnedBytes
                      << " extraction-ms=" << environment->telemetry().extractionMilliseconds << '\n';
    return ok ? 0 : 1;
}
