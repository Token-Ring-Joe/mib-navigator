#include "mibservice.h"
#include "mibservice_internal.h"
#include "mibdiagnosticmodel.h"
#include "mibtreemodel.h"
#include "mibcandidatefilter.h"
#include "mibmodelview.h"

#include <QApplication>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QTextStream>
#include <QSortFilterProxyModel>
#include <algorithm>

namespace {
int failures = 0;
void check(bool condition, const char *message)
{
    if (!condition) { QTextStream(stderr) << "FAIL: " << message << Qt::endl; ++failures; }
}

int nodeCount(const MibTreeNodeRecord &node)
{
    int result = 1;
    for (const auto &child : node.children) result += nodeCount(child);
    return result;
}
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QTemporaryDir temporary;
    check(temporary.isValid(), "temporary directory created");
    const QString bundled = QStringLiteral(SNMPB_SOURCE_DIR "/libsmi/mibs/ietf");

    const auto writeFixture = [&](const QString &name, const QString &text) {
        QFile file(temporary.filePath(name));
        check(file.open(QIODevice::WriteOnly | QIODevice::Text), "custom-path fixture created");
        QTextStream(&file) << text;
        return file.fileName();
    };
    const QString vendorBasePath = writeFixture("VENDOR-BASE-MIB",
        "VENDOR-BASE-MIB DEFINITIONS ::= BEGIN\n"
        "IMPORTS enterprises FROM RFC1155-SMI;\n"
        "vendorBase OBJECT IDENTIFIER ::= { enterprises 424242 }\nEND\n");
    const QString vendorChildPath = writeFixture("different-physical-name.my",
        "VENDOR-CHILD-MIB DEFINITIONS ::= BEGIN\n"
        "IMPORTS vendorBase FROM VENDOR-BASE-MIB;\n"
        "vendorChild OBJECT IDENTIFIER ::= { vendorBase 1 }\nEND\n");
    const QString missingDependencyPath = writeFixture("VENDOR-MISSING-MIB",
        "VENDOR-MISSING-MIB DEFINITIONS ::= BEGIN\n"
        "IMPORTS missingRoot FROM NO-SUCH-VENDOR-BASE-MIB;\n"
        "vendorMissing OBJECT IDENTIFIER ::= { missingRoot 1 }\nEND\n");
    const QString multiModulePath = writeFixture("vendor-multi-bundle.mib",
        "VENDOR-MULTI-ONE-MIB DEFINITIONS ::= BEGIN\n"
        "IMPORTS enterprises FROM RFC1155-SMI;\n"
        "vendorMultiOne OBJECT IDENTIFIER ::= { enterprises 424243 }\nEND\n"
        "VENDOR-MULTI-TWO-MIB DEFINITIONS ::= BEGIN\n"
        "IMPORTS enterprises FROM RFC1155-SMI;\n"
        "vendorMultiTwo OBJECT IDENTIFIER ::= { enterprises 424244 }\nEND\n");

    const QString malformedPath = temporary.filePath("BROKEN-TEST-MIB");
    QFile malformed(malformedPath);
    check(malformed.open(QIODevice::WriteOnly | QIODevice::Text), "malformed fixture created");
    QTextStream(&malformed)
        << "BROKEN-TEST-MIB DEFINITIONS ::= BEGIN\n"
           "IMPORTS MODULE-IDENTITY FROM SNMPv2-SMI;\n"
           "broken MODULE-IDENTITY LAST-UPDATED \"bad\" STATUS current "
           "DESCRIPTION \"broken\" ::= { enterprises 99999 }\nEND\n";
    malformed.close();
    const QString malformedPath2 = temporary.filePath("BROKEN-TEST-MIB-TWO");
    QFile malformed2(malformedPath2);
    check(malformed2.open(QIODevice::WriteOnly | QIODevice::Text), "second malformed fixture created");
    QTextStream(&malformed2)
        << "BROKEN-TEST-MIB-TWO DEFINITIONS ::= BEGIN\n"
           "broken OBJECT IDENTIFIER ::= { definitelyMissing 1 }\nEND\n";
    malformed2.close();

    smiInit("snmpb-mib-service-test");
    smiSetFlags((smiGetFlags() | SMI_FLAG_ERRORS) & ~SMI_FLAG_NODESCR);
    MibService service;
    service.setSearchPaths({temporary.path(), bundled});
    check(service.searchPaths() == QStringList{temporary.path(), bundled},
          "configured path order is retained");

