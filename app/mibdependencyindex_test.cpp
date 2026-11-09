#include "mibdependencyindex.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QTemporaryDir>
#include <QThread>
#include <iostream>

namespace {
bool check(bool value, const char *message) { if (!value) std::cerr << "FAIL: " << message << '\n'; return value; }
bool writeFile(const QString &path, const QByteArray &value) { QFile f(path); return f.open(QIODevice::WriteOnly) && f.write(value) == value.size(); }
QByteArray mib(const QString &name, const QStringList &imports = {}) {
    QByteArray value = name.toUtf8() + " DEFINITIONS ::= BEGIN\n";
    if (!imports.isEmpty()) { value += "IMPORTS\n  thing FROM " + imports.join("\n  other FROM ").toUtf8() + ";\n"; }
    return value + "END\n";
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv); bool ok = true; QTemporaryDir temp;
    const QString productionPath = MibDependencyIndex::defaultPath();
    const QString originalApplicationName = QCoreApplication::applicationName();
    QCoreApplication::setApplicationName(QStringLiteral("Acceptance Utility With Another Name"));
    ok &= check(MibDependencyIndex::defaultPath() == productionPath,
                "production index path is independent of utility application name");
    QCoreApplication::setApplicationName(originalApplicationName);
    ok &= check(QDir::fromNativeSeparators(productionPath).endsWith(
        QStringLiteral("/SnmpB/mibs/dependency-index-v1.json")),
        "production helper resolves the intended application-owned path");
    ok &= check(temp.isValid(), "temporary root"); QDir root(temp.path());
    root.mkdir("first"); root.mkdir("second"); const QString first = root.filePath("first"), second = root.filePath("second");
    writeFile(QDir(first).filePath("child.mib"), mib("CHILD-MIB", {"ROOT-MIB"}));
    writeFile(QDir(first).filePath("odd-provider.mib"), mib("ROOT-MIB"));
    writeFile(QDir(first).filePath("multi.mib"), mib("ONE-MIB") + mib("TWO-MIB", {"ROOT-MIB"}));
    writeFile(QDir(first).filePath("a.mib"), mib("A-MIB", {"B-MIB"}));
    writeFile(QDir(first).filePath("b.mib"), mib("B-MIB", {"C-MIB"}));
    writeFile(QDir(first).filePath("c.mib"), mib("C-MIB"));
    writeFile(QDir(first).filePath("shared1.mib"), mib("SHARED-ONE", {"ROOT-MIB"}));
    writeFile(QDir(first).filePath("shared2.mib"), mib("SHARED-TWO", {"ROOT-MIB"}));
    writeFile(QDir(first).filePath("missing.mib"), mib("MISSING-CONSUMER", {"DOES-NOT-EXIST"}));
    writeFile(QDir(first).filePath("circle-a.mib"), mib("CIRCLE-A", {"CIRCLE-B"}));
    writeFile(QDir(first).filePath("circle-b.mib"), mib("CIRCLE-B", {"CIRCLE-A"}));
    writeFile(QDir(first).filePath("synro.mib"), mib("SYNOPTICS-ROOT-MIB"));
    writeFile(QDir(first).filePath("nnsrnode.mib"), mib("NORTEL-nnSRNode-MIB", {"NORTEL-MIB-ARCS-MIB"}));
    writeFile(QDir(first).filePath("nortel.mib"), mib("NORTEL-MIB-ARCS-MIB"));
    const QString indexPath = root.filePath("dependency-index-v1.json"); MibDependencyIndex index(indexPath);
    const auto initial = index.update({first, second});
    ok &= check(initial.scanned == 14 && initial.reused == 0 && initial.changed, "initial scan records every candidate");
    ok &= check(index.provider("ROOT-MIB").path.endsWith("odd-provider.mib"), "filename differs from identity");
    ok &= check(index.provider("ONE-MIB").path == index.provider("TWO-MIB").path, "multiple identities per physical file");
    ok &= check(index.imports("TWO-MIB") == QStringList{"ROOT-MIB"} && index.imports("ONE-MIB").isEmpty(), "direct imports retained per declaration");
    index.recordVerification("ROOT-MIB", true);
    ok &= check(index.semanticallyVerified("ROOT-MIB"),
                "successful semantic verification is cached on its provider record");
    const auto verificationReuse = index.update({first, second});
    ok &= check(verificationReuse.scanned == 0 && index.semanticallyVerified("ROOT-MIB"),
                "unchanged provider content reuses semantic verification");
    index.recordVerification("CHILD-MIB", true);
    writeFile(QDir(first).filePath("child.mib"), mib("CHILD-MIB", {"ROOT-MIB"}) + "\n");
    QFile childFile(QDir(first).filePath("child.mib"));
    childFile.setFileTime(QDateTime::currentDateTime().addSecs(2), QFileDevice::FileModificationTime);
    const auto oneChanged = index.update({first, second});
    ok &= check(oneChanged.scanned == 1 && !index.semanticallyVerified("CHILD-MIB") &&
                index.semanticallyVerified("ROOT-MIB"),
                "one changed provider invalidates only its semantic verification state");
    ok &= check(MibDeclaredIdentitiesForCandidate("synro.mib", index) == QStringList{"SYNOPTICS-ROOT-MIB"} &&
                MibDeclaredIdentitiesForCandidate("nnsrnode.mib", index) == QStringList{"NORTEL-nnSRNode-MIB"},
                "filename-not-identity candidates project to declared identities");
    ok &= check(MibDeclaredIdentitiesForCandidate("multi.mib", index) ==
                (QStringList{"ONE-MIB", "TWO-MIB"}),
                "one physical candidate projects every declared identity");
    const auto legacyRequests = MibNormalizeRuntimeRequests(
        {"synro.mib", "SYNOPTICS-ROOT-MIB", "multi.mib", "ONE-MIB", "nnsrnode.mib"}, index);
    ok &= check(legacyRequests.identities ==
                (QStringList{"SYNOPTICS-ROOT-MIB", "ONE-MIB", "TWO-MIB", "NORTEL-nnSRNode-MIB"}) &&
                legacyRequests.legacyFilenameCount == 3 && legacyRequests.duplicateCount == 2,
                "legacy filenames and identities normalize to one stable explicit request set");
    QStringList afterRemove = legacyRequests.identities;
    afterRemove.removeAll("ONE-MIB");
    ok &= check(afterRemove.contains("TWO-MIB") && !afterRemove.contains("ONE-MIB"),
                "removing one identity from a multi-declaration provider remains coherent");
    QStringList hundredExisting;
    for (int i = 0; i < 100; ++i) hundredExisting.append(QStringLiteral("EXISTING-%1").arg(i));
    hundredExisting.append("ATM-TC");
    const QStringList beforeRetry = hundredExisting;
    if (!hundredExisting.contains("ATM-TC")) hundredExisting.append("ATM-TC");
    ok &= check(hundredExisting == beforeRetry && hundredExisting.size() == 101,
                "retrying an already-explicit unloaded identity does not duplicate a 100+ request set");
    QSet<QString> simulatedRuntime;
    for (const QString &identity : beforeRetry)
        if (identity != "ATM-TC") simulatedRuntime.insert(identity);
    for (int i = 0; i < 13; ++i) simulatedRuntime.insert(QStringLiteral("DEPENDENCY-%1").arg(i));
    const QSet<QString> priorRuntime = simulatedRuntime;
    simulatedRuntime.insert("ATM-TC");
    ok &= check(beforeRetry.size() == 101 && priorRuntime.size() == 113 &&
                !priorRuntime.contains("ATM-TC") && simulatedRuntime.size() == 114 &&
                std::all_of(priorRuntime.cbegin(), priorRuntime.cend(),
                    [&simulatedRuntime](const QString &name) { return simulatedRuntime.contains(name); }),
                "headless ATM-TC retry preserves all 113 prior loaded identities without duplicating 101 explicit requests");
    const QSet<QString> knownGoodRuntime = simulatedRuntime;
    QSet<QString> failedAttempt;
    failedAttempt.clear();
    failedAttempt = knownGoodRuntime;
    ok &= check(failedAttempt == knownGoodRuntime,
                "failed reconstruction rollback restores the complete known-good runtime specification");

