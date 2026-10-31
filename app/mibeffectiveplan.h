#ifndef MIBEFFECTIVEPLAN_H
#define MIBEFFECTIVEPLAN_H

#include "mibdependencyindex.h"
#include "mibprofile.h"

#include <QList>
#include <QMap>
#include <QStringList>

enum class MibRuntimeCollectionRole { Product, Standards, General, Pib };
enum class MibRuntimeStandardsPolicy { Excluded, Fallback };

struct MibRuntimeCollectionReference {
    QString id;
    MibRuntimeCollectionRole role = MibRuntimeCollectionRole::General;
    QString canonicalRoot;
    bool includesPibs = false;
};

struct MibRuntimeRootAlias {
    QString identity;
    QString collectionId;
    QString canonicalPath;
    QString sha256;
};

// Phase A transition value: immutable runtime instructions.
// It deliberately coexists with the provider-oriented Effective Plan until
// later phases move the loader to Profile-controlled search paths.
class MibProfileRuntimeConfiguration {
public:
    static constexpr int SchemaVersion = 2;

    QString profileId() const { return profileIdValue; }
    MibProfileType profileType() const { return MibProfileType::Custom; }
    QString profileRevisionSha256() const { return profileRevisionSha256Value; }
    const QStringList &explicitRoots() const { return explicitRootsValue; }
    const QList<MibRuntimeCollectionReference> &orderedCollections() const { return noCollections; }
    MibRuntimeStandardsPolicy standardsPolicy() const { return MibRuntimeStandardsPolicy::Excluded; }
    quint64 libraryGeneration() const { return libraryGenerationValue; }
    const QMap<QString, MibRuntimeRootAlias> &rootAliases() const { return rootAliasesValue; }
    const QList<MibProfileMember> &authorizedFiles() const { return authorizedFilesValue; }
    const QMap<QString, QString> &stagedToOriginal() const { return stagedToOriginalValue; }
    QString sha256() const { return sha256Value; }

private:
    friend class MibProfileRuntimeConfigurationBuilder;
    friend class MibRuntimeStage;
    QString profileIdValue;
    QString profileRevisionSha256Value;
    QStringList explicitRootsValue;
    quint64 libraryGenerationValue = 0;
    QMap<QString, MibRuntimeRootAlias> rootAliasesValue;
    QList<MibProfileMember> authorizedFilesValue;
    QMap<QString, QString> stagedToOriginalValue;
    QList<MibRuntimeCollectionReference> noCollections;
    QString sha256Value;
};

class MibProfileRuntimeConfigurationBuilder
{
public:
    MibProfileRuntimeConfiguration build(
        const MibProfileRecord &profile, const MibDependencyIndex &library,
        QList<MibRuntimeCollectionReference> orderedCollections) const;
    static QByteArray canonicalBytes(const MibProfileRuntimeConfiguration &configuration);
};

struct MibRuntimePathEntry {
    QString canonicalPath;
    QString collectionId;
    MibRuntimeCollectionRole collectionRole = MibRuntimeCollectionRole::General;
    bool includesPibs = false;
};

class MibRuntimePathConfiguration {
public:
    static constexpr int SchemaVersion = 1;

    const QList<MibRuntimePathEntry> &entries() const { return entriesValue; }
    QStringList orderedPaths() const;
    bool isValid() const { return diagnosticsValue.isEmpty(); }
    const QStringList &diagnostics() const { return diagnosticsValue; }
    QString sha256() const { return sha256Value; }

private:
    friend class MibRuntimePathConfigurationBuilder;
    friend class MibRuntimeStage;
    QList<MibRuntimePathEntry> entriesValue;
    QStringList diagnosticsValue;
    QString sha256Value;
};

class MibRuntimePathConfigurationBuilder
{
public:
    MibRuntimePathConfiguration derive(
        const MibProfileRuntimeConfiguration &configuration,
        const MibDependencyIndex &library) const;
    static QByteArray canonicalBytes(const MibRuntimePathConfiguration &configuration,
                                     const QString &runtimeConfigurationSha256);
};

enum class MibPlanMembershipReason { Explicit, Dependency, Missing, Ambiguous, PinFailure };
enum class MibPlanProviderReason {
    None, ExplicitPin, SingleProvider, EquivalentProviders, AutomaticProfileFolder,
    GlobalPrecedence, Ambiguous, InvalidPin, ExactProfileMember
};

struct MibEffectivePlanMember {
    QString identity;
    MibPlanMembershipReason membershipReason = MibPlanMembershipReason::Dependency;
    MibPlanProviderReason providerReason = MibPlanProviderReason::None;
    MibProviderPin requestedPin;
    MibIndexedProvider provider;
    QList<MibIndexedProvider> alternatives;
    QStringList imports;
};

