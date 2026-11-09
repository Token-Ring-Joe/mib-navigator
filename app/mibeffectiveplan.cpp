#include "mibeffectiveplan.h"

#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QSet>
#include <algorithm>
#include <functional>

namespace {
QStringList sortedUnique(QStringList values)
{
    values.removeAll(QString());
    values.removeDuplicates();
    std::sort(values.begin(), values.end(), [](const QString &a, const QString &b) {
        return QString::compare(a, b, Qt::CaseSensitive) < 0;
    });
    return values;
}

QString canonicalPath(const QString &path)
{
    const QString canonical = QFileInfo(path).canonicalFilePath();
    return QDir::cleanPath(canonical.isEmpty() ? QFileInfo(path).absoluteFilePath() : canonical);
}

bool samePath(const QString &a, const QString &b)
{
#ifdef Q_OS_WIN
    return QString::compare(canonicalPath(a), canonicalPath(b), Qt::CaseInsensitive) == 0;
#else
    return canonicalPath(a) == canonicalPath(b);
#endif
}

bool pathWithin(const QString &path, const QString &directory)
{
    if (directory.isEmpty()) return false;
    const QString candidate = QDir::fromNativeSeparators(canonicalPath(path));
    QString root = QDir::fromNativeSeparators(canonicalPath(directory));
    if (!root.endsWith('/')) root += '/';
#ifdef Q_OS_WIN
    return candidate.startsWith(root, Qt::CaseInsensitive);
#else
    return candidate.startsWith(root, Qt::CaseSensitive);
#endif
}

QJsonArray strings(const QStringList &values)
{
    QJsonArray result; for (const QString &value : values) result.append(value); return result;
}
}

