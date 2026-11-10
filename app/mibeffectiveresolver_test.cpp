#include "mibeffectiveplan.h"
#include "mibruntimestage.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QTemporaryDir>
#include <algorithm>
#include <iostream>

namespace {
bool check(bool value, const char *message)
{
    if (!value) std::cerr << "FAIL: " << message << '\n';
    return value;
}

QByteArray mib(const QString &identity, const QStringList &imports = {})
{
    QByteArray result = identity.toUtf8() + " DEFINITIONS ::= BEGIN\n";
    if (!imports.isEmpty())
        result += "IMPORTS value FROM " + imports.join("\n other FROM ").toUtf8() + ";\n";
    return result + "END\n";
}

QString write(const QString &directory, const QString &name, const QByteArray &contents)
{
    QDir().mkpath(directory);
    const QString path = QDir(directory).filePath(name);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(contents) != contents.size()) return {};
    return QFileInfo(path).canonicalFilePath();
}

const MibEffectivePlanFile *fileProviding(const MibEffectivePlan &plan, const QString &identity)
{
    const auto found = std::find_if(plan.runtimeFiles.cbegin(), plan.runtimeFiles.cend(),
        [&identity](const auto &file) { return file.identities.contains(identity); });
    return found == plan.runtimeFiles.cend() ? nullptr : &*found;
}

