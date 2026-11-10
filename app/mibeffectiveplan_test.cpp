#include "mibeffectiveplan.h"
#include "mibenvironmentextractor.h"
#include "mibruntimeparser.h"
#include "mibruntimestage.h"
#include "smi.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QTemporaryDir>
#include <QSet>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <utility>

namespace {
bool check(bool value, const char *message) { if (!value) std::cerr << "FAIL: " << message << '\n'; return value; }
bool writeFile(const QString &path, const QByteArray &data) { QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(data) == data.size(); }
QString canonical(const QString &path) {
    const QString result = QFileInfo(path).canonicalFilePath();
    return QDir::fromNativeSeparators(result.isEmpty() ? QFileInfo(path).absoluteFilePath() : result);
}
QByteArray mib(const QString &identity, const QStringList &imports = {}) {
    QByteArray data = identity.toUtf8() + " DEFINITIONS ::= BEGIN\n";
    if (!imports.isEmpty()) data += "IMPORTS value FROM " + imports.join("\n other FROM ").toUtf8() + ";\n";
    return data + "END\n";
}
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    if (application.arguments().value(1) == QStringLiteral("--voss-migrate")) {
        const QString libraryRoot = application.arguments().value(2);
        const QString profilePath = application.arguments().value(3);
        const QString profileId = application.arguments().value(4);
        const QString indexPath = application.arguments().value(5);
        const QString stagePath = application.arguments().value(6);
        const QString pinPath = canonical(application.arguments().value(7));
        QString error;
        MibDependencyIndex index(indexPath, libraryRoot);
        index.update({libraryRoot}, &error);
        MibProfileRepository repository(profilePath);
        QList<MibProfileRecord> records = repository.load(&error);
        auto seed = std::find_if(records.begin(), records.end(), [&profileId](const auto &profile) {
            return profile.id == profileId;
        });
        const auto providers = index.providersFor(QStringLiteral("INET-ADDRESS-MIB"));
        const auto pin = std::find_if(providers.cbegin(), providers.cend(), [&pinPath](const auto &provider) {
            return canonical(provider.canonicalPath) == pinPath;
        });
        if (!error.isEmpty() || seed == records.end() || pin == providers.cend()) return 2;
        seed->providerPins.insert(QStringLiteral("INET-ADDRESS-MIB"),
            {pin->canonicalPath, pin->sha256, QStringLiteral("Explicit VOSS migration decision")});
        if (!repository.save(records, &error)) return 3;
        MibProfileService service(repository);
        if (!service.migrateLegacyProfiles(index, &error)) return 4;
        const MibProfileRecord *profile = service.find(profileId);
        if (!profile) return 5;
        MibEffectivePlanResolverInput input;
        input.id = profile->id;
        input.roots = profile->roots;
        input.orderedScopes = MibProfileScopePaths(*profile);
        input.pins = profile->providerPins;
        MibEffectivePlan plan = MibEffectiveRuntimePlanResolver().resolve(input, index);
        int roots = 0, dependencies = 0;
        for (const auto &file : plan.runtimeFiles)
            file.origin == MibEffectivePlanFileOrigin::Root ? ++roots : ++dependencies;
        const auto stage = plan.isComplete() ? MibRuntimeStage::prepare(plan, stagePath)
                                             : MibRuntimeStageResult{};
        MibEnvironmentPtr environment;
        int failedLoads = 0;
        if (stage.success) {
            plan.runtimeConfiguration = stage.configuration;
            plan.runtimePaths = stage.paths;
            plan.hasRuntimePaths = true;
            smiInit("voss-migration-test");
            const auto reset = MibRuntimeParser::reset(plan.runtimePaths);
            const auto loads = reset.success
                ? MibRuntimeParser::loadExplicitRoots(plan.runtimeConfiguration, plan.runtimePaths)
                : MibExplicitRootLoadBatch{};
            failedLoads = loads.failedIdentities().size();
            environment = MibEnvironmentExtractor().extract(plan, loads.failedIdentities(), loads.roots,
                reset.success ? QStringList{} : QStringList{reset.error});
        }
        std::cout << "legacy_members=" << seed->members.size()
                  << " manifest=" << MibProfileIsManifest(*profile)
                  << " migrated_roots=" << profile->roots.size()
                  << " scopes=" << MibProfileScopePaths(*profile).join(';').toStdString()
                  << " pins=" << profile->providerPins.size()
                  << " unresolved_legacy=" << profile->unresolvedLegacyModules.size()
                  << " plan_roots=" << roots << " plan_dependencies=" << dependencies
                  << " plan_total=" << plan.runtimeFiles.size()
                  << " ambiguities=" << plan.ambiguousModules.size()
                  << " unresolved=" << plan.missingModules.size()
                  << " stage=" << stage.success << " failed_loads=" << failedLoads
                  << " parser_modules=" << (environment ? environment->loadedCount() : 0)
                  << " unauthorized=" << (environment && environment->providersAuthorized() ? 0 : 1)
                  << " nodes=" << (environment ? environment->nodes().size() : 0)
                  << " types=" << (environment ? environment->types().size() : 0)
                  << " publishable=" << (environment && environment->publishable()) << '\n';
        if (environment) smiExit();
        return environment && profile->roots.size() == 42 && roots == 42 && dependencies == 31 &&
            plan.runtimeFiles.size() == 73 && environment->loadedCount() == 73 &&
            environment->providersAuthorized() && environment->publishable() ? 0 : 6;
    }
    if (application.arguments().value(1) == QStringLiteral("--corpus")) {
        MibDependencyIndex corpus(application.arguments().value(2));
        QString error;
        if (!corpus.load(&error)) { std::cerr << error.toStdString() << '\n'; return 2; }
        int single = 0, identical = 0, conflicting = 0;
        QSet<QString> conflictingIdentities;
        for (const QString &identity : corpus.moduleNames()) {
            const auto providers = corpus.providersFor(identity);
            if (providers.size() <= 1) { ++single; continue; }
            QSet<QString> hashes; for (const auto &provider : providers) hashes.insert(provider.sha256);
            if (hashes.size() == 1 && !hashes.contains(QString())) ++identical;
            else { ++conflicting; conflictingIdentities.insert(identity); }
        }
        QList<MibProfileRecord> profiles = MibProfileDefinitions::builtIns();
        profiles.append(MibProfileRepository(application.arguments().value(3)).load(&error));
        int resolvedConflicts = 0, largest = 0, largestPasses = 0;
        qint64 slowestMsecs = 0; QString largestName;
        QSet<QString> unresolved;
        for (const auto &profile : profiles) {
            QElapsedTimer timer; timer.start();
            const auto plan = MibEffectivePlanResolver().resolve(profile, corpus);
            const qint64 elapsed = timer.elapsed();
            if (plan.effectiveModules.size() > largest) {
                largest = plan.effectiveModules.size(); largestName = profile.name;
                largestPasses = plan.convergencePasses;
            }
            slowestMsecs = (std::max)(slowestMsecs, elapsed);
            for (const QString &identity : conflictingIdentities) {
                const auto *member = plan.member(identity);
                if (!member) continue;
                if (member->provider.canonicalPath.isEmpty()) unresolved.insert(identity);
                else ++resolvedConflicts;
            }
        }
        std::cout << "corpus_identities=" << corpus.moduleNames().size() << " single=" << single
                  << " identical_duplicates=" << identical << " differing_conflicts=" << conflicting
                  << " plan_resolved_conflict_instances=" << resolvedConflicts
                  << " genuinely_unresolved_identities=" << unresolved.size() << '\n'
                  << "profiles=" << profiles.size() << " largest_profile=\"" << largestName.toStdString()
                  << "\" largest_modules=" << largest << " convergence_passes=" << largestPasses
                  << " slowest_plan_ms=" << slowestMsecs << '\n';
        return 0;
    }
    if (application.arguments().value(1) == QStringLiteral("--materialize-profile")) {
        const QString libraryRoot = application.arguments().value(2);
        MibDependencyIndex index(application.arguments().value(5), libraryRoot);
        QString error;
        index.update({libraryRoot}, &error);
        if (!error.isEmpty()) { std::cerr << error.toStdString() << '\n'; return 2; }
        const auto profiles = MibProfileRepository(application.arguments().value(3)).load(&error);
        const QString profileId = application.arguments().value(4);
        const auto found = std::find_if(profiles.cbegin(), profiles.cend(), [&profileId](const auto &item) {
            return item.id == profileId;
        });
        if (found == profiles.cend()) { std::cerr << "profile not found\n"; return 3; }
        MibEffectivePlan plan = MibEffectivePlanResolver().resolve(*found, index);
        plan.runtimeConfiguration = MibProfileRuntimeConfigurationBuilder().build(*found, index, {});
        plan.runtimePaths = MibRuntimePathConfigurationBuilder().derive(plan.runtimeConfiguration, index);
        plan.hasRuntimePaths = true;
        MibEffectivePlanResolver::sealRuntimeAuthority(&plan);
        const auto diagnosticResolution = MibResolveProfileDependencies(*found, index);
        const auto stage = MibRuntimeStage::prepare(plan, application.arguments().value(6));
        std::cout << "added_before=" << found->members.size()
                  << " plan_files=" << plan.runtimeFiles.size()
                  << " catalog_candidates_not_added=" << diagnosticResolution.addedDependencies.size()
                  << " unresolved=" << diagnosticResolution.unresolved.size()
                  << " stage_success=" << stage.success << " stage_reused=" << stage.reused << '\n';
        for (const QString &identity : {QStringLiteral("ATM-TC-MIB"), QStringLiteral("BRIDGE-MIB"),
                                       QStringLiteral("EXTREME-BASE-MIB"), QStringLiteral("UDP-MIB")}) {
            for (const auto &member : found->members)
                if (member.identities.contains(identity))
                    std::cout << identity.toStdString() << '=' << canonical(member.canonicalPath).toStdString() << '\n';
        }
        if (!diagnosticResolution.unresolved.isEmpty())
            std::cout << "unresolved_identities="
                      << diagnosticResolution.unresolved.join(',').toStdString() << '\n';
        if (!stage.success) { std::cerr << stage.error.toStdString() << '\n'; return 4; }
        plan.runtimeConfiguration = stage.configuration;
        plan.runtimePaths = stage.paths; plan.hasRuntimePaths = true;
        const auto reset = MibRuntimeParser::reset(plan.runtimePaths);
        const auto loads = reset.success
            ? MibRuntimeParser::loadExplicitRoots(plan.runtimeConfiguration, plan.runtimePaths)
            : MibExplicitRootLoadBatch{};
        QStringList unavailable = loads.failedIdentities() + plan.missingModules +
            plan.ambiguousModules + plan.pinFailureModules;
        unavailable.removeDuplicates();
        const auto environment = MibEnvironmentExtractor().extract(plan, unavailable, loads.roots);
        std::cout << "runtime_paths=" << plan.runtimePaths.orderedPaths().join(';').toStdString()
                  << " parser_modules=" << environment->loadedCount()
                  << " providers_authorized=" << environment->providersAuthorized()
                  << " publishable=" << environment->publishable()
                  << " nodes=" << environment->nodes().size() << '\n';
        if (!environment->publishable())
            std::cout << "diagnostics=" << environment->constructionDiagnostics().join(" | ").toStdString() << '\n';
        return environment->publishable() ? 0 : 5;
    }
    bool ok = true; QTemporaryDir temp;
    const QString standards = QDir(temp.path()).filePath("Standards/IETF");
    const QString productA = QDir(temp.path()).filePath("Vendor/Product A");
    const QString productB = QDir(temp.path()).filePath("Vendor/Product B");
    for (const QString &path : {standards, productA, productB}) QDir().mkpath(path);
    writeFile(QDir(standards).filePath("dep.mib"), mib("DEPENDENCY-MIB"));
    writeFile(QDir(standards).filePath("global-root.mib"), mib("ROOT-MIB", {"DEPENDENCY-MIB"}));
    writeFile(QDir(productA).filePath("odd-name.mib"), mib("ROOT-MIB", {"PRODUCT-ONLY-MIB"}));
    writeFile(QDir(productA).filePath("multi.mib"), mib("PRODUCT-ONLY-MIB", {"CYCLE-A"}) + mib("SECOND-IDENTITY-MIB"));
    writeFile(QDir(productA).filePath("cycle-a.mib"), mib("CYCLE-A", {"CYCLE-B"}));
    writeFile(QDir(productA).filePath("cycle-b.mib"), mib("CYCLE-B", {"CYCLE-A"}));
    writeFile(QDir(productA).filePath("missing.mib"), mib("MISSING-USER-MIB", {"ABSENT-MIB"}));
    const QByteArray equivalent = mib("EQUIVALENT-MIB");
    writeFile(QDir(productA).filePath("equivalent-a.mib"), equivalent);
    writeFile(QDir(productA).filePath("equivalent-b.mib"), equivalent);
    writeFile(QDir(productA).filePath("conflict-a.mib"), mib("CONFLICT-MIB") + "A-VARIANT ::= 1\n");
    writeFile(QDir(productA).filePath("conflict-b.mib"), mib("CONFLICT-MIB") + "B-VARIANT ::= 1\n");
    writeFile(QDir(productB).filePath("same-identity.mib"), mib("ROOT-MIB", {"DEPENDENCY-MIB"}));
    writeFile(QDir(productA).filePath("TEST-MIB"), mib("TEST-MIB") + "provider-A-VARIANT ::= 1\n");
    writeFile(QDir(productB).filePath("TEST-MIB"), mib("TEST-MIB") + "provider-B-VARIANT ::= 1\n");
    MibDependencyIndex index(QDir(temp.path()).filePath("index.json")); index.update({standards, productA, productB});