    MibLoadResult vendorBaseLoad = service.loadModules(
        {QDir::toNativeSeparators(vendorBasePath)}, 9);
    check(vendorBaseLoad.status == MibLoadStatus::Success &&
          vendorBaseLoad.loadedModules == QStringList{"VENDOR-BASE-MIB"},
          "custom-path module loads from physical file");
    MibLoadResult vendorChildLoad = service.loadModules(
        {QDir::toNativeSeparators(vendorChildPath)}, 9);
    check(vendorChildLoad.status == MibLoadStatus::Success &&
          vendorChildLoad.loadedModules == QStringList{"VENDOR-CHILD-MIB"} &&
          smiIsLoaded("VENDOR-BASE-MIB"),
          "filename-different module loads by declared identity with vendor dependency resolved");
    const QList<MibModuleRecord> childRecords = service.modulesFromFile(vendorChildPath);
    check(childRecords.size() == 1 && childRecords.first().name == "VENDOR-CHILD-MIB" &&
          QFileInfo(childRecords.first().path).canonicalFilePath() ==
              QFileInfo(vendorChildPath).canonicalFilePath(),
          "custom file snapshots declared identity without retaining libsmi pointers");
    MibLoadResult multiModuleLoad = service.loadModules(
        {QDir::toNativeSeparators(multiModulePath)}, 9);
    const QList<MibModuleRecord> multiRecords = service.modulesFromFile(multiModulePath);
    QStringList multiNames;
    for (const MibModuleRecord &record : multiRecords) multiNames.append(record.name);
    check(multiModuleLoad.status == MibLoadStatus::Success &&
          multiNames.contains("VENDOR-MULTI-ONE-MIB") &&
          multiNames.contains("VENDOR-MULTI-TWO-MIB"),
          "multiple declared modules in one physical file remain discoverable");
    MibLoadResult missingDependency = service.loadModules(
        {QDir::toNativeSeparators(missingDependencyPath)}, 9);
    check(!missingDependency.diagnostics.isEmpty() && smiIsLoaded("VENDOR-CHILD-MIB") &&
          (missingDependency.loadedModules.contains("VENDOR-MISSING-MIB") ||
           missingDependency.unavailableModules.contains(
               QDir::toNativeSeparators(missingDependencyPath))),
          "missing dependency is diagnosed in isolation without removing valid vendor modules");