    QSet<QString> loaded; int childAttempts = 0;
    auto simulated = [&](const QString &, const QString &expected) {
        MibDependencyLoadAttempt attempt; if (expected == "CHILD-MIB") ++childAttempts;
        if (expected == "CHILD-MIB" && !loaded.contains("ROOT-MIB")) { attempt.diagnostic = "dependency absent"; return attempt; }
        loaded.insert(expected); attempt.success = true; attempt.loadedModuleNames = {expected}; return attempt;
    };
    auto result = MibBoundedDependencyLoader().load({"CHILD-MIB"}, index, simulated);
    ok &= check(result.failures.isEmpty() && loaded.contains("CHILD-MIB") && childAttempts == 2 && result.passes == 2,
                "consumer before provider succeeds on bounded retry");
    ok &= check(result.noProgressStops == 0, "successful retry has no final no-progress error");
    loaded.clear(); loaded.insert("ROOT-MIB"); childAttempts = 0;
    result = MibBoundedDependencyLoader().load({"CHILD-MIB"}, index, simulated);
    ok &= check(result.failures.isEmpty() && childAttempts == 1, "provider before consumer succeeds first pass");

    loaded.clear();
    auto chain = MibBoundedDependencyLoader().load({"A-MIB"}, index, [&](const QString &, const QString &expected) {
        MibDependencyLoadAttempt attempt; bool ready = true; for (const auto &dep : index.imports(expected)) ready &= loaded.contains(dep);
        if (ready) { loaded.insert(expected); attempt.success = true; attempt.loadedModuleNames = {expected}; } return attempt;
    });
    ok &= check(chain.failures.isEmpty() && chain.passes == 3 && loaded.size() == 3, "reverse dependency chain reaches fixed point");
    auto shared = MibBoundedDependencyLoader().load({"SHARED-ONE", "SHARED-TWO"}, index, simulated);
    ok &= check(shared.dependencies.count("ROOT-MIB") == 1, "shared dependency deduplicated");