    // Phase F authority guard: only exact physical members reach runtime
    // configuration. Collection order, Profile type, directory, and identity-only
    // fields cannot broaden that authority.
    MibProfileRecord exact;
    exact.id = "exact"; exact.name = "Exact"; exact.type = MibProfileType::Folder;
    exact.directory = productB; exact.includeStandardBase = true;
    exact.explicitModules = {"CONFLICT-MIB", "DEPENDENCY-MIB"};
    exact.members = MibProfileMembersFromFiles({
        QDir(productA).filePath("odd-name.mib"),
        QDir(productA).filePath("multi.mib")});
    const MibProfileRuntimeConfigurationBuilder exactBuilder;
    const auto phaseFRuntime = exactBuilder.build(exact, index, {});
    const auto phaseFPaths = MibRuntimePathConfigurationBuilder().derive(phaseFRuntime, index);
    ok &= check(phaseFRuntime.explicitRoots().contains("ROOT-MIB") &&
                phaseFRuntime.explicitRoots().contains("SECOND-IDENTITY-MIB") &&
                !phaseFRuntime.explicitRoots().contains("CONFLICT-MIB") &&
                phaseFRuntime.rootAliases().value("ROOT-MIB").canonicalPath.endsWith("odd-name.mib"),
                "runtime authority comes only from exact Profile members");
    ok &= check(phaseFPaths.isValid() && phaseFPaths.orderedPaths() ==
                    QStringList{canonical(productA)},
                "runtime paths contain only exact-member parent directories");
    MibEffectivePlan phaseFPlan = MibEffectivePlanResolver().resolve(exact, index);
    ok &= check(phaseFPlan.member("ROOT-MIB") &&
                phaseFPlan.member("ROOT-MIB")->provider.canonicalPath.endsWith("odd-name.mib") &&
                !phaseFPlan.member("CONFLICT-MIB"),
                "Effective Plan cannot select outside exact membership");
    phaseFPlan.runtimeConfiguration = phaseFRuntime;
    phaseFPlan.runtimePaths = phaseFPaths;
    phaseFPlan.hasRuntimePaths = true;
    MibEffectivePlanResolver::sealRuntimeAuthority(&phaseFPlan);
    const auto oddPlanFile = std::find_if(phaseFPlan.runtimeFiles.cbegin(), phaseFPlan.runtimeFiles.cend(),
        [&exact](const auto &file) { return file.canonicalPath == canonical(exact.members.first().canonicalPath); });
    ok &= check(phaseFPlan.runtimeFiles.size() == exact.members.size() &&
                oddPlanFile != phaseFPlan.runtimeFiles.cend() &&
                !oddPlanFile->sha256.isEmpty() &&
                !oddPlanFile->identities.isEmpty() &&
                !oddPlanFile->aliases.isEmpty() &&
                !phaseFPlan.runtimeAuthoritySha256.isEmpty(),
                "Effective Plan carries the complete exact runtime file authority");
    QTemporaryDir phaseFStageCache;
    const auto phaseFStage = MibRuntimeStage::prepare(phaseFPlan, phaseFStageCache.path());
    const QStringList phaseFArtifacts = QDir(phaseFStage.paths.orderedPaths().value(0))
        .entryList(QDir::Files | QDir::NoDotAndDotDot);
    ok &= check(phaseFStage.success &&
                QDir(phaseFStage.directory).dirName() == phaseFPlan.runtimeAuthoritySha256 &&
                phaseFStage.configuration.authorizedFiles().size() == exact.members.size() &&
                QSet<QString>(phaseFArtifacts.cbegin(), phaseFArtifacts.cend()) ==
                    QSet<QString>(phaseFRuntime.explicitRoots().cbegin(),
                                  phaseFRuntime.explicitRoots().cend()),
                "runtime stage is keyed by and consumes sealed Effective Plan authority");
    MibProfileRecord renamedExact = exact;
    renamedExact.name = "Cosmetic rename";
    MibEffectivePlan renamedPlan = MibEffectivePlanResolver().resolve(renamedExact, index);
    renamedPlan.runtimeConfiguration = exactBuilder.build(renamedExact, index, {});
    renamedPlan.runtimePaths = MibRuntimePathConfigurationBuilder().derive(
        renamedPlan.runtimeConfiguration, index);
    renamedPlan.hasRuntimePaths = true;
    MibEffectivePlanResolver::sealRuntimeAuthority(&renamedPlan);
    ok &= check(renamedPlan.runtimeAuthoritySha256 == phaseFPlan.runtimeAuthoritySha256 &&
                renamedPlan.sha256 == phaseFPlan.sha256,
                "cosmetic Profile name changes do not invalidate runtime authority or Plan identity");
    MibProfileRecord changedContentAuthority = exact;
    changedContentAuthority.members.first().sha256 = QString(64, 'a');
    MibEffectivePlan changedContentPlan = MibEffectivePlanResolver().resolve(
        changedContentAuthority, index);
    changedContentPlan.runtimeConfiguration = exactBuilder.build(changedContentAuthority, index, {});
    changedContentPlan.runtimePaths = MibRuntimePathConfigurationBuilder().derive(
        changedContentPlan.runtimeConfiguration, index);
    changedContentPlan.hasRuntimePaths = true;
    MibEffectivePlanResolver::sealRuntimeAuthority(&changedContentPlan);
    ok &= check(changedContentPlan.runtimeAuthoritySha256 != phaseFPlan.runtimeAuthoritySha256,
                "exact provider content hashes participate in Plan authority identity");
    MibProfileRecord dependencyMarked = exact;
    dependencyMarked.members.last().reason = MibProfileMemberReason::Dependency;
    const auto dependencyMarkedPlan = MibEffectivePlanResolver().resolve(dependencyMarked, index);
    ok &= check(std::any_of(dependencyMarkedPlan.runtimeFiles.cbegin(),
                    dependencyMarkedPlan.runtimeFiles.cend(), [](const auto &file) {
                        return file.origin == MibEffectivePlanFileOrigin::Dependency;
                    }),
                "existing deterministic Profile metadata marks dependency-origin Plan files");
    MibProfileRecord admissionProfile;
    admissionProfile.id = "plan-admission"; admissionProfile.name = "Plan admission";
    admissionProfile.type = MibProfileType::Custom;
    admissionProfile.members = MibProfileMembersFromFiles({QDir(productA).filePath("TEST-MIB")});
    MibEffectivePlan admissionPlan = MibEffectivePlanResolver().resolve(admissionProfile, index);
    admissionPlan.runtimeConfiguration = exactBuilder.build(admissionProfile, index, {});
    admissionPlan.runtimePaths = MibRuntimePathConfigurationBuilder().derive(
        admissionPlan.runtimeConfiguration, index);
    admissionPlan.hasRuntimePaths = true;
    MibEffectivePlanResolver::sealRuntimeAuthority(&admissionPlan);
    QTemporaryDir admissionStageCache;
    const auto admissionStage = MibRuntimeStage::prepare(admissionPlan, admissionStageCache.path());
    smiInit("effective-runtime-plan-test");
    MibEffectivePlan stagedAdmissionPlan = admissionPlan;
    stagedAdmissionPlan.runtimeConfiguration = admissionStage.configuration;
    stagedAdmissionPlan.runtimePaths = admissionStage.paths;
    const auto admissionReset = MibRuntimeParser::reset(stagedAdmissionPlan.runtimePaths);
    const auto admissionLoads = admissionReset.success
        ? MibRuntimeParser::loadExplicitRoots(stagedAdmissionPlan.runtimeConfiguration,
                                              stagedAdmissionPlan.runtimePaths)
        : MibExplicitRootLoadBatch{};
    const auto admitted = MibEnvironmentExtractor().extract(
        stagedAdmissionPlan, admissionLoads.failedIdentities(), admissionLoads.roots);
    MibEffectivePlan excludedPlan = stagedAdmissionPlan;
    excludedPlan.runtimeFiles.clear();
    const auto excluded = MibEnvironmentExtractor().extract(
        excludedPlan, admissionLoads.failedIdentities(), admissionLoads.roots);
    ok &= check(admissionStage.success && admissionReset.success && admitted->publishable() &&
                canonical(admitted->loadedProviderPaths().value("TEST-MIB")) ==
                    canonical(QDir(productA).filePath("TEST-MIB")) &&
                !excluded->publishable() && !excluded->providersAuthorized(),
                "post-load admission restores provenance and authorizes only Plan files");
    smiExit();
    MibProfileRecord legacy = exact; legacy.members.clear(); legacy.unresolvedLegacyModules.clear();
    const auto rejected = MibEffectivePlanResolver().resolve(legacy, index);
    ok &= check(!rejected.authorityError.isEmpty() && rejected.members.isEmpty(),
                "identity-only Profile authority is rejected");
    MibProfileRecord altered = exact; altered.type = MibProfileType::Custom;
    altered.directory = standards; altered.includeStandardBase = false;
    altered.explicitModules = {"ABSENT-MIB"};
    ok &= check(exactBuilder.build(altered, index, {}).sha256() == phaseFRuntime.sha256(),
                "legacy type, directory, and identity fields do not affect exact runtime authority");
    return ok ? 0 : 1;