    MibLoadResult first = service.loadModules({"SNMPv2-MIB"}, 9);
    check(first.status == MibLoadStatus::Success &&
          first.loadedModules.contains("SNMPv2-MIB"), "known bundled module loads canonically");
    MibLoadResult already = service.loadModules({"SNMPv2-MIB"}, 9);
    check(already.alreadyLoadedModules == QStringList{"SNMPv2-MIB"},
          "already-loaded module is classified without reload");
    check(already.diagnostics.isEmpty(), "already-loaded successful request emits no diagnostics");
    MibLoadResult partial = service.loadModules({"IF-MIB", "NO-SUCH-MIB-FOR-TEST"}, 9);
    check(partial.status == MibLoadStatus::Partial &&
          partial.loadedModules.contains("IF-MIB") &&
          partial.unavailableModules == QStringList{"NO-SUCH-MIB-FOR-TEST"},
          "multiple-module partial result is deterministic");
    MibLoadResult preloads = service.loadPreloads({"IF-MIB", "SNMPv2-MIB"}, 9);
    check(preloads.alreadyLoadedModules == QStringList({"IF-MIB", "SNMPv2-MIB"}),
          "preload handling preserves request ordering and loaded state");
    MibLoadResult metadataLoads = service.loadModules(
        {"ACCOUNTING-CONTROL-MIB", "BRIDGE-MIB"}, 9);
    check(metadataLoads.status == MibLoadStatus::Success,
          "representative metadata modules load");
    const MibModuleRecord accounting = SnapshotMibModule(
        smiGetModule("ACCOUNTING-CONTROL-MIB"));
    check(accounting.name == "ACCOUNTING-CONTROL-MIB" &&
          accounting.rootOid == "1.3.6.1.2.1.60",
          "MODULE-IDENTITY name and numeric OID snapshot");
    check(accounting.organization == "IETF AToM MIB Working Group" &&
          accounting.contactInfo.contains("Cisco Systems, Inc.") &&
          accounting.contactInfo.contains('\n') && accounting.description.contains('\n') &&
          accounting.description.contains("collection and storage of"),
          "organization/contact/description preserve multiline metadata");
    check(accounting.lastRevision.toUTC() ==
          QDateTime(QDate(1998, 9, 28), QTime(10, 0), Qt::UTC) &&
          !accounting.revisions.isEmpty(), "latest revision date snapshot");
    const MibModuleRecord bridge = SnapshotMibModule(smiGetModule("BRIDGE-MIB"));
    check(bridge.name == "BRIDGE-MIB" && bridge.reference.isEmpty(),
          "missing optional module reference is represented safely");
    MibLoadResult bad = service.loadModules({malformedPath}, 9);
    check(!bad.diagnostics.isEmpty(), "malformed-MIB diagnostics captured");
    MibLoadResult badAgain = service.loadModules({malformedPath2}, 9);
    QList<MibDiagnosticRecord> multiple = bad.diagnostics;
    multiple.append(badAgain.diagnostics);
    check(multiple.size() > 1 && multiple.first().operationId == bad.operationId &&
          multiple.last().operationId == badAgain.operationId,
          "multiple diagnostics preserve operation and callback ordering");
    for (int i = 0; i < bad.diagnostics.size(); ++i) {
        const auto &diagnostic = bad.diagnostics.at(i);
        check(diagnostic.operationId == bad.operationId && diagnostic.line >= 0 &&
              !diagnostic.message.isEmpty() && !diagnostic.rawText.isEmpty(),
              "diagnostic attribution and original wording retained");
        if (i > 0)
            check(bad.diagnostics.at(i - 1).operationId == diagnostic.operationId,
                  "diagnostic ordering retained within operation");
    }
    MibDiagnosticModel diagnosticModel;
    diagnosticModel.setDiagnostics(bad.diagnostics);
    check(diagnosticModel.rowCount() == bad.diagnostics.size() &&
          diagnosticModel.data(diagnosticModel.index(0, MibDiagnosticModel::MessageColumn)) ==
              bad.diagnostics.first().message &&
          diagnosticModel.data(diagnosticModel.index(0, 0),
                               MibDiagnosticModel::RawTextRole) ==
              bad.diagnostics.first().rawText,
          "diagnostics model exposes structured and original wording");
    QSortFilterProxyModel diagnosticFilter;
    diagnosticFilter.setSourceModel(&diagnosticModel);
    diagnosticFilter.setFilterKeyColumn(MibDiagnosticModel::SeverityColumn);
    diagnosticFilter.setFilterRegularExpression(QStringLiteral("^(Error|Warning|Info)"));
    check(diagnosticFilter.rowCount() == diagnosticModel.rowCount(),
          "structured diagnostic severity filtering retains original records");
    check(MibCandidateFilter::accepts("NET-SNMP-EXAMPLE.txt") &&
          MibCandidateFilter::accepts("VENDOR-MIB") &&
          !MibCandidateFilter::accepts("notes.log") &&
          !MibCandidateFilter::accepts("BROKEN-MIB-orig"),
          "historical MIB candidate rules retain txt support and reject unrelated extensions");

    const QList<MibModuleRecord> inventory = service.moduleInventory();
    check(inventory.size() >= 2, "multiple loaded modules inventoried");
    for (int i = 1; i < inventory.size(); ++i)
        check(inventory.at(i - 1).name <= inventory.at(i).name,
              "module inventory has deterministic canonical ordering");
    auto snmpModule = std::find_if(inventory.begin(), inventory.end(), [](const auto &module) {
        return module.name == "SNMPv2-MIB";
    });
    check(snmpModule != inventory.end() && snmpModule->loaded &&
          !snmpModule->path.isEmpty() && !snmpModule->imports.isEmpty(),
          "module record snapshots metadata without pointers");

    const MibTreeNodeRecord snapshot = service.treeSnapshot({"SNMPv2-MIB", "IF-MIB"});
    check(snapshot.name == "MIB Tree" && snapshot.oid == "1" &&
          !snapshot.children.isEmpty() && nodeCount(snapshot) > 100,
          "value tree captures hierarchy at useful scale");
    MibTreeModel model;
    model.setSnapshot(snapshot);
    const QModelIndex sysDescr = model.indexForOid("1.3.6.1.2.1.1.1");
    check(sysDescr.isValid() && model.data(sysDescr).toString() == "sysDescr" &&
          model.data(sysDescr, MibTreeModel::ModuleRole).toString() == "SNMPv2-MIB",
          "tree roles and OID identity are available");
    check(!model.data(sysDescr, MibTreeModel::BaseTypeRole).toString().isEmpty() &&
          model.data(sysDescr, MibTreeModel::AccessRole).toString() == "read-only",
          "tree snapshot exposes value-based node details");
    check(model.parent(sysDescr).isValid(), "model parent relationship is valid");
    MibTreeFilterModel filter;
    filter.setSourceModel(&model);
    filter.setFilterFixedString("sysDescr");
    check(filter.rowCount() > 0, "recursive filtering preserves matching ancestry");