enum class MibEffectivePlanFileOrigin { Root, Dependency };
enum class MibPlanResolutionTier { Root, ExplicitPin, RequesterDirectory, RequesterBatch,
                                  OrderedScope, Unresolved, OutOfScope, Ambiguous,
                                  StalePin, ProviderConflict, InvalidRoot,
                                  // Informational: the root file differs from what was recorded
                                  // when it was added; the current file is used.
                                  ModifiedRoot,
                                  // A module the profile lists that the Library cannot supply.
                                  ListedNotFound };

struct MibPlanResolutionDiagnostic {
    QString identity;
    QStringList requesters;
    QString dependencyKind = QStringLiteral("IMPORTS");
    QList<MibIndexedProvider> candidates;
    MibPlanResolutionTier tier = MibPlanResolutionTier::Unresolved;
    QString scope;
    QString reason;
};

struct MibEffectivePlanResolverInput {
    QString id;
    QList<MibProfileMember> roots;
    QStringList orderedScopes;
    QMap<QString, MibProviderPin> pins;
};

// The one place that decides what the resolver is given for a profile: its roots, its folders in
// order, its pins, and then the Library's Standards folder as the last place to look. Vendor MIBs
// import standard ones (SNMPv2-MIB, IF-MIB, ENTITY-MIB, ...), so every profile can reach them
// without carrying its own copies. Both the start-up path and the normal path use this.
MibEffectivePlanResolverInput MibResolverInputForProfile(const MibProfileRecord &profile,
                                                         const QString &libraryRoot);

struct MibEffectivePlanFile {
    QString canonicalPath;
    QString sha256;
    QStringList identities;
    MibEffectivePlanFileOrigin origin = MibEffectivePlanFileOrigin::Root;
    QStringList requiredBy;
    int loadOrder = -1;
    QList<MibRuntimeRootAlias> aliases;
    QStringList diagnostics;
    MibPlanResolutionTier resolutionTier = MibPlanResolutionTier::Root;
    QString resolutionRationale;
};

struct MibEffectivePlan {
    static constexpr int SchemaVersion = 3;
    static constexpr int PolicyVersion = 2;
    static constexpr int RuntimeAuthoritySchemaVersion = 1;
    static constexpr int RuntimeStageSchemaVersion = 1;
    static constexpr int DependencyResolverPolicyVersion = 1;
    static constexpr int MaximumConvergencePasses = 8;

    QString profileId;
    QString profileName;
    MibProfileType profileType = MibProfileType::Custom;
    quint64 libraryGeneration = 0;
    QStringList explicitModules;
    QList<MibEffectivePlanMember> members;
    QStringList effectiveModules;
    QStringList dependencyModules;
    QStringList missingModules;
    QStringList ambiguousModules;
    QStringList pinFailureModules;
    QStringList nonConvergentModules;
    QStringList cycles;
    QStringList initialLoadOrder;
    QList<MibEffectivePlanFile> runtimeFiles;
    QString runtimeAuthoritySha256;
    int resolverPolicyVersion = 0;
    QList<MibPlanResolutionDiagnostic> resolutionDiagnostics;
    QString authorityError;
    int convergencePasses = 0;
    bool converged = true;
    QString sha256;
    MibProfileRuntimeConfiguration runtimeConfiguration;
    MibRuntimePathConfiguration runtimePaths;
    bool hasRuntimePaths = false;
    // The inputs this plan was resolved from, so it can be re-resolved after a
    // MIB file is edited. Not part of the plan hash.
    MibEffectivePlanResolverInput resolverInput;
    bool hasResolverInput = false;

    const MibEffectivePlanMember *member(const QString &identity) const;
    bool isComplete() const { return authorityError.isEmpty() && converged && missingModules.isEmpty() &&
        ambiguousModules.isEmpty() && pinFailureModules.isEmpty(); }
};

// A module the profile lists but the Library cannot supply is skipped and reported;
// it never blocks the rest of the profile from loading.
inline void MibEffectivePlanAddListedNotFound(MibEffectivePlan *plan, const QStringList &identities)
{
    for (const QString &identity : identities) {
        if (!plan->missingModules.contains(identity)) plan->missingModules.append(identity);
        MibPlanResolutionDiagnostic finding;
        finding.identity = identity;
        finding.dependencyKind = QStringLiteral("PROFILE");
        finding.tier = MibPlanResolutionTier::ListedNotFound;
        plan->resolutionDiagnostics.append(finding);
    }
    plan->missingModules.removeDuplicates();
    plan->missingModules.sort(Qt::CaseSensitive);
}

class MibEffectivePlanResolver
{
public:
    MibEffectivePlan resolve(const MibProfileRecord &profile,
                             const MibDependencyIndex &library) const;
    static QByteArray canonicalBytes(const MibEffectivePlan &plan);
    static QByteArray runtimeAuthorityCanonicalBytes(const MibEffectivePlan &plan);
    static void sealRuntimeAuthority(MibEffectivePlan *plan);
};

class MibEffectiveRuntimePlanResolver
{
public:
    MibEffectivePlan resolve(const MibEffectivePlanResolverInput &input,
                             const MibDependencyIndex &library) const;
};

#endif