    const MibRuntimeCollectionReference standardsCollection{
        "standards", MibRuntimeCollectionRole::Standards, standards, false};
    const MibRuntimeCollectionReference productCollection{
        "vendor/product-a", MibRuntimeCollectionRole::Product, productA, false};
    const MibRuntimeCollectionReference pibCollection{
        "vendor/pibs", MibRuntimeCollectionRole::Pib, productB, true};
    const MibProfileRuntimeConfigurationBuilder runtimeBuilder;

    MibProfileRecord automatic; automatic.id = "automatic-a"; automatic.name = "Vendor Product A";
    automatic.type = MibProfileType::Folder; automatic.directory = productA;
    automatic.explicitModules = {"SECOND-IDENTITY-MIB", "ROOT-MIB", "MISSING-USER-MIB", "EQUIVALENT-MIB", "CONFLICT-MIB"};
    MibProfileRecord automaticRuntimeProfile = automatic;
    automaticRuntimeProfile.includeStandardBase = true;
    const auto automaticRuntime = runtimeBuilder.build(
        automaticRuntimeProfile, index, {productCollection, standardsCollection, pibCollection});
    const auto repeatedRuntime = runtimeBuilder.build(
        automaticRuntimeProfile, index, {productCollection, standardsCollection, pibCollection});
    const MibRuntimePathConfigurationBuilder pathBuilder;
    const auto automaticPaths = pathBuilder.derive(automaticRuntime, index);
    const auto repeatedPaths = pathBuilder.derive(repeatedRuntime, index);
    ok &= check(automaticRuntime.profileId() == automatic.id &&
                automaticRuntime.profileType() == MibProfileType::Folder &&
                automaticRuntime.explicitRoots().contains("ROOT-MIB") &&
                automaticRuntime.orderedCollections().first().id == productCollection.id &&
                automaticRuntime.standardsPolicy() == MibRuntimeStandardsPolicy::Fallback,
                "automatic Profile maps to product-first collections, roots, and Standards fallback");
    ok &= check(automaticRuntime.sha256() == repeatedRuntime.sha256() &&
                MibProfileRuntimeConfigurationBuilder::canonicalBytes(automaticRuntime) ==
                    MibProfileRuntimeConfigurationBuilder::canonicalBytes(repeatedRuntime),
                "equivalent runtime configurations are byte-stable");
    ok &= check(automaticRuntime.rootAliases().value("ROOT-MIB").canonicalPath.endsWith("odd-name.mib") &&
                automaticRuntime.rootAliases().value("ROOT-MIB").collectionId == productCollection.id,
                "filename-to-identity mismatch is represented for an explicit root");
    ok &= check(automaticRuntime.rootAliases().value("SECOND-IDENTITY-MIB").canonicalPath.endsWith("multi.mib") &&
                automaticRuntime.rootAliases().value("SECOND-IDENTITY-MIB").identity == "SECOND-IDENTITY-MIB",
                "multi-identity file alias is represented for an explicit root");
    ok &= check(automaticRuntime.orderedCollections().last().includesPibs &&
                automaticRuntime.orderedCollections().last().role == MibRuntimeCollectionRole::Pib,
                "PIB participation is explicit in the collection contract");
    ok &= check(automaticPaths.isValid() && automaticPaths.orderedPaths().first() ==
                    QFileInfo(productA).canonicalFilePath() &&
                automaticPaths.entries().first().collectionId == productCollection.id &&
                automaticPaths.orderedPaths().contains(QFileInfo(standards).canonicalFilePath()),
                "Automatic Profile derives authorized product-first runtime paths");
    ok &= check(automaticPaths.orderedPaths() == repeatedPaths.orderedPaths() &&
                automaticPaths.sha256() == repeatedPaths.sha256(),
                "equivalent runtime path derivation is stable");
    const auto reversedRuntime = runtimeBuilder.build(
        automaticRuntimeProfile, index, {standardsCollection, productCollection, pibCollection});
    const auto reversedPaths = pathBuilder.derive(reversedRuntime, index);
    ok &= check(reversedRuntime.orderedCollections().first().id == standardsCollection.id &&
                reversedRuntime.sha256() != automaticRuntime.sha256(),
                "collection precedence order is preserved and hash-sensitive");
    ok &= check(reversedPaths.orderedPaths().first() == QFileInfo(standards).canonicalFilePath() &&
                reversedPaths.orderedPaths() != automaticPaths.orderedPaths(),
                "runtime path derivation preserves Standards-before-product precedence");
    MibProfileRecord changedRoots = automaticRuntimeProfile;
    changedRoots.explicitModules.removeAll("ROOT-MIB");
    ok &= check(runtimeBuilder.build(changedRoots, index,
                    {productCollection, standardsCollection, pibCollection}).sha256() != automaticRuntime.sha256(),
                "runtime hash changes with explicit roots");
    MibDependencyIndex newerIndex(QDir(temp.path()).filePath("newer-index.json"));
    newerIndex.update({standards, productA, productB});
    writeFile(QDir(productB).filePath("generation-only.mib"), mib("GENERATION-ONLY-MIB"));
    newerIndex.update({standards, productA, productB});
    ok &= check(runtimeBuilder.build(automaticRuntimeProfile, newerIndex,
                    {productCollection, standardsCollection, pibCollection}).sha256() != automaticRuntime.sha256(),
                "runtime hash changes with Library generation");
    const auto first = MibEffectivePlanResolver().resolve(automatic, index);
    const auto second = MibEffectivePlanResolver().resolve(automatic, index);
    ok &= check(first.sha256 == second.sha256 && MibEffectivePlanResolver::canonicalBytes(first) == MibEffectivePlanResolver::canonicalBytes(second), "same inputs are byte-identical");
    ok &= check(first.explicitModules == QStringList({"CONFLICT-MIB", "EQUIVALENT-MIB", "MISSING-USER-MIB", "ROOT-MIB", "SECOND-IDENTITY-MIB"}), "explicit identity canonicalization");
    ok &= check(first.member("ROOT-MIB") && first.member("ROOT-MIB")->provider.canonicalPath.contains("Product A") && first.member("ROOT-MIB")->providerReason == MibPlanProviderReason::AutomaticProfileFolder, "automatic product provider policy");
    ok &= check(first.member("ROOT-MIB")->imports == QStringList{"PRODUCT-ONLY-MIB"} && first.dependencyModules.contains("PRODUCT-ONLY-MIB") && !first.dependencyModules.contains("DEPENDENCY-MIB"), "provider-specific closure");
    ok &= check(first.member("PRODUCT-ONLY-MIB")->membershipReason == MibPlanMembershipReason::Dependency && first.member("ROOT-MIB")->membershipReason == MibPlanMembershipReason::Explicit, "membership reasons");
    ok &= check(first.missingModules == QStringList{"ABSENT-MIB"} && first.ambiguousModules == QStringList{"CONFLICT-MIB"}, "missing and ambiguous findings");
    ok &= check(!first.cycles.isEmpty() && first.initialLoadOrder.contains("CYCLE-A") && first.initialLoadOrder.contains("CYCLE-B"), "cycle termination");
    const MibProfileRecord standardsProfile = MibProfileDefinitions::builtIns().first();
    const auto standardsRuntime = runtimeBuilder.build(standardsProfile, index, {standardsCollection});
    const auto standardsPaths = pathBuilder.derive(standardsRuntime, index);
    QStringList expectedStandardRoots = MibProfileDefinitions::standardsModules();
    expectedStandardRoots.sort(Qt::CaseSensitive);
    ok &= check(standardsRuntime.explicitRoots() == expectedStandardRoots &&
                standardsRuntime.explicitRoots().size() == 14 &&
                standardsRuntime.orderedCollections().size() == 1 &&
                standardsRuntime.orderedCollections().first().role == MibRuntimeCollectionRole::Standards,
                "built-in Standards Profile keeps its small root set distinct from its collection");
    ok &= check(standardsPaths.isValid() && standardsPaths.entries().size() == 1 &&
                standardsPaths.entries().first().collectionRole == MibRuntimeCollectionRole::Standards,
                "built-in Standards runtime paths contain only the Standards collection");
    QMap<QString, int> attempts;
    const auto retried = MibBoundedDependencyLoader().load(first,
        [&attempts](const QString &, const QString &identity) {
            MibDependencyLoadAttempt attempt; const int count = ++attempts[identity];
            if (identity != "ROOT-MIB" || count > 1) {
                attempt.success = true; attempt.loadedModuleNames = {identity};
            }
            return attempt;
        });
    ok &= check(attempts.value("ROOT-MIB") == 2 && retried.passes == 2 &&
                retried.failures.size() == first.missingModules.size() + first.ambiguousModules.size(),
                "effective plan materialization retains bounded fixed-point retry");