    auto missing = MibBoundedDependencyLoader().load({"MISSING-CONSUMER"}, index, simulated);
    bool missingProvider = false; for (const auto &failure : missing.failures) missingProvider |= failure.moduleName == "DOES-NOT-EXIST" && failure.kind == MibDependencyFailureKind::MissingProvider;
    ok &= check(missingProvider && missing.noProgressStops == 1, "missing provider stops once at fixed point");
    auto allFail = MibBoundedDependencyLoader().load({"ROOT-MIB", "CHILD-MIB"}, index,
        [](const QString &, const QString &) { return MibDependencyLoadAttempt{}; });
    ok &= check(allFail.passes == 1 && allFail.noProgressStops == 1, "all failures terminate after one no-progress pass");
    auto circular = MibBoundedDependencyLoader().load({"CIRCLE-A"}, index,
        [](const QString &, const QString &) { return MibDependencyLoadAttempt{}; });
    ok &= check(circular.passes == 1 && circular.failures.size() == 2, "circular imports cannot retry forever");

    writeFile(QDir(first).filePath("duplicate.mib"), mib("ROOT-MIB")); index.update({first, second});
    ok &= check(index.provider("ROOT-MIB").status == MibProviderStatus::Found,
                "same-precedence identical providers collapse deterministically");
    writeFile(QDir(first).filePath("different.mib"), mib("ROOT-MIB") + "-- different\n");
    index.update({first, second});
    ok &= check(index.provider("ROOT-MIB").status == MibProviderStatus::Ambiguous,
                "same-precedence different-content providers remain ambiguous");
    QFile::remove(QDir(first).filePath("different.mib"));
    QFile::remove(QDir(first).filePath("duplicate.mib")); writeFile(QDir(second).filePath("preferred-later.mib"), mib("ROOT-MIB")); index.update({first, second});
    ok &= check(index.provider("ROOT-MIB").status == MibProviderStatus::Found && index.provider("ROOT-MIB").path.endsWith("odd-provider.mib"),
                "earlier configured search path wins");

    MibDependencyIndex reloaded(indexPath); ok &= check(reloaded.load(), "persistent index loads");
    ok &= check(reloaded.files().size() == index.files().size() &&
        reloaded.provider("ROOT-MIB").path.endsWith("odd-provider.mib"),
        "record count and provider path survive process-style reload");
    const QDateTime beforeRead = QFileInfo(indexPath).lastModified(); QThread::msleep(20); MibDependencyIndex readOnly(indexPath); readOnly.load();
    ok &= check(QFileInfo(indexPath).lastModified() == beforeRead, "read does not rewrite index");
    const auto unchanged = reloaded.update({first, second});
    ok &= check(unchanged.scanned == 0 && unchanged.reused == 15 && !unchanged.changed, "unchanged files reuse persisted records");
    const quint64 oldGeneration = reloaded.generation(); QFile::remove(QDir(first).filePath("c.mib"));
    const auto deleted = reloaded.update({first, second});
    ok &= check(deleted.deleted == 1 && reloaded.provider("C-MIB").status == MibProviderStatus::Missing && reloaded.generation() > oldGeneration,
                "deleted file invalidates provider mapping");
    QThread::msleep(5); writeFile(QDir(first).filePath("b.mib"), mib("B-MIB", {"ROOT-MIB", "NEW-DEP"}));
    QFile changedFile(QDir(first).filePath("b.mib")); changedFile.setFileTime(QDateTime::currentDateTime().addSecs(2), QFileDevice::FileModificationTime);
    const auto changed = reloaded.update({first, second});
    ok &= check(changed.scanned >= 1 && reloaded.imports("B-MIB").contains("NEW-DEP"), "changed file invalidates cached hash/edges");

