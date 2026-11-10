#include <QTimeZone>
#include "miblibrary.h"
#include "mibcollection.h"
#include "mibdownloadtransport.h"
#include "mibdiagnosticcollector.h"
#include "mibengine.h"
#include "mibdependencyindex.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <QSettings>
#include <iostream>

namespace {
void openFixture(QFile &file, QIODevice::OpenMode mode)
{
    if (!file.open(mode))
        qFatal("Cannot open test fixture %s: %s", qPrintable(file.fileName()), qPrintable(file.errorString()));
}

class ScriptedTransport : public MibDownloadTransport
{
public:
    MibDownloadResult scripted;
    void get(const QUrl &) override { emit finished(scripted); }
    void cancel() override { scripted.cancelled = true; scripted.error = "Download cancelled"; }
};
bool check(bool value, const char *message)
{
    if (!value) std::cerr << message << '\n';
    return value;
}

QByteArray mib(const QString &name, const QStringList &imports = {})
{
    QString text = name + " DEFINITIONS ::= BEGIN\n";
    if (!imports.isEmpty()) {
        text += "IMPORTS\n";
        for (const QString &dependency : imports)
            text += "  objectIdentifier FROM " + dependency + "\n";
        text += ";\n";
    }
    return (text + "END\n").toUtf8();
}

MibCatalogEntry entry(const QString &module, const QStringList &imports = {})
{
    MibCatalogEntry result;
    result.sourceId = "fixture"; result.sourceName = "Fixture";
    result.category = "Test"; result.moduleName = module;
    result.url = "https://example.invalid/" + module;
    result.filename = module.toLower() + ".txt";
    result.imports = imports;
    return result;
}
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    bool ok = true;
    QString error;
    MibLibraryRecord origin;
    origin.status = MibLibraryStatus::Standards;
    ok &= check(MibLibraryOriginText(origin) == "Standards", "Standards origin label");
    origin.status = MibLibraryStatus::Installed; origin.sourceId = "iana";
    ok &= check(MibLibraryOriginText(origin) == "IANA", "IANA origin label");
    origin.sourceId.clear(); origin.sourceName.clear(); origin.localPath = "local.mib";
    ok &= check(MibLibraryOriginText(origin) == "Imported", "imported origin label");
    origin.status = MibLibraryStatus::Standards; origin.sourceName = "Standards";
    const MibLibraryFileInfo builtInInfo = MibLibraryFileInformation(origin);
    ok &= check(builtInInfo.origin == "Standards" && !builtInInfo.showProvider &&
                !builtInInfo.showSourceUrl && !builtInInfo.showTimestamp &&
                !builtInInfo.showSha256 && !builtInInfo.showState,
                "Standards file info hides redundant/non-applicable provenance");
    MibLibraryRecord ianaInfoRecord;
    ianaInfoRecord.moduleName = "IANA-FIXTURE-MIB";
    ianaInfoRecord.status = MibLibraryStatus::Installed;
    ianaInfoRecord.sourceId = "iana"; ianaInfoRecord.sourceName = "IANA";
    ianaInfoRecord.sourceUrl = "https://www.iana.org/assignments/fixture/fixture";
    ianaInfoRecord.localPath = "downloaded/fixture.mib";
    ianaInfoRecord.sourceFilename = "fixture.mib";
    ianaInfoRecord.sha256 = "abc123";
    ianaInfoRecord.downloadedAt = QDateTime::fromSecsSinceEpoch(1, QTimeZone::UTC);
    const MibLibraryFileInfo ianaInfo = MibLibraryFileInformation(ianaInfoRecord);
    ok &= check(ianaInfo.origin == "IANA" && ianaInfo.provider == "IANA" &&
                ianaInfo.showProvider && ianaInfo.showSourceUrl &&
                ianaInfo.showTimestamp && ianaInfo.showSha256 && !ianaInfo.showState,
                "IANA file info retains useful provenance without repetitive state");
    MibLibraryRecord renamedFile;
    renamedFile.moduleName = "INET-ADDRESS-MIB"; renamedFile.sourceFilename = "INET-ADDRESS-MIB";
    renamedFile.localPath = "5520/inet_address_tc.mib"; renamedFile.status = MibLibraryStatus::Installed;
    ok &= check(MibLibraryFileInformation(renamedFile).filename == "inet_address_tc.mib",
                "File Information names the file on disk, not the module or catalog name");
    const QByteArray formatted = R"(-- FROM IGNORED-MIB
A-MIB DEFINITIONS ::= BEGIN
IMPORTS
    MODULE-IDENTITY, OBJECT-TYPE
        FROM SNMPv2-SMI
    DisplayString
        FROM SNMPv2-TC
    ifIndex FROM IF-MIB;
END
)";
    const auto scan = MibImportScanner::scan(formatted);
    ok &= check(scan.moduleNames == QStringList{"A-MIB"}, "module identity scan");
    ok &= check(scan.imports == QStringList({"SNMPv2-SMI", "SNMPv2-TC", "IF-MIB"}),
                "IMPORTS scan/comments/whitespace");
    ok &= check(MibImportScanner::scan(mib("NO-IMPORTS")).imports.isEmpty(),
                "no IMPORTS");
    ok &= check(MibImportScanner::scan("BAD DEFINITIONS ::= BEGIN\nIMPORTS x FROM Y")
                    .malformedImports, "malformed IMPORTS");
    const auto semantic = MibImportScanner::scan(
        "CAP-MIB DEFINITIONS ::= BEGIN\n"
        "IMPORTS MODULE-IDENTITY, AGENT-CAPABILITIES FROM SNMPv2-SMI;\n"
        "cap MODULE-IDENTITY ::= { 1 3 6 1 4 1 999 1 }\n"
        "agent AGENT-CAPABILITIES\nSUPPORTS UDP-MIB\n"
        "INCLUDES { udpGroup }\nMODULE IF-MIB\n"
        "MODULE\nMANDATORY-GROUPS { udpGroup }\n"
        "VARIATION ifIndex DESCRIPTION \"x\" ::= { cap 1 }\nEND\n");
    ok &= check(semantic.importsByModule.value("CAP-MIB").contains("UDP-MIB") &&
                semantic.importsByModule.value("CAP-MIB").contains("IF-MIB"),
                "SUPPORTS and named MODULE clauses are semantic dependencies");
    ok &= check(!semantic.importsByModule.value("CAP-MIB").contains("udpGroup") &&
                !semantic.importsByModule.value("CAP-MIB").contains("ifIndex") &&
                !semantic.importsByModule.value("CAP-MIB").contains("MANDATORY-GROUPS"),
                "INCLUDES and VARIATION symbols are not mistaken for module dependencies");
    const auto pibScan = MibImportScanner::scan(
        "ACCOUNTING-FRAMEWORK-PIB PIB-DEFINITIONS ::= BEGIN\n"
        "IMPORTS x FROM COPS-PR-SPPI;\nEND\n");
    ok &= check(pibScan.moduleNames == QStringList{"ACCOUNTING-FRAMEWORK-PIB"} &&
                pibScan.importsByModule.value("ACCOUNTING-FRAMEWORK-PIB") == QStringList{"COPS-PR-SPPI"},
                "SPPI PIB-DEFINITIONS declaration and imports discovered");

    const QByteArray ianaIndex = R"(<html><body>
      <h2>IANA-Maintained MIBs</h2>
      <a href="/assignments/ianaiftype-mib">IANAifType-MIB</a>
      <a href="https://www.iana.org/assignments/ianaenergyrelation-mib">IANA-ENERGY-RELATION-MIB</a>
      <a href="/assignments/ianaiftype-mib">IANAifType-MIB</a>
      <h2>iCalendar Element Registries</h2>
      <a href="https://evil.invalid/assignments/IANA-BAD-MIB">IANA-BAD-MIB</a>
    </body></html>)";
    MibCatalog ianaCatalog;
    ok &= check(IanaMibSourceProvider::parseIndex(ianaIndex, &ianaCatalog, &error) &&
                ianaCatalog.entries().size() == 2 &&
                ianaCatalog.find("IANAifType-MIB") &&
                ianaCatalog.find("IANAifType-MIB")->url ==
                    "https://www.iana.org/assignments/ianaiftype-mib/ianaiftype-mib",
                "authoritative IANA index parse, normalization, and duplicate removal");
    ok &= check(!IanaMibSourceProvider::parseIndex("<html>malformed</html>",
                                                   &ianaCatalog, &error),
                "malformed provider response rejected");
    ok &= check(IanaMibSourceProvider::indexUrl() == QUrl("https://www.iana.org/protocols"),
                "authoritative provider mapping");
    const QUrl secureSource("https://www.iana.org/assignments/source");
    auto redirect = EvaluateMibRedirect(secureSource,
        QUrl("https://www.iana.org/assignments/target"), 0, {secureSource});
    ok &= check(redirect.accepted && redirect.resolvedTarget.scheme() == "https",
                "absolute HTTPS redirect accepted");
    redirect = EvaluateMibRedirect(secureSource, QUrl("target"), 0, {secureSource});
    ok &= check(redirect.accepted && redirect.resolvedTarget ==
                    QUrl("https://www.iana.org/assignments/target"),
                "relative redirect resolved before HTTPS evaluation");
    redirect = EvaluateMibRedirect(secureSource,
        QUrl("http://www.iana.org/assignments/target"), 0, {secureSource});
    ok &= check(!redirect.accepted &&
                    redirect.rejectionReason.contains("HTTPS to HTTP"),
                "HTTPS downgrade rejected");
    redirect = EvaluateMibRedirect(secureSource, secureSource, 0, {secureSource});
    ok &= check(!redirect.accepted && redirect.rejectionReason.contains("loop"),
                "redirect loop rejected");
    redirect = EvaluateMibRedirect(secureSource, QUrl("target"), 5, {secureSource});
    ok &= check(!redirect.accepted && redirect.rejectionReason.contains("Excessive"),
                "excessive redirects rejected");
    MibDownloadSessionState sessionState;
    sessionState.fail("IANA-TEST-MIB", "Redirect from HTTPS to HTTP rejected");
    ok &= check(sessionState.status("IANA-TEST-MIB", MibLibraryStatus::Available) ==
                    MibLibraryStatus::Failed &&
                sessionState.failureReason("IANA-TEST-MIB").contains("HTTPS to HTTP"),
                "failed attempt exposes Failed state and useful reason");
    sessionState.begin("IANA-TEST-MIB");
    sessionState.succeed("IANA-TEST-MIB");
    ok &= check(sessionState.status("IANA-TEST-MIB", MibLibraryStatus::Installed) ==
                    MibLibraryStatus::Installed &&
                sessionState.failureReason("IANA-TEST-MIB").isEmpty(),
                "successful retry transitions Failed to Installed");
    ok &= check(MibValidationErrorLevel(MibValidationLevel::Errors) == 3 &&
                MibValidationErrorLevel(MibValidationLevel::ErrorsAndWarnings) == 5 &&
                MibValidationErrorLevel(MibValidationLevel::FullReview) == 9 &&
                !MibValidationRecursive(MibValidationLevel::ErrorsAndWarnings) &&
                MibValidationRecursive(MibValidationLevel::FullReview),
                "documented explicit validation-level mapping");

    QList<MibCatalogEntry> entries{entry("A", {"B", "C"}), entry("B", {"C"}),
        entry("C"), entry("CYCLE-A", {"CYCLE-B"}), entry("CYCLE-B", {"CYCLE-A"}),
        entry("DIFFERENT-MODULE")};
    entries.last().filename = "vendor-file.my";
    MibCatalog catalog; catalog.setEntries(entries);
    MibCatalog parsed;
    ok &= check(MibCatalog::parse(catalog.serialize(), &parsed, &error) &&
                parsed.find("DIFFERENT-MODULE") &&
                parsed.find("DIFFERENT-MODULE")->filename == "vendor-file.my",
                "versioned catalog round trip and filename mapping");
    QByteArray unsafe = catalog.serialize().replace("vendor-file.my", "../escape");
    ok &= check(!MibCatalog::parse(unsafe, &parsed, &error), "catalog path traversal rejection");

    QMap<QString, MibLibraryStatus> known{{"C", MibLibraryStatus::Standards}};
    MibDependencyPlan plan = MibDependencyResolver().resolve({"A"}, known, catalog);
    ok &= check(plan.orderedDownloads == QStringList({"B", "A"}),
                "dependency-first deterministic deduplicated order");
    ok &= check(plan.unresolved.isEmpty() && plan.roots.size() == 1,
                "bundled dependency classification");
    plan = MibDependencyResolver().resolve({"CYCLE-A", "MISSING"}, {}, catalog);
    ok &= check(plan.orderedDownloads == QStringList({"CYCLE-B", "CYCLE-A"}) &&
                plan.unresolved == QStringList{"MISSING"}, "cycle protection/unresolved");

    QTemporaryDir temporary; QDir root(temporary.path());
    const QString bundled = root.filePath("bundled"); QDir().mkpath(bundled);
    QFile bundledFile(QDir(bundled).filePath("base.txt"));
    ok &= check(bundledFile.open(QIODevice::WriteOnly), "open bundled fixture");
    bundledFile.write(mib("BASE-MIB")); bundledFile.close();
    MibLibraryService library(root.filePath("user-mibs"), root.filePath("internal-state"));
    const MibCollectionResult initialized = MibCollection(library.rootPath()).initialize(
        {bundled}, root.filePath("legacy-mibs"));
    ok &= check(initialized.success && QDir(library.standardsPath()).exists() &&
                QDir(library.downloadedPath()).exists() && QDir(library.rootPath()).exists(),
                "user-visible Standards/Imported/Profiles tree initializes");
    smiInit("snmpb-mib-library-test");
    const int normalFlags = smiGetFlags() | SMI_FLAG_ERRORS | SMI_FLAG_NODESCR;
    smiSetFlags(normalFlags);
    smiSetPath((temporary.path() + QDir::listSeparator() +
        QStringLiteral(SNMPB_SOURCE_DIR "/libsmi/mibs/ietf")).toLocal8Bit().constData());
    MibDependencyIndex verificationIndex(temporary.filePath("verification-index.json"));
    verificationIndex.update({QStringLiteral(SNMPB_SOURCE_DIR "/libsmi/mibs/ietf")}, &error);
    QFile savedIndex(verificationIndex.path());
    ok &= check(savedIndex.open(QIODevice::ReadOnly), "open persisted verification baseline");
    const QByteArray persistedIndexBefore = savedIndex.readAll();
    savedIndex.close();
    const auto verify = [&](const QByteArray &bytes, const QStringList &paths, int level, bool recursive) {
        return MibEngine::instance().validateSource(bytes, paths, level, recursive, verificationIndex);
    };
    const auto engineBad=verify(
        "ENGINE-BAD-MIB DEFINITIONS ::= BEGIN\nIMPORTS OBJECT-TYPE FROM SNMPv2-SMI;\nbroken OBJECT-TYPE t\nEND\n",
        {QStringLiteral(SNMPB_SOURCE_DIR "/libsmi/mibs/ietf")},
        MibValidationErrorLevel(MibValidationLevel::FullReview),true);
    const auto engineGood=verify(
        mib("ENGINE-GOOD-MIB"),{QStringLiteral(SNMPB_SOURCE_DIR "/libsmi/mibs/ietf")},
        MibValidationErrorLevel(MibValidationLevel::ErrorsAndWarnings),false);
    ok &= check(!engineBad.diagnostics.isEmpty() && engineGood.success,
                "engine editor validation isolates diagnostics and recovers after malformed input");
    ok &= check(smiGetFlags()==normalFlags,"engine editor validation restores parser flags");
    const QString standards = QStringLiteral(SNMPB_SOURCE_DIR "/libsmi/mibs/ietf");
    const auto hasTag = [](const MibEngineValidationResult &result, const QString &tag) {
        for (const auto &diagnostic : result.diagnostics) if (diagnostic.tag == tag) return true;
        return false;
    };
    const auto writeFixture = [&](const QString &path, const QByteArray &bytes) {
        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
    };
    const QString importsDirectory = temporary.filePath("imports");
    QDir().mkpath(importsDirectory);
    const QByteArray firstDependency =
        "VERIFY-BASE-MIB DEFINITIONS ::= BEGIN\nIMPORTS enterprises FROM SNMPv2-SMI;\n"
        "verifyBase OBJECT IDENTIFIER ::= { enterprises 64991 }\nEND\n";
    const QByteArray secondDependency =
        "VERIFY-DEP-MIB DEFINITIONS ::= BEGIN\nIMPORTS verifyBase FROM VERIFY-BASE-MIB;\n"
        "verifyChild OBJECT IDENTIFIER ::= { verifyBase 1 }\nEND\n";
    const QByteArray editorSource =
        "VERIFY-ROOT-MIB DEFINITIONS ::= BEGIN\nIMPORTS verifyChild FROM VERIFY-DEP-MIB;\n"
        "verifyLeaf OBJECT IDENTIFIER ::= { verifyChild 1 }\nEND\n";
    ok &= check(writeFixture(QDir(importsDirectory).filePath("unrelated-name.my"), firstDependency) &&
                writeFixture(QDir(importsDirectory).filePath("another-vendor-file.txt"), secondDependency),
                "write filename-independent recursive editor imports");
    const QStringList verificationPaths{standards, importsDirectory};
    MibEngine::instance().initialize(standards, true);
    smiSetFlags(normalFlags);
    const auto completeVerification = verify(editorSource, verificationPaths, 5, false);
    ok &= check(completeVerification.success && !hasTag(completeVerification, "module-not-found") &&
                !hasTag(completeVerification, "unknown-identifier"),
                "editor imports resolve by Library identity outside the active Query parser paths");
    const auto unresolvedVerification = verify(editorSource, {standards}, 5, false);
    ok &= check(hasTag(unresolvedVerification, "module-not-found"),
                "fresh verification does not reuse imports from the previous document/check");
    const auto repeatedVerification = verify(editorSource, verificationPaths, 5, false);
    ok &= check(repeatedVerification.success && !hasTag(repeatedVerification, "module-not-found"),
                "restoring Library availability recovers without restarting the app");
    const auto editedVerification = verify(
        QByteArray(editorSource).replace("verifyLeaf OBJECT IDENTIFIER", "verifyLeaf OBJECT-TYPE t"), verificationPaths, 5, false);
    bool editedSyntaxError = false;
    for (const auto &diagnostic : editedVerification.diagnostics)
        editedSyntaxError |= diagnostic.severity <= 3 && diagnostic.message.contains("syntax error") &&
                             diagnostic.sourcePath == editedVerification.stagedSourcePath;
    ok &= check(editedSyntaxError, "current unsaved editor bytes are reparsed for an existing identity");
    const QString duplicate = QDir(importsDirectory).filePath("duplicate-base.mib");
    ok &= check(writeFixture(duplicate, firstDependency), "write identical provider duplicate");
    const auto identicalVerification = verify(editorSource, verificationPaths, 5, false);
    ok &= check(!hasTag(identicalVerification, "editor-provider-conflict") &&
                !hasTag(identicalVerification, "module-not-found"), "identical duplicate imports remain usable");
    ok &= check(writeFixture(duplicate, QByteArray(firstDependency).replace("64991", "64992")), "write conflicting provider");
    const auto conflictVerification = verify(editorSource, verificationPaths, 5, false);
    ok &= check(hasTag(conflictVerification, "editor-provider-conflict"),
                "different-content providers produce an explicit Library conflict instead of an arbitrary choice");
    QFile::remove(duplicate);
    QFile pib(QStringLiteral(SNMPB_SOURCE_DIR "/libsmi/pibs/tubs/ACCESSBIND-PIB"));
    ok &= check(pib.open(QIODevice::ReadOnly), "open representative PIB");
    const auto pibVerification = verify(pib.readAll(),
        {standards, QStringLiteral(SNMPB_SOURCE_DIR "/libsmi/mibs/iana"),
         QStringLiteral(SNMPB_SOURCE_DIR "/libsmi/mibs/tubs"),
         QStringLiteral(SNMPB_SOURCE_DIR "/libsmi/pibs/ietf"),
         QStringLiteral(SNMPB_SOURCE_DIR "/libsmi/pibs/tubs")}, 5, false);
    ok &= check(!hasTag(pibVerification, "module-not-found") && hasTag(pibVerification, "attribute-not-in-group"),
                "ACCESSBIND retains real compliance findings without profile-induced missing imports");
    for (const auto &diagnostic : pibVerification.diagnostics)
        ok &= check(!diagnostic.sourcePath.contains("/aliases/"), "import diagnostics show original Library source paths");
    ok &= check(savedIndex.open(QIODevice::ReadOnly) && savedIndex.readAll() == persistedIndexBefore,
                "verification never writes its refreshed snapshot into the persisted Library index");
    savedIndex.close();
    QFile atm(standards + "/ATM-MIB");
    ok &= check(atm.open(QIODevice::ReadOnly), "open representative RowStatus warning");
    const auto atmVerification = verify(atm.readAll(),
        {standards, QStringLiteral(SNMPB_SOURCE_DIR "/libsmi/mibs/iana")}, 5, false);
    bool rowStatusWarning = false;
    for (const auto &diagnostic : atmVerification.diagnostics)
        if (diagnostic.tag == "rowstatus-default" && diagnostic.severity == 4) rowStatusWarning = true;
    ok &= check(rowStatusWarning, "fresh verification preserves the ATM-MIB RowStatus warning severity");
    std::cout << "Verification import scope/reparse/conflict/PIB workflows passed; PIB-ms="
              << pibVerification.elapsedMilliseconds << std::endl;
    auto explicitDefect = [&](const QString &filename, const QByteArray &bytes) {
        const QString path = temporary.filePath(filename);
        QFile fixture(path);
        if (!fixture.open(QIODevice::WriteOnly) || fixture.write(bytes) != bytes.size()) return false;
        fixture.close();
        const int saved = smiGetFlags();
        smiSetFlags(saved | SMI_FLAG_RECURSIVE);
        MibDiagnosticCollector collector(1, filename);
        collector.install(MibValidationErrorLevel(MibValidationLevel::FullReview));
        smiLoadModule(QDir::toNativeSeparators(path).toLocal8Bit().constData());
        collector.finish(nullptr, 3);
        smiSetFlags(saved);
        return !collector.diagnostics().isEmpty() && smiGetFlags() == normalFlags;
    };
    ok &= check(explicitDefect("BAD-OBJECT-TYPE-MIB",
        "BAD-OBJECT-TYPE-MIB DEFINITIONS ::= BEGIN\n"
        "IMPORTS OBJECT-TYPE FROM SNMPv2-SMI;\n"
        "broken OBJECT-TYPE t\nEND\n"),
        "patched libsmi catches OBJECT-TYPE t in explicit full review without flag leakage");
    ok &= check(explicitDefect("BAD-COUNTERS-MIB",
        "BAD-COUNTERS-MIB DEFINITIONS ::= BEGIN\n"
        "IMPORTS OBJECT-TYPE FROM SNMPv2-SMI;\n"
        "broken OBJECT-TYPE\nSYNTAX CounterS\nMAX-ACCESS read-only\n"
        "STATUS current\nDESCRIPTION \"broken\"\n::= { 1 3 }\nEND\n"),
        "patched libsmi catches SYNTAX CounterS in explicit full review without flag leakage");
    const QString liveDirectory = qEnvironmentVariable("SNMPB_LIVE_IANA_DIR");
    if (!liveDirectory.isEmpty()) {
        for (const QString &name : {QStringLiteral("IANA-ENERGY-RELATION-MIB"),
                                    QStringLiteral("IANA-SMF-MIB")}) {
            QFile source(QDir(liveDirectory).filePath(name));
            ok &= check(source.open(QIODevice::ReadOnly), "open controlled live IANA artifact");
            const QByteArray bytes = source.readAll();
            const auto scan = MibImportScanner::scan(bytes);
            MibCatalogEntry live;
            live.sourceId = "iana"; live.sourceName = "IANA";
            live.category = "Standards MIB"; live.moduleName = name;
            const QString slug = name == QStringLiteral("IANA-ENERGY-RELATION-MIB")
                ? QStringLiteral("ianaenergyrelation-mib")
                : QStringLiteral("ianasmf-mib");
            live.url = QString("https://www.iana.org/assignments/%1/%1").arg(slug);
            live.filename = name + ".mib"; live.revision = scan.revision;
            MibLibraryRecord record;
            ok &= check(scan.moduleNames.contains(name) &&
                library.install(live, bytes, {bundled}, &record, &error,
                    [](const QString &path, QString *message) {
                        const int saved = smiGetFlags();
                        smiSetFlags(saved | SMI_FLAG_ERRORS | SMI_FLAG_NODESCR);
                        MibDiagnosticCollector collector(2, path);
                        collector.install(5);
                        char *loaded = smiLoadModule(
                            QDir::toNativeSeparators(path).toLocal8Bit().constData());
                        const auto diagnostics = collector.diagnostics();
                        collector.finish(nullptr, 3); smiSetFlags(saved);
                        if (!loaded && message) *message = diagnostics.isEmpty()
                            ? QStringLiteral("libsmi validation failed")
                            : diagnostics.first().message;
                        return loaded != nullptr;
                    }), "controlled live IANA identity/validation/atomic install");
            QFile provenance(QDir(library.metadataPath()).filePath(name + ".json"));
            ok &= check(record.status == MibLibraryStatus::Installed &&
                        !live.revision.isEmpty() &&
                        !record.sha256.isEmpty() && provenance.open(QIODevice::ReadOnly) &&
                        provenance.readAll().contains("www.iana.org"),
                        "controlled live IANA installed status and provenance");
        }
    }
    const QString cachePath = root.filePath("user-mibs/cache/catalog-v1.json");
    const MibCatalogCacheInfo cacheInfo{QDateTime::fromString("2026-08-12T12:00:00Z", Qt::ISODate),
        "iana", "https://www.iana.org/protocols"};
    ok &= check(MibCatalogCache::save(cachePath, ianaCatalog, cacheInfo, &error),
                "atomic normalized provider catalog cache");
    MibCatalog cached; MibCatalogCacheInfo loadedInfo;
    ok &= check(MibCatalogCache::load(cachePath, &cached, &loadedInfo, &error) &&
                cached.entries().size() == 2 && loadedInfo.sourceId == "iana" &&
                loadedInfo.refreshedAt == cacheInfo.refreshedAt,
                "offline catalog cache metadata round trip");
    QFile priorCacheFile(cachePath);
    ok &= check(priorCacheFile.open(QIODevice::ReadOnly), "open prior catalog cache");
    const QByteArray priorCache = priorCacheFile.readAll();
    MibCatalog invalidRefresh;
    ok &= check(!IanaMibSourceProvider::parseIndex("unavailable", &invalidRefresh, &error),
                "provider failure is explicit");
    QFile cacheAfter(cachePath);
    ok &= check(cacheAfter.open(QIODevice::ReadOnly) && cacheAfter.readAll() == priorCache,
                "failed refresh preserves prior valid cache");
    MibCatalogEntry downloadable = entry("VENDOR-MIB", {"BASE-MIB"});
    downloadable.filename = "not-the-module-name.mib";
    const QByteArray content = mib("VENDOR-MIB", {"BASE-MIB"});
    downloadable.sha256 = QString::fromLatin1(
        QCryptographicHash::hash(content, QCryptographicHash::Sha256).toHex());
    MibLibraryRecord installed;
    bool validatorCalled = false;
    ok &= check(library.install(downloadable, content, {bundled}, &installed, &error,
        [&](const QString &path, QString *) {
            validatorCalled = QFileInfo::exists(path); return true;
        }), "valid atomic install");
    ok &= check(validatorCalled && installed.moduleName == "VENDOR-MIB" &&
                QFileInfo::exists(installed.localPath) &&
                QFileInfo::exists(QDir(library.metadataPath()).filePath("VENDOR-MIB.json")),
                "validation/provenance/install files");
    MibModuleRecord localOne;
    localOne.name = "DECLARED-LOCAL-MIB";
    localOne.path = QDir(bundled).filePath("physical-vendor-bundle.mib");
    MibModuleRecord localTwo = localOne; localTwo.name = "SECOND-DECLARED-LOCAL-MIB";
    const auto inventory = library.inventory({bundled}, catalog, {localOne, localTwo});
    bool bundledFound = false, downloadedFound = false, localOneFound = false, localTwoFound = false;
    for (const auto &record : inventory) {
        bundledFound |= record.moduleName == "BASE-MIB" && record.status == MibLibraryStatus::Standards;
        downloadedFound |= record.moduleName == "VENDOR-MIB" && record.status == MibLibraryStatus::Installed;
        localOneFound |= record.moduleName == localOne.name && record.localPath == localOne.path &&
            MibLibraryOriginText(record) == "Imported";
        localTwoFound |= record.moduleName == localTwo.name && record.localPath == localTwo.path &&
            MibLibraryOriginText(record) == "Imported";
    }
    ok &= check(bundledFound && downloadedFound && localOneFound && localTwoFound,
                "bundled/downloaded and multiple declared local identities enter inventory");

    MibCatalogEntry mismatch = entry("EXPECTED"); mismatch.filename = "mismatch.mib";
    ok &= check(!library.install(mismatch, mib("ACTUAL"), {bundled}, nullptr, &error),
                "module-name mismatch rejected");
    MibCatalogEntry badHash = entry("HASH-MIB"); badHash.filename = "hash.mib";
    badHash.sha256 = QString(64, '0');
    ok &= check(!library.install(badHash, mib("HASH-MIB"), {bundled}, nullptr, &error),
                "checksum mismatch rejected");
    MibCatalogEntry invalid = entry("INVALID-MIB"); invalid.filename = "invalid.mib";
    ok &= check(!library.install(invalid, mib("INVALID-MIB"), {bundled}, nullptr, &error,
        [](const QString &, QString *message) { *message = "scripted invalid"; return false; }) &&
        !QFileInfo::exists(QDir(library.downloadedPath()).filePath("invalid.mib")),
        "semantic validation failure leaves no installed file");
    MibCatalogEntry bundledCollision = entry("BASE-MIB"); bundledCollision.filename = "base-new.mib";
    ok &= check(!library.install(bundledCollision, mib("BASE-MIB"), {bundled}, nullptr, &error),
                "a download never overwrites a module that exists in the Standards folder");
    ok &= check(error.contains("Standards folder") && !error.contains("immutable"),
                "refusal explains that the module is in the user-owned Standards folder");
    QString standardsFileForBase;
    for (const auto &record : library.inventory({bundled}, catalog, {}))
        if (record.moduleName == "BASE-MIB") standardsFileForBase = record.localPath;
    ok &= check(!standardsFileForBase.isEmpty() &&
                (QFile::permissions(standardsFileForBase) & QFileDevice::WriteUser),
                "Standards module files are user-writable and therefore editable");
    ok &= check(!library.install(downloadable, content, {bundled}, nullptr, &error),
                "duplicate install rejected");
    const QByteArray unsaved = mib("BUFFER-MIB") + "SYNTAX CounterS\n";
    bool exactBufferObserved = false;
    ok &= check(MibValidationStaging::validate(unsaved, root.filePath("verify"),
        [&](const QString &path) {
            QFile staged(path); if (!staged.open(QIODevice::ReadOnly)) return false;
            exactBufferObserved = staged.readAll() == unsaved; return true;
        }, &error) && exactBufferObserved, "unsaved editor buffer validation staging");
    for (const QByteArray &defect : {QByteArray("OBJECT-TYPE t"), QByteArray("SYNTAX CounterS")}) {
        bool defectObserved = false;
        ok &= check(!MibValidationStaging::validate(defect, root.filePath("verify"),
            [&](const QString &path) {
                QFile staged(path); if (!staged.open(QIODevice::ReadOnly)) return false;
                defectObserved = staged.readAll() == defect; return false;
            }) && defectObserved, "unsaved defect reaches validator exactly");
    }
    ScriptedTransport transport;
    MibDownloadResult observed;
    QObject::connect(&transport, &MibDownloadTransport::finished,
                     [&](const MibDownloadResult &result) { observed = result; });
    transport.scripted.content = content; transport.scripted.httpStatus = 200;
    transport.get(QUrl("https://example.invalid/module"));
    ok &= check(observed.httpStatus == 200 && observed.content == content,
                "scripted transport success");
    transport.scripted = {}; transport.scripted.httpStatus = 404; transport.scripted.error = "Not Found";
    transport.get(QUrl("https://example.invalid/missing"));
    ok &= check(observed.httpStatus == 404 && !observed.error.isEmpty(), "scripted HTTP failure");
    transport.scripted = {}; transport.scripted.timedOut = true; transport.scripted.error = "timeout";
    transport.get(QUrl("https://example.invalid/timeout"));
    ok &= check(observed.timedOut, "scripted timeout");
    transport.cancel(); transport.get(QUrl("https://example.invalid/cancel"));
    ok &= check(observed.cancelled, "scripted cancellation");

    QTemporaryDir migration;
    const QString baseline = migration.filePath("baseline");
    const QString legacy = migration.filePath("legacy");
    const QString targetRoot = migration.filePath("visible-root");
    QDir().mkpath(baseline); QDir().mkpath(QDir(legacy).filePath("downloaded"));
    QFile standard(QDir(baseline).filePath("STANDARD-MIB"));
    openFixture(standard, QIODevice::WriteOnly); standard.write(mib("STANDARD-MIB")); standard.close();
    QFile imported(QDir(legacy).filePath("downloaded/vendor-file.mib"));
    openFixture(imported, QIODevice::WriteOnly); imported.write(mib("DECLARED-VENDOR-MIB")); imported.close();
    const QByteArray legacyBefore = [&] { QFile f(imported.fileName()); openFixture(f, QIODevice::ReadOnly); return f.readAll(); }();
    MibCollection collection(targetRoot);
    const MibCollectionResult migrated = collection.initialize({baseline}, legacy);
    const MibCollectionResult migratedAgain = collection.initialize({baseline}, legacy);
    ok &= check(migrated.success && migrated.standardsCopied == 1 && migrated.importedCopied == 1 &&
                QFileInfo::exists(QDir(collection.importedPath()).filePath("vendor-file.mib")),
                "legacy imported file copies into visible Imported library");
    QFile legacyAfter(imported.fileName()); openFixture(legacyAfter, QIODevice::ReadOnly);
    ok &= check(legacyAfter.readAll() == legacyBefore && migratedAgain.success &&
                migratedAgain.standardsCopied == 0 && migratedAgain.importedCopied == 0,
                "migration is idempotent and leaves legacy originals untouched");
    QFile conflict(QDir(collection.importedPath()).filePath("vendor-file.mib"));
    openFixture(conflict, QIODevice::WriteOnly | QIODevice::Truncate); conflict.write("different"); conflict.close();
    const MibCollectionResult conflicted = collection.initialize({baseline}, legacy);
    QFile preserved(conflict.fileName()); openFixture(preserved, QIODevice::ReadOnly);
    ok &= check(conflicted.success && conflicted.conflicts.contains(conflict.fileName()) &&
                preserved.readAll() == QByteArray("different"),
                "different-content migration collision is reported and not overwritten");
    QDir().mkpath(QDir(targetRoot).filePath("Library/Standards/IETF"));
    QDir().mkpath(QDir(targetRoot).filePath("Library/Imported/Vendor"));
    QFile prototypeStandard(QDir(targetRoot).filePath("Library/Standards/IETF/OLD-STANDARD.mib"));
    openFixture(prototypeStandard, QIODevice::WriteOnly); prototypeStandard.write(mib("OLD-STANDARD")); prototypeStandard.close();
    QFile prototypeImported(QDir(targetRoot).filePath("Library/Imported/Vendor/OLD-VENDOR.mib"));
    openFixture(prototypeImported, QIODevice::WriteOnly); prototypeImported.write(mib("OLD-VENDOR")); prototypeImported.close();
    const MibCollectionResult transitioned = collection.initialize({baseline}, QString());
    ok &= check(transitioned.success &&
                QFileInfo::exists(QDir(collection.standardsPath()).filePath("IETF/OLD-STANDARD.mib")) &&
                QFileInfo::exists(QDir(collection.unassignedPath()).filePath("Vendor/OLD-VENDOR.mib")) &&
                prototypeStandard.exists() && prototypeImported.exists(),
                "prototype library trees copy safely without deleting originals");
    ok &= check(MibCollection::defaultRoot().startsWith(
                    QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)) &&
                MibCollection::internalStateRoot().startsWith(
                    QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)),
                "visible root uses Documents while dependency index remains internal");
    QSettings rootSettings(migration.filePath("settings.ini"), QSettings::IniFormat);
    const QString validRoot = migration.filePath("configured-root");
    MibCollectionResult configured;
    ok &= check(MibCollection::setConfiguredRoot(rootSettings, validRoot, {baseline}, &configured) &&
                MibCollection::configuredRoot(rootSettings) == QDir::cleanPath(validRoot),
                "valid user root persists across settings reload");
    const QString invalidRoot = migration.filePath("not-a-directory");
    QFile invalidRootFile(invalidRoot); openFixture(invalidRootFile, QIODevice::WriteOnly); invalidRootFile.write("x"); invalidRootFile.close();
    MibCollectionResult rejected;
    ok &= check(!MibCollection::setConfiguredRoot(rootSettings, invalidRoot, {baseline}, &rejected) &&
                MibCollection::configuredRoot(rootSettings) == QDir::cleanPath(validRoot),
                "invalid root change preserves prior valid configuration");
    std::cout << "Engine editor validation malformed-ms=" << engineBad.elapsedMilliseconds
              << " valid-ms=" << engineGood.elapsedMilliseconds << std::endl;
    smiExit();
    return ok ? 0 : 1;
}