    MibProfileRecord custom = automatic; custom.id = "custom"; custom.type = MibProfileType::Custom;
    custom.directory = productA; custom.explicitModules = {"ROOT-MIB"}; custom.includeStandardBase = true;
    const auto customPlan = MibEffectivePlanResolver().resolve(custom, index);
    const auto customRuntime = runtimeBuilder.build(
        custom, index, {productCollection, standardsCollection});
    const auto customPaths = pathBuilder.derive(customRuntime, index);
    ok &= check(customRuntime.explicitRoots() == QStringList{"ROOT-MIB"} &&
                customRuntime.orderedCollections().size() == 2 &&
                customRuntime.standardsPolicy() == MibRuntimeStandardsPolicy::Fallback,
                "Custom Profile preserves explicit roots and caller-supplied collection order");
    ok &= check(customPaths.isValid() && customPaths.orderedPaths().first() ==
                    QFileInfo(productA).canonicalFilePath(),
                "Custom Profile derives paths without expanding explicit roots");

    const auto missingStandardsRuntime = runtimeBuilder.build(custom, index, {productCollection});
    const auto missingStandardsPaths = pathBuilder.derive(missingStandardsRuntime, index);
    ok &= check(!missingStandardsPaths.isValid() &&
                missingStandardsPaths.diagnostics().join('\n').contains("Standards fallback"),
                "Standards fallback without a Standards collection fails validation");

    const MibRuntimeCollectionReference inactivePib{
        "inactive-pib", MibRuntimeCollectionRole::Pib, productB, false};
    MibProfileRecord noStandards = custom; noStandards.includeStandardBase = false;
    const auto inactivePibRuntime = runtimeBuilder.build(
        noStandards, index, {productCollection, inactivePib});
    const auto inactivePibPaths = pathBuilder.derive(inactivePibRuntime, index);
    ok &= check(inactivePibPaths.isValid() &&
                !inactivePibPaths.orderedPaths().contains(QFileInfo(productB).canonicalFilePath()),
                "inactive PIB collection does not leak into runtime paths");
    const auto activePibRuntime = runtimeBuilder.build(
        noStandards, index, {productCollection, pibCollection});
    const auto activePibPaths = pathBuilder.derive(activePibRuntime, index);
    ok &= check(activePibPaths.isValid() && activePibPaths.orderedPaths().contains(
                    QFileInfo(productB).canonicalFilePath()) &&
                activePibPaths.entries().last().includesPibs,
                "active PIB collection participates in the shared ordered path list");

    const MibRuntimeCollectionReference duplicateProduct{
        "duplicate-product", MibRuntimeCollectionRole::General,
        QDir(productA).filePath("."), false};
    const auto duplicateRuntime = runtimeBuilder.build(
        noStandards, index, {productCollection, duplicateProduct});
    const auto duplicatePaths = pathBuilder.derive(duplicateRuntime, index);
    ok &= check(duplicatePaths.isValid() &&
                duplicatePaths.orderedPaths().count(QFileInfo(productA).canonicalFilePath()) == 1 &&
                duplicatePaths.entries().first().collectionId == productCollection.id,
                "canonical first occurrence wins during path deduplication");

    const QString emptyCollectionRoot = QDir(temp.path()).filePath("Vendor/Empty");
    QDir().mkpath(emptyCollectionRoot);
    const MibRuntimeCollectionReference firstSharedId{
        "shared-id", MibRuntimeCollectionRole::General, emptyCollectionRoot, false};
    const MibRuntimeCollectionReference secondSharedId{
        "shared-id", MibRuntimeCollectionRole::Product, productA, false};
    const auto unauthorizedAliasRuntime = runtimeBuilder.build(
        noStandards, index, {firstSharedId, secondSharedId});
    const auto unauthorizedAliasPaths = pathBuilder.derive(unauthorizedAliasRuntime, index);
    ok &= check(!unauthorizedAliasPaths.isValid() &&
                unauthorizedAliasPaths.diagnostics().join('\n').contains("Root alias") &&
                unauthorizedAliasPaths.diagnostics().join('\n').contains("authorized collection"),
                "root alias outside the uniquely identified authorized collection is rejected");

    QDir().mkpath(QDir(productA).filePath("z-child/deep"));
    QDir().mkpath(QDir(productA).filePath("a-child"));
    writeFile(QDir(productA).filePath("z-child/deep/NESTED-MIB"), mib("NESTED-MIB"));
    writeFile(QDir(productA).filePath("a-child/EARLY-MIB"), mib("EARLY-MIB"));
    MibDependencyIndex nestedIndex(QDir(temp.path()).filePath("nested-index.json"));
    nestedIndex.update({standards, productA});
    const auto nestedRuntime = runtimeBuilder.build(noStandards, nestedIndex, {productCollection});
    const auto nestedPaths = pathBuilder.derive(nestedRuntime, nestedIndex);
    const QString aChild = QFileInfo(QDir(productA).filePath("a-child")).canonicalFilePath();
    const QString zDeep = QFileInfo(QDir(productA).filePath("z-child/deep")).canonicalFilePath();
    ok &= check(nestedPaths.isValid() && nestedPaths.orderedPaths().contains(aChild) &&
                nestedPaths.orderedPaths().contains(zDeep) &&
                nestedPaths.orderedPaths().indexOf(aChild) < nestedPaths.orderedPaths().indexOf(zDeep),
                "indexed descendant directories expand recursively in stable lexical order");

    QSettings legacySettings(QDir(temp.path()).filePath("legacy.ini"), QSettings::IniFormat);
    legacySettings.beginWriteArray("mibpaths"); legacySettings.setArrayIndex(0);
    legacySettings.setValue("dir", "C:/projects/snmpb-next/build-graph-presentation/app/mibs");
    legacySettings.setArrayIndex(1); legacySettings.setValue("dir", "C:/Save/ExtremeMibs");
    legacySettings.endArray(); legacySettings.sync();
    const auto legacyIsolatedPaths = pathBuilder.derive(nestedRuntime, nestedIndex);
    ok &= check(legacyIsolatedPaths.orderedPaths() == nestedPaths.orderedPaths() &&
                !legacyIsolatedPaths.orderedPaths().join('\n').contains("build-graph-presentation") &&
                !legacyIsolatedPaths.orderedPaths().join('\n').contains("ExtremeMibs"),
                "legacy mibpaths settings cannot contaminate pure runtime path derivation");

    MibProfileRecord parserProfile;
    parserProfile.id = "parser-profile"; parserProfile.name = "Parser Profile";
    parserProfile.type = MibProfileType::Custom; parserProfile.explicitModules = {"TEST-MIB"};
    const auto parserConfiguration = [&](const MibRuntimeCollectionReference &collection) {
        const auto runtime = runtimeBuilder.build(parserProfile, index, {collection});
        return pathBuilder.derive(runtime, index);
    };
    const auto parserA = parserConfiguration(productCollection);
    const MibRuntimeCollectionReference productBCollection{
        "vendor/product-b", MibRuntimeCollectionRole::Product, productB, false};
    const auto parserB = parserConfiguration(productBCollection);
    smiInit("phase-c-runtime-test");
    smiSetFlags(SMI_FLAG_ERRORS | SMI_FLAG_NODESCR);
    bool restoreCalled = false;
    const auto resetA1 = MibRuntimeParser::reset(parserA, [&] { restoreCalled = true; });
    ok &= check(resetA1.success && restoreCalled &&
                (resetA1.restoredFlags & SMI_FLAG_ERRORS) &&
                (resetA1.restoredFlags & SMI_FLAG_NODESCR),
                "fresh runtime parser restores flags and application configuration callback");
    ok &= check(smiLoadModule("TEST-MIB") != nullptr && smiGetModule("TEST-MIB") &&
                canonical(smiGetModule("TEST-MIB")->path).contains("Product A"),
                "Profile A loads same-identity provider A");
    const auto resetB = MibRuntimeParser::reset(parserB);
    ok &= check(resetB.success && smiLoadModule("TEST-MIB") != nullptr &&
                canonical(smiGetModule("TEST-MIB")->path).contains("Product B"),
                "Profile B reset prevents first-loaded provider A from leaking");
    const auto resetA2 = MibRuntimeParser::reset(parserA);
    ok &= check(resetA2.success && smiLoadModule("TEST-MIB") != nullptr &&
                canonical(smiGetModule("TEST-MIB")->path).contains("Product A") &&
                resetA1.appliedPaths == resetA2.appliedPaths,
                "Profile A-B-A produces deterministic paths and provider result");

    MibProfileRecord invalidParserProfile = parserProfile;
    invalidParserProfile.includeStandardBase = true;
    const auto invalidRuntime = runtimeBuilder.build(
        invalidParserProfile, index, {productCollection});
    const auto invalidPaths = pathBuilder.derive(invalidRuntime, index);
    std::unique_ptr<char, decltype(&std::free)> beforeInvalidPath{smiGetPath(), std::free};
    const QString beforeInvalidProvider = canonical(smiGetModule("TEST-MIB")->path);
    const auto invalidReset = MibRuntimeParser::reset(invalidPaths);
    std::unique_ptr<char, decltype(&std::free)> afterInvalidPath{smiGetPath(), std::free};
    ok &= check(!invalidReset.success && smiIsLoaded("TEST-MIB") &&
                beforeInvalidProvider == canonical(smiGetModule("TEST-MIB")->path) &&
                QByteArray(beforeInvalidPath.get()) == QByteArray(afterInvalidPath.get()),
                "invalid runtime configuration is rejected before parser reset");

    QDir().mkpath(QDir(temp.path()).filePath("legacy-provider"));
    writeFile(QDir(temp.path()).filePath("legacy-provider/TEST-MIB"),
              mib("TEST-MIB") + "stale-provider-VARIANT ::= 1\n");
    legacySettings.beginWriteArray("mibpaths"); legacySettings.setArrayIndex(0);
    legacySettings.setValue("dir", QDir(temp.path()).filePath("legacy-provider"));
    legacySettings.endArray(); legacySettings.sync();
    const auto resetLegacyIsolation = MibRuntimeParser::reset(parserB);
    std::unique_ptr<char, decltype(&std::free)> appliedLegacyIsolation{smiGetPath(), std::free};
    ok &= check(resetLegacyIsolation.success &&
                !QString::fromLocal8Bit(appliedLegacyIsolation.get()).contains("legacy-provider") &&
                smiLoadModule("TEST-MIB") &&
                canonical(smiGetModule("TEST-MIB")->path).contains("Product B"),
                "actual parser reset excludes competing legacy mibpaths provider");