const MibPlanResolutionDiagnostic *diagnostic(const MibEffectivePlan &plan, const QString &identity)
{
    const auto found = std::find_if(plan.resolutionDiagnostics.cbegin(),
        plan.resolutionDiagnostics.cend(), [&identity](const auto &item) {
            return item.identity == identity;
        });
    return found == plan.resolutionDiagnostics.cend() ? nullptr : &*found;
}
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    if (application.arguments().value(1) == QStringLiteral("--voss")) {
        const QString libraryRoot = application.arguments().value(2);
        const QString profilePath = application.arguments().value(3);
        const QString profileId = application.arguments().value(4);
        const QString indexPath = application.arguments().value(5);
        const QString stagePath = application.arguments().value(6);
        QString error;
        MibDependencyIndex index(indexPath, libraryRoot);
        index.update({libraryRoot}, &error);
        const auto profiles = MibProfileRepository(profilePath).load(&error);
        const auto found = std::find_if(profiles.cbegin(), profiles.cend(),
            [&profileId](const auto &profile) { return profile.id == profileId; });
        if (!error.isEmpty() || found == profiles.cend()) {
            std::cerr << (error.isEmpty() ? "profile not found" : error.toStdString()) << '\n';
            return 2;
        }
        const QString fabricEngine = QDir(libraryRoot).filePath("Extreme Networks/Fabric Engine");
        const QString standardsRoot = QDir(libraryRoot).filePath("Standards");
        const QString unassignedRoot = QDir(libraryRoot).filePath("Unassigned");
        const auto within = [](const QString &path, const QString &root) {
            QString prefix = QDir::fromNativeSeparators(QFileInfo(root).canonicalFilePath());
            const QString candidate = QDir::fromNativeSeparators(QFileInfo(path).canonicalFilePath());
            if (!prefix.endsWith('/')) prefix += '/';
            return candidate.startsWith(prefix, Qt::CaseInsensitive);
        };
        MibEffectivePlanResolverInput input;
        input.id = QStringLiteral("voss-resolver-v1-diagnostic");
        QList<MibProfileMember> unassigned;
        int standardsMembers = 0;
        for (const auto &member : found->members) {
            if (within(member.canonicalPath, fabricEngine)) input.roots.append(member);
            else if (within(member.canonicalPath, unassignedRoot)) {
                input.roots.append(member);
                unassigned.append(member);
            } else if (within(member.canonicalPath, standardsRoot)) ++standardsMembers;
        }
        input.orderedScopes = {fabricEngine, standardsRoot};
        const MibEffectivePlan plan = MibEffectiveRuntimePlanResolver().resolve(input, index);
        int rootsCount = 0, dependenciesCount = 0;
        for (const auto &file : plan.runtimeFiles)
            file.origin == MibEffectivePlanFileOrigin::Root ? ++rootsCount : ++dependenciesCount;
        std::cout << "profile_members=" << found->members.size()
                  << " fabric_engine_roots=" << input.roots.size() - unassigned.size()
                  << " standards_members_not_roots=" << standardsMembers
                  << " unassigned_roots=" << unassigned.size()
                  << " plan_roots=" << rootsCount
                  << " plan_dependencies=" << dependenciesCount
                  << " plan_total=" << plan.runtimeFiles.size()
                  << " ambiguities=" << plan.ambiguousModules.size()
                  << " unresolved=" << plan.missingModules.size()
                  << " pin_failures=" << plan.pinFailureModules.size() << '\n';
        for (const auto &member : unassigned)
            std::cout << "unassigned=" << QDir::fromNativeSeparators(member.canonicalPath).toStdString()
                      << " identities=" << member.identities.join(',').toStdString()
                      << " treatment=explicit-root\n";
        for (const QString &identity : {QStringLiteral("ATM-TC-MIB"), QStringLiteral("BRIDGE-MIB"),
                QStringLiteral("EXTREME-BASE-MIB"), QStringLiteral("INET-ADDRESS-MIB"),
                QStringLiteral("IF-MIB"), QStringLiteral("IANAifType-MIB"),
                QStringLiteral("ENTITY-MIB"), QStringLiteral("UDP-MIB")}) {
            if (const auto *file = fileProviding(plan, identity))
                std::cout << identity.toStdString() << '='
                          << QDir::fromNativeSeparators(file->canonicalPath).toStdString()
                          << " origin=" << static_cast<int>(file->origin)
                          << " tier=" << static_cast<int>(file->resolutionTier) << '\n';
            else if (const auto *finding = diagnostic(plan, identity))
                std::cout << identity.toStdString() << "=UNRESOLVED tier="
                          << static_cast<int>(finding->tier)
                          << " candidates=" << finding->candidates.size()
                          << " reason=" << finding->reason.toStdString() << '\n';
            else std::cout << identity.toStdString() << "=NOT-REQUIRED\n";
        }
        for (const auto &finding : plan.resolutionDiagnostics) {
            std::cout << "problem=" << finding.identity.toStdString()
                      << " requesters=" << finding.requesters.join(',').toStdString()
                      << " tier=" << static_cast<int>(finding.tier)
                      << " candidates=" << finding.candidates.size()
                      << " reason=" << finding.reason.toStdString() << '\n';
            for (const auto &candidate : finding.candidates)
                std::cout << "candidate=" << finding.identity.toStdString() << '|'
                          << QDir::fromNativeSeparators(candidate.canonicalPath).toStdString()
                          << '|' << candidate.sha256.toStdString() << '\n';
        }
        if (plan.isComplete()) {
            const auto stage = MibRuntimeStage::prepare(plan, stagePath);
            std::cout << "stage_attempted=1 stage_success=" << stage.success
                      << " staged_aliases=" << (stage.success
                          ? QDir(stage.paths.orderedPaths().first()).entryList(
                                QDir::Files | QDir::NoDotAndDotDot).size() : 0) << '\n';
            return stage.success ? 0 : 3;
        }
        std::cout << "stage_attempted=0 reason=incomplete-plan\n";
        return 0;
    }
    bool ok = true;
    QTemporaryDir fixture;
    const QString batch = QDir(fixture.path()).filePath("Vendor/Fabric Engine");
    const QString roots = QDir(batch).filePath("roots");
    const QString local = QDir(batch).filePath("local");
    const QString sibling = QDir(batch).filePath("dependencies");
    const QString standards = QDir(fixture.path()).filePath("Standards");
    const QString outside = QDir(fixture.path()).filePath("Other Vendor/Old");

    const QString simpleRoot = write(local, "primary.my", mib("PRIMARY-MIB", {"SIMPLE-DEP"}));
    const QString simpleLocal = write(local, "dep-local.mib", mib("SIMPLE-DEP"));
    write(standards, "SIMPLE-DEP", mib("SIMPLE-DEP") + "standards-VARIANT ::= 1\n");
    const QString transitiveRoot = write(roots, "transitive.my", mib("TRANSITIVE-ROOT", {"TRANSITIVE-DEP"}));
    write(sibling, "transitive-dep.mib", mib("TRANSITIVE-DEP", {"TRANSITIVE-LEAF"}));
    write(standards, "transitive-leaf.mib", mib("TRANSITIVE-LEAF"));
    const QString cycleRoot = write(roots, "cycle-root.mib", mib("CYCLE-ROOT", {"CYCLE-DEP"}));
    write(sibling, "cycle-dep.mib", mib("CYCLE-DEP", {"CYCLE-ROOT"}));
    const QString scopeRoot = write(roots, "scope-root.mib", mib("SCOPE-ROOT", {"BATCH-DEP", "STANDARD-DEP"}));
    write(sibling, "batch-dep.mib", mib("BATCH-DEP"));
    write(standards, "standard-dep.mib", mib("STANDARD-DEP"));
    const QString outsideRoot = write(roots, "outside-root.mib", mib("OUTSIDE-ROOT", {"OUTSIDE-ONLY"}));
    write(outside, "outside-only.mib", mib("OUTSIDE-ONLY"));
    const QString pinRoot = write(roots, "pin-root.mib", mib("PIN-ROOT", {"PIN-DEP"}));
    const QString pinBatch = write(sibling, "pin-batch.mib", mib("PIN-DEP") + "batch-VARIANT ::= 1\n");
    const QString pinStandards = write(standards, "pin-standard.mib", mib("PIN-DEP") + "standard-VARIANT ::= 1\n");
    const QByteArray equivalent = mib("EQUIVALENT-DEP");
    const QString equivalentRoot = write(roots, "equivalent-root.mib", mib("EQUIVALENT-ROOT", {"EQUIVALENT-DEP"}));
    const QString equivalentA = write(sibling, "equivalent-a.mib", equivalent);
    const QString equivalentB = write(sibling, "equivalent-b.mib", equivalent);
    const QString ambiguityRoot = write(roots, "ambiguity-root.mib",
        mib("AMBIGUITY-ROOT", {"AMBIGUOUS-A", "AMBIGUOUS-B"}));
    write(sibling, "ambiguous-a-1.mib", mib("AMBIGUOUS-A") + "one-VARIANT ::= 1\n");
    write(sibling, "ambiguous-a-2.mib", mib("AMBIGUOUS-A") + "two-VARIANT ::= 1\n");
    write(sibling, "ambiguous-b-1.mib", mib("AMBIGUOUS-B") + "one-VARIANT ::= 1\n");
    write(sibling, "ambiguous-b-2.mib", mib("AMBIGUOUS-B") + "two-VARIANT ::= 1\n");
    const QString multiRoot = write(roots, "multi-root.mib", mib("MULTI-ROOT", {"MULTI-DEP", "EXTRA-IDENTITY"}));
    const QString multiProvider = write(sibling, "odd-multi-name.my",
        mib("MULTI-DEP") + mib("EXTRA-IDENTITY"));
    const QString conflictRoot = write(roots, "conflict-root.mib",
        mib("CONFLICT-ROOT", {"NEEDED-IDENTITY"}) + mib("OVERLAP-IDENTITY"));
    write(sibling, "conflicting-multi.mib",
        mib("NEEDED-IDENTITY") + mib("OVERLAP-IDENTITY") + "different-file-VARIANT ::= 1\n");
    const QString filenameRoot = write(roots, "filename-root.mib",
        mib("FILENAME-ROOT", {"ATM-TC-MIB", "BRIDGE-MIB"}));
    const QString atm = write(sibling, "atm_tc.mib", mib("ATM-TC-MIB"));
    const QString bridge = write(sibling, "rfc4188.mib", mib("BRIDGE-MIB"));

    MibDependencyIndex index(QDir(fixture.path()).filePath("index.json"), fixture.path());
    index.update({batch, standards, outside});
    const MibEffectiveRuntimePlanResolver resolver;
    const auto resolve = [&](QString id, const QStringList &rootFiles, QStringList scopes,
                             QMap<QString, MibProviderPin> pins = {}) {
        MibEffectivePlanResolverInput input;
        input.id = std::move(id);
        input.roots = MibProfileMembersFromFiles(rootFiles);
        input.orderedScopes = std::move(scopes);
        input.pins = std::move(pins);
        return resolver.resolve(input, index);
    };

    const auto emptyManifest = resolve("empty-manifest", {}, {});
    ok &= check(emptyManifest.authorityError.isEmpty() && emptyManifest.runtimeFiles.isEmpty() &&
                emptyManifest.explicitModules.isEmpty(),
                "valid empty schema-5 manifest produces an authorized zero-root plan");

    const auto simple = resolve("simple", {simpleRoot}, {batch, standards});
    ok &= check(simple.isComplete() && simple.runtimeFiles.size() == 2 &&
                fileProviding(simple, "PRIMARY-MIB")->origin == MibEffectivePlanFileOrigin::Root &&
                fileProviding(simple, "SIMPLE-DEP")->canonicalPath == simpleLocal &&
                fileProviding(simple, "SIMPLE-DEP")->resolutionTier ==
                    MibPlanResolutionTier::RequesterDirectory,
                "exact root and same-directory dependency");

    const auto transitive = resolve("transitive", {transitiveRoot}, {batch, standards});
    ok &= check(transitive.isComplete() && transitive.runtimeFiles.size() == 3 &&
                fileProviding(transitive, "TRANSITIVE-DEP")->resolutionTier ==
                    MibPlanResolutionTier::RequesterBatch &&
                fileProviding(transitive, "TRANSITIVE-LEAF")->resolutionTier ==
                    MibPlanResolutionTier::OrderedScope,
                "transitive requester-batch and ordered-scope resolution");

    const auto cycle = resolve("cycle", {cycleRoot}, {batch, standards});
    ok &= check(cycle.isComplete() && cycle.runtimeFiles.size() == 2,
                "cycles terminate through already-provided identities");

    const auto scoped = resolve("scoped", {scopeRoot}, {batch, standards});
    ok &= check(scoped.isComplete() &&
                fileProviding(scoped, "BATCH-DEP")->resolutionTier == MibPlanResolutionTier::RequesterBatch &&
                fileProviding(scoped, "STANDARD-DEP")->resolutionTier == MibPlanResolutionTier::OrderedScope,
                "requester batch precedes ordered Standards fallback");

    const auto noGlobal = resolve("no-global", {outsideRoot}, {batch, standards});
    ok &= check(!noGlobal.isComplete() && !fileProviding(noGlobal, "OUTSIDE-ONLY") &&
                diagnostic(noGlobal, "OUTSIDE-ONLY") &&
                diagnostic(noGlobal, "OUTSIDE-ONLY")->tier == MibPlanResolutionTier::OutOfScope,
                "Catalog providers outside scope remain unresolved");

    const auto pinProvider = index.providersFor("PIN-DEP");
    const auto standardPin = std::find_if(pinProvider.cbegin(), pinProvider.cend(),
        [&pinStandards](const auto &provider) { return provider.canonicalPath == pinStandards; });
    const auto pinned = resolve("pinned", {pinRoot}, {batch, standards},
        {{"PIN-DEP", {standardPin->canonicalPath, standardPin->sha256}}});
    ok &= check(pinned.isComplete() && fileProviding(pinned, "PIN-DEP")->canonicalPath == pinStandards &&
                fileProviding(pinned, "PIN-DEP")->resolutionTier == MibPlanResolutionTier::ExplicitPin,
                "explicit pin overrides locality absolutely");
    const auto stalePin = resolve("stale-pin", {pinRoot}, {batch, standards},
        {{"PIN-DEP", {pinBatch, QString(64, '0')}}});
    ok &= check(!stalePin.isComplete() && diagnostic(stalePin, "PIN-DEP") &&
                diagnostic(stalePin, "PIN-DEP")->tier == MibPlanResolutionTier::StalePin,
                "stale explicit pin is a hard error without fallback");

    const auto equivalentPlan = resolve("equivalent", {equivalentRoot}, {batch, standards});
    ok &= check(equivalentPlan.isComplete() &&
                fileProviding(equivalentPlan, "EQUIVALENT-DEP")->canonicalPath ==
                    (std::min)(equivalentA, equivalentB) &&
                fileProviding(equivalentPlan, "EQUIVALENT-DEP")->resolutionRationale.contains(
                    "equivalent alternatives"),
                "identical providers resolve by deterministic canonical path");

    const auto ambiguous = resolve("ambiguous", {ambiguityRoot}, {batch, standards});
    QSet<QString> ambiguousIdentities;
    for (const auto &finding : ambiguous.resolutionDiagnostics)
        if (finding.tier == MibPlanResolutionTier::Ambiguous)
            ambiguousIdentities.insert(finding.identity);
    ok &= check(!ambiguous.isComplete() && ambiguousIdentities ==
                    QSet<QString>({"AMBIGUOUS-A", "AMBIGUOUS-B"}),
                "all independent differing-provider ambiguities are reported");

    const auto multi = resolve("multi", {multiRoot}, {batch, standards});
    ok &= check(multi.isComplete() && multi.runtimeFiles.size() == 2 &&
                fileProviding(multi, "MULTI-DEP")->canonicalPath == multiProvider &&
                fileProviding(multi, "EXTRA-IDENTITY") == fileProviding(multi, "MULTI-DEP"),
                "one selected multi-identity file satisfies every declared identity");
    const auto conflict = resolve("conflict", {conflictRoot}, {batch, standards});
    ok &= check(!conflict.isComplete() && diagnostic(conflict, "NEEDED-IDENTITY") &&
                diagnostic(conflict, "NEEDED-IDENTITY")->tier ==
                    MibPlanResolutionTier::ProviderConflict,
                "multi-identity provider cannot introduce overlapping exact authority");

    const auto filenames = resolve("filenames", {filenameRoot}, {batch, standards});
    ok &= check(filenames.isComplete() && fileProviding(filenames, "ATM-TC-MIB")->canonicalPath == atm &&
                fileProviding(filenames, "BRIDGE-MIB")->canonicalPath == bridge,
                "declared identity resolves independently of ATM-TC and BRIDGE filenames");

    const auto deterministicA = resolve("deterministic", {simpleRoot, transitiveRoot}, {batch, standards});
    const auto deterministicB = resolve("deterministic", {transitiveRoot, simpleRoot}, {batch, standards});
    ok &= check(deterministicA.runtimeAuthoritySha256 == deterministicB.runtimeAuthoritySha256 &&
                deterministicA.sha256 == deterministicB.sha256,
                "root order does not affect Plan entries or hashes");
    write(outside, "unrelated-new.mib", mib("UNRELATED-NEW-MIB"));
    index.update({batch, standards, outside});
    const auto outsideAddition = resolve("deterministic", {simpleRoot, transitiveRoot}, {batch, standards});
    ok &= check(outsideAddition.runtimeFiles.size() == deterministicA.runtimeFiles.size() &&
                outsideAddition.runtimeAuthoritySha256 == deterministicA.runtimeAuthoritySha256 &&
                outsideAddition.sha256 == deterministicA.sha256,
                "Catalog additions outside scope do not change Plan files or hashes");

    MibEffectivePlanResolverInput changedRootInput;
    changedRootInput.id = "changed-root";
    changedRootInput.roots = MibProfileMembersFromFiles({simpleRoot});
    changedRootInput.roots.first().sha256 = QString(64, 'f');
    changedRootInput.orderedScopes = {batch, standards};
    const auto changedRoot = resolver.resolve(changedRootInput, index);
    ok &= check(changedRoot.authorityError.isEmpty() && changedRoot.runtimeFiles.size() >= 1 &&
                std::any_of(changedRoot.resolutionDiagnostics.cbegin(), changedRoot.resolutionDiagnostics.cend(),
                            [](const auto &diagnostic) {
                                return diagnostic.tier == MibPlanResolutionTier::ModifiedRoot;
                            }),
                "a root whose content differs from the recorded hash still loads and is tagged modified");

    // A root that no longer exists is reported and skipped; the other roots still resolve.
    MibEffectivePlanResolverInput missingRootInput;
    missingRootInput.id = "missing-root";
    missingRootInput.roots = MibProfileMembersFromFiles({simpleRoot});
    MibProfileMember vanished = missingRootInput.roots.first();
    vanished.canonicalPath = QDir(batch).filePath("vanished-root.mib");
    vanished.identities = {"VANISHED-MIB"};
    missingRootInput.roots.append(vanished);
    missingRootInput.orderedScopes = {batch, standards};
    const auto missingRoot = resolver.resolve(missingRootInput, index);
    ok &= check(missingRoot.authorityError.isEmpty() && !missingRoot.runtimeFiles.isEmpty() &&
                std::any_of(missingRoot.resolutionDiagnostics.cbegin(), missingRoot.resolutionDiagnostics.cend(),
                            [](const auto &diagnostic) {
                                return diagnostic.tier == MibPlanResolutionTier::InvalidRoot &&
                                       diagnostic.identity == QStringLiteral("VANISHED-MIB");
                            }),
                "a missing root is reported and skipped without blocking the other roots");

    // Newest LAST-UPDATED wins within the winning scope, for roots and imports alike.
    // Filenames deliberately put the newest revision last.
    const auto dated = [](const QByteArray &date) {
        return QByteArray("REV-MIB DEFINITIONS ::= BEGIN\nrev MODULE-IDENTITY\nLAST-UPDATED \"") + date +
            "\"\nDESCRIPTION \"LAST-UPDATED \"\"209901010000Z\"\"\"\n::= { 1 3 }\nEND\n";
    };
    const QString oldRevision = write(sibling, "a-old.mib", dated("9901010000Z"));
    const QString newRevision = write(sibling, "z-new.mib", dated("200205090000Z"));
    const QString revisionRoot = write(roots, "revision-root.mib", mib("REV-ROOT", {"REV-MIB"}));
    index.update({batch, standards, outside});
    for (bool selected : {false, true}) {
        const auto revised = resolve("revision", selected ? QStringList{oldRevision, newRevision}
            : QStringList{revisionRoot}, {batch, standards});
        const auto *chosen = fileProviding(revised, "REV-MIB");
        const auto *finding = diagnostic(revised, "REV-MIB");
        ok &= check(chosen && chosen->canonicalPath == newRevision && finding &&
                    finding->reason.contains("2002-05-09") && finding->reason.contains("1999-01-01") &&
                    finding->reason.contains("newest LAST-UPDATED"),
                    "newest revision wins for selected files and imported dependencies, with dated report");
    }
    MibEffectivePlanResolverInput editedConflictInput;
    editedConflictInput.id = "edited-conflict";
    editedConflictInput.roots = MibProfileMembersFromFiles({oldRevision, newRevision});
    editedConflictInput.roots.last().sha256 = QString(64, 'f');
    editedConflictInput.orderedScopes = {batch, standards};
    const auto editedConflict = resolver.resolve(editedConflictInput, index);
    bool conflictReported = false, modifiedReported = false;
    for (const auto &finding : editedConflict.resolutionDiagnostics) {
        conflictReported |= finding.tier == MibPlanResolutionTier::NewestRevision;
        modifiedReported |= finding.tier == MibPlanResolutionTier::ModifiedRoot;
    }
    ok &= check(conflictReported && modifiedReported && editedConflict.resolvedByRevision("REV-MIB") &&
                !editedConflict.ambiguousModules.contains("REV-MIB"),
                "a Modified note cannot hide the same module's conflict finding; a newer revision resolves it");
    editedConflictInput.roots = MibProfileMembersFromFiles({oldRevision, newRevision});
    editedConflictInput.roots.first().sha256 = QString(64, 'f');
    const auto modifiedLoser = resolver.resolve(editedConflictInput, index);
    bool shadowedClaimedLoaded = false;
    for (const auto &finding : modifiedLoser.resolutionDiagnostics)
        shadowedClaimedLoaded |= finding.tier == MibPlanResolutionTier::ModifiedRoot;
    ok &= check(!shadowedClaimedLoaded && modifiedLoser.resolvedByRevision("REV-MIB"),
                "a shadowed modified duplicate is explained as a conflict, never claimed to be loaded");
    MibEffectivePlanResolverInput staleConflictInput;
    staleConflictInput.id = "stale-pin-conflict";
    staleConflictInput.roots = MibProfileMembersFromFiles({revisionRoot});
    staleConflictInput.orderedScopes = {batch, standards};
    staleConflictInput.pins.insert("REV-MIB", {oldRevision, QString(64, 'f')});
    const auto staleConflict = resolver.resolve(staleConflictInput, index);
    bool staleReported = false; conflictReported = false;
    for (const auto &finding : staleConflict.resolutionDiagnostics) {
        staleReported |= finding.tier == MibPlanResolutionTier::StalePin;
        conflictReported |= finding.tier == MibPlanResolutionTier::NewestRevision;
    }
    ok &= check(staleReported && conflictReported && fileProviding(staleConflict, "REV-MIB") &&
                fileProviding(staleConflict, "REV-MIB")->canonicalPath == newRevision,
                "a stale pin and the fallback provider conflict are both reported");
    // Equal dates, invalid dates and absent dates all have a deterministic path fallback.
    for (const QByteArray &date : {QByteArray("9901010000Z"), QByteArray("200299990000Z"), QByteArray()}) {
        write(sibling, "z-new.mib", dated(date));
        index.update({batch, standards, outside});
        const auto fallback = resolve("revision-fallback", {oldRevision, newRevision}, {batch, standards});
        const auto *chosen = fileProviding(fallback, "REV-MIB");
        ok &= check(chosen && chosen->canonicalPath == oldRevision,
                    "equal or missing revisions fall back to path");
        const auto *tie = diagnostic(fallback, "REV-MIB");
        // (The first variant is byte-identical to the other copy, so there is nothing to resolve.)
        ok &= check(date == QByteArray("9901010000Z") ||
                    (tie && tie->tier == MibPlanResolutionTier::Ambiguous && !fallback.resolvedByRevision("REV-MIB") &&
                     fallback.ambiguousModules.contains("REV-MIB")),
                    "when revisions cannot decide, the conflict stays unresolved");
    }

    // Copies that differ only by leading/trailing whitespace or line endings are not a conflict;
    // a real content difference still is.
    {
        const QByteArray base = dated("200501010000Z");
        QByteArray padded = "  \t";
        for (const QByteArray &line : base.split('\n')) padded += "   " + line + "  \r\n";
        write(sibling, "a-old.mib", base);
        write(sibling, "z-new.mib", padded + "\r\n\r\n");
        index.update({batch, standards, outside});
        const auto spaced = resolve("whitespace-only", {oldRevision, newRevision}, {batch, standards});
        ok &= check(!diagnostic(spaced, "REV-MIB") && !spaced.ambiguousModules.contains("REV-MIB"),
                    "copies differing only by edge whitespace are not a conflict");
        QByteArray changed = padded; changed.replace("MODULE-IDENTITY", "MODULE-IDENTITY ");
        changed.replace("LAST-UPDATED", "LAST-UPDATED-X");
        write(sibling, "z-new.mib", changed);
        index.update({batch, standards, outside});
        const auto different = resolve("whitespace-plus-content", {oldRevision, newRevision}, {batch, standards});
        ok &= check(diagnostic(different, "REV-MIB") != nullptr,
                    "a real content difference is still reported");
        // Comments and reflowed whitespace are not a difference either.
        QByteArray commented = "-- vendor banner ----------------\n";
        for (const QByteArray &line : base.split('\n')) commented += "\t" + line + "   -- note -- \n";
        commented.replace(" ", "  ");
        write(sibling, "z-new.mib", commented);
        index.update({batch, standards, outside});
        const auto reflowed = resolve("comments-and-whitespace", {oldRevision, newRevision}, {batch, standards});
        ok &= check(!diagnostic(reflowed, "REV-MIB") && !reflowed.ambiguousModules.contains("REV-MIB"),
                    "copies differing only by comments and whitespace are not a conflict");
    }

    // The normalization rules themselves.
    {
        const auto n = [](const char *text) { return MibNormalizedContent(QByteArray(text)); };
        ok &= check(n("A  B\t\r\n C\n\n") == n("A B C"), "whitespace runs and line endings collapse");
        ok &= check(n("\xEF\xBB\xBF" "A B") == n("A B"), "a byte-order mark is ignored");
        ok &= check(n("A -- c -- B") == n("A B") && n("A -- to end of line\nB") == n("A B"),
                    "comments end at the next -- or the end of the line");
        ok &= check(n("A ---------- banner\nB") == n("A B"), "banner lines are comments");
        ok &= check(n("A -- it's \"quoted\" in a comment\nB") == n("A B"), "quotes inside comments do not open strings");
        ok &= check(n("D \"keep -- this\" E") != n("D \"keep\" E"), "-- inside a string is text, not a comment");
        ok &= check(n("D \"a\n   b  \" E") == n("D \"a\nb\" E"), "string lines lose only their edge whitespace");
        ok &= check(n("D \"a  b\" E") != n("D \"a b\" E"), "spaces inside a line of a string are significant");
        ok &= check(n("D \"unclosed -- x\nE") != n("D \"unclosed\nE"), "an unclosed string stays verbatim, so copies look different");
        ok &= check(n("A B") != n("A C"), "different content is different");
    }

    // Each declaration has its own date; fake clauses in comments/descriptions do not count.
    write(sibling, "a-old.mib", dated("200501010000Z"));
    QByteArray other = dated("209901010000Z"); other.replace("REV-MIB", "OTHER-REV-MIB");
    write(sibling, "z-new.mib", other + "-- LAST-UPDATED \"209912310000Z\"\n" + dated("200205090000Z"));
    index.update({batch, standards, outside});
    const auto separateDates = resolve("per-module-revisions", {revisionRoot}, {batch, standards});
    const auto *perModule = fileProviding(separateDates, "REV-MIB");
    ok &= check(perModule && perModule->canonicalPath == oldRevision,
                "another module and quoted descriptions cannot supply the revision for this module");

    write(sibling, "z-new.mib", other + dated("200205090000Z") + "-- changed losing provider\n");
    index.update({batch, standards, outside});
    const auto changedAlternative = resolve("per-module-revisions", {revisionRoot}, {batch, standards});
    ok &= check(fileProviding(changedAlternative, "REV-MIB") &&
                fileProviding(changedAlternative, "REV-MIB")->canonicalPath == oldRevision &&
                changedAlternative.sha256 != separateDates.sha256,
                "a changed losing provider invalidates the cached conflict report");
    QFile::remove(newRevision);
    index.update({batch, standards, outside});
    const auto noConflict = resolve("per-module-revisions", {revisionRoot}, {batch, standards});
    ok &= check(!diagnostic(noConflict, "REV-MIB") && noConflict.sha256 != changedAlternative.sha256,
                "removing a losing provider invalidates the cached conflict warning");

    QTemporaryDir stageCache;
    const auto stage = MibRuntimeStage::prepare(transitive, stageCache.path());
    ok &= check(stage.success && QDir(stage.paths.orderedPaths().first())
                    .entryList(QDir::Files | QDir::NoDotAndDotDot).size() ==
                    transitive.effectiveModules.size(),
                "resolver-generated Plan remains directly stage-compatible");

    return ok ? 0 : 1;
}