    MibModelView visibleTree;
    visibleTree.setTreeModel(&model);
    QString details;
    QObject::connect(&visibleTree, &MibModelView::NodeProperties,
                     [&details](const QString &text) { details = text; });
    visibleTree.SelectFromOid("1.3.6.1.2.1.1.1");
    check(qobject_cast<MibTreeFilterModel *>(visibleTree.model()) &&
          visibleTree.selectedOid() == "1.3.6.1.2.1.1.1" &&
          details.contains("sysDescr") && details.contains("SNMPv2-MIB"),
          "visible QTreeView uses filtered value model and value-based details");
    const QString retainedOid = model.oidForIndex(sysDescr);

    MibTreeNodeRecord tableFixture;
    tableFixture.oid = "1.3.6.1.4.1.999.1";
    tableFixture.name = "exampleTable";
    tableFixture.nodeKind = SMI_NODEKIND_TABLE;
    MibTreeNodeRecord entryFixture;
    entryFixture.oid = "1.3.6.1.4.1.999.1.1";
    entryFixture.name = "exampleEntry";
    entryFixture.nodeKind = SMI_NODEKIND_ROW;
    MibTreeNodeRecord columnFixture;
    columnFixture.oid = "1.3.6.1.4.1.999.1.1.1";
    columnFixture.name = "exampleName";
    columnFixture.nodeKind = SMI_NODEKIND_COLUMN;
    entryFixture.children.append(columnFixture);
    tableFixture.children.append(entryFixture);
    MibTreeNodeRecord tableRoot;
    tableRoot.oid = "1";
    tableRoot.name = "MIB Tree";
    tableRoot.children.append(tableFixture);
    model.setSnapshot(tableRoot);

    QStringList tableRequests;
    QObject::connect(&visibleTree, &MibModelView::TableViewFromOid,
                     [&tableRequests](const QString &oid) { tableRequests.append(oid); });
    visibleTree.SelectFromOid(tableFixture.oid);
    check(!visibleTree.queryTableAvailable(),
          "table query remains unavailable without connection prerequisites");
    visibleTree.SetQueryPrerequisitesAvailable(true);
    check(visibleTree.queryTableAvailable() && details.contains("Type") &&
              details.contains("Table"),
          "table selection enables visible action and identifies its structure");
    visibleTree.QueryTableFromCurrent();
    visibleTree.SelectFromOid(entryFixture.oid);
    check(visibleTree.queryTableAvailable() && details.contains("Table Entry"),
          "entry selection enables the same action and identifies its structure");
    visibleTree.QueryTableFromCurrent();
    visibleTree.SelectFromOid(columnFixture.oid);
    check(!visibleTree.queryTableAvailable() && details.contains("Column"),
          "column retains object behavior rather than becoming a default table target");
    visibleTree.QueryTableFromCurrent();
    check(tableRequests == QStringList({tableFixture.oid, entryFixture.oid}),
          "only table and entry selections emit table-query requests");

    visibleTree.setCurrentIndex({});
    visibleTree.SelectFromOid(retainedOid);
    model.setSnapshot(service.treeSnapshot({"SNMPv2-MIB"}));
    check(model.indexForOid(retainedOid).isValid() && visibleTree.selectedOid() == retainedOid,
          "visible selection OID survives compatible rebuild");
    model.setSnapshot(MibTreeNodeRecord{QStringLiteral("1"), QStringLiteral("MIB Tree")});
    check(!model.indexForOid(retainedOid).isValid(), "removed selection safely becomes invalid");

    QSettings settings(temporary.filePath("SnmpB.ini"), QSettings::IniFormat);
    settings.setValue("sentinel", 42); settings.sync();
    const QByteArray before = [&] { QFile f(settings.fileName()); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray(); }();
    service.moduleInventory();
    const QByteArray after = [&] { QFile f(settings.fileName()); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray(); }();
    check(before == after, "viewing inventory does not rewrite settings");

    const QString retainedOrganization = accounting.organization;
    const QString retainedContact = accounting.contactInfo;
    const QString retainedMetadataOid = accounting.rootOid;
    smiExit();
    check(retainedOrganization == "IETF AToM MIB Working Group" &&
          retainedContact.contains("kzm@cisco.com") && retainedMetadataOid == "1.3.6.1.2.1.60",
          "metadata values remain valid after libsmi lifetime ends");
    return failures == 0 ? 0 : 1;
}