    const auto resetInactivePib = MibRuntimeParser::reset(inactivePibPaths);
    std::unique_ptr<char, decltype(&std::free)> inactivePibPath{smiGetPath(), std::free};
    const auto resetActivePib = MibRuntimeParser::reset(activePibPaths);
    std::unique_ptr<char, decltype(&std::free)> activePibPath{smiGetPath(), std::free};
    ok &= check(resetInactivePib.success && resetActivePib.success &&
                !QString::fromLocal8Bit(inactivePibPath.get()).contains("Product B") &&
                QString::fromLocal8Bit(activePibPath.get()).contains("Product B"),
                "PIB runtime path participates only when configured and does not leak across reset");
    const QString bundledMibs = QDir(QCoreApplication::applicationDirPath()).filePath("mibs");
    if (QDir(bundledMibs).exists()) {
        MibDependencyIndex bundledIndex(QDir(temp.path()).filePath("bundled-index.json"));
        bundledIndex.update({bundledMibs});
        MibProfileRecord bundledProfile;
        bundledProfile.id = "bundled"; bundledProfile.name = "Bundled";
        bundledProfile.type = MibProfileType::Custom;
        bundledProfile.explicitModules = {"SNMPv2-MIB"};
        const MibRuntimeCollectionReference bundledCollection{
            "bundled", MibRuntimeCollectionRole::Standards, bundledMibs, false};
        const auto bundledRuntime = runtimeBuilder.build(
            bundledProfile, bundledIndex, {bundledCollection});
        const auto bundledPaths = pathBuilder.derive(bundledRuntime, bundledIndex);
        const auto bundledReset = MibRuntimeParser::reset(bundledPaths);
        ok &= check(bundledReset.success && smiLoadModule("SNMPv2-MIB") &&
                    smiGetModule("SNMPv2-MIB"),
                    "fresh parser loads a representative bundled standards module");
        const auto bundledExactReset = MibRuntimeParser::reset(bundledPaths);
        const QString bundledProvider = QDir(bundledMibs).filePath("SNMPv2-MIB");
        ok &= check(bundledExactReset.success &&
                    smiLoadModule(QDir::toNativeSeparators(bundledProvider).toLocal8Bit().constData()) &&
                    smiGetModule("SNMPv2-MIB"),
                    "fresh parser retains existing exact-provider root loading");
    }

    QTemporaryDir aliasFixture;
    const QString aliasProduct = QDir(aliasFixture.path()).filePath("Vendor/Product");
    const QString aliasStandards = QDir(aliasFixture.path()).filePath("Standards");
    QDir().mkpath(aliasProduct); QDir().mkpath(aliasStandards);
    writeFile(QDir(aliasStandards).filePath("DEPENDENCY-MIB"),
              "DEPENDENCY-MIB DEFINITIONS ::= BEGIN\n"
              "value OBJECT IDENTIFIER ::= { 1 3 6 1 4 1 99999 }\nEND\n");
    writeFile(QDir(aliasProduct).filePath("AAA-TEST-MIB.mib"),
              "AAA-TEST-MIB DEFINITIONS ::= BEGIN\n"
              "aaaRoot OBJECT IDENTIFIER ::= { 1 3 6 1 4 1 99998 }\nEND\n");
    const QString atmAliasFile = QDir(aliasProduct).filePath("atm_tc.mib");
    const QString bridgeAliasFile = QDir(aliasProduct).filePath("rfc4188.mib");
    const QString baseAliasFile = QDir(aliasProduct).filePath("base.my");
    const QString multiAliasFile = QDir(aliasProduct).filePath("vendor-file.mib");
    writeFile(atmAliasFile, mib("ATM-TC-MIB", {"DEPENDENCY-MIB"}));
    writeFile(bridgeAliasFile, mib("BRIDGE-MIB"));
    writeFile(baseAliasFile, mib("EXTREME-BASE-MIB"));
    writeFile(multiAliasFile, mib("VENDOR-ROOT-MIB") + mib("VENDOR-TC-MIB"));
    MibDependencyIndex aliasIndex(QDir(aliasFixture.path()).filePath("alias-index.json"));
    aliasIndex.update({aliasProduct, aliasStandards});
    MibProfileRecord aliasProfile;
    aliasProfile.id = "alias-profile"; aliasProfile.name = "Alias Profile";
    aliasProfile.type = MibProfileType::Custom;
    aliasProfile.explicitModules = {"AAA-TEST-MIB", "ATM-TC-MIB", "BRIDGE-MIB",
        "EXTREME-BASE-MIB", "VENDOR-ROOT-MIB", "VENDOR-TC-MIB"};
    const MibRuntimeCollectionReference aliasProductCollection{
        "alias-product", MibRuntimeCollectionRole::Product, aliasProduct, false};
    const MibRuntimeCollectionReference aliasStandardsCollection{
        "alias-standards", MibRuntimeCollectionRole::Standards, aliasStandards, false};
    const auto aliasRuntime = runtimeBuilder.build(
        aliasProfile, aliasIndex, {aliasProductCollection, aliasStandardsCollection});
    const auto aliasPaths = pathBuilder.derive(aliasRuntime, aliasIndex);
    ok &= check(aliasRuntime.rootAliases().contains("ATM-TC-MIB") &&
                aliasRuntime.rootAliases().contains("BRIDGE-MIB") &&
                aliasRuntime.rootAliases().contains("EXTREME-BASE-MIB") &&
                !aliasRuntime.rootAliases().contains("AAA-TEST-MIB"),
                "runtime contract distinguishes filename aliases from conventional roots");
    MibRuntimeParser::reset(aliasPaths);
    const auto aliasLoads = MibRuntimeParser::loadExplicitRoots(aliasRuntime, aliasPaths);
    QMap<QString, MibExplicitRootLoadResult> aliasResults;
    for (const auto &result : aliasLoads.roots) aliasResults.insert(result.identity, result);
    ok &= check(aliasResults.value("AAA-TEST-MIB").success &&
                (aliasResults.value("AAA-TEST-MIB").status == MibExplicitRootLoadStatus::LoadedByIdentity ||
                 aliasResults.value("AAA-TEST-MIB").status == MibExplicitRootLoadStatus::AlreadyLoaded),
                "conventional explicit root resolves without a filename alias");
    ok &= check(aliasResults.value("ATM-TC-MIB").success &&
                aliasResults.value("ATM-TC-MIB").status == MibExplicitRootLoadStatus::LoadedByAlias,
                "ATM-TC-MIB loads through authorized atm_tc.mib alias");
    ok &= check(aliasResults.value("BRIDGE-MIB").success &&
                aliasResults.value("BRIDGE-MIB").status == MibExplicitRootLoadStatus::LoadedByAlias,
                "BRIDGE-MIB loads through authorized rfc4188.mib alias");
    ok &= check(aliasResults.value("EXTREME-BASE-MIB").success &&
                aliasResults.value("EXTREME-BASE-MIB").status == MibExplicitRootLoadStatus::LoadedByAlias,
                "EXTREME-BASE-MIB loads through authorized base.my alias");
    ok &= check(smiGetModule("DEPENDENCY-MIB") &&
                !aliasRuntime.rootAliases().contains("DEPENDENCY-MIB") &&
                aliasLoads.roots.size() == aliasRuntime.explicitRoots().size(),
                "alias-loaded root imports resolve natively without dependency exact-file planning");
    ok &= check(aliasResults.value("VENDOR-ROOT-MIB").success &&
                aliasResults.value("VENDOR-TC-MIB").success &&
                aliasResults.value("VENDOR-TC-MIB").status == MibExplicitRootLoadStatus::AlreadyLoaded,
                "one alias file satisfies multiple explicit identities and second root verifies already loaded");

    MibEffectivePlan authorizedEnvironmentPlan;
    authorizedEnvironmentPlan.profileId = aliasProfile.id;
    authorizedEnvironmentPlan.profileName = aliasProfile.name;
    authorizedEnvironmentPlan.sha256 = "phase-e-authorized";
    authorizedEnvironmentPlan.runtimeConfiguration = aliasRuntime;
    authorizedEnvironmentPlan.runtimePaths = aliasPaths;
    authorizedEnvironmentPlan.hasRuntimePaths = true;
    authorizedEnvironmentPlan.explicitModules = aliasRuntime.explicitRoots();
    authorizedEnvironmentPlan.effectiveModules = aliasRuntime.explicitRoots();
    authorizedEnvironmentPlan.effectiveModules.append("DEPENDENCY-MIB");
    authorizedEnvironmentPlan.effectiveModules.removeDuplicates();
    const auto authorizedEnvironment = MibEnvironmentExtractor().extract(
        authorizedEnvironmentPlan, {}, aliasLoads.roots);
    ok &= check(authorizedEnvironment->publishable() &&
                authorizedEnvironment->providersAuthorized() &&
                authorizedEnvironment->profileId() == aliasProfile.id &&
                authorizedEnvironment->runtimeConfigurationHash() == aliasRuntime.sha256() &&
                authorizedEnvironment->runtimePathHash() == aliasPaths.sha256() &&
                authorizedEnvironment->rootLoadOutcomes().size() ==
                    aliasRuntime.explicitRoots().size() &&
                authorizedEnvironment->loadedProviderPaths().contains("DEPENDENCY-MIB"),
                "complete alias roots and native Standards dependency produce an authorized publishable Environment");

    QList<MibExplicitRootLoadResult> partialOutcomes = aliasLoads.roots;
    partialOutcomes.first().success = false;
    partialOutcomes.first().status = MibExplicitRootLoadStatus::ParserError;
    partialOutcomes.first().diagnostic = "synthetic parser failure";
    const auto partialEnvironment = MibEnvironmentExtractor().extract(
        authorizedEnvironmentPlan, {partialOutcomes.first().identity}, partialOutcomes);
    ok &= check(!partialEnvironment->publishable() &&
                partialEnvironment->status() == MibEnvironmentStatus::Unusable,
                "one failed required root rejects a partially materialized Environment");