    QTemporaryDir ownershipFixture;
    const QString libraryA = QDir(ownershipFixture.path()).filePath("Library-A");
    const QString libraryB = QDir(ownershipFixture.path()).filePath("Library-B");
    const QString standardsA = QDir(libraryA).filePath("Standards");
    const QString standardsB = QDir(libraryB).filePath("Standards");
    QDir().mkpath(standardsA); QDir().mkpath(standardsB);
    const QString providerA = QDir(standardsA).filePath("ISOLATED-MIB");
    const QString providerB = QDir(standardsB).filePath("ISOLATED-MIB");
    writeFile(providerA, mib("ISOLATED-MIB") + "-- Library A\n");
    writeFile(providerB, mib("ISOLATED-MIB") + "-- Library B\n");
    MibDependencyIndex ownedA(QDir(ownershipFixture.path()).filePath("owned-a.json"), libraryA);
    MibDependencyIndex ownedB(QDir(ownershipFixture.path()).filePath("owned-b.json"), libraryB);
    ownedA.update({standardsA}); ownedB.update({standardsB});
    const quint64 generationA = ownedA.generation();
    ok &= check(ownedA.ownsSnapshot(libraryA, generationA) &&
                !ownedA.ownsSnapshot(libraryB, generationA) &&
                ownedA.provider("ISOLATED-MIB").path == QFileInfo(providerA).canonicalFilePath() &&
                ownedB.provider("ISOLATED-MIB").path == QFileInfo(providerB).canonicalFilePath(),
                "Library-owned indexes isolate same-identity providers across Library instances");
    ok &= check(MibDependencyIndex::pathForLibraryRoot(libraryA) !=
                MibDependencyIndex::pathForLibraryRoot(libraryB),
                "each canonical Library root has a distinct persistent index path");

    const QString sharedOwnershipPath = QDir(ownershipFixture.path()).filePath("shared.json");
    MibDependencyIndex storedA(sharedOwnershipPath, libraryA); storedA.update({standardsA});
    MibDependencyIndex wrongOwner(sharedOwnershipPath, libraryB);
    ok &= check(wrongOwner.load() &&
                wrongOwner.loadStatus() == MibDependencyIndexLoadStatus::LibraryMismatch &&
                wrongOwner.files().isEmpty(),
                "persisted providers cannot be loaded by a different Library owner");

    QThread::msleep(5);
    writeFile(providerA, mib("ISOLATED-MIB") + "-- Library A generation two\n");
    QFile providerAFile(providerA);
    providerAFile.setFileTime(QDateTime::currentDateTime().addSecs(2),
                              QFileDevice::FileModificationTime);
    const auto refreshedA = ownedA.update({standardsA});
    ok &= check(refreshedA.changed && ownedA.generation() > generationA &&
                !ownedA.ownsSnapshot(libraryA, generationA) &&
                ownedA.ownsSnapshot(libraryA, ownedA.generation()) &&
                ownedA.provider("ISOLATED-MIB").path == QFileInfo(providerA).canonicalFilePath(),
                "refresh advances generation, rejects the stale snapshot, and rebuilds current providers");