MibProfileRuntimeConfiguration MibProfileRuntimeConfigurationBuilder::build(
    const MibProfileRecord &profile, const MibDependencyIndex &library,
    QList<MibRuntimeCollectionReference> orderedCollections) const
{
    MibProfileRuntimeConfiguration configuration;
    configuration.profileIdValue = profile.id;
    configuration.authorizedFilesValue = profile.members;
    std::sort(configuration.authorizedFilesValue.begin(), configuration.authorizedFilesValue.end(),
        [](const auto &a, const auto &b) {
            return QString::compare(a.canonicalPath, b.canonicalPath, Qt::CaseInsensitive) < 0;
        });
    configuration.explicitRootsValue = MibProfileMemberIdentities(profile.members);
    configuration.libraryGenerationValue = library.generation();

    Q_UNUSED(orderedCollections)
    // Collection roots are retained as a source-compatible input only. Exact
    // Profile members, never directory containment, define runtime authority.

    if (!configuration.authorizedFilesValue.isEmpty()) {
        for (const auto &member : std::as_const(configuration.authorizedFilesValue)) {
            const QString providerPath = canonicalPath(member.canonicalPath);
            for (const QString &identity : member.identities)
                if (!configuration.rootAliasesValue.contains(identity))
                    configuration.rootAliasesValue.insert(identity, {identity, QString(),
                        providerPath, member.sha256});
        }
    }

    {
        QMap<QString, QStringList> selectedImports;
        for (const QString &identity : std::as_const(configuration.explicitRootsValue)) {
            const auto alias = configuration.rootAliasesValue.constFind(identity);
            if (alias == configuration.rootAliasesValue.cend()) continue;
            for (const auto &provider : library.providersFor(identity)) {
                if (samePath(provider.canonicalPath, alias->canonicalPath) &&
                    provider.sha256 == alias->sha256) {
                    selectedImports.insert(identity, sortedUnique(provider.imports));
                    break;
                }
            }
        }
        const QSet<QString> authorized(configuration.explicitRootsValue.cbegin(),
                                       configuration.explicitRootsValue.cend());
        QSet<QString> visited;
        QSet<QString> active;
        QStringList dependencyFirst;
        std::function<void(const QString &)> visit = [&](const QString &identity) {
            if (visited.contains(identity) || active.contains(identity)) return;
            active.insert(identity);
            for (const QString &dependency : selectedImports.value(identity))
                if (authorized.contains(dependency)) visit(dependency);
            active.remove(identity);
            visited.insert(identity);
            dependencyFirst.append(identity);
        };
        for (const QString &identity : std::as_const(configuration.explicitRootsValue)) visit(identity);
        configuration.explicitRootsValue = dependencyFirst;
    }

    QJsonObject revision;
    revision.insert(QStringLiteral("id"), profile.id);
    revision.insert(QStringLiteral("roots"), strings(configuration.explicitRootsValue));
    configuration.profileRevisionSha256Value = QString::fromLatin1(QCryptographicHash::hash(
        QJsonDocument(revision).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex());
    configuration.sha256Value = QString::fromLatin1(QCryptographicHash::hash(
        canonicalBytes(configuration), QCryptographicHash::Sha256).toHex());
    return configuration;
}

QByteArray MibProfileRuntimeConfigurationBuilder::canonicalBytes(
    const MibProfileRuntimeConfiguration &configuration)
{
    QJsonObject root;
    root.insert(QStringLiteral("schema"), MibProfileRuntimeConfiguration::SchemaVersion);
    root.insert(QStringLiteral("profileId"), configuration.profileIdValue);
    root.insert(QStringLiteral("profileRevision"), configuration.profileRevisionSha256Value);
    root.insert(QStringLiteral("roots"), strings(configuration.explicitRootsValue));
    root.insert(QStringLiteral("libraryGeneration"), QString::number(configuration.libraryGenerationValue));
    QJsonArray aliases;
    for (auto it = configuration.rootAliasesValue.cbegin(); it != configuration.rootAliasesValue.cend(); ++it) {
        const auto &alias = it.value();
        aliases.append(QJsonObject{{QStringLiteral("identity"), alias.identity},
            {QStringLiteral("collection"), alias.collectionId},
            {QStringLiteral("path"), QDir::fromNativeSeparators(alias.canonicalPath)},
            {QStringLiteral("hash"), alias.sha256}});
    }
    root.insert(QStringLiteral("aliases"), aliases);
    QJsonArray files;
    for (const auto &member : configuration.authorizedFilesValue)
        files.append(QJsonObject{{QStringLiteral("path"), QDir::fromNativeSeparators(member.canonicalPath)},
            {QStringLiteral("hash"), member.sha256},
            {QStringLiteral("identities"), strings(sortedUnique(member.identities))},
            {QStringLiteral("reason"), static_cast<int>(member.reason)}});
    root.insert(QStringLiteral("authorizedFiles"), files);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

QStringList MibRuntimePathConfiguration::orderedPaths() const
{
    QStringList result;
    for (const auto &entry : entriesValue) result.append(entry.canonicalPath);
    return result;
}

MibRuntimePathConfiguration MibRuntimePathConfigurationBuilder::derive(
    const MibProfileRuntimeConfiguration &configuration,
    const MibDependencyIndex &library) const
{
    MibRuntimePathConfiguration result;
    Q_UNUSED(library)
    QSet<QString> seenPaths;
    const auto pathKey = [](const QString &path) {
#ifdef Q_OS_WIN
        return QDir::fromNativeSeparators(path).toLower();
#else
        return QDir::fromNativeSeparators(path);
#endif
    };
    const auto append = [&result, &seenPaths, &pathKey](const QString &path) {
        const QString canonical = canonicalPath(path);
        const QString key = pathKey(canonical);
        if (seenPaths.contains(key)) return;
        seenPaths.insert(key);
        result.entriesValue.append({canonical, QString(), MibRuntimeCollectionRole::General, false});
    };
    for (const auto &member : configuration.authorizedFiles()) {
        const QFileInfo file(member.canonicalPath);
        if (!file.exists() || !file.isFile() || !file.isReadable()) {
            result.diagnosticsValue.append(QObject::tr(
                "Authorized MIB file is not readable: %1").arg(member.canonicalPath));
            continue;
        }
        append(file.absolutePath());
    }

    for (auto it = configuration.rootAliases().cbegin();
         it != configuration.rootAliases().cend(); ++it) {
        const MibRuntimeRootAlias &alias = it.value();
        const QString aliasDirectory = canonicalPath(QFileInfo(alias.canonicalPath).absolutePath());
        if (!seenPaths.contains(pathKey(aliasDirectory)))
            result.diagnosticsValue.append(QObject::tr(
                "Root alias for %1 is outside the derived runtime paths").arg(alias.identity));
    }
    result.diagnosticsValue.removeDuplicates();
    result.sha256Value = QString::fromLatin1(QCryptographicHash::hash(
        canonicalBytes(result, configuration.sha256()), QCryptographicHash::Sha256).toHex());
    return result;
}

QByteArray MibRuntimePathConfigurationBuilder::canonicalBytes(
    const MibRuntimePathConfiguration &configuration,
    const QString &runtimeConfigurationSha256)
{
    QJsonObject root{{QStringLiteral("schema"), MibRuntimePathConfiguration::SchemaVersion},
                     {QStringLiteral("runtimeConfiguration"), runtimeConfigurationSha256}};
    QJsonArray paths;
    for (const auto &entry : configuration.entriesValue)
        paths.append(QJsonObject{{QStringLiteral("path"), QDir::fromNativeSeparators(entry.canonicalPath)},
            {QStringLiteral("collection"), entry.collectionId},
            {QStringLiteral("role"), static_cast<int>(entry.collectionRole)},
            {QStringLiteral("pibs"), entry.includesPibs}});
    root.insert(QStringLiteral("paths"), paths);
    root.insert(QStringLiteral("diagnostics"), strings(configuration.diagnosticsValue));
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

const MibEffectivePlanMember *MibEffectivePlan::member(const QString &identity) const
{
    for (const auto &item : members) if (item.identity == identity) return &item;
    return nullptr;
}

MibEffectivePlan MibEffectivePlanResolver::resolve(const MibProfileRecord &profile,
                                                    const MibDependencyIndex &library) const
{
    MibEffectivePlan plan;
    plan.profileId = profile.id;
    plan.profileName = profile.name;
    plan.profileType = profile.type;
    plan.libraryGeneration = library.generation();
    const bool exactProfile = MibProfileIsManifest(profile) || !profile.members.isEmpty() ||
                              !profile.unresolvedLegacyModules.isEmpty();
    if (!exactProfile) {
        plan.authorityError = QObject::tr(
            "Profile has legacy identity-only membership and must be migrated before use");
        plan.sha256 = QString::fromLatin1(QCryptographicHash::hash(
            canonicalBytes(plan), QCryptographicHash::Sha256).toHex());
        return plan;
    }
    plan.explicitModules = exactProfile ? MibProfileMemberIdentities(profile.members)
        : profile.type == MibProfileType::Standards
            ? sortedUnique(MibProfileDefinitions::standardsModules())
            : sortedUnique(profile.explicitModules);
    const QSet<QString> explicitSet(plan.explicitModules.cbegin(), plan.explicitModules.cend());
    QMap<QString, MibEffectivePlanMember> resolved;
    QStringList pending = plan.explicitModules;
    if (exactProfile) plan.missingModules.append(profile.unresolvedLegacyModules);
    if (!exactProfile && (profile.type == MibProfileType::Custom || profile.type == MibProfileType::Folder) &&
        profile.includeStandardBase)
        pending = sortedUnique(pending + MibProfileDefinitions::standardsModules());
    while (!pending.isEmpty() && plan.convergencePasses < MibEffectivePlan::MaximumConvergencePasses) {
        ++plan.convergencePasses;
        const QStringList pass = sortedUnique(pending);
        pending.clear();
        for (const QString &identity : pass) {
        if (resolved.contains(identity)) continue;
        MibEffectivePlanMember item;
        item.identity = identity;
        item.membershipReason = explicitSet.contains(identity)
            ? MibPlanMembershipReason::Explicit : MibPlanMembershipReason::Dependency;
        QList<MibIndexedProvider> choices = library.providersFor(identity);
        QList<MibIndexedProvider> selected;
        for (const auto &member : profile.members) {
            if (!member.identities.contains(identity)) continue;
            for (const auto &choice : choices)
                if (samePath(choice.canonicalPath, member.canonicalPath) &&
                    choice.sha256 == member.sha256) selected.append(choice);
        }
        choices = selected;
        if (choices.isEmpty()) {
            item.membershipReason = MibPlanMembershipReason::Missing;
            plan.missingModules.append(identity);
        } else {
            item.providerReason = MibPlanProviderReason::ExactProfileMember;
            if (choices.size() > 1) {
                item.membershipReason = MibPlanMembershipReason::Ambiguous;
                item.providerReason = MibPlanProviderReason::Ambiguous;
                plan.ambiguousModules.append(identity);
            } else {
                item.provider = choices.first();
                item.imports = sortedUnique(item.provider.imports);
                for (const QString &dependency : item.imports)
                    if (!explicitSet.contains(dependency)) plan.missingModules.append(dependency);
            }
        }
        resolved.insert(identity, item);
        }
        pending = sortedUnique(pending);
        for (auto it = pending.begin(); it != pending.end(); ) {
            if (resolved.contains(*it)) it = pending.erase(it);
            else ++it;
        }
    }
    pending = sortedUnique(pending);
    if (!pending.isEmpty()) {
        plan.converged = false;
        plan.nonConvergentModules = pending;
    }

    plan.members = resolved.values();
    for (const auto &item : plan.members) {
        if (item.membershipReason == MibPlanMembershipReason::Explicit ||
            item.membershipReason == MibPlanMembershipReason::Dependency) {
            plan.effectiveModules.append(item.identity);
            if (item.membershipReason == MibPlanMembershipReason::Dependency)
                plan.dependencyModules.append(item.identity);
        }
    }
    plan.effectiveModules = sortedUnique(plan.effectiveModules);
    plan.dependencyModules = sortedUnique(plan.dependencyModules);
    plan.missingModules = sortedUnique(plan.missingModules);
    plan.ambiguousModules = sortedUnique(plan.ambiguousModules);
    plan.pinFailureModules = sortedUnique(plan.pinFailureModules);

    QSet<QString> visited, active;
    std::function<void(const QString &, QStringList)> visit = [&](const QString &identity, QStringList stack) {
        if (visited.contains(identity)) return;
        if (active.contains(identity)) {
            const int start = stack.indexOf(identity);
            QStringList cycle = start >= 0 ? stack.mid(start) : stack;
            cycle.append(identity);
            plan.cycles.append(cycle.join(QStringLiteral(" -> ")));
            return;
        }
        active.insert(identity); stack.append(identity);
        const auto *item = plan.member(identity);
        if (item) for (const QString &dependency : item->imports) visit(dependency, stack);
        active.remove(identity); visited.insert(identity);
        if (item && (item->membershipReason == MibPlanMembershipReason::Explicit ||
                     item->membershipReason == MibPlanMembershipReason::Dependency))
            plan.initialLoadOrder.append(identity);
    };
    for (const QString &root : plan.explicitModules) visit(root, {});
    for (const QString &identity : plan.effectiveModules) visit(identity, {});
    plan.cycles = sortedUnique(plan.cycles);
    plan.initialLoadOrder.removeDuplicates();

    QList<MibProfileMember> profileFiles = profile.members;
    std::sort(profileFiles.begin(), profileFiles.end(), [](const auto &a, const auto &b) {
        const QString left = canonicalPath(a.canonicalPath), right = canonicalPath(b.canonicalPath);
        const int insensitive = QString::compare(left, right, Qt::CaseInsensitive);
        return insensitive != 0 ? insensitive < 0
                                : QString::compare(left, right, Qt::CaseSensitive) < 0;
    });
    for (const auto &profileFile : std::as_const(profileFiles)) {
        MibEffectivePlanFile file;
        file.canonicalPath = canonicalPath(profileFile.canonicalPath);
        file.sha256 = profileFile.sha256;
        file.identities = sortedUnique(profileFile.identities);
        file.origin = profileFile.reason == MibProfileMemberReason::Dependency
            ? MibEffectivePlanFileOrigin::Dependency : MibEffectivePlanFileOrigin::Root;
        for (const QString &identity : std::as_const(file.identities)) {
            const int position = plan.initialLoadOrder.indexOf(identity);
            if (position >= 0 && (file.loadOrder < 0 || position < file.loadOrder))
                file.loadOrder = position;
            file.aliases.append({identity, QString(), file.canonicalPath, file.sha256});
            for (const auto &candidate : std::as_const(plan.members))
                if (candidate.imports.contains(identity)) file.requiredBy.append(candidate.identity);
        }
        file.requiredBy = sortedUnique(file.requiredBy);
        if (!QFileInfo(file.canonicalPath).isFile())
            file.diagnostics.append(QObject::tr("Planned MIB file is not readable: %1")
                                        .arg(file.canonicalPath));
        plan.runtimeFiles.append(file);
    }
    plan.sha256 = QString::fromLatin1(QCryptographicHash::hash(
        canonicalBytes(plan), QCryptographicHash::Sha256).toHex());
    return plan;
}

QByteArray MibEffectivePlanResolver::canonicalBytes(const MibEffectivePlan &plan)
{
    QJsonObject root;
    root.insert(QStringLiteral("schema"), MibEffectivePlan::SchemaVersion);
    root.insert(QStringLiteral("policy"), MibEffectivePlan::PolicyVersion);
    root.insert(QStringLiteral("profileType"), static_cast<int>(plan.profileType));
    if (plan.resolverPolicyVersion == 0)
        root.insert(QStringLiteral("libraryGeneration"), QString::number(plan.libraryGeneration));
    root.insert(QStringLiteral("explicit"), strings(plan.explicitModules));
    root.insert(QStringLiteral("converged"), plan.converged);
    root.insert(QStringLiteral("authorityError"), plan.authorityError);
    root.insert(QStringLiteral("convergencePasses"), plan.convergencePasses);
    root.insert(QStringLiteral("resolverPolicy"), plan.resolverPolicyVersion);
    root.insert(QStringLiteral("nonConvergent"), strings(plan.nonConvergentModules));
    QJsonArray members;
    for (const auto &item : plan.members) {
        QJsonObject value;
        value.insert(QStringLiteral("identity"), item.identity);
        value.insert(QStringLiteral("membership"), static_cast<int>(item.membershipReason));
        value.insert(QStringLiteral("providerReason"), static_cast<int>(item.providerReason));
        value.insert(QStringLiteral("pinPath"), QDir::fromNativeSeparators(item.requestedPin.canonicalPath));
        value.insert(QStringLiteral("pinHash"), item.requestedPin.sha256);
        value.insert(QStringLiteral("path"), QDir::fromNativeSeparators(item.provider.canonicalPath));
        value.insert(QStringLiteral("hash"), item.provider.sha256);
        value.insert(QStringLiteral("origin"), item.provider.origin);
        value.insert(QStringLiteral("precedence"), item.provider.searchPathPrecedence);
        value.insert(QStringLiteral("imports"), strings(item.imports));
        QJsonArray alternatives;
        for (const auto &provider : item.alternatives) {
            QJsonObject alternative;
            alternative.insert(QStringLiteral("path"), QDir::fromNativeSeparators(provider.canonicalPath));
            alternative.insert(QStringLiteral("hash"), provider.sha256);
            alternative.insert(QStringLiteral("origin"), provider.origin);
            alternative.insert(QStringLiteral("precedence"), provider.searchPathPrecedence);
            alternative.insert(QStringLiteral("imports"), strings(sortedUnique(provider.imports)));
            alternatives.append(alternative);
        }
        value.insert(QStringLiteral("alternatives"), alternatives);
        members.append(value);
    }
    root.insert(QStringLiteral("members"), members);
    root.insert(QStringLiteral("order"), strings(plan.initialLoadOrder));
    root.insert(QStringLiteral("cycles"), strings(plan.cycles));
    root.insert(QStringLiteral("runtimeAuthority"), plan.runtimeAuthoritySha256);
    root.insert(QStringLiteral("runtimeFiles"),
                QJsonDocument::fromJson(runtimeAuthorityCanonicalBytes(plan)).object()
                    .value(QStringLiteral("files")));
    if (plan.hasRuntimePaths && plan.resolverPolicyVersion == 0) {
        root.insert(QStringLiteral("runtimeConfiguration"), plan.runtimeConfiguration.sha256());
        root.insert(QStringLiteral("runtimePaths"), plan.runtimePaths.sha256());
    }
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

QByteArray MibEffectivePlanResolver::runtimeAuthorityCanonicalBytes(const MibEffectivePlan &plan)
{
    QJsonObject root{{QStringLiteral("schema"), MibEffectivePlan::RuntimeAuthoritySchemaVersion},
                     {QStringLiteral("stageSchema"), MibEffectivePlan::RuntimeStageSchemaVersion},
                     {QStringLiteral("resolverPolicy"), plan.resolverPolicyVersion}};
    if (plan.resolverPolicyVersion == 0) {
        root.insert(QStringLiteral("runtimeConfiguration"), plan.runtimeConfiguration.sha256());
        root.insert(QStringLiteral("runtimePaths"), plan.runtimePaths.sha256());
    } else {
        root.insert(QStringLiteral("runtimeConfigurationSchema"),
                    MibProfileRuntimeConfiguration::SchemaVersion);
        root.insert(QStringLiteral("runtimePathSchema"), MibRuntimePathConfiguration::SchemaVersion);
        root.insert(QStringLiteral("roots"), strings(plan.runtimeConfiguration.explicitRoots()));
    }
    QJsonArray files;
    for (const auto &file : plan.runtimeFiles) {
        QJsonArray aliases;
        for (const auto &alias : file.aliases)
            aliases.append(QJsonObject{{QStringLiteral("identity"), alias.identity},
                {QStringLiteral("path"), QDir::fromNativeSeparators(alias.canonicalPath)},
                {QStringLiteral("hash"), alias.sha256}});
        files.append(QJsonObject{{QStringLiteral("path"), QDir::fromNativeSeparators(file.canonicalPath)},
            {QStringLiteral("hash"), file.sha256},
            {QStringLiteral("identities"), strings(file.identities)},
            {QStringLiteral("origin"), static_cast<int>(file.origin)},
            {QStringLiteral("requiredBy"), strings(file.requiredBy)},
            {QStringLiteral("loadOrder"), file.loadOrder},
            {QStringLiteral("aliases"), aliases},
            {QStringLiteral("diagnostics"), strings(file.diagnostics)},
            {QStringLiteral("resolutionTier"), static_cast<int>(file.resolutionTier)},
            {QStringLiteral("resolutionRationale"), file.resolutionRationale}});
    }
    root.insert(QStringLiteral("files"), files);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

void MibEffectivePlanResolver::sealRuntimeAuthority(MibEffectivePlan *plan)
{
    if (!plan) return;
    plan->runtimeAuthoritySha256 = QString::fromLatin1(QCryptographicHash::hash(
        runtimeAuthorityCanonicalBytes(*plan), QCryptographicHash::Sha256).toHex());
    plan->sha256 = QString::fromLatin1(QCryptographicHash::hash(
        canonicalBytes(*plan), QCryptographicHash::Sha256).toHex());
}

MibEffectivePlan MibEffectiveRuntimePlanResolver::resolve(
    const MibEffectivePlanResolverInput &input, const MibDependencyIndex &library) const
{
    struct Selection {
        MibProfileMember member;
        MibPlanResolutionTier tier = MibPlanResolutionTier::Root;
        QString rationale;
    };
    const auto key = [](const QString &path) {
#ifdef Q_OS_WIN
        return QDir::fromNativeSeparators(canonicalPath(path)).toLower();
#else
        return QDir::fromNativeSeparators(canonicalPath(path));
#endif
    };
    const auto fileHash = [](const QString &path) {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? QString::fromLatin1(QCryptographicHash::hash(
            file.readAll(), QCryptographicHash::Sha256).toHex()) : QString();
    };
    QStringList scopes;
    for (const QString &scope : input.orderedScopes) {
        const QString value = canonicalPath(scope);
        if (!value.isEmpty() && !scopes.contains(value)) scopes.append(value);
    }
    QList<MibDependencyFileRecord> records = library.files();
    std::sort(records.begin(), records.end(), [&key](const auto &a, const auto &b) {
        return key(a.canonicalPath) < key(b.canonicalPath);
    });
    const auto recordFor = [&records, &key](const QString &path, const QString &hash)
        -> const MibDependencyFileRecord * {
        const QString wanted = key(path);
        for (const auto &record : records)
            if (key(record.canonicalPath) == wanted && record.sha256 == hash) return &record;
        return nullptr;
    };

    QMap<QString, Selection> selected;
    QList<MibPlanResolutionDiagnostic> diagnostics;
    for (MibProfileMember root : input.roots) {
        root.canonicalPath = canonicalPath(root.canonicalPath);
        root.identities = sortedUnique(root.identities);
        root.reason = MibProfileMemberReason::Added;
        const QString actualHash = fileHash(root.canonicalPath);
        // A root whose content differs from what was recorded is still loaded: the
        // current file wins and the plan reports it as modified. The recorded hash
        // is only a way to notice the difference, never a reason to refuse the file.
        const bool modified = !actualHash.isEmpty() && actualHash != root.sha256;
        const auto *record = actualHash.isEmpty() ? nullptr : recordFor(root.canonicalPath, actualHash);
        QString failure;
        if (actualHash.isEmpty()) failure = QObject::tr("The file is missing or cannot be read");
        else if (!record) failure = modified
            ? QObject::tr("The file changed since the Library was last scanned; refresh the Library")
            : QObject::tr("The file is not in the Library snapshot");
        else for (const QString &identity : std::as_const(root.identities))
            if (!record->importsByModule.contains(identity)) {
                failure = QObject::tr("The file no longer declares %1").arg(identity); break;
            }
        if (!failure.isEmpty()) {
            const QStringList identities = root.identities.isEmpty()
                ? QStringList{QFileInfo(root.canonicalPath).fileName()} : root.identities;
            for (const QString &identity : identities)
                diagnostics.append({identity, {}, QStringLiteral("ROOT"), {},
                    MibPlanResolutionTier::InvalidRoot, root.canonicalPath, failure});
            continue;
        }
        root.identities = sortedUnique(record->importsByModule.keys());
        root.sha256 = actualHash;
        if (modified)
            for (const QString &identity : std::as_const(root.identities))
                diagnostics.append({identity, {}, QStringLiteral("ROOT"), {},
                    MibPlanResolutionTier::ModifiedRoot, root.canonicalPath,
                    QObject::tr("Modified since it was added to this profile; the current file is used")});
        selected.insert(key(root.canonicalPath), {root, MibPlanResolutionTier::Root,
            modified ? QObject::tr("User-selected root (modified since it was added)")
                     : QObject::tr("Exact user-selected root")});
    }

    // Several selected files can declare the same module: different revisions of a standard MIB
    // sitting side by side in a vendor folder are common. One module must have one provider in
    // the environment, so keep the first file by path, set the others aside for that module, and
    // report it. This must never stop the profile from loading.
    {
        QMap<QString, QString> keptBy;   // module -> key of the file that provides it
        const QStringList order = selected.keys();
        for (const QString &fileKey : order) {
            auto it = selected.find(fileKey);
            QStringList kept, setAside;
            for (const QString &identity : std::as_const(it->member.identities)) {
                if (keptBy.contains(identity)) setAside << identity;
                else { keptBy.insert(identity, fileKey); kept << identity; }
            }
            if (setAside.isEmpty()) continue;
            for (const QString &identity : std::as_const(setAside)) {
                const QString keptPath = selected.value(keptBy.value(identity)).member.canonicalPath;
                QList<MibIndexedProvider> candidates;
                for (const auto &provider : library.providersFor(identity))
                    if (samePath(provider.canonicalPath, keptPath) || samePath(provider.canonicalPath, it->member.canonicalPath))
                        candidates.append(provider);
                diagnostics.append({identity, {}, QStringLiteral("ROOT"), candidates,
                    MibPlanResolutionTier::Ambiguous, keptPath,
                    QObject::tr("Several selected files declare this module; %1 was used because it comes first "
                                "by path, and %2 was set aside for this module")
                        .arg(keptPath, it->member.canonicalPath)});
            }
            if (kept.isEmpty()) selected.remove(fileKey);
            else it->member.identities = kept;
        }
    }

    bool progress = true;
    while (progress) {
        progress = false;
        QMap<QString, QString> identityOwner;
        QMap<QString, QStringList> requiredBy;
        for (auto it = selected.cbegin(); it != selected.cend(); ++it) {
            for (const QString &identity : it->member.identities) identityOwner.insert(identity, it.key());
            if (const auto *record = recordFor(it->member.canonicalPath, it->member.sha256))
                for (auto declaration = record->importsByModule.cbegin();
                     declaration != record->importsByModule.cend(); ++declaration)
                    for (const QString &dependency : declaration.value())
                        if (!identityOwner.contains(dependency))
                            requiredBy[dependency].append(declaration.key());
        }
        QStringList frontier = requiredBy.keys();
        frontier = sortedUnique(frontier);
        for (const QString &identity : std::as_const(frontier)) {
            if (identityOwner.contains(identity)) continue;
            QStringList requesters = sortedUnique(requiredBy.value(identity));
            QList<MibIndexedProvider> all = library.providersFor(identity);
            QList<MibIndexedProvider> winning;
            MibPlanResolutionTier tier = MibPlanResolutionTier::Unresolved;
            QString winningScope;
            const auto eligible = [&scopes](const MibIndexedProvider &provider) {
                return std::any_of(scopes.cbegin(), scopes.cend(), [&provider](const QString &scope) {
                    return pathWithin(provider.canonicalPath, scope);
                });
            };
            bool pinHonoured = false;
            if (const auto pin = input.pins.constFind(identity); pin != input.pins.cend()) {
                tier = MibPlanResolutionTier::ExplicitPin;
                for (const auto &provider : std::as_const(all))
                    if (samePath(provider.canonicalPath, pin->canonicalPath) &&
                        provider.sha256 == pin->sha256 && eligible(provider) &&
                        fileHash(provider.canonicalPath) == pin->sha256) winning.append(provider);
                if (winning.isEmpty()) {
                    // The pinned file is gone or changed. Do not give up on the module:
                    // report the stale pin and fall back to normal provider selection.
                    diagnostics.append({identity, requesters, QStringLiteral("IMPORTS"), all,
                        MibPlanResolutionTier::StalePin, {},
                        QObject::tr("The pinned provider is missing, changed, or out of scope; "
                                    "normal provider selection was used instead")});
                    tier = MibPlanResolutionTier::Unresolved;
                } else pinHonoured = true;
            }
            if (!pinHonoured) {
                QStringList requesterDirectories;
                for (const QString &requester : std::as_const(requesters)) {
                    const auto owner = selected.constFind(identityOwner.value(requester));
                    if (owner == selected.cend()) continue;
                    const QString directory = QFileInfo(owner->member.canonicalPath).absolutePath();
                    if (!requesterDirectories.contains(directory)) requesterDirectories.append(directory);
                    for (const auto &provider : std::as_const(all))
                        if (eligible(provider) && samePath(QFileInfo(provider.canonicalPath).absolutePath(), directory))
                            winning.append(provider);
                }
                if (!winning.isEmpty()) {
                    tier = MibPlanResolutionTier::RequesterDirectory;
                    winningScope = requesterDirectories.join(QStringLiteral(";"));
                }
                if (winning.isEmpty()) {
                    QStringList requesterScopes;
                    for (const QString &requester : std::as_const(requesters)) {
                        const auto owner = selected.constFind(identityOwner.value(requester));
                        if (owner == selected.cend()) continue;
                        QString best;
                        for (const QString &scope : std::as_const(scopes))
                            if (pathWithin(owner->member.canonicalPath, scope) && scope.size() > best.size()) best = scope;
                        if (!best.isEmpty() && !requesterScopes.contains(best)) requesterScopes.append(best);
                    }
                    for (const auto &provider : std::as_const(all))
                        if (std::any_of(requesterScopes.cbegin(), requesterScopes.cend(),
                            [&provider](const QString &scope) { return pathWithin(provider.canonicalPath, scope); }))
                            winning.append(provider);
                    if (!winning.isEmpty()) {
                        tier = MibPlanResolutionTier::RequesterBatch;
                        winningScope = requesterScopes.join(QStringLiteral(";"));
                    }
                }
                if (winning.isEmpty()) for (const QString &scope : std::as_const(scopes)) {
                    for (const auto &provider : std::as_const(all))
                        if (pathWithin(provider.canonicalPath, scope)) winning.append(provider);
                    if (!winning.isEmpty()) {
                        tier = MibPlanResolutionTier::OrderedScope; winningScope = scope; break;
                    }
                }
            }
            std::sort(winning.begin(), winning.end(), [&key](const auto &a, const auto &b) {
                return key(a.canonicalPath) < key(b.canonicalPath);
            });
            winning.erase(std::unique(winning.begin(), winning.end(), [&key](const auto &a, const auto &b) {
                return key(a.canonicalPath) == key(b.canonicalPath);
            }), winning.end());
            if (winning.isEmpty()) {
                diagnostics.append({identity, requesters, QStringLiteral("IMPORTS"), all,
                    all.isEmpty() ? MibPlanResolutionTier::Unresolved : MibPlanResolutionTier::OutOfScope,
                    {}, all.isEmpty() ? QObject::tr("No Catalog provider exists")
                                      : QObject::tr("Providers exist only outside ordered scope")});
                continue;
            }
            QSet<QString> hashes;
            for (const auto &provider : std::as_const(winning)) hashes.insert(provider.sha256);
            MibIndexedProvider chosen = winning.first();
            if (hashes.size() != 1 || hashes.contains(QString())) {
                // Several different files declare this module. Do not refuse it: use the
                // first readable one by path (a stable rule) and say so in the report.
                const auto usable = std::find_if(winning.cbegin(), winning.cend(),
                    [](const auto &provider) { return !provider.sha256.isEmpty(); });
                diagnostics.append({identity, requesters, QStringLiteral("IMPORTS"), winning,
                    MibPlanResolutionTier::Ambiguous, winningScope,
                    usable == winning.cend()
                        ? QObject::tr("Several files declare this module and none can be read")
                        : QObject::tr("Several different files declare this module; %1 was used "
                                      "because it comes first by path").arg(usable->canonicalPath)});
                if (usable == winning.cend()) continue;
                chosen = *usable;
            }
            const auto *record = recordFor(chosen.canonicalPath, chosen.sha256);
            if (!record) continue;
            const QStringList identities = sortedUnique(record->importsByModule.keys());
            QString conflictIdentity;
            for (const QString &provided : identities) {
                const auto owner = selected.constFind(identityOwner.value(provided));
                if (owner != selected.cend() &&
                    !samePath(owner->member.canonicalPath, chosen.canonicalPath)) {
                    conflictIdentity = provided; break;
                }
            }
            if (!conflictIdentity.isEmpty()) {
                diagnostics.append({identity, requesters, QStringLiteral("IMPORTS"), winning,
                    MibPlanResolutionTier::ProviderConflict, winningScope,
                    QObject::tr("Selected multi-identity file conflicts on %1").arg(conflictIdentity)});
                continue;
            }
            MibProfileMember dependency{canonicalPath(chosen.canonicalPath), chosen.sha256,
                                        identities, MibProfileMemberReason::Dependency};
            QString rationale = tier == MibPlanResolutionTier::ExplicitPin ? QObject::tr("Explicit pin")
                : tier == MibPlanResolutionTier::RequesterDirectory
                    ? QObject::tr("Requester directory %1").arg(winningScope)
                : tier == MibPlanResolutionTier::RequesterBatch ? QObject::tr("Requester batch %1").arg(winningScope)
                : QObject::tr("Ordered scope %1").arg(winningScope);
            if (winning.size() > 1)
                rationale += QObject::tr("; %1 byte-identical alternatives; chose canonical path first")
                    .arg(winning.size());
            selected.insert(key(dependency.canonicalPath), {dependency, tier, rationale});
            progress = true;
        }
    }

    MibProfileRecord manifest;
    manifest.manifestVersion = 1;
    manifest.id = input.id.isEmpty() ? QStringLiteral("resolver-v1") : input.id;
    manifest.name = QStringLiteral("Resolved runtime plan");
    manifest.type = MibProfileType::Custom;
    for (auto it = selected.cbegin(); it != selected.cend(); ++it) manifest.members.append(it->member);
    MibEffectivePlan plan = MibEffectivePlanResolver().resolve(manifest, library);
    plan.resolverPolicyVersion = MibEffectivePlan::DependencyResolverPolicyVersion;
    QSet<QString> provided;
    QSet<QString> rootIdentities;
    for (const auto &selection : std::as_const(selected)) {
        for (const QString &identity : selection.member.identities) provided.insert(identity);
        if (selection.member.reason == MibProfileMemberReason::Added)
            for (const QString &identity : selection.member.identities) rootIdentities.insert(identity);
    }
    QMap<QString, MibPlanResolutionDiagnostic> finalByIdentity;
    for (const auto &diagnostic : std::as_const(diagnostics)) {
        const bool reportEvenWhenProvided = diagnostic.tier == MibPlanResolutionTier::Ambiguous ||
            diagnostic.tier == MibPlanResolutionTier::StalePin;
        if (diagnostic.dependencyKind != QStringLiteral("ROOT") && provided.contains(diagnostic.identity) &&
            !reportEvenWhenProvided)
            continue;
        auto existing = finalByIdentity.find(diagnostic.identity);
        if (existing == finalByIdentity.end()) finalByIdentity.insert(diagnostic.identity, diagnostic);
        else existing->requesters = sortedUnique(existing->requesters + diagnostic.requesters);
    }
    const QList<MibPlanResolutionDiagnostic> finalDiagnostics = finalByIdentity.values();
    plan.resolutionDiagnostics = finalDiagnostics;
    plan.missingModules.clear(); plan.ambiguousModules.clear(); plan.pinFailureModules.clear();
    for (const auto &diagnostic : std::as_const(finalDiagnostics)) {
        if (diagnostic.tier == MibPlanResolutionTier::ModifiedRoot) continue;  // informational only
        if (diagnostic.tier == MibPlanResolutionTier::Ambiguous ||
            diagnostic.tier == MibPlanResolutionTier::ProviderConflict)
            plan.ambiguousModules.append(diagnostic.identity);
        else if (diagnostic.tier == MibPlanResolutionTier::StalePin)
            plan.pinFailureModules.append(diagnostic.identity);
        else plan.missingModules.append(diagnostic.identity);
    }
    plan.missingModules = sortedUnique(plan.missingModules);
    plan.ambiguousModules = sortedUnique(plan.ambiguousModules);
    plan.pinFailureModules = sortedUnique(plan.pinFailureModules);
    plan.dependencyModules.clear();
    for (auto &member : plan.members) {
        member.membershipReason = rootIdentities.contains(member.identity)
            ? MibPlanMembershipReason::Explicit : MibPlanMembershipReason::Dependency;
        if (member.membershipReason == MibPlanMembershipReason::Dependency)
            plan.dependencyModules.append(member.identity);
    }
    plan.dependencyModules = sortedUnique(plan.dependencyModules);
    for (auto &file : plan.runtimeFiles) {
        const auto selection = selected.constFind(key(file.canonicalPath));
        if (selection != selected.cend()) {
            file.resolutionTier = selection->tier;
            file.resolutionRationale = selection->rationale;
        }
    }
    plan.runtimeConfiguration = MibProfileRuntimeConfigurationBuilder().build(manifest, library, {});
    plan.runtimePaths = MibRuntimePathConfigurationBuilder().derive(plan.runtimeConfiguration, library);
    plan.hasRuntimePaths = true;
    plan.resolverInput = input;
    plan.hasResolverInput = true;
    MibEffectivePlanResolver::sealRuntimeAuthority(&plan);
    return plan;
}

MibDependencyCheckResult MibBoundedDependencyLoader::load(
    const MibEffectivePlan &plan, const LoadFile &loadFile) const
{
    QElapsedTimer timer; timer.start(); MibDependencyCheckResult result;
    result.requested = plan.explicitModules; result.dependencies = plan.dependencyModules;
    QStringList pending = plan.initialLoadOrder;
    const int maximumPasses = pending.size() + 2;
    while (!pending.isEmpty() && result.passes < maximumPasses) {
        ++result.passes; QStringList next; int progress = 0;
        for (const QString &identity : std::as_const(pending)) {
            const MibEffectivePlanMember *member = plan.member(identity);
            if (!member || member->provider.canonicalPath.isEmpty()) { next.append(identity); continue; }
            const MibDependencyLoadAttempt attempt = loadFile(member->provider.canonicalPath, identity);
            if (attempt.success) {
                ++progress; result.loaded.append(attempt.loadedModuleNames.isEmpty()
                    ? QStringList{identity} : attempt.loadedModuleNames);
            } else next.append(identity);
        }
        pending = next;
        if (progress == 0) { ++result.noProgressStops; break; }
    }
    result.loaded.removeDuplicates();
    for (const QString &identity : std::as_const(plan.missingModules))
        result.failures.append({identity, MibDependencyFailureKind::MissingProvider, QObject::tr("No indexed provider")});
    for (const QString &identity : std::as_const(plan.ambiguousModules))
        result.failures.append({identity, MibDependencyFailureKind::AmbiguousProvider, QObject::tr("Provider conflict")});
    for (const QString &identity : std::as_const(plan.pinFailureModules))
        result.failures.append({identity, MibDependencyFailureKind::AmbiguousProvider, QObject::tr("Explicit provider pin is invalid")});
    for (const QString &identity : std::as_const(plan.nonConvergentModules))
        result.failures.append({identity, MibDependencyFailureKind::DependencyUnresolved, QObject::tr("Provider/dependency planning did not converge")});
    for (const QString &identity : std::as_const(pending))
        result.failures.append({identity, MibDependencyFailureKind::ParserSemanticFailure, QObject::tr("Planned provider did not materialize")});
    result.elapsedMsecs = timer.elapsed(); return result;
}

MibEffectivePlanResolverInput MibResolverInputForProfile(const MibProfileRecord &profile,
                                                         const QString &libraryRoot)
{
    MibEffectivePlanResolverInput input;
    input.id = profile.id;
    input.roots = profile.roots;
    input.orderedScopes = MibProfileScopePaths(profile);
    const QString standardsFolder = QFileInfo(QDir(libraryRoot).filePath(QStringLiteral("Standards"))).canonicalFilePath();
    if (!libraryRoot.isEmpty() && !standardsFolder.isEmpty() && !input.orderedScopes.contains(standardsFolder))
        input.orderedScopes.append(standardsFolder);
    input.pins = profile.providerPins;
    return input;
}