    MibProfileRecord exactProfile;
    exactProfile.id = "exact"; exactProfile.name = "Exact";
    exactProfile.type = MibProfileType::Custom;
    exactProfile.members = MibProfileMembersFromFiles({bridgeAliasFile});
    exactProfile.explicitModules = MibProfileMemberIdentities(exactProfile.members);
    const auto exactRuntime = runtimeBuilder.build(exactProfile, aliasIndex,
        {aliasProductCollection, aliasStandardsCollection});
    const auto exactPaths = pathBuilder.derive(exactRuntime, aliasIndex);
    MibRuntimeParser::reset(exactPaths);
    const auto exactLoads = MibRuntimeParser::loadExplicitRoots(exactRuntime, exactPaths);
    MibEffectivePlan exactPlan;
    exactPlan.profileId = exactProfile.id; exactPlan.profileName = exactProfile.name;
    exactPlan.sha256 = "phase-e-exact"; exactPlan.runtimeConfiguration = exactRuntime;
    exactPlan.runtimePaths = exactPaths; exactPlan.hasRuntimePaths = true;
    exactPlan.explicitModules = {"BRIDGE-MIB"}; exactPlan.effectiveModules = {"BRIDGE-MIB"};
    const auto exactEnvironment = MibEnvironmentExtractor().extract(
        exactPlan, {}, exactLoads.roots);
    ok &= check(exactEnvironment->publishable() && exactEnvironment->providersAuthorized(),
                "an exact Profile member is authorized by physical path and hash");

    const QString selectedFile = QDir(aliasProduct).filePath("selected-root.mib");
    writeFile(selectedFile,
              "SELECTED-ROOT-MIB DEFINITIONS ::= BEGIN\n"
              "IMPORTS aaaRoot FROM AAA-TEST-MIB;\n"
              "selectedRoot OBJECT IDENTIFIER ::= { aaaRoot 1 }\nEND\n");
    aliasIndex.update({aliasProduct, aliasStandards});
    MibProfileRecord siblingProfile;
    siblingProfile.id = "sibling"; siblingProfile.name = "Sibling";
    siblingProfile.type = MibProfileType::Custom;
    siblingProfile.members = MibProfileMembersFromFiles({selectedFile});
    siblingProfile.explicitModules = MibProfileMemberIdentities(siblingProfile.members);
    const auto siblingRuntime = runtimeBuilder.build(siblingProfile, aliasIndex,
        {aliasProductCollection, aliasStandardsCollection});
    const auto siblingPaths = pathBuilder.derive(siblingRuntime, aliasIndex);
    MibRuntimeParser::reset(siblingPaths);
    const auto siblingLoads = MibRuntimeParser::loadExplicitRoots(siblingRuntime, siblingPaths);
    MibEffectivePlan siblingPlan;
    siblingPlan.profileId = siblingProfile.id; siblingPlan.profileName = siblingProfile.name;
    siblingPlan.sha256 = "phase-e-sibling"; siblingPlan.runtimeConfiguration = siblingRuntime;
    siblingPlan.runtimePaths = siblingPaths; siblingPlan.hasRuntimePaths = true;
    siblingPlan.explicitModules = {"SELECTED-ROOT-MIB"};
    siblingPlan.effectiveModules = {"SELECTED-ROOT-MIB"};
    const auto unauthorizedSiblingEnvironment = MibEnvironmentExtractor().extract(
        siblingPlan, {}, siblingLoads.roots);
    ok &= check(!unauthorizedSiblingEnvironment->publishable() &&
                !unauthorizedSiblingEnvironment->providersAuthorized() &&
                unauthorizedSiblingEnvironment->constructionDiagnostics().join('\n').contains(
                "not in the Effective Plan"),
                "a sibling file in an authorized directory is rejected unless it is an exact member");

    const QString collisionDir = QDir(aliasProduct).filePath("collision");
    QDir().mkpath(collisionDir);
    const QString providerAFile = QDir(collisionDir).filePath("provider-A.mib");
    const QString providerBFile = QDir(collisionDir).filePath("provider-B.mib");
    writeFile(providerAFile, "SAME-DIR-MIB DEFINITIONS ::= BEGIN\n"
                             "sameRoot OBJECT IDENTIFIER ::= { 1 3 6 1 4 1 99101 }\nEND\n");
    writeFile(providerBFile, "SAME-DIR-MIB DEFINITIONS ::= BEGIN\n"
                             "sameRoot OBJECT IDENTIFIER ::= { 1 3 6 1 4 1 99102 }\nEND\n");
    const QString dependencyRootFile = QDir(collisionDir).filePath("dependency-root.mib");
    const QString dependencyAFile = QDir(collisionDir).filePath("dependency-A.mib");
    const QString dependencyBFile = QDir(collisionDir).filePath("dependency-B.mib");
    writeFile(dependencyRootFile, "DEPENDENT-ROOT-MIB DEFINITIONS ::= BEGIN\n"
                                  "IMPORTS depRoot FROM EXACT-DEPENDENCY-MIB;\n"
                                  "dependentRoot OBJECT IDENTIFIER ::= { depRoot 1 }\nEND\n");
    writeFile(dependencyAFile, "EXACT-DEPENDENCY-MIB DEFINITIONS ::= BEGIN\n"
                               "depRoot OBJECT IDENTIFIER ::= { 1 3 6 1 4 1 99201 }\nEND\n");
    writeFile(dependencyBFile, "EXACT-DEPENDENCY-MIB DEFINITIONS ::= BEGIN\n"
                               "depRoot OBJECT IDENTIFIER ::= { 1 3 6 1 4 1 99202 }\nEND\n");
    aliasIndex.update({aliasProduct, aliasStandards});
    QTemporaryDir stageCache;
    const auto materializeExact = [&](const QString &id, const QStringList &files) {
        MibProfileRecord profile;
        profile.id = id; profile.name = id; profile.type = MibProfileType::Custom;
        profile.members = MibProfileMembersFromFiles(files);
        profile.explicitModules = MibProfileMemberIdentities(profile.members);
        MibEffectivePlan plan = MibEffectivePlanResolver().resolve(profile, aliasIndex);
        plan.runtimeConfiguration = runtimeBuilder.build(
            profile, aliasIndex, {aliasProductCollection, aliasStandardsCollection});
        plan.runtimePaths = pathBuilder.derive(plan.runtimeConfiguration, aliasIndex);
        plan.hasRuntimePaths = true;
        MibEffectivePlanResolver::sealRuntimeAuthority(&plan);
        const auto stage = MibRuntimeStage::prepare(plan, stageCache.path());
        if (!stage.success) return std::pair<MibEffectivePlan, MibEnvironmentPtr>{plan, {}};
        plan.runtimeConfiguration = stage.configuration;
        plan.runtimePaths = stage.paths;
        MibRuntimeParser::reset(plan.runtimePaths);
        const auto loads = MibRuntimeParser::loadExplicitRoots(
            plan.runtimeConfiguration, plan.runtimePaths);
        QStringList unavailable = loads.failedIdentities();
        unavailable.append(plan.missingModules);
        unavailable.append(plan.ambiguousModules);
        unavailable.append(plan.pinFailureModules);
        unavailable.removeDuplicates();
        return std::pair<MibEffectivePlan, MibEnvironmentPtr>{plan,
            MibEnvironmentExtractor().extract(plan, unavailable, loads.roots)};
    };
    const auto selectedA = materializeExact("same-a", {providerAFile});
    ok &= check(selectedA.second && selectedA.second->publishable() &&
                canonical(selectedA.second->loadedProviderPaths().value("SAME-DIR-MIB")) ==
                    canonical(providerAFile),
                "same-identity same-directory provider A is deliberately materialized");
    const auto selectedB = materializeExact("same-b", {providerBFile});
    ok &= check(selectedB.second && selectedB.second->publishable() &&
                canonical(selectedB.second->loadedProviderPaths().value("SAME-DIR-MIB")) ==
                    canonical(providerBFile),
                "reversed exact selection deliberately materializes provider B");

    const auto dependencyA = materializeExact(
        "dependency-a", {dependencyRootFile, dependencyAFile});
    ok &= check(dependencyA.first.runtimeConfiguration.explicitRoots().first() ==
                    "EXACT-DEPENDENCY-MIB" && dependencyA.second->publishable() &&
                canonical(dependencyA.second->loadedProviderPaths().value("EXACT-DEPENDENCY-MIB")) ==
                    canonical(dependencyAFile),
                "authorized dependency is loaded before its importer and provider A publishes");
    MibProfileRecord dependencyAProfile;
    dependencyAProfile.id = "dependency-a"; dependencyAProfile.name = "dependency-a";
    dependencyAProfile.type = MibProfileType::Custom;
    dependencyAProfile.members = MibProfileMembersFromFiles({dependencyRootFile, dependencyAFile});
    const auto dependencyAStage = MibRuntimeStage::prepare(
        dependencyA.first, stageCache.path());
    ok &= check(dependencyAStage.success && dependencyAStage.reused &&
                dependencyAStage.paths.orderedPaths().size() == 1 &&
                !dependencyAStage.paths.orderedPaths().first().contains(collisionDir),
                "completed exact runtime stage is reusable and excludes the source directory");
    const QStringList stagedNames = QDir(dependencyA.first.runtimePaths.orderedPaths().first())
        .entryList(QDir::Files | QDir::NoDotAndDotDot);
    ok &= check(QSet<QString>(stagedNames.cbegin(), stagedNames.cend()) ==
                    QSet<QString>({"DEPENDENT-ROOT-MIB", "EXACT-DEPENDENCY-MIB"}),
                "runtime stage exposes only authorized identity aliases");
    const auto dependencyB = materializeExact(
        "dependency-b", {dependencyRootFile, dependencyBFile});
    ok &= check(dependencyB.second->publishable() &&
                canonical(dependencyB.second->loadedProviderPaths().value("EXACT-DEPENDENCY-MIB")) ==
                    canonical(dependencyBFile),
                "replacing dependency membership deliberately materializes provider B");
    const auto missingDependency = materializeExact("dependency-missing", {dependencyRootFile});
    ok &= check(missingDependency.first.missingModules.contains("EXACT-DEPENDENCY-MIB") &&
                !missingDependency.second->publishable(),
                "removing an exact dependency reports it missing and cannot authorize a Catalog substitute");

    const auto duplicateAuthority = materializeExact(
        "duplicate-authority", {providerAFile, providerBFile});
    ok &= check(!duplicateAuthority.second,
                "differing exact providers for one identity fail deterministically before parsing");

