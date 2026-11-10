#include <QTimeZone>
#include "mibdownloadtransport.h"
#include "mibcollection.h"
#include "miblibrarywidget.h"

#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QDialog>
#include <QFile>
#include <QElapsedTimer>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QThread>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QGroupBox>
#include <QComboBox>
#include <QAbstractButton>
#include <QLayout>
#include <QTabBar>
#include <functional>
#include "mibdependencyindex.h"
#include <QSplitter>
#include <QTextEdit>
#include <iostream>

namespace {
class IdleTransport : public MibDownloadTransport
{
public:
    void get(const QUrl &url) override { requested = url; }
    void cancel() override {}
    void finishWithError() { MibDownloadResult result; result.error = "scripted"; emit finished(result); }
    QUrl requested;
};
bool check(bool value, const char *message) { if (!value) std::cerr << message << '\n'; return value; }
void writeMib(const QString &path, const QByteArray &content)
{
    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) file.write(content);
}
void clickHeaderCheckbox(QHeaderView *header)
{
    header->findChild<QCheckBox *>("MibLibraryHeaderSelectAll")->click();
}
}

int main(int argc, char **argv)
{
    QStandardPaths::setTestModeEnabled(true);
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName("SnmpBNextTests");
    QCoreApplication::setApplicationName("MibInventoryWidgetTest");
    QSettings().remove("mib-library/dependencies-status-width");
    QSettings().remove("mib-library/inventory-details-splitter");
    QTemporaryDir temp;
    application.setProperty("snmpb.isolatedStateRoot", temp.filePath("state"));
    const QString fixtureRoot = temp.filePath("MIBRoot");
    const QString standardsRoot = QDir(fixtureRoot).filePath("Standards");
    QDir().mkpath(standardsRoot);
    QSettings().setValue("mib-library/root", fixtureRoot);
    QSettings().sync();
    writeMib(QDir(standardsRoot).filePath("A-MIB"), "A-MIB DEFINITIONS ::= BEGIN\nIMPORTS x FROM DEP-MIB;\nEND\n");
    writeMib(QDir(standardsRoot).filePath("DEP-MIB"), "DEP-MIB DEFINITIONS ::= BEGIN\nEND\n");
    writeMib(QDir(standardsRoot).filePath("IANA-ONE-MIB"), "IANA-ONE-MIB DEFINITIONS ::= BEGIN\nEND\n");
    writeMib(QDir(standardsRoot).filePath("IANA-TWO-MIB"), "IANA-TWO-MIB DEFINITIONS ::= BEGIN\nEND\n");
    const QString importSource = temp.filePath("filename-does-not-match.mib");
    writeMib(importSource, "DECLARED-IMPORT-MIB DEFINITIONS ::= BEGIN\nEND\n");
    const MibCollectionResult imported = MibCollection(fixtureRoot).importFiles({importSource});
    bool ok = check(imported.success && imported.importedCopied == 1 &&
                    QFileInfo::exists(QDir(fixtureRoot).filePath("Unassigned/filename-does-not-match.mib")),
                    "Import Files safely copies source filenames to Unassigned without changing identity authority");
    const MibCollectionResult repeatedImport = MibCollection(fixtureRoot).importFiles({importSource});
    ok &= check(repeatedImport.success && repeatedImport.identicalSkipped == 1 &&
                repeatedImport.conflicts.isEmpty(),
                "Import Files is idempotent for identical content");
    IdleTransport transport;
    MibModuleRecord sourceMetadata;
    sourceMetadata.name = "A-MIB"; sourceMetadata.rootOid = "1.3.6.1.2.1.60";
    sourceMetadata.lastRevision = QDateTime(QDate(1998, 9, 28), QTime(10, 0), QTimeZone::UTC);
    sourceMetadata.organization = "  Org\r\nGroup  ";
    sourceMetadata.contactInfo =
        "Name  \r\nCo\r\n\r\n\r\nAddr\nPhone\nMail  \r\nLine 7\nLine 8\nLine 9\n"
        "Line 10\nLine 11\nLine 12\nLine 13\nLine 14\r\n\r\n";
    sourceMetadata.description =
        "  First\r\nline.  \r\n\r\n\r\nNext\npart.  ";
    sourceMetadata.reference = " RFC\nsection ";
    sourceMetadata.revisions.append({sourceMetadata.lastRevision,
        " Initial\r\ndetail.\r\n\r\n\r\nNext\nparagraph. "});
    MibModuleRecord layoutMetadata;
    layoutMetadata.name = "A-MIB"; layoutMetadata.rootOid = "1.3.6.1.2.1.60";
    layoutMetadata.lastRevision = sourceMetadata.lastRevision;
    layoutMetadata.organization = "Organization\nWorking Group";
    layoutMetadata.contactInfo = "Maintainer\nmaintainer@example.invalid";
    layoutMetadata.description = "First line\nSecond line";
    layoutMetadata.reference = "RFC fixture\nSection 1";
    layoutMetadata.revisions.append({layoutMetadata.lastRevision, "Initial revision\nDetails"});
    MibLibraryWidget::Callbacks callbacks;
    callbacks.metadata = [&layoutMetadata](const QString &, const QString &) {
        return layoutMetadata;
    };
    callbacks.localInventory = [standardsRoot]() {
        QList<MibModuleRecord> modules;
        for (const QString &name : {QStringLiteral("A-MIB"), QStringLiteral("DEP-MIB"),
                                    QStringLiteral("IANA-ONE-MIB"), QStringLiteral("IANA-TWO-MIB")}) {
            MibModuleRecord module; module.name = name;
            module.path = QDir(standardsRoot).filePath(name); modules.append(module);
        }
        return modules;
    };
    int dependencyChecks = 0; MibProfileDependencyCheck cachedDependencyCheck;
    MibLibraryWidget::DependencySummary dependencySummary;
    dependencySummary.stale = true;
    dependencySummary.knownModules = 4;
    dependencySummary.relationships = 1;
    const auto performDependencyCheck = [&dependencyChecks, &cachedDependencyCheck,
                                          &dependencySummary]() {
        ++dependencyChecks; MibProfileDependencyCheck result;
        result.checkedUtc = QDateTime::currentDateTimeUtc();
        result.effectiveModules = {"A-MIB", "DEP-MIB", "IANA-ONE-MIB", "IANA-TWO-MIB"};
        result.dependencies = {"DEP-MIB"}; cachedDependencyCheck = result;
        dependencySummary.stale = false;
        dependencySummary.lastCheckedUtc = result.checkedUtc;
        return result;
    };
    callbacks.libraryDependencySummary = [&dependencySummary]() { return dependencySummary; };
    std::function<void(const QString &)> finishDependencyCheck;
    callbacks.checkDependencies = [&performDependencyCheck, &finishDependencyCheck](
        std::function<void(const QList<MibModuleRecord> &)> prepare,
        std::function<void(const QString &)> completed) {
        performDependencyCheck();
        prepare({});
        finishDependencyCheck = std::move(completed);
    };
    callbacks.effectivePlan = [](const MibProfileRecord &profile) {
        MibEffectivePlan plan; plan.profileId = profile.id; plan.profileName = profile.name;
        plan.profileType = profile.type; plan.explicitModules = profile.explicitModules;
        plan.effectiveModules = plan.explicitModules; plan.sha256 = "test-plan"; return plan;
    };
    MibCatalog fixtureCatalog;
    for (const auto &name : {QStringLiteral("IANA-ONE-MIB"), QStringLiteral("IANA-TWO-MIB")}) {
        MibCatalogEntry entry; entry.moduleName = name; entry.filename = name;
        entry.sourceId = "iana"; entry.sourceName = "IANA";
        entry.url = "https://example.invalid/" + name;
        fixtureCatalog.upsert(entry);
    }
    MibCatalogCacheInfo fixtureInfo; fixtureInfo.sourceId = "iana";
    ok &= check(MibCatalogCache::save(QDir(MibCollection::legacyManagedRoot()).filePath("cache/catalog-v1.json"),
                                    fixtureCatalog, fixtureInfo), "scripted IANA catalog is cached without network access");
    MibLibraryWidget widget({}, nullptr, &transport, callbacks);
    {
        // Every input control is placed by a layout (a splitter places its own children). An
        // unplaced child is drawn at its parent's top-left corner: the "All states" filter once
        // sat over the Library / Profiles tab names that way.
        std::function<bool(const QLayout *, const QWidget *)> inLayout =
            [&inLayout](const QLayout *layout, const QWidget *control) {
            if (!layout) return false;
            for (int i = 0; i < layout->count(); ++i) {
                QLayoutItem *item = layout->itemAt(i);
                if (item->widget() == control) return true;
                if (item->layout() && inLayout(item->layout(), control)) return true;
            }
            return false;
        };
        QStringList unplaced;
        for (QWidget *control : widget.findChildren<QWidget *>()) {
            if (!qobject_cast<QComboBox *>(control) && !qobject_cast<QAbstractButton *>(control) &&
                !qobject_cast<QLineEdit *>(control)) continue;
            QWidget *parent = control->parentWidget();
            // Controls that belong to another control (a combo box's editor, a tab bar's buttons,
            // a header's select-all box in its viewport) are placed by their owner.
            if (!parent || qobject_cast<QComboBox *>(parent) || qobject_cast<QLineEdit *>(parent) || qobject_cast<QTabBar *>(parent) ||
                qobject_cast<QHeaderView *>(parent) || qobject_cast<QAbstractScrollArea *>(parent) ||
                qobject_cast<QAbstractScrollArea *>(parent->parentWidget()) ||   // drawn in a viewport
                qobject_cast<QSplitter *>(parent) || control->isWindow() ||
                parent->inherits("QAbstractSpinBox") || parent->inherits("QScrollBar")) continue;
            if (!inLayout(parent->layout(), control))
                unplaced << (control->objectName().isEmpty() ? QString::fromLatin1(control->metaObject()->className())
                                                             : control->objectName());
        }
        if (!unplaced.isEmpty()) std::cerr << "unplaced: " << unplaced.join(", ").toStdString() << '\n';
        ok &= check(unplaced.isEmpty(), "every Library and Profiles control is placed in a layout");
    }
    int initialPublications = 0;
    bool publishedOnGuiThread = false;
    QObject::connect(&widget, &MibLibraryWidget::initialRefreshFinished,
                     [&]() {
        ++initialPublications;
        publishedOnGuiThread = QThread::currentThread() == application.thread();
    });
    widget.beginInitialRefresh(1);
    widget.beginInitialRefresh(2);
    QElapsedTimer initialWait; initialWait.start();
    while (!widget.initialRefreshReady() && initialWait.elapsed() < 5000) {
        application.processEvents();
        QThread::msleep(1);
    }
    ok &= check(widget.initialRefreshReady() && initialPublications == 1 &&
                publishedOnGuiThread,
                "stale Catalog preparation is rejected and current result publishes once on GUI thread");
    int runtimeSelectionRequests = 0;
    QObject::connect(&widget, &MibLibraryWidget::profileSelectionChanged,
                     [&runtimeSelectionRequests](const QString &, const MibEffectivePlan &) {
        ++runtimeSelectionRequests;
    });
    const MibLibraryWidget::PerformanceCounters beforeActivation = widget.performanceCounters();
    widget.activate();
    widget.activate();
    const MibLibraryWidget::PerformanceCounters afterActivation = widget.performanceCounters();
    ok &= check(afterActivation.activations == beforeActivation.activations + 2 &&
                afterActivation.automaticProfileScans == beforeActivation.automaticProfileScans &&
                afterActivation.inventoryBuilds == beforeActivation.inventoryBuilds &&
                afterActivation.profilePopulations == beforeActivation.profilePopulations &&
                runtimeSelectionRequests == 0,
                "Tree-MIBs-Tree-MIBs activation reuses cached models without reconciliation");
    QFile mainWindowUi(QStringLiteral(SNMPB_SOURCE_DIR "/app/mainw.ui"));
    QFile applicationSource(QStringLiteral(SNMPB_SOURCE_DIR "/app/snmpb.cpp"));
    QFile moduleSource(QStringLiteral(SNMPB_SOURCE_DIR "/app/mibmodule.cpp"));
    QFile profileSourceFile(QStringLiteral(SNMPB_SOURCE_DIR "/app/mibprofile.cpp"));
    QFile effectivePlanSource(QStringLiteral(SNMPB_SOURCE_DIR "/app/mibeffectiveplan.cpp"));
    QFile viewSource(QStringLiteral(SNMPB_SOURCE_DIR "/app/mibview.cpp"));
    QFile applicationBuild(QStringLiteral(SNMPB_SOURCE_DIR "/app/CMakeLists.txt"));
    ok &= check(mainWindowUi.open(QIODevice::ReadOnly) &&
                applicationSource.open(QIODevice::ReadOnly) &&
                moduleSource.open(QIODevice::ReadOnly) &&
                profileSourceFile.open(QIODevice::ReadOnly) &&
                effectivePlanSource.open(QIODevice::ReadOnly) &&
                viewSource.open(QIODevice::ReadOnly) &&
                applicationBuild.open(QIODevice::ReadOnly),
                "navigation sources are readable");
    const QByteArray mainWindowUiText = mainWindowUi.readAll();
    const QByteArray applicationSourceText = applicationSource.readAll();
    const QByteArray moduleSourceText = moduleSource.readAll();
    const QByteArray profileSourceFileText = profileSourceFile.readAll();
    const QByteArray effectivePlanSourceText = effectivePlanSource.readAll();
    const QByteArray viewSourceText = viewSource.readAll();
    const QByteArray applicationBuildText = applicationBuild.readAll();
    ok &= check(!mainWindowUiText.contains("name=\"ModulesTab\"") &&
                !mainWindowUiText.contains("MibLibraryCheckDependencies") &&
                !applicationSourceText.contains("w.MibLibraryCheckDependencies") &&
                applicationSourceText.contains("documents.library = mibLibrary;"),
                "the legacy MIB Library tab is gone and the Library opens as one middle-area document");
    ok &= check(applicationSourceText.contains("CurrentEnvironmentStatus") &&
                applicationSourceText.contains("ConnectionBar") &&
                applicationSourceText.contains("QueryToolBar") &&
                applicationSourceText.contains("CollectionSearch") &&
                applicationSourceText.contains("MibTreeFilter"),
                "the main window exposes persistent context, query actions, target, and the MIB tree filter");
    // Results is checked through the real application in launch-smoke and through
    // query-results workflows, rather than requiring its implementation in snmpb.cpp.
    ok &= check(!moduleSourceText.contains("MainUI()->LoadedModules") &&
                !moduleSourceText.contains("MainUI()->UnloadedModules") &&
                !moduleSourceText.contains("MainUI()->ModuleAdd") &&
                !moduleSourceText.contains("MainUI()->ModuleDelete"),
                "legacy Available/Loaded Modules presentation plumbing is disconnected");
    ok &= check(applicationSourceText.contains("mibLibrary->activate();") &&
                !applicationSourceText.contains("currentTab == mibLibrary) {\n        SetEditorMenus(false);\n        MainUI()->actionMultipleVarbinds->setEnabled(false);\n        mibLibrary->refresh();"),
                "top-level MIBs activation does not call Library refresh");
    ok &= check(!moduleSourceText.contains("legacyPlan") &&
                !moduleSourceText.contains("Wanted") &&
                !moduleSourceText.contains("mibpreloads"),
                "legacy preload state is absent from normal MibModule runtime authority");
    ok &= check(!moduleSourceText.contains("ReconstructRuntime"),
                "no synchronous duplicate Environment publication path remains");
    ok &= check(!moduleSourceText.contains("LoadBestModule") &&
                !moduleSourceText.contains("MIBLOAD_DEFAULT"),
                "dead legacy automatic preload authority is absent");
    ok &= check(!applicationSourceText.contains("LoadPreferredModules") &&
                !moduleSourceText.contains("LoadPreferredModules") &&
                !moduleSourceText.contains("PreferredMibResolver") &&
                !viewSourceText.contains("EnsureLoaded") &&
                !viewSourceText.contains("SetEnvironment({}, modules)") &&
                !applicationBuildText.contains("    preferredmibresolver.cpp\n") &&
                !applicationBuildText.contains("    preferredmibresolver.h\n"),
                "preferred-module resolver and synthetic Tree Environment are absent from production");
    ok &= check(applicationSourceText.contains("profileService()->addFiles") &&
                applicationSourceText.contains("MibNormalizeRuntimeRequests") &&
                applicationSourceText.contains("providers.size() == 1") &&
                applicationSourceText.contains("mibLibrary->selectProfile(activeProfileId)") &&
                !applicationSourceText.contains("loadPreferredMibsRequested") &&
                !applicationSourceText.contains("DevicePane"),
                "download entry point cannot bypass Profile intent and the retired Connection (device) entry points are gone");
    ok &= check(applicationSourceText.contains("modules->CheckProfileDependencies(QStringLiteral(\"mib-library\")"),
                "MIB Library owns the library-wide Check Dependencies action");
    ok &= check(applicationSourceText.contains("&MibEditor::FileSaved") &&
                applicationSourceText.contains("refreshRootHash(saved, hash)") &&
                applicationSourceText.contains("mibLibrary->selectProfile(current.id)"),
                "saving a MIB refreshes the Library index, stored profile hashes, and the active Environment");
    ok &= check(!applicationSourceText.contains("MibFileOpenReadOnly") &&
                !viewSourceText.contains("MibFileOpenReadOnly"),
                "no MIB file is opened read-only: every MIB stays editable so errors can be fixed");
    ok &= check(moduleSourceText.contains("Legacy module editor ignored; use a Profile") &&
                !moduleSourceText.contains("PersistWanted"),
                "retired Available/Loaded arrows cannot recreate shadow legacy authority");
    ok &= check(moduleSourceText.contains("for (SmiModule *mod = smiGetFirstModule()") &&
                moduleSourceText.contains("LoadedMibModule lmodule(SnapshotMibModule(mod))") &&
                moduleSourceText.contains("declarations.contains(Loaded[j].name)"),
                "Loaded and Available projections use actual libsmi identities rather than stale requested state");
    ok &= check(applicationSourceText.contains("ApplyProfileRuntime(plan, &error)") &&
                applicationSourceText.contains("effectivePlan = [this]") &&
                applicationSourceText.contains("setVisibleModules(plan.effectiveModules)") &&
                moduleSourceText.contains("environmentManager->request(plan)") &&
                moduleSourceText.contains("s->MibLoaderObj()->SetEnvironment(environment,loadedModules)") &&
                !moduleSourceText.contains("ApplyProfileRuntime(const QStringList &modules, QString *error)\n{\n    PersistWanted"),
                "profile selection asynchronously constructs the exact tree runtime without persisting legacy preloads");
    ok &= check(!profileSourceFileText.contains("MibProfileResolver::resolve") &&
                !effectivePlanSourceText.contains("smiLoadModule") &&
                !effectivePlanSourceText.contains("Wanted") &&
                !effectivePlanSourceText.contains("mibpreloads") &&
                moduleSourceText.contains("MibRuntimeParser::loadExplicitRoots(") &&
                !moduleSourceText.contains("MibBoundedDependencyLoader().load(plan, load)") &&
                moduleSourceText.contains("MibEnvironmentExtractor().extract(isolatedPlan,missing,rootOutcomes,{},") &&
                moduleSourceText.contains("MibRuntimeStage::prepare(plan)"),
                "one pure Effective Plan resolver owns membership while runtime loads explicit roots alias-first and leaves imports native");
    auto *table = widget.findChild<QTableWidget *>("MibLibraryTable");
    ok &= check(table && table->columnCount() == 7,
                "Inventory has checkbox, module, status, In Profiles, revision, and source file columns");
    if (!table || table->rowCount() < 4) {
        std::cerr << "Inventory fixture rows unavailable: "
                  << (table ? table->rowCount() : -1) << '\n';
        return 1;
    }
    ok &= check(table && table->horizontalHeaderItem(2)->text() == "Status" &&
                table->horizontalHeaderItem(3)->text() == "In Profiles" &&
                table->horizontalHeaderItem(5)->text() == "Source file",
                "Inventory exposes Status, Active Profile, and Origin");
    {
        auto *scope = widget.findChild<QComboBox *>("MibLibraryScope");
        ok &= check(scope && scope->itemData(0).toString() == "@active" && scope->itemData(1).toString() == "@all" &&
                    scope->currentIndex() == 0, "Library opens on the active profile's view, with the whole collection one choice away");
        int visibleActive = 0;
        for (int row = 0; row < table->rowCount(); ++row) visibleActive += !table->isRowHidden(row);
        if (scope) scope->setCurrentIndex(1);
        application.processEvents();
        int visibleAll = 0;
        for (int row = 0; row < table->rowCount(); ++row) visibleAll += !table->isRowHidden(row);
        ok &= check(visibleAll == table->rowCount() && visibleActive <= visibleAll,
                    "the profile view shows only that profile's modules and the collection view shows every module");
        ok &= check(!table->item(0, 3)->text().isEmpty(), "every module says which profiles contain it, or None");
        const QString shownPath = table->item(0, 5)->text();
        ok &= check(!shownPath.isEmpty() && !QDir::isAbsolutePath(shownPath) &&
                    QDir::isAbsolutePath(QDir::fromNativeSeparators(table->item(0, 5)->toolTip())),
                    "Source file shows the path inside the Library, with the full path in the tooltip");
    }
    ok &= check(table && table->editTriggers() == QAbstractItemView::NoEditTriggers,
                "Inventory table disables editing");
    for (int row=0; table && row<table->rowCount(); ++row)
        for (int column=0; column<table->columnCount(); ++column)
            ok &= check(!(table->item(row,column)->flags() & Qt::ItemIsEditable),
                        "Inventory item is read-only");
    ok &= check(table && !table->item(0,5)->text().isEmpty(), "module origin displayed");
    QString opened;
    QObject::connect(&widget, &MibLibraryWidget::openModuleRequested,
                     [&](const QString &path) { opened = path; });
    table->setCurrentCell(0, 0);
    QMetaObject::invokeMethod(table, "cellDoubleClicked", Qt::DirectConnection,
                              Q_ARG(int, 0), Q_ARG(int, 0));
    ok &= check(opened.endsWith("/Standards/A-MIB") ||
                opened.endsWith("\\Standards\\A-MIB"),
                "double click opens selected working Standards file");
    ok &= check(!opened.isEmpty(), "every library module opens in the editor, which is always editable");
    auto *moduleInfo = widget.findChild<QLabel *>("MibLibraryInfo_module");
    ok &= check(moduleInfo && moduleInfo->text() == "A-MIB", "Info reflects selected record");
    ok &= check(widget.findChild<QLabel *>("MibLibraryInfo_moduleOid")->text() ==
                "1.3.6.1.2.1.60", "Info displays numeric MODULE-IDENTITY OID");
    ok &= check(widget.findChild<QLabel *>("MibLibraryInfo_lastUpdated")->text() ==
                "1998-09-28 10:00 UTC", "Info displays human-readable latest revision");
    MibLibraryWidget::Callbacks normalizationCallbacks;
    normalizationCallbacks.metadata = [&sourceMetadata](const QString &, const QString &) {
        return sourceMetadata;
    };
    normalizationCallbacks.localInventory = callbacks.localInventory;
    normalizationCallbacks.effectivePlan = callbacks.effectivePlan;
    MibLibraryWidget normalizationWidget({temp.path()}, nullptr, &transport,
                                         normalizationCallbacks);
    normalizationWidget.beginInitialRefresh(1);
    QElapsedTimer normalizationWait; normalizationWait.start();
    while (!normalizationWidget.initialRefreshReady() && normalizationWait.elapsed() < 5000) {
        application.processEvents();
        QThread::msleep(1);
    }
    normalizationWidget.findChild<QTableWidget *>("MibLibraryTable")->setCurrentCell(0, 0);
    ok &= check(normalizationWidget.findChild<QLabel *>("MibLibraryInfo_organization")->text() ==
                "Org Group",
                "Organization hard wraps are reflowed for display");
    ok &= check(normalizationWidget.findChild<QLabel *>("MibLibraryInfo_description")->text() ==
                "First line.\n\nNext part.",
                "Description reflows hard wraps and preserves one paragraph break");
    ok &= check(normalizationWidget.findChild<QLabel *>("MibLibraryInfo_reference")->text() ==
                "RFC section",
                "Reference hard wraps are reflowed for display");
    ok &= check(normalizationWidget.findChild<QLabel *>("MibLibraryInfo_revisionHistory")->text() ==
                "1998-09-28\nInitial detail.\n\nNext paragraph.",
                "Revision descriptions reflow without losing paragraph breaks");
    auto *contact = normalizationWidget.findChild<QTextEdit *>("MibLibraryInfo_contact");
    const QString expectedContact =
        "Name\nCo\n\nAddr\nPhone\nMail\nLine 7\nLine 8\nLine 9\nLine 10\nLine 11\n"
        "Line 12\nLine 13\nLine 14";
    ok &= check(contact && contact->toPlainText() == expectedContact,
                "Contact Information retains logical lines and collapses blank lines");
    ok &= check(contact && contact->isReadOnly() &&
                (contact->textInteractionFlags() & Qt::TextSelectableByMouse) &&
                contact->lineWrapMode() == QTextEdit::WidgetWidth,
                "Contact Information is read-only and selectable");
    normalizationWidget.resize(1100, 700); normalizationWidget.show(); application.processEvents();
    const int visibleContactLines = contact->maximumHeight() / contact->fontMetrics().lineSpacing();
    ok &= check(visibleContactLines >= 9 && visibleContactLines <= 11 &&
                contact->verticalScrollBar()->maximum() > 0 &&
                contact->horizontalScrollBarPolicy() == Qt::ScrollBarAlwaysOff,
                "Contact box shows about ten wrapped lines with vertical scrolling only as needed");
    ok &= check(sourceMetadata.organization == "  Org\r\nGroup  " &&
                sourceMetadata.description ==
                    "  First\r\nline.  \r\n\r\n\r\nNext\npart.  " &&
                sourceMetadata.contactInfo ==
                    "Name  \r\nCo\r\n\r\n\r\nAddr\nPhone\nMail  \r\nLine 7\nLine 8\nLine 9\n"
                    "Line 10\nLine 11\nLine 12\nLine 13\nLine 14\r\n\r\n" &&
                sourceMetadata.revisions.first().description ==
                    " Initial\r\ndetail.\r\n\r\n\r\nNext\nparagraph. ",
                "presentation formatting leaves source metadata unchanged");
    auto *moduleGroup = widget.findChild<QGroupBox *>("MibLibraryModuleInformation");
    auto *fileGroup = widget.findChild<QGroupBox *>("MibLibraryFileInformation");
    auto *infoSplitter = widget.findChild<QSplitter *>("MibLibraryInformationSplitter");
    auto *moduleScroll = widget.findChild<QScrollArea *>("MibLibraryModuleInformationScroll");
    auto *fileScroll = widget.findChild<QScrollArea *>("MibLibraryFileInformationScroll");
    ok &= check(moduleGroup && fileGroup && infoSplitter &&
                infoSplitter->orientation() == Qt::Horizontal &&
                moduleScroll && fileScroll &&
                infoSplitter->indexOf(moduleScroll) == 0 && infoSplitter->indexOf(fileScroll) == 1 &&
                moduleScroll->widget() == moduleGroup && fileScroll->widget() == fileGroup,
                "Module and File Information use side-by-side splitter");
    widget.resize(1100, 700); widget.show(); application.processEvents();
    auto *header = table->horizontalHeader();
    auto *search = widget.findChild<QLineEdit *>("MibLibrarySearch");
    auto *count = widget.findChild<QLabel *>("MibLibrarySelectedCount");
    auto *download = widget.findChild<QPushButton *>("MibLibraryDownload");
    auto *install = widget.findChild<QPushButton *>("MibLibraryInstallWithDependencies");
    auto *refresh = widget.findChild<QPushButton *>("MibLibraryRefreshCatalog");
    auto *updates = widget.findChild<QPushButton *>("MibLibraryCheckForUpdates");
    auto *cancel = widget.findChild<QPushButton *>("MibLibraryCancel");
    ok &= check(header->property("checkState").toInt() == Qt::Unchecked &&
                count->text() == "Selected: 0" && !download->isEnabled() && !install->isEnabled(),
                "empty checkbox selection has unchecked header, zero count, and disabled actions");
    auto *headerCheckbox = header->findChild<QCheckBox *>("MibLibraryHeaderSelectAll");
    ok &= check(headerCheckbox &&
                headerCheckbox->geometry().left() <= header->sectionViewportPosition(0) + 6 &&
                headerCheckbox->geometry().center().x() <=
                    header->sectionViewportPosition(0) + header->sectionSize(0) / 2,
                "Inventory header checkbox is left-aligned with row checkboxes");
    ok &= check(download->text() == "Download to Library" &&
                install->text() == "Download and Add to Active Profile" &&
                refresh->text() == "Refresh Catalog" && !updates,
                "Inventory actions use final grouped labels");
    table->item(0, 0)->setCheckState(Qt::Checked); application.processEvents();
    ok &= check(header->property("checkState").toInt() == Qt::PartiallyChecked &&
                count->text() == "Selected: 1" && !download->isEnabled() && !install->isEnabled(),
                "Library dependency selection does not select IANA downloads");
    search->setText("IANA"); application.processEvents();
    ok &= check(header->property("checkState").toInt() == Qt::Unchecked,
                "filter change recalculates header against displayed rows");
    clickHeaderCheckbox(header); application.processEvents();
    ok &= check(header->property("checkState").toInt() == Qt::Checked &&
                count->text() == "Selected: 3" && table->item(0, 0)->checkState() == Qt::Checked,
                "unchecked header selects filtered rows without clearing hidden selection");
    // Rows are sorted by module name and the fixture also imports DECLARED-IMPORT-MIB,
    // so address the visible IANA row by name instead of by position.
    int visibleFilteredRow = -1;
    for (int row = 0; row < table->rowCount(); ++row)
        if (table->item(row, 1)->text() == "IANA-ONE-MIB") visibleFilteredRow = row;
    ok &= check(visibleFilteredRow >= 0 && !table->isRowHidden(visibleFilteredRow),
                "IANA-ONE-MIB is a visible filtered row");
    if (visibleFilteredRow >= 0)
        table->item(visibleFilteredRow, 0)->setCheckState(Qt::Unchecked);
    application.processEvents();
    ok &= check(header->property("checkState").toInt() == Qt::PartiallyChecked,
                "individual filtered checkbox updates header to partial");
    clickHeaderCheckbox(header); application.processEvents();
    ok &= check(header->property("checkState").toInt() == Qt::Checked && count->text() == "Selected: 3",
                "partial header click selects all filtered rows");
    clickHeaderCheckbox(header); application.processEvents();
    ok &= check(header->property("checkState").toInt() == Qt::Unchecked &&
                count->text() == "Selected: 1" && table->item(0, 0)->checkState() == Qt::Checked,
                "checked header clears filtered rows while preserving hidden selection");
    auto *iana = widget.findChild<QDialog *>("IanaMibDialog");
    auto *ianaTable = widget.findChild<QTableWidget *>("IanaCatalogTable");
    auto *getIana = widget.findChild<QPushButton *>("MibLibraryGetIana");
    ok &= check(iana && ianaTable && getIana && !iana->isVisible() && !transport.requested.isValid(),
                "IANA has its own initially closed window and never requests network automatically");
    getIana->click(); application.processEvents();
    ok &= check(iana->isVisible() && iana->isAncestorOf(refresh) && iana->isAncestorOf(download) &&
                iana->isAncestorOf(install) && ianaTable->rowCount() == 2,
                "IANA catalog and download actions are grouped in their own window");
    ianaTable->selectRow(0); application.processEvents();
    ok &= check(download->isEnabled() && install->isEnabled() ==
                    (widget.currentProfile().type == MibProfileType::Custom),
                "IANA selection enables downloads and respects active-profile editability");
    ok &= check(refresh->isEnabled() && !updates && !cancel->isVisible(),
                "catalog actions are selection-independent and Cancel is hidden while idle");
    refresh->click(); application.processEvents();
    ok &= check(cancel->isVisible() && !refresh->isEnabled() && transport.requested.isValid(),
                "Cancel is visible during scripted catalog refresh");
    transport.finishWithError(); application.processEvents();
    ok &= check(!cancel->isVisible(), "Cancel hides after catalog operation finishes");
    iana->close();
    bool resolveButtonFound = false;
    for (QPushButton *button : widget.findChildren<QPushButton *>())
        resolveButtonFound |= button->text() == "Resolve Dependencies";
    ok &= check(!resolveButtonFound, "standalone Resolve Dependencies button is absent");
    const QList<int> infoSizes = infoSplitter->sizes();
    const int infoWidth = infoSizes.size() == 2 ? infoSizes[0] + infoSizes[1] : 0;
    const double moduleShare = infoWidth > 0
        ? static_cast<double>(infoSizes[0]) / infoWidth : 0.0;
    if (moduleShare < 0.62 || moduleShare > 0.68)
        std::cerr << "Information splitter module share: " << moduleShare << '\n';
    ok &= check(infoSizes.size() == 2 && moduleShare >= 0.62 && moduleShare <= 0.68,
                "Module and File Information retain an approximately 65/35 responsive share");
    ok &= check(!widget.findChild<QLabel *>("MibLibraryInfo_origin")->text().isEmpty(),
                "Library displays origin for local providers");
    auto *dependencyTree = widget.findChild<QTreeWidget *>("MibDependencyTree");
    ok &= check(dependencyTree && dependencyTree->topLevelItemCount() == 1,
                "Dependencies reflects selected module");
    auto *inventorySplitter = widget.findChild<QSplitter *>("MibLibraryInventoryDetailsSplitter");
    auto *tablePane = widget.findChild<QWidget *>("MibLibraryInventoryTablePane");
    ok &= check(inventorySplitter && inventorySplitter->orientation() == Qt::Vertical &&
                !inventorySplitter->childrenCollapsible() && tablePane &&
                inventorySplitter->indexOf(tablePane) == 0 &&
                inventorySplitter->indexOf(widget.findChild<QTabWidget *>("MibLibraryDetailsTabs")) == 1,
                "Inventory table and lower information area use a non-collapsible vertical splitter");
    const QList<int> inventorySizes = inventorySplitter->sizes();
    ok &= check(inventorySizes.size() == 2 && inventorySizes[0] >= 140 &&
                inventorySizes[1] >= 160,
                "Inventory splitter allocates meaningful default space to both areas");
    ok &= check(moduleScroll->verticalScrollBarPolicy() == Qt::ScrollBarAsNeeded &&
                fileScroll->verticalScrollBarPolicy() == Qt::ScrollBarAsNeeded &&
                moduleScroll->horizontalScrollBarPolicy() == Qt::ScrollBarAlwaysOff &&
                fileScroll->horizontalScrollBarPolicy() == Qt::ScrollBarAlwaysOff,
                "Module and File Information panes scroll vertically and wrap horizontally");
    auto *dependencyHeader = dependencyTree->header();
    widget.findChild<QTabWidget *>("MibLibraryDetailsTabs")->setCurrentWidget(dependencyTree);
    application.processEvents();
    const int dependencyWidth = dependencyHeader->sectionSize(0) + dependencyHeader->sectionSize(1);
    const double statusShare = dependencyWidth > 0
        ? static_cast<double>(dependencyHeader->sectionSize(1)) / dependencyWidth : 0.0;
    ok &= check(dependencyHeader->minimumSectionSize() >= 120 &&
                statusShare >= 0.20 && statusShare <= 0.30,
                "Dependency Status defaults near 25 percent with a sensible minimum");
    dependencyHeader->resizeSection(1, 180); application.processEvents();
    const int userStatusWidth = dependencyHeader->sectionSize(1);
    widget.refresh(); application.processEvents();
    ok &= check(dependencyHeader->sectionSize(1) == userStatusWidth,
                "Dependency Status user width survives Inventory refresh");
    MibLibraryWidget restoredWidget({temp.path()}, nullptr, &transport, callbacks);
    restoredWidget.resize(1100, 700); restoredWidget.show(); application.processEvents();
    auto *restoredDependencyTree = restoredWidget.findChild<QTreeWidget *>("MibDependencyTree");
    restoredWidget.findChild<QTabWidget *>("MibLibraryDetailsTabs")
        ->setCurrentWidget(restoredDependencyTree);
    application.processEvents();
    ok &= check(restoredDependencyTree->header()->sectionSize(1) >= 120,
                "Dependency Status retains a usable width across widget sessions");
    auto *tabs = widget.findChild<QTabWidget *>("MibLibraryTabs");
    ok &= check(tabs && tabs->count() == 2 && tabs->tabText(0) == "Library" &&
                tabs->tabText(1) == "Profiles", "Library and Profiles are separate tabs");
    auto *profileSelector = widget.findChild<QComboBox *>("MibProfileEditorSelector");
    auto *profileSummary = widget.findChild<QLabel *>("MibProfileDependencySummary");
    auto *profileLabel = widget.findChild<QLabel *>("MibProfileEditorLabel");
    auto *profileGuidance = widget.findChild<QLabel *>("MibProfileGuidance");
    ok &= check(profileSelector && profileSummary && !profileSummary->isHidden() &&
                profileLabel && profileLabel->text() == "Edit profile" &&
                profileGuidance && profileGuidance->text().contains("Custom profile") &&
                widget.findChild<QPushButton *>("MibLibraryCheckDependencies") &&
                !widget.findChild<QPushButton *>("MibProfileCheckDependencies") &&
                !widget.findChild<QPushButton *>("MibProfileDownloadMissing") &&
                !widget.findChild<QListWidget *>("MibProfileRequiredDependencies")->isHidden(),
                "Profiles present exact-file snapshots and read-only plan results without dependency-management controls");
    const QString activeBeforeEditing = widget.currentProfile().id;
    const int requestsBeforeEditing = runtimeSelectionRequests;
    profileSelector->setCurrentIndex(1); application.processEvents();
    ok &= check(widget.currentProfile().id == activeBeforeEditing && runtimeSelectionRequests == requestsBeforeEditing,
                "choosing a profile to edit preserves Query's active profile and runtime");
    ok &= check(dependencyChecks == 0,
                "switching Profiles does not invoke library dependency validation");
    auto *checkAction = widget.findChild<QPushButton *>("MibLibraryCheckDependencies");
    checkAction->click();
    ok &= check(finishDependencyCheck && !checkAction->isEnabled() &&
                !widget.findChild<QPushButton *>("MibLibraryImportFiles")->isEnabled() &&
                widget.findChild<QLineEdit *>("MibLibrarySearch")->isEnabled(),
                "dependency check leaves browsing enabled and prevents overlapping collection changes");
    finishDependencyCheck({});
    ok &= check(checkAction->isEnabled() &&
                widget.findChild<QLabel *>("MibLibraryStatus")->text().contains("complete"),
                "asynchronous dependency completion restores controls and status");
    auto *publicationTable = widget.findChild<QTableWidget *>("MibLibraryTable");
    auto *publicationAvailable = widget.findChild<QTreeWidget *>("MibProfileAvailableTree");
    ok &= check(publicationTable && publicationTable->updatesEnabled() &&
                publicationTable->horizontalHeader()->sectionResizeMode(0) == QHeaderView::ResizeToContents &&
                publicationTable->horizontalHeader()->sectionResizeMode(1) == QHeaderView::Interactive,
                "dependency publication restores painting and Library column sizing");
    ok &= check(publicationAvailable && publicationAvailable->updatesEnabled() &&
                publicationAvailable->header()->sectionResizeMode(0) == QHeaderView::Stretch &&
                publicationAvailable->header()->sectionResizeMode(1) == QHeaderView::ResizeToContents,
                "Profile tree publication restores painting and automatic column sizing");
    checkAction->click();
    finishDependencyCheck("Synthetic index write failure");
    ok &= check(checkAction->isEnabled() &&
                widget.findChild<QLabel *>("MibLibraryStatus")->text() == "Synthetic index write failure",
                "asynchronous dependency failure restores controls and preserves the error");
    ok &= check(dependencyChecks == 2,
                "library-wide dependency refresh does not become Profile UI state");
    {
        auto *temporary = new MibLibraryWidget({}, nullptr, &transport, callbacks);
        temporary->findChild<QPushButton *>("MibLibraryCheckDependencies")->click();
        auto lateCompletion = finishDependencyCheck;
        delete temporary;
        lateCompletion({});
        ok &= check(true, "a dependency completion after widget destruction is safely ignored");
    }


    QTemporaryDir customTemp;
    writeMib(customTemp.filePath("base.mib"),
             "SNMPv2-SMI DEFINITIONS ::= BEGIN\nIMPORTS x FROM NORTEL-nnSRNode-MIB;\nEND\n");
    writeMib(customTemp.filePath("nnsrnode.mib"),
             "NORTEL-nnSRNode-MIB DEFINITIONS ::= BEGIN\nEND\n");
    QList<MibModuleRecord> liveModules;
    MibModuleRecord baseModule; baseModule.name = "SNMPv2-SMI";
    baseModule.path = customTemp.filePath("base.mib"); liveModules.append(baseModule);
    MibProfileDependencyCheck liveCached;
    MibLibraryWidget::Callbacks synchronizationCallbacks;
    synchronizationCallbacks.localInventory = [&liveModules]() { return liveModules; };
    MibLibraryWidget::DependencySummary liveSummary; liveSummary.stale = true;
    synchronizationCallbacks.libraryDependencySummary = [&]() { return liveSummary; };
    synchronizationCallbacks.effectivePlan = [&liveModules](const MibProfileRecord &profile) {
        MibEffectivePlan plan; plan.profileId = profile.id; plan.profileType = profile.type;
        plan.explicitModules = profile.explicitModules;
        plan.effectiveModules = profile.explicitModules;
        if (profile.id == MibProfileDefinitions::standardsId()) {
            MibEffectivePlanMember dependency; dependency.identity = "NORTEL-nnSRNode-MIB";
            const bool available = std::any_of(liveModules.cbegin(), liveModules.cend(), [](const MibModuleRecord &record) { return record.name == "NORTEL-nnSRNode-MIB"; });
            dependency.membershipReason = available ? MibPlanMembershipReason::Dependency : MibPlanMembershipReason::Missing;
            plan.members.append(dependency);
            if (available) { plan.dependencyModules.append(dependency.identity); plan.effectiveModules.append(dependency.identity); }
            else plan.missingModules.append(dependency.identity);
        }
        return plan;
    };
    const auto synchronizeDependencies = [&]() {
        MibModuleRecord learned; learned.name = "NORTEL-nnSRNode-MIB";
        learned.path = customTemp.filePath("nnsrnode.mib");
        liveModules.append(learned);
        liveCached.checkedUtc = QDateTime::currentDateTimeUtc();
        liveCached.effectiveModules = {"SNMPv2-SMI", "NORTEL-nnSRNode-MIB"};
        liveCached.dependencies = {"NORTEL-nnSRNode-MIB"};
        liveCached.unresolved.clear();
        liveSummary.stale = false; liveSummary.knownModules = 2;
        liveSummary.relationships = 1; liveSummary.lastCheckedUtc = liveCached.checkedUtc;
        return liveCached;
    };
    MibLibraryWidget synchronizationWidget({}, nullptr, &transport, synchronizationCallbacks);
    synchronizationWidget.refresh();
    auto *syncSelector = synchronizationWidget.findChild<QComboBox *>("MibProfileEditorSelector");
    synchronizationWidget.selectProfile(MibProfileDefinitions::standardsId());
    syncSelector->setCurrentIndex(syncSelector->findData(MibProfileDefinitions::standardsId()));
    auto *syncTable = synchronizationWidget.findChild<QTableWidget *>("MibLibraryTable");
    auto *syncAvailableFilter = synchronizationWidget.findChild<QLineEdit *>("MibProfileAvailableFilter");
    auto *syncMemberFilter = synchronizationWidget.findChild<QLineEdit *>("MibProfileMemberFilter");
    auto *syncAvailable = synchronizationWidget.findChild<QTreeWidget *>("MibProfileAvailableTree");
    auto *syncRequired = synchronizationWidget.findChild<QListWidget *>("MibProfileRequiredDependencies");
    auto *syncIncludeStandards = synchronizationWidget.findChild<QCheckBox *>("MibProfileIncludeStandards");
    const bool standardsStateBefore = syncIncludeStandards->isChecked();
    auto containsListIdentity = [](QListWidget *list, const QString &identity) {
        return !list->findItems(identity, Qt::MatchExactly).isEmpty();
    };
    // The Profiles page lists Library files with the modules each declares.
    auto containsTreeIdentity = [](QTreeWidget *tree, const QString &identity) {
        std::function<bool(QTreeWidgetItem *)> walk = [&](QTreeWidgetItem *item) {
            if (item->data(0, Qt::UserRole + 3).toStringList().contains(identity)) return true;
            for (int i = 0; i < item->childCount(); ++i) if (walk(item->child(i))) return true;
            return false;
        };
        for (int i = 0; tree && i < tree->topLevelItemCount(); ++i) if (walk(tree->topLevelItem(i))) return true;
        return false;
    };
    auto containsTableIdentity = [](QTableWidget *inventory, const QString &identity) {
        for (int row = 0; row < inventory->rowCount(); ++row)
            if (inventory->item(row, 1)->text() == identity) return true;
        return false;
    };
    ok &= check(!containsTableIdentity(syncTable, "NORTEL-nnSRNode-MIB") &&
                syncAvailable && !containsTreeIdentity(syncAvailable, "NORTEL-nnSRNode-MIB"),
                "new custom identity is absent before dependency discovery");
    bool initiallyMissing = false;
    for (int row = 0; row < syncRequired->count(); ++row)
        initiallyMissing |= syncRequired->item(row)->text().contains("NORTEL-nnSRNode-MIB") &&
                            syncRequired->item(row)->text().contains("Missing");
    ok &= check(initiallyMissing, "required dependency begins with truthful missing state");
    syncAvailableFilter->setText("NORTEL"); syncMemberFilter->setText("BASE");
    synchronizeDependencies();
    synchronizationWidget.refresh();
    application.processEvents();
    ok &= check(containsTableIdentity(syncTable, "NORTEL-nnSRNode-MIB"),
                "Check Dependencies refreshes Inventory with learned identity immediately");
    ok &= check(containsTreeIdentity(syncAvailable, "NORTEL-nnSRNode-MIB"),
                "Check Dependencies refreshes Profile Available MIBs immediately");
    ok &= check(syncSelector->currentData().toString() == MibProfileDefinitions::standardsId() &&
                syncAvailableFilter->text() == "NORTEL" && syncMemberFilter->text() == "BASE" &&
                syncIncludeStandards->isChecked() == standardsStateBefore,
                "profile identity, filters, and standards-base state survive refresh");
    QStringList synchronizedStandards = synchronizationWidget.profileService()
        ->find(MibProfileDefinitions::standardsId())->explicitModules;
    QStringList expectedStandards = MibProfileDefinitions::standardsModules();
    synchronizedStandards.sort(); expectedStandards.sort();
    ok &= check(synchronizedStandards == expectedStandards,
                "explicit profile membership survives synchronization refresh");
    int learnedRows = 0; bool staleMissing = false;
    for (int row = 0; row < syncRequired->count(); ++row) {
        const QString text = syncRequired->item(row)->text();
        if (text.contains("NORTEL-nnSRNode-MIB")) {
            ++learnedRows; staleMissing |= text.contains("Missing");
        }
    }
    ok &= check(learnedRows == 1 && !staleMissing,
                "required dependencies refresh to one available row without stale missing duplicate");
    // ---- Profiles page: follow folders, exclude and take back files, add single files ----------
    {
        constexpr int PathRole = Qt::UserRole, KindRole = Qt::UserRole + 1, ExcludedRole = Qt::UserRole + 2;
        const QDir library(QFileInfo(fixtureRoot).canonicalFilePath());
        QDir().mkpath(library.filePath("Acme/Lab/deep"));
        writeMib(library.filePath("Acme/Lab/a.mib"), "ACME-A DEFINITIONS ::= BEGIN\nEND\n");
        writeMib(library.filePath("Acme/Lab/deep/b.mib"), "ACME-B DEFINITIONS ::= BEGIN\nEND\n");
        QDir().mkpath(library.filePath("Acme/Empty"));
        const QString lab = QFileInfo(library.filePath("Acme/Lab")).canonicalFilePath();
        const QString aFile = QFileInfo(library.filePath("Acme/Lab/a.mib")).canonicalFilePath();
        const QString bFile = QFileInfo(library.filePath("Acme/Lab/deep/b.mib")).canonicalFilePath();
        const QString standardsFile = QFileInfo(QDir(standardsRoot).filePath("A-MIB")).canonicalFilePath();
        MibLibraryWidget::Callbacks folderCallbacks = callbacks;
        folderCallbacks.libraryFiles = [&]() {
            QList<MibDependencyFileRecord> files;
            const auto add = [&files](const QString &path, const QString &module) {
                if (!QFileInfo::exists(path)) return;
                MibDependencyFileRecord file; file.canonicalPath = QFileInfo(path).canonicalFilePath();
                file.filename = QFileInfo(path).fileName(); file.sha256 = "aa";
                file.importsByModule.insert(module, {}); files << file;
            };
            add(library.filePath("Acme/Lab/a.mib"), "ACME-A");
            add(library.filePath("Acme/Lab/deep/b.mib"), "ACME-B");
            add(library.filePath("Acme/Moved/a.mib"), "ACME-A");
            add(standardsFile, "A-MIB");
            return files;
        };
        MibLibraryWidget page({}, nullptr, &transport, folderCallbacks);
        page.refresh();
        page.showWorkspaceMode(1);
        auto *available = page.findChild<QTreeWidget *>("MibProfileAvailableTree");
        auto *members = page.findChild<QTreeWidget *>("MibProfileMemberTree");
        auto *statusLine = page.findChild<QLabel *>("MibLibraryStatus");
        QPushButton *add = nullptr, *remove = nullptr;
        bool oldButtonsGone = true;
        for (QPushButton *button : page.findChildren<QPushButton *>()) {
            if (button->text() == "Add >") add = button;
            if (button->text() == "< Remove") remove = button;
            oldButtonsGone = oldButtonsGone && !button->text().startsWith("Add Files") &&
                             !button->text().startsWith("Add Folder Snapshot");
        }
        ok &= check(available && members && statusLine && add && remove && oldButtonsGone,
                    "the Profiles page has folder trees with Add and Remove, and no Add Files or Add Folder Snapshot");
        const auto find = [](QTreeWidget *tree, const QString &kind, const QString &path) -> QTreeWidgetItem * {
            QTreeWidgetItem *found = nullptr;
            std::function<void(QTreeWidgetItem *)> walk = [&](QTreeWidgetItem *item) {
                if (item->data(0, KindRole).toString() == kind && item->data(0, PathRole).toString() == path) found = item;
                for (int i = 0; i < item->childCount(); ++i) walk(item->child(i));
            };
            for (int i = 0; tree && i < tree->topLevelItemCount(); ++i) walk(tree->topLevelItem(i));
            return found;
        };
        const auto selectOnly = [](QTreeWidget *tree, QTreeWidgetItem *item) {
            tree->clearSelection(); if (item) item->setSelected(true);
        };
        ok &= check(find(available, "folder", lab) && find(available, "file", bFile) &&
                    find(available, "folder", QFileInfo(library.filePath("Acme/Empty")).canonicalFilePath()),
                    "the left side shows the Library's folders (even empty ones) and the MIB files in them");

        // The built-in profile cannot be edited, and says what to do instead.
        auto *editSelector = page.findChild<QComboBox *>("MibProfileEditorSelector");
        editSelector->setCurrentIndex(editSelector->findData(MibProfileDefinitions::standardsId()));
        selectOnly(available, find(available, "folder", lab));
        add->setEnabled(true); add->click();
        // (Here the Standards id is held by a profile from before exact files; either way it is not
        // changed, the page says why, and its modules are listed.)
        const MibProfileRecord *standards = page.profileService()->find(MibProfileDefinitions::standardsId());
        ok &= check(standards && standards->folders.isEmpty() && !statusLine->text().isEmpty() &&
                    !statusLine->text().startsWith("Profile updated") &&
                    members->topLevelItemCount() > 0 &&
                    (members->topLevelItem(0)->text(2) == "Built in" || members->topLevelItem(0)->text(2) == "Listed module"),
                    "a profile that cannot follow folders is left unchanged, the page says why, and its modules are listed");

        QString error;
        const QString id = page.profileService()->create("Folder lab", &error);
        page.refresh();
        editSelector->setCurrentIndex(editSelector->findData(id));
        selectOnly(available, find(available, "folder", lab));
        add->click();
        const MibProfileRecord *profile = page.profileService()->find(id);
        QTreeWidgetItem *followed = find(members, "followed-folder", lab);
        ok &= check(profile && profile->folders.size() == 1 && profile->folders.at(0).canonicalPath == lab &&
                    followed && followed->childCount() == 2 && followed->text(2) == "Followed · 2 MIB file(s)" &&
                    find(available, "folder", lab)->text(1) == "Followed" &&
                    find(available, "file", aFile)->text(1) == "Via folder",
                    "Add on a folder follows it; the profile shows the folder with its files");

        selectOnly(members, find(members, "folder-file", bFile));
        remove->click();
        profile = page.profileService()->find(id);
        QTreeWidgetItem *excludedRow = find(members, "folder-file", bFile);
        ok &= check(profile->folders.size() == 1 && profile->folders.at(0).excludedFiles == QStringList{bFile} && excludedRow &&
                    excludedRow->data(0, ExcludedRole).toBool() && excludedRow->text(2) == "Excluded" &&
                    find(members, "followed-folder", lab)->text(2) == "Followed · 1 MIB file(s) · 1 excluded" &&
                    find(available, "file", bFile)->text(1) == "Excluded",
                    "Remove on a file inside a followed folder excludes just that file, and both sides show it");

        selectOnly(available, find(available, "file", bFile));
        add->click();
        ok &= check(page.profileService()->find(id)->folders.size() == 1 &&
                    page.profileService()->find(id)->folders.at(0).excludedFiles.isEmpty() &&
                    find(members, "folder-file", bFile) && find(members, "folder-file", bFile)->text(2) == "Included",
                    "Add on an excluded file takes it back in");

        selectOnly(available, find(available, "file", aFile));
        add->click();
        ok &= check(page.profileService()->find(id)->roots.isEmpty() && statusLine->text().contains("not added again"),
                    "adding a file that the followed folder already brings in changes nothing and says why");

        selectOnly(available, find(available, "file", standardsFile));
        add->click();
        profile = page.profileService()->find(id);
        ok &= check(profile->roots.size() == 1 && find(members, "single-file", standardsFile) &&
                    find(available, "file", standardsFile)->text(1) == "Added",
                    "Add on a file outside the followed folders adds it as a single file");

        selectOnly(members, find(members, "followed-folder", lab));
        remove->click();
        profile = page.profileService()->find(id);
        ok &= check(profile->folders.isEmpty() && profile->roots.size() == 1 && !find(members, "followed-folder", lab),
                    "Remove on a followed folder stops following it and keeps the single files");

        // A followed folder that is moved away shows as missing; nothing is dropped.
        selectOnly(available, find(available, "folder", lab));
        add->click();
        ok &= check(QDir().rename(lab, library.filePath("Acme/Moved")), "folder moved for the test");
        page.refresh();
        editSelector->setCurrentIndex(editSelector->findData(id));
        QTreeWidgetItem *gone = find(members, "followed-folder", lab);
        ok &= check(page.profileService()->find(id)->folders.size() == 1 && gone &&
                    gone->text(2) == "Folder not found" && gone->childCount() == 0,
                    "a followed folder that was moved away stays in the profile, marked Folder not found");
        QDir().rename(library.filePath("Acme/Moved"), lab);
    }
    // A large Profile tree keeps its selection/scroll and sizing through a batched rebuild.
    {
        auto largeCallbacks = callbacks;
        largeCallbacks.libraryFiles = [&]() {
            QList<MibDependencyFileRecord> files;
            for (int i = 0; i < 360; ++i) {
                MibDependencyFileRecord file;
                file.canonicalPath = QDir(fixtureRoot).filePath(QString("Acme/Lab/large-%1.mib").arg(i, 4, 10, QChar('0')));
                file.filename = QFileInfo(file.canonicalPath).fileName(); file.sha256 = "fixture";
                file.importsByModule.insert(QString("LARGE-%1-MIB").arg(i), {});
                files.append(file);
            }
            return files;
        };
        MibLibraryWidget page({}, nullptr, &transport, largeCallbacks);
        page.resize(1000, 500); page.showWorkspaceMode(1); page.refresh(); page.show();
        auto *tree = page.findChild<QTreeWidget *>("MibProfileAvailableTree");
        tree->expandAll(); tree->doItemsLayout(); QCoreApplication::processEvents();
        QTreeWidgetItem *selected = nullptr;
        QTreeWidgetItemIterator iterator(tree);
        while (*iterator) {
            if ((*iterator)->data(0, Qt::UserRole).toString().endsWith("large-0200.mib")) selected = *iterator;
            ++iterator;
        }
        ok &= check(selected, "large publication fixture contains the selected file");
        if (selected) {
            const QString path = selected->data(0, Qt::UserRole).toString();
            tree->setCurrentItem(selected); tree->scrollToItem(selected, QAbstractItemView::PositionAtCenter);
            QCoreApplication::processEvents();
            const int scroll = tree->verticalScrollBar()->value();
            ok &= check(scroll > 0, "large Profile publication fixture has a scrolled viewport");
            page.refresh(); QCoreApplication::processEvents();
            bool retained = false;
            QTreeWidgetItemIterator rebuilt(tree);
            while (*rebuilt) {
                if ((*rebuilt)->data(0, Qt::UserRole).toString() == path) retained = (*rebuilt)->isSelected();
                ++rebuilt;
            }
            ok &= check(retained && tree->verticalScrollBar()->value() == scroll,
                        "batched Profile publication restores stable file selection and scroll position");
            ok &= check(tree->updatesEnabled() && tree->header()->sectionResizeMode(1) == QHeaderView::ResizeToContents,
                        "large Profile publication restores painting and automatic column sizing");
        }
    }
    // A download retains its requested destination even after the user changes
    // Query's active profile and closes the IANA window. Only scripted transport.
    {
        const QString savedRoot = QSettings().value("mib-library/root").toString();
        const QString savedActive = QSettings().value("mib-library/current-profile").toString();
        QSettings().setValue("mib-library/root", temp.filePath("DownloadLibrary"));
        QDir().mkpath(temp.filePath("DownloadLibrary"));
        IdleTransport downloadTransport;
        QString deliveredDestination; int delivered = 0;
        MibLibraryWidget::Callbacks downloadCallbacks;
        downloadCallbacks.validate = [](const QString &, QString *) { return true; };
        downloadCallbacks.downloadsCompleted = [&](const QStringList &, bool add, int count, const QString &destination) {
            if (add && count == 1) { deliveredDestination = destination; ++delivered; }
            return QStringLiteral("Scripted download complete");
        };
        MibLibraryWidget downloads({}, nullptr, &downloadTransport, downloadCallbacks);
        downloads.refresh();
        QString error;
        const QString destination = downloads.profileService()->create("Download destination", &error);
        const QString other = downloads.profileService()->create("Keep Query here", &error);
        downloads.reloadProfiles(); downloads.selectProfile(destination);
        downloads.findChild<QPushButton *>("MibLibraryGetIana")->click();
        auto *catalog = downloads.findChild<QTableWidget *>("IanaCatalogTable");
        auto *dialog = downloads.findChild<QDialog *>("IanaMibDialog");
        catalog->selectRow(0);
        downloads.findChild<QPushButton *>("MibLibraryInstallWithDependencies")->click();
        ok &= check(downloadTransport.requested.isValid(), "IANA Add starts one scripted download");
        downloads.selectProfile(other); dialog->close();
        MibDownloadResult result; result.initialUrl = downloadTransport.requested;
        result.finalUrl = result.initialUrl; result.httpStatus = 200;
        result.content = catalog->item(0, 0)->text().toUtf8() + " DEFINITIONS ::= BEGIN\nEND\n";
        emit downloadTransport.finished(result);
        ok &= check(delivered == 1 && deliveredDestination == destination &&
                    downloads.currentProfile().id == other && !dialog->isVisible() &&
                    downloads.findChild<QLabel *>("IanaStatus")->text() == "Scripted download complete",
                    "IANA completion retains its destination, Query profile and closed-window state");
        QSettings().setValue("mib-library/root", savedRoot);
        QSettings().setValue("mib-library/current-profile", savedActive);
    }
    return ok ? 0 : 1;
}