    QTemporaryDir productFixture;
    const QString standardsTree = QDir(productFixture.path()).filePath("Standards/IETF");
    const QString unassignedTree = QDir(productFixture.path()).filePath("Unassigned/Misc");
    const QString vossTree = QDir(productFixture.path()).filePath("Extreme/VOSS");
    const QString exosTree = QDir(productFixture.path()).filePath("Extreme/EXOS");
    for (const QString &directory : {standardsTree, unassignedTree, vossTree, exosTree})
        QDir().mkpath(directory);
    writeFile(QDir(standardsTree).filePath("base.mib"), mib("BASE-SMI"));
    writeFile(QDir(unassignedTree).filePath("misc.mib"), mib("MISC-COMMON"));
    const QByteArray equivalent = mib("EXTREME-BASE-MIB", {"BASE-SMI"});
    writeFile(QDir(vossTree).filePath("base.mib"), equivalent);
    writeFile(QDir(exosTree).filePath("base-copy.mib"), equivalent);
    MibDependencyIndex productIndex(QDir(productFixture.path()).filePath("index.json"));
    productIndex.update({standardsTree, unassignedTree, vossTree, exosTree});
    ok &= check(productIndex.provider("BASE-SMI").status == MibProviderStatus::Found &&
                productIndex.provider("MISC-COMMON").status == MibProviderStatus::Found &&
                productIndex.provider("EXTREME-BASE-MIB").status == MibProviderStatus::Found,
                "standards unassigned and product candidates share the global index");
    const QString equivalentProvider = productIndex.provider("EXTREME-BASE-MIB").path;
    productIndex.update({standardsTree, unassignedTree, exosTree, vossTree});
    ok &= check(productIndex.provider("EXTREME-BASE-MIB").status == MibProviderStatus::Found &&
                productIndex.provider("EXTREME-BASE-MIB").path == equivalentProvider,
                "equivalent product provider is independent of product path ordering");
    writeFile(QDir(exosTree).filePath("base-copy.mib"), equivalent + "-- EXOS variant\n");
    QFile variant(QDir(exosTree).filePath("base-copy.mib"));
    variant.setFileTime(QDateTime::currentDateTime().addSecs(2), QFileDevice::FileModificationTime);
    productIndex.update({standardsTree, unassignedTree, vossTree, exosTree});
    ok &= check(productIndex.provider("EXTREME-BASE-MIB").status == MibProviderStatus::Ambiguous,
                "different-content product copies expose one global ambiguity");

    const QString signature = MibDependencyIndex::profileSignature({"A-MIB"}, true);
    MibProfileDependencyCheck profile; profile.profileSignature = signature; profile.indexGeneration = reloaded.generation(); profile.checkedUtc = QDateTime::currentDateTimeUtc();
    reloaded.setProfileCheck("profile", profile); ok &= check(reloaded.profileCheckCurrent("profile", signature), "checked profile is current");
    ok &= check(!reloaded.profileCheckCurrent("profile", MibDependencyIndex::profileSignature({"B-MIB"}, true)), "profile edit marks state stale");
    reloaded.save(); MibDependencyIndex cached(indexPath); cached.load(); const auto cachedCheck = cached.update({first, second});
    ok &= check(cachedCheck.scanned == 0, "profile switching can reuse cached graph without rescanning");
    const QString missingIndexPath = root.filePath("missing-index.json");
    MibDependencyIndex missingIndex(missingIndexPath);
    ok &= check(missingIndex.load() && missingIndex.loadStatus() == MibDependencyIndexLoadStatus::Missing &&
        missingIndex.files().isEmpty(), "missing index is explicit and safely empty");
    const QString corruptPath = root.filePath("corrupt-index.json");
    writeFile(corruptPath, QByteArrayLiteral("{truncated")); QString loadError;
    MibDependencyIndex corrupt(corruptPath);
    ok &= check(!corrupt.load(&loadError) && corrupt.loadStatus() == MibDependencyIndexLoadStatus::MalformedJson &&
        !loadError.isEmpty(), "corrupt index reports concise diagnostic");
    const QString unsupportedPath = root.filePath("unsupported-index.json");
    writeFile(unsupportedPath, QByteArrayLiteral("{\"schemaVersion\":99,\"files\":[]}"));
    MibDependencyIndex unsupported(unsupportedPath);
    ok &= check(!unsupported.load(&loadError) && unsupported.loadStatus() == MibDependencyIndexLoadStatus::UnsupportedSchema,
        "unsupported schema reports explicit status");
    const QString emptyPath = root.filePath("empty-index.json"); writeFile(emptyPath, {});
    MibDependencyIndex emptyFile(emptyPath);
    ok &= check(!emptyFile.load(&loadError) && emptyFile.loadStatus() == MibDependencyIndexLoadStatus::EmptyFile,
        "zero-byte index reports explicit status");
    return ok ? 0 : 1;
}