    QTemporaryDir refreshFixture;
    const QString refreshProduct = QDir(refreshFixture.path()).filePath("Vendor Product");
    const QString refreshStandards = QDir(refreshFixture.path()).filePath("Standards");
    QDir().mkpath(refreshProduct); QDir().mkpath(refreshStandards);
    const QString refreshRootFile = QDir(refreshProduct).filePath("root.my");
    const QString productDependency = QDir(refreshProduct).filePath("atm_tc.mib");
    const QString standardsDependency = QDir(refreshStandards).filePath("ATM-TC-MIB");
    writeFile(refreshRootFile, mib("PRODUCT-ROOT-MIB", {"ATM-TC-MIB"}));
    writeFile(productDependency, mib("ATM-TC-MIB") + "product-VARIANT ::= 1\n");
    writeFile(standardsDependency, mib("ATM-TC-MIB") + "standards-VARIANT ::= 1\n");
    MibDependencyIndex refreshIndex(QDir(refreshFixture.path()).filePath("index.json"));
    refreshIndex.update({refreshProduct, refreshStandards});
    MibProfileRecord refreshProfile;
    refreshProfile.id = "refresh"; refreshProfile.name = "Refresh";
    refreshProfile.type = MibProfileType::Custom;
    refreshProfile.members = MibProfileMembersFromFiles({refreshRootFile});
    const auto refreshed = MibResolveProfileDependencies(refreshProfile, refreshIndex);
    ok &= check(refreshed.addedDependencies.size() == 1 && refreshed.unresolved.isEmpty() &&
                canonical(refreshed.addedDependencies.first().canonicalPath) ==
                    canonical(productDependency) &&
                refreshed.addedDependencies.first().reason == MibProfileMemberReason::Dependency,
                "dependency refresh uses one exact same-folder provider without global guessing");
    MibEffectivePlan refreshedPlan = MibEffectivePlanResolver().resolve(refreshed.profile, refreshIndex);
    const auto refreshedConfiguration = runtimeBuilder.build(refreshed.profile, refreshIndex, {});
    refreshedPlan.runtimeConfiguration = refreshedConfiguration;
    refreshedPlan.runtimePaths = pathBuilder.derive(refreshedPlan.runtimeConfiguration, refreshIndex);
    refreshedPlan.hasRuntimePaths = true;
    MibEffectivePlanResolver::sealRuntimeAuthority(&refreshedPlan);
    const auto refreshedStage = MibRuntimeStage::prepare(
        refreshedPlan, stageCache.path());
    refreshedPlan.runtimeConfiguration = refreshedStage.configuration;
    refreshedPlan.runtimePaths = refreshedStage.paths; refreshedPlan.hasRuntimePaths = true;
    MibRuntimeParser::reset(refreshedPlan.runtimePaths);
    const auto refreshedLoads = MibRuntimeParser::loadExplicitRoots(
        refreshedPlan.runtimeConfiguration, refreshedPlan.runtimePaths);
    const auto isolatedRefresh = MibEnvironmentExtractor().extract(
        refreshedPlan, {}, refreshedLoads.roots);
    ok &= check(refreshedStage.success && isolatedRefresh->publishable() &&
                canonical(isolatedRefresh->loadedProviderPaths().value("ATM-TC-MIB")) ==
                    canonical(productDependency),
                "isolated runtime preserves contextual exact dependency provider");
    const QByteArray originalDependencyBytes = mib("ATM-TC-MIB") + "product-VARIANT ::= 1\n";
    writeFile(productDependency, mib("ATM-TC-MIB") + "-- changed\n");
    const auto changedSourceStage = MibRuntimeStage::prepare(
        refreshedPlan, stageCache.path());
    ok &= check(!changedSourceStage.success,
                "changed source hash is rejected before cached-stage reuse");
    writeFile(productDependency, originalDependencyBytes);
    const QString injectedArtifact = QDir(refreshedStage.paths.orderedPaths().first())
        .filePath("UNAUTHORIZED-MIB");
    writeFile(injectedArtifact, mib("UNAUTHORIZED-MIB"));
    const auto repairedStage = MibRuntimeStage::prepare(
        refreshedPlan, stageCache.path());
    ok &= check(repairedStage.success && !repairedStage.reused &&
                !QFileInfo::exists(injectedArtifact),
                "unrecognized staged artifacts invalidate and rebuild the stage");
    QFile::remove(QDir(repairedStage.directory).filePath("complete.json"));
    const auto incompleteStage = MibRuntimeStage::prepare(
        refreshedPlan, stageCache.path());
    ok &= check(incompleteStage.success && !incompleteStage.reused,
                "incomplete stage is rejected and reconstructed atomically");

    writeFile(QDir(refreshProduct).filePath("atm_tc_second.mib"),
              mib("ATM-TC-MIB") + "-- second product\n");
    refreshIndex.update({refreshProduct, refreshStandards});
    const auto ambiguousRefresh = MibResolveProfileDependencies(refreshProfile, refreshIndex);
    ok &= check(ambiguousRefresh.addedDependencies.isEmpty() &&
                ambiguousRefresh.unresolved.contains("ATM-TC-MIB"),
                "dependency refresh retains same-folder ambiguity instead of selecting by order");

    const auto exactMulti = materializeExact("multi", {multiAliasFile});
    ok &= check(exactMulti.second->publishable() && exactMulti.second->authorizedFiles().size() == 1 &&
                canonical(exactMulti.second->loadedProviderPaths().value("VENDOR-ROOT-MIB")) ==
                    canonical(multiAliasFile) &&
                canonical(exactMulti.second->loadedProviderPaths().value("VENDOR-TC-MIB")) ==
                    canonical(multiAliasFile),
                "one exact multi-identity file authorizes every identity it declares");

    MibProfileRecord emptyProfile;
    emptyProfile.id = "empty"; emptyProfile.name = "Empty";
    emptyProfile.type = MibProfileType::Custom;
    const auto emptyRuntime = runtimeBuilder.build(emptyProfile, aliasIndex,
        {aliasProductCollection, aliasStandardsCollection});
    const auto emptyPaths = pathBuilder.derive(emptyRuntime, aliasIndex);
    MibRuntimeParser::reset(emptyPaths);
    MibEffectivePlan emptyPlan;
    emptyPlan.profileId = emptyProfile.id; emptyPlan.profileName = emptyProfile.name;
    emptyPlan.sha256 = "phase-e-empty"; emptyPlan.runtimeConfiguration = emptyRuntime;
    emptyPlan.runtimePaths = emptyPaths; emptyPlan.hasRuntimePaths = true;
    const auto emptyEnvironment = MibEnvironmentExtractor().extract(emptyPlan);
    ok &= check(emptyEnvironment->publishable() && emptyEnvironment->explicitRoots().isEmpty(),
                "valid zero-root Profile produces an authorized empty Environment");

    MibRuntimeParser::reset(parserA);
    const auto unauthorizedAliasLoads = MibRuntimeParser::loadExplicitRoots(aliasRuntime, parserA);
    ok &= check(std::any_of(unauthorizedAliasLoads.roots.cbegin(), unauthorizedAliasLoads.roots.cend(),
                    [](const auto &result) {
                        return result.identity == "ATM-TC-MIB" && !result.success &&
                            result.status == MibExplicitRootLoadStatus::UnauthorizedAlias;
                    }),
                "alias outside the active runtime collection is rejected defensively");

    QFile::remove(atmAliasFile);
    MibRuntimeParser::reset(aliasPaths);
    const auto missingAliasLoads = MibRuntimeParser::loadExplicitRoots(aliasRuntime, aliasPaths);
    ok &= check(std::any_of(missingAliasLoads.roots.cbegin(), missingAliasLoads.roots.cend(),
                    [](const auto &result) {
                        return result.identity == "ATM-TC-MIB" && !result.success &&
                            result.status == MibExplicitRootLoadStatus::MissingAliasFile;
                    }),
                "missing explicit-root alias fails without alternate-provider fallback");

    writeFile(atmAliasFile, mib("ATM-TC-MIB", {"DEPENDENCY-MIB"}));
    aliasIndex.update({aliasProduct, aliasStandards});
    const auto currentAliasRuntime = runtimeBuilder.build(
        aliasProfile, aliasIndex, {aliasProductCollection, aliasStandardsCollection});
    const auto currentAliasPaths = pathBuilder.derive(currentAliasRuntime, aliasIndex);
    QFile changedAlias(atmAliasFile);
    ok &= check(changedAlias.open(QIODevice::Append) &&
                changedAlias.write("-- changed after planning\n") > 0,
                "changed-alias fixture mutation succeeds");
    changedAlias.close();
    MibRuntimeParser::reset(currentAliasPaths);
    const auto changedAliasLoads = MibRuntimeParser::loadExplicitRoots(
        currentAliasRuntime, currentAliasPaths);
    ok &= check(std::any_of(changedAliasLoads.roots.cbegin(), changedAliasLoads.roots.cend(),
                    [](const auto &result) {
                        return result.identity == "ATM-TC-MIB" && !result.success &&
                            result.status == MibExplicitRootLoadStatus::AliasContentChanged;
                    }),
                "changed alias hash fails before exact loading");

    MibProfileRecord missingNativeProfile;
    missingNativeProfile.id = "missing-native";
    missingNativeProfile.name = "Missing Native";
    missingNativeProfile.type = MibProfileType::Custom;
    missingNativeProfile.explicitModules = {"NO-SUCH-NATIVE-MIB"};
    const auto missingNativeRuntime = runtimeBuilder.build(missingNativeProfile, aliasIndex,
        {aliasProductCollection, aliasStandardsCollection});
    const auto missingNativePaths = pathBuilder.derive(missingNativeRuntime, aliasIndex);
    MibRuntimeParser::reset(missingNativePaths);
    const auto missingNativeLoads = MibRuntimeParser::loadExplicitRoots(
        missingNativeRuntime, missingNativePaths);
    ok &= check(missingNativeLoads.roots.size() == 1 &&
                !missingNativeLoads.roots.first().success &&
                missingNativeLoads.roots.first().status == MibExplicitRootLoadStatus::NativeLookupFailed,
                "missing conventional root reports native lookup failure without provider fallback");

    const QString wrongAliasFile = QDir(aliasProduct).filePath("wrong-provider.mib");
    writeFile(wrongAliasFile, mib("ACTUAL-IDENTITY-MIB"));
    const QString wrongIndexPath = QDir(aliasFixture.path()).filePath("wrong-index.json");
    MibDependencyIndex wrongIndex(wrongIndexPath);
    wrongIndex.update({aliasProduct, aliasStandards});
    QFile wrongIndexFile(wrongIndexPath);
    ok &= check(wrongIndexFile.open(QIODevice::ReadOnly), "wrong-identity index fixture opens");
    QJsonObject wrongIndexRoot = QJsonDocument::fromJson(wrongIndexFile.readAll()).object();
    wrongIndexFile.close();
    QJsonArray wrongFiles = wrongIndexRoot.value("files").toArray();
    for (qsizetype i = 0; i < wrongFiles.size(); ++i) {
        QJsonObject file = wrongFiles.at(i).toObject();
        if (canonical(file.value("path").toString()) != canonical(wrongAliasFile)) continue;
        file.insert("modules", QJsonObject{{"CLAIMED-IDENTITY-MIB", QJsonArray{}}});
        wrongFiles.replace(i, file);
    }
    wrongIndexRoot.insert("files", wrongFiles);
    ok &= check(writeFile(wrongIndexPath, QJsonDocument(wrongIndexRoot).toJson()),
                "wrong-identity index fixture persists");
    MibDependencyIndex claimedIndex(wrongIndexPath);
    QString claimedIndexError;
    ok &= check(claimedIndex.load(&claimedIndexError), "wrong-identity index fixture reloads");
    MibProfileRecord claimedProfile;
    claimedProfile.id = "claimed-identity";
    claimedProfile.name = "Claimed Identity";
    claimedProfile.type = MibProfileType::Custom;
    claimedProfile.explicitModules = {"CLAIMED-IDENTITY-MIB"};
    const auto claimedRuntime = runtimeBuilder.build(claimedProfile, claimedIndex,
        {aliasProductCollection, aliasStandardsCollection});
    const auto claimedPaths = pathBuilder.derive(claimedRuntime, claimedIndex);
    MibRuntimeParser::reset(claimedPaths);
    const auto claimedLoads = MibRuntimeParser::loadExplicitRoots(claimedRuntime, claimedPaths);
    ok &= check(claimedLoads.roots.size() == 1 && !claimedLoads.roots.first().success &&
                claimedLoads.roots.first().status ==
                    MibExplicitRootLoadStatus::RequestedIdentityNotDeclared &&
                claimedLoads.roots.first().diagnostic.contains("CLAIMED-IDENTITY-MIB") &&
                claimedLoads.roots.first().diagnostic.contains("wrong-provider.mib"),
                "alias file that does not declare the requested identity fails with file and identity diagnostic");
    smiExit();
    ok &= check(customPlan.member("ROOT-MIB") && !customPlan.member("ROOT-MIB")->provider.canonicalPath.contains("Product A") && customPlan.dependencyModules.contains("DEPENDENCY-MIB"), "custom has no folder affinity");
    ok &= check(customPlan.sha256 != first.sha256,
                "changing selected provider and provider-specific imports changes plan hash");
    ok &= check(customPlan.explicitModules == QStringList{"ROOT-MIB"},
                "computed standards base does not mutate custom explicit membership");
    const MibIndexedProvider productRoot = first.member("ROOT-MIB")->provider;
    MibProfileRecord pinned = custom;
    pinned.providerPins.insert("ROOT-MIB", {productRoot.canonicalPath, productRoot.sha256});
    const auto pinnedPlan = MibEffectivePlanResolver().resolve(pinned, index);
    ok &= check(pinnedPlan.member("ROOT-MIB") &&
                pinnedPlan.member("ROOT-MIB")->provider.canonicalPath == productRoot.canonicalPath &&
                pinnedPlan.member("ROOT-MIB")->providerReason == MibPlanProviderReason::ExplicitPin &&
                pinnedPlan.dependencyModules.contains("PRODUCT-ONLY-MIB") &&
                !pinnedPlan.dependencyModules.contains("DEPENDENCY-MIB"),
                "explicit pin has highest precedence and drives provider-specific closure");
    MibProfileRecord invalidPin = custom;
    invalidPin.providerPins.insert("ROOT-MIB", {productRoot.canonicalPath, QString(64, '0')});
    const auto invalidPinPlan = MibEffectivePlanResolver().resolve(invalidPin, index);
    ok &= check(!invalidPinPlan.isComplete() && invalidPinPlan.pinFailureModules == QStringList{"ROOT-MIB"} &&
                invalidPinPlan.member("ROOT-MIB") && invalidPinPlan.member("ROOT-MIB")->provider.canonicalPath.isEmpty() &&
                invalidPinPlan.member("ROOT-MIB")->providerReason == MibPlanProviderReason::InvalidPin,
                "invalid explicit pin is a structured hard finding without fallback");
    MibProfileRecord differentInvalidPin = invalidPin;
    differentInvalidPin.providerPins["ROOT-MIB"].sha256 = QString(64, '1');
    ok &= check(MibEffectivePlanResolver().resolve(differentInvalidPin, index).sha256 != invalidPinPlan.sha256,
                "invalid explicit pin identity participates in the plan hash");
    MibProfileRecord missingPin = custom;
    missingPin.explicitModules = {"NO-SUCH-MIB"};
    missingPin.providerPins.insert("NO-SUCH-MIB", {QDir(temp.path()).filePath("removed.mib"), QString(64, '2')});
    const auto missingPinPlan = MibEffectivePlanResolver().resolve(missingPin, index);
    ok &= check(missingPinPlan.pinFailureModules == QStringList{"NO-SUCH-MIB"} &&
                !missingPinPlan.missingModules.contains("NO-SUCH-MIB"),
                "pin to a removed provider is reported as a pin failure");
    MibProfileRecord boundary = automatic;
    boundary.directory = QDir(temp.path()).filePath("Vendor/Product");
    const auto boundaryPlan = MibEffectivePlanResolver().resolve(boundary, index);
    ok &= check(boundaryPlan.member("ROOT-MIB") &&
                boundaryPlan.member("ROOT-MIB")->providerReason != MibPlanProviderReason::AutomaticProfileFolder,
                "automatic affinity uses a true path boundary");
    MibProfileRecord reordered = automatic; std::reverse(reordered.explicitModules.begin(), reordered.explicitModules.end());
    const auto reorderedPlan = MibEffectivePlanResolver().resolve(reordered, index);
    ok &= check(first.sha256 == reorderedPlan.sha256 && first.initialLoadOrder == reorderedPlan.initialLoadOrder, "insertion order independence");
    MibDependencyIndex reorderedIndex(QDir(temp.path()).filePath("reordered-index.json"));
    reorderedIndex.update({productB, productA, standards});
    const auto filesystemReorderedPlan = MibEffectivePlanResolver().resolve(automatic, reorderedIndex);
    ok &= check(first.sha256 == filesystemReorderedPlan.sha256 &&
                first.initialLoadOrder == filesystemReorderedPlan.initialLoadOrder,
                "filesystem and provider insertion order do not affect plan");

    const QString oldHash = first.sha256;
    writeFile(QDir(productA).filePath("odd-name.mib"), mib("ROOT-MIB", {"PRODUCT-ONLY-MIB"}) + "-- changed\n");
    QFile changed(QDir(productA).filePath("odd-name.mib")); changed.setFileTime(QDateTime::currentDateTime().addSecs(2), QFileDevice::FileModificationTime);
    index.update({standards, productA, productB});
    ok &= check(MibEffectivePlanResolver().resolve(automatic, index).sha256 != oldHash, "provider content changes hash");

    QTemporaryDir largeTemp; const QString largeRoot = QDir(largeTemp.path()).filePath("Vendor/Large Product"); QDir().mkpath(largeRoot);
    QStringList explicitLarge;
    for (int i = 0; i < 250; ++i) {
        const QString identity = QStringLiteral("LARGE-%1-MIB").arg(i, 3, 10, QLatin1Char('0'));
        QStringList imports; if (i > 0) imports.append(QStringLiteral("LARGE-%1-MIB").arg(i - 1, 3, 10, QLatin1Char('0')));
        if (i > 2 && i % 17 == 0) imports.append("LARGE-000-MIB");
        writeFile(QDir(largeRoot).filePath(QStringLiteral("file-%1.mib").arg(i)), mib(identity, imports)); explicitLarge.append(identity);
    }
    MibDependencyIndex largeIndex(QDir(largeTemp.path()).filePath("index.json")); largeIndex.update({largeRoot});
    MibProfileRecord largeProfile; largeProfile.id = "large"; largeProfile.name = "Large"; largeProfile.type = MibProfileType::Folder; largeProfile.directory = largeRoot; largeProfile.explicitModules = explicitLarge;
    QElapsedTimer timer; timer.start(); const auto largePlan = MibEffectivePlanResolver().resolve(largeProfile, largeIndex); const qint64 elapsed = timer.elapsed();
    ok &= check(largePlan.explicitModules.size() == 250 && largePlan.effectiveModules.size() == 250 && largePlan.initialLoadOrder.size() == 250 && !largePlan.sha256.isEmpty(), "large profile completeness");
    ok &= check(elapsed < 5000, "large profile performance");
    ok &= check(largePlan.converged && largePlan.convergencePasses == 1,
                "large explicit profile converges in one bounded pass");

    QTemporaryDir deepTemp; const QString deepRoot = QDir(deepTemp.path()).filePath("Unassigned"); QDir().mkpath(deepRoot);
    for (int i = 0; i < 10; ++i) {
        const QString identity = QStringLiteral("DEEP-%1-MIB").arg(i);
        const QStringList imports = i < 9 ? QStringList{QStringLiteral("DEEP-%1-MIB").arg(i + 1)} : QStringList{};
        writeFile(QDir(deepRoot).filePath(QStringLiteral("deep-%1.mib").arg(i)), mib(identity, imports));
    }
    MibDependencyIndex deepIndex(QDir(deepTemp.path()).filePath("index.json")); deepIndex.update({deepRoot});
    MibProfileRecord deep; deep.id = "deep"; deep.explicitModules = {"DEEP-0-MIB"};
    const auto deepPlan = MibEffectivePlanResolver().resolve(deep, deepIndex);
    ok &= check(!deepPlan.converged && deepPlan.convergencePasses == MibEffectivePlan::MaximumConvergencePasses &&
                deepPlan.nonConvergentModules == QStringList{"DEEP-8-MIB"} && !deepPlan.isComplete(),
                "provider/dependency fixed point stops deterministically at pass eight");
    std::cout << "large-plan explicit=" << largePlan.explicitModules.size() << " effective=" << largePlan.effectiveModules.size()
              << " passes=" << largePlan.convergencePasses << " elapsed_ms=" << elapsed << '\n';
    return ok ? 0 : 1;
}
