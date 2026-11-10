#include "mibprofile.h"

#include "mibdependencyindex.h"
#include "mibcandidatefilter.h"

#include <QDir>
#include <QFile>
#include <QDirIterator>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QUuid>
#include <algorithm>
#include <utility>

namespace {
QStringList uniqueSorted(QStringList values)
{
    values.removeDuplicates();
    values.sort(Qt::CaseInsensitive);
    return values;
}

QJsonArray stringsToJson(const QStringList &values)
{
    QJsonArray result;
    for (const QString &value : values) result.append(value);
    return result;
}

QStringList stringsFromJson(const QJsonArray &values)
{
    QStringList result;
    for (const QJsonValue &value : values)
        if (value.isString() && !value.toString().isEmpty()) result.append(value.toString());
    return uniqueSorted(result);
}

QMap<QString, MibProviderPin> pinsFromJson(const QJsonObject &values)
{
    QMap<QString, MibProviderPin> result;
    for (auto it = values.begin(); it != values.end(); ++it) {
        const QJsonObject pin = it.value().toObject();
        const QString path = pin.value(QStringLiteral("path")).toString();
        const QString hash = pin.value(QStringLiteral("sha256")).toString();
        if (!it.key().isEmpty() && !path.isEmpty() && !hash.isEmpty())
            result.insert(it.key(), {path, hash, pin.value(QStringLiteral("reason")).toString()});
    }
    return result;
}

QJsonObject pinsToJson(const QMap<QString, MibProviderPin> &pins)
{
    QJsonObject result;
    for (auto it = pins.begin(); it != pins.end(); ++it)
        result.insert(it.key(), QJsonObject{{QStringLiteral("path"), it->canonicalPath},
                                            {QStringLiteral("sha256"), it->sha256},
                                            {QStringLiteral("reason"), it->reason}});
    return result;
}

QString canonicalFilePath(const QString &path)
{
    const QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    return QDir::cleanPath(canonical.isEmpty() ? info.absoluteFilePath() : canonical);
}

QList<MibProfileMember> membersFromJson(const QJsonArray &values)
{
    QList<MibProfileMember> result;
    for (const QJsonValue &value : values) {
        const QJsonObject object = value.toObject();
        MibProfileMember member;
        member.canonicalPath = canonicalFilePath(object.value("path").toString());
        member.sha256 = object.value("sha256").toString();
        member.identities = stringsFromJson(object.value("identities").toArray());
        member.reason = object.value("reason").toString() == QStringLiteral("dependency")
            ? MibProfileMemberReason::Dependency : MibProfileMemberReason::Added;
        if (!member.canonicalPath.isEmpty()) result.append(member);
    }
    return result;
}

QJsonArray membersToJson(QList<MibProfileMember> members)
{
    std::sort(members.begin(), members.end(), [](const auto &a, const auto &b) {
        return QString::compare(a.canonicalPath, b.canonicalPath, Qt::CaseInsensitive) < 0;
    });
    QJsonArray result;
    for (const auto &member : members)
        result.append(QJsonObject{{"path", QDir::fromNativeSeparators(member.canonicalPath)},
            {"sha256", member.sha256}, {"identities", stringsToJson(uniqueSorted(member.identities))},
            {"reason", member.reason == MibProfileMemberReason::Dependency
                ? QStringLiteral("dependency") : QStringLiteral("added")}});
    return result;
}

QList<MibProfileScope> scopesFromJson(const QJsonArray &values)
{
    QList<MibProfileScope> result;
    for (const QJsonValue &value : values) {
        const QJsonObject object = value.toObject();
        const QString path = canonicalFilePath(object.value(QStringLiteral("path")).toString());
        if (!path.isEmpty()) result.append({object.value(QStringLiteral("id")).toString(), path});
    }
    return result;
}

QJsonArray scopesToJson(const QList<MibProfileScope> &scopes)
{
    QJsonArray result;
    for (const auto &scope : scopes)
        result.append(QJsonObject{{QStringLiteral("id"), scope.id},
            {QStringLiteral("path"), QDir::fromNativeSeparators(scope.canonicalPath)}});
    return result;
}
}

MibProfileMemberState MibProfileMemberCurrentState(const MibProfileMember &member)
{
    QFile file(member.canonicalPath);
    if (!QFileInfo(file).isFile() || !file.open(QIODevice::ReadOnly))
        return MibProfileMemberState::Missing;
    const QString hash = QString::fromLatin1(
        QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex());
    return hash == member.sha256 ? MibProfileMemberState::Current
                                 : MibProfileMemberState::Changed;
}

QList<MibProfileMember> MibProfileMembersFromFiles(
    const QStringList &files, MibProfileMemberReason reason, QStringList *diagnostics)
{
    QList<MibProfileMember> result;
    QSet<QString> seen;
    for (const QString &path : files) {
        const QString canonical = canonicalFilePath(path);
#ifdef Q_OS_WIN
        const QString key = canonical.toLower();
#else
        const QString key = canonical;
#endif
        if (canonical.isEmpty() || seen.contains(key)) continue;
        seen.insert(key);
        QFile file(canonical);
        if (!QFileInfo(file).isFile() || !file.open(QIODevice::ReadOnly)) {
            if (diagnostics) diagnostics->append(QObject::tr("Profile file is not readable: %1").arg(canonical));
            continue;
        }
        const QByteArray content = file.readAll();
        const auto scan = MibImportScanner::scan(content);
        if (scan.moduleNames.isEmpty()) {
            if (diagnostics) diagnostics->append(QObject::tr("No declared MIB identity found: %1").arg(canonical));
            continue;
        }
        result.append({canonical, QString::fromLatin1(QCryptographicHash::hash(
            content, QCryptographicHash::Sha256).toHex()), uniqueSorted(scan.moduleNames), reason});
    }
    std::sort(result.begin(), result.end(), [](const auto &a, const auto &b) {
        return QString::compare(a.canonicalPath, b.canonicalPath, Qt::CaseInsensitive) < 0;
    });
    return result;
}

QStringList MibProfileMemberIdentities(const QList<MibProfileMember> &members)
{
    QStringList result;
    for (const auto &member : members) result.append(member.identities);
    return uniqueSorted(result);
}

bool MibProfileRequiresExactMigration(const MibProfileRecord &profile)
{
    return !MibProfileIsManifest(profile);
}


bool MibProfileIsManifest(const MibProfileRecord &profile)
{ return profile.manifestVersion == 1; }

QStringList MibProfileScopePaths(const MibProfileRecord &profile)
{
    QStringList result;
    for (const MibProfileFolder &folder : profile.folders)
        if (!result.contains(folder.canonicalPath)) result.append(folder.canonicalPath);
    for (const auto &scope : profile.orderedScopes)
        if (!result.contains(scope.canonicalPath)) result.append(scope.canonicalPath);
    return result;
}

bool MibPathIsInsideFolder(const QString &path, const QString &folder)
{
    if (path.isEmpty() || folder.isEmpty()) return false;
    const QString cleanPath = QDir::cleanPath(QDir::fromNativeSeparators(path));
    QString prefix = QDir::cleanPath(QDir::fromNativeSeparators(folder));
#ifdef Q_OS_WIN
    const Qt::CaseSensitivity sensitivity = Qt::CaseInsensitive;
#else
    const Qt::CaseSensitivity sensitivity = Qt::CaseSensitive;
#endif
    if (cleanPath.compare(prefix, sensitivity) == 0) return true;
    if (!prefix.endsWith(QLatin1Char('/'))) prefix += QLatin1Char('/');
    return cleanPath.startsWith(prefix, sensitivity);
}

QList<MibProfileMember> MibProfileFolderMembers(const MibProfileFolder &folder,
                                                const QList<MibDependencyFileRecord> &files,
                                                bool includeExcluded)
{
    QList<MibProfileMember> result;
    for (const MibDependencyFileRecord &file : files) {
        if (file.sha256.isEmpty() || file.importsByModule.isEmpty()) continue;   // unreadable or no module
        if (!MibPathIsInsideFolder(file.canonicalPath, folder.canonicalPath)) continue;
        const bool excluded = std::any_of(folder.excludedFiles.cbegin(), folder.excludedFiles.cend(),
            [&file](const QString &path) { return MibPathIsInsideFolder(file.canonicalPath, path); });
        if (excluded && !includeExcluded) continue;
        MibProfileMember member;
        member.canonicalPath = file.canonicalPath;
        member.sha256 = file.sha256;
        member.identities = file.importsByModule.keys();
        member.identities.sort();
        member.reason = MibProfileMemberReason::Added;
        result.append(member);
    }
    std::sort(result.begin(), result.end(), [](const MibProfileMember &a, const MibProfileMember &b) {
        return a.canonicalPath < b.canonicalPath;
    });
    return result;
}

MibProfileRecord MibProfileWithFolderFiles(const MibProfileRecord &profile,
                                           const QList<MibDependencyFileRecord> &files,
                                           QStringList *missingFolders)
{
    MibProfileRecord result = profile;
    for (const MibProfileFolder &folder : profile.folders) {
        if (!QFileInfo(folder.canonicalPath).isDir()) {
            if (missingFolders) missingFolders->append(folder.canonicalPath);
            continue;
        }
        for (const MibProfileMember &member : MibProfileFolderMembers(folder, files)) {
            // A file that is also an exact root keeps the root (and its recorded hash).
            const bool known = std::any_of(result.roots.cbegin(), result.roots.cend(),
                [&member](const MibProfileMember &root) {
                    return MibPathIsInsideFolder(root.canonicalPath, member.canonicalPath);
                });
            if (!known) result.roots.append(member);
        }
    }
    result.explicitModules = MibProfileMemberIdentities(result.roots);
    return result;
}

QString MibProfileDefinitions::standardsId() { return QStringLiteral("builtin.standards-mib2"); }

QStringList MibProfileDefinitions::standardsModules()
{
    // Deliberately small, reviewable base for ordinary SNMP/MIB-II work.
    return {QStringLiteral("SNMPv2-SMI"), QStringLiteral("SNMPv2-TC"),
            QStringLiteral("SNMPv2-CONF"), QStringLiteral("SNMPv2-MIB"),
            QStringLiteral("IF-MIB"), QStringLiteral("IP-MIB"),
            QStringLiteral("TCP-MIB"), QStringLiteral("UDP-MIB"),
            QStringLiteral("ENTITY-MIB"), QStringLiteral("HOST-RESOURCES-MIB"),
            QStringLiteral("BRIDGE-MIB"), QStringLiteral("Q-BRIDGE-MIB"),
            QStringLiteral("INET-ADDRESS-MIB")};
}

QList<MibProfileRecord> MibProfileDefinitions::builtIns()
{
    return {{standardsId(), QObject::tr("Standards / MIB-II"),
             MibProfileType::Standards, standardsModules(), false}};
}

QString MibProfileDefinitions::validCurrentId(const QString &savedId,
                                               const QList<MibProfileRecord> &profiles)
{
    for (const MibProfileRecord &profile : profiles)
        if (profile.id == savedId) return savedId;
    return profiles.isEmpty() ? QString() : profiles.first().id;
}

MibProfileRepository::MibProfileRepository(QString path) : filePath(std::move(path)) {}

QString MibProfileRepository::foldersPath() const
{
    return QFileInfo(filePath).dir().filePath(QStringLiteral("profile-folders-v1.json"));
}

QList<MibProfileRecord> MibProfileRepository::load(QString *error) const
{
    QFile file(filePath);
    if (!file.exists()) return {};
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = file.errorString();
        return {};
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    const int schemaVersion = document.object().value(QStringLiteral("schemaVersion")).toInt();
    if (parseError.error != QJsonParseError::NoError || !document.isObject() ||
        !QSet<int>{1, 2, 3, 4, 5}.contains(schemaVersion)) {
        if (error) *error = QObject::tr("Unsupported or invalid MIB profile file");
        return {};
    }
    QList<MibProfileRecord> result;
    for (const QJsonValue &value : document.object().value(QStringLiteral("profiles")).toArray()) {
        const QJsonObject object = value.toObject();
        MibProfileRecord profile;
        profile.id = object.value(QStringLiteral("id")).toString();
        profile.name = object.value(QStringLiteral("name")).toString();
        profile.type = object.value(QStringLiteral("type")).toString() == QStringLiteral("folder")
            ? MibProfileType::Folder : MibProfileType::Custom;
        profile.explicitModules = stringsFromJson(object.value(QStringLiteral("modules")).toArray());
        profile.includeStandardBase = object.value(QStringLiteral("includeStandardBase")).toBool();
        profile.directory = object.value(QStringLiteral("directory")).toString();
        profile.providerPins = pinsFromJson(object.value(QStringLiteral("providerPins")).toObject());
        profile.members = membersFromJson(object.value(QStringLiteral("members")).toArray());
        profile.unresolvedLegacyModules = stringsFromJson(
            object.value(QStringLiteral("unresolvedLegacyModules")).toArray());
        if (schemaVersion >= 5 && object.value(QStringLiteral("manifestVersion")).toInt() == 1) {
            profile.manifestVersion = 1;
            profile.roots = membersFromJson(object.value(QStringLiteral("roots")).toArray());
            profile.orderedScopes = scopesFromJson(object.value(QStringLiteral("scope")).toArray());
            profile.providerPins = pinsFromJson(object.value(QStringLiteral("pins")).toObject());
            profile.explicitModules = MibProfileMemberIdentities(profile.roots);
        }
        if (!MibProfileIsManifest(profile) && !profile.members.isEmpty())
            profile.explicitModules = uniqueSorted(MibProfileMemberIdentities(profile.members) +
                                                   profile.unresolvedLegacyModules);
        if (!profile.id.isEmpty() && !profile.name.isEmpty()) result.append(profile);
    }
    // Followed folders, from the companion file. An unreadable or unknown companion file leaves
    // the profiles as they are (and is reported); entries for profiles that no longer exist are
    // ignored.
    QFile foldersFile(foldersPath());
    if (foldersFile.exists()) {
        QJsonParseError foldersError;
        const QJsonDocument folders = foldersFile.open(QIODevice::ReadOnly)
            ? QJsonDocument::fromJson(foldersFile.readAll(), &foldersError) : QJsonDocument();
        if (folders.isObject() && folders.object().value(QStringLiteral("schemaVersion")).toInt() == 1) {
            const QJsonObject byId = folders.object().value(QStringLiteral("profiles")).toObject();
            for (MibProfileRecord &profile : result) {
                for (const QJsonValue &value : byId.value(profile.id).toObject()
                                                   .value(QStringLiteral("folders")).toArray()) {
                    const QJsonObject object = value.toObject();
                    MibProfileFolder folder;
                    folder.canonicalPath = object.value(QStringLiteral("path")).toString();
                    folder.excludedFiles = stringsFromJson(object.value(QStringLiteral("excluded")).toArray());
                    if (!folder.canonicalPath.isEmpty()) profile.folders.append(folder);
                }
            }
        } else if (error) {
            *error = QObject::tr("The followed-folder file %1 could not be read; profiles are shown "
                                 "without their followed folders").arg(QDir::toNativeSeparators(foldersPath()));
        }
    }
    return result;
}

bool MibProfileRepository::save(const QList<MibProfileRecord> &profiles, QString *error) const
{
    // The companion file first: if the profile file then cannot be written, the companion file is
    // put back as it was, so the two never disagree.
    QJsonObject foldersById;
    for (const MibProfileRecord &profile : profiles) {
        if (profile.type != MibProfileType::Custom || profile.folders.isEmpty()) continue;
        QJsonArray folders;
        for (const MibProfileFolder &folder : profile.folders)
            folders.append(QJsonObject{{QStringLiteral("path"), folder.canonicalPath},
                                       {QStringLiteral("excluded"), stringsToJson(folder.excludedFiles)}});
        foldersById.insert(profile.id, QJsonObject{{QStringLiteral("folders"), folders}});
    }
    QByteArray previousFolders;
    QFile previousFoldersFile(foldersPath());
    const bool hadFolders = previousFoldersFile.exists();
    if (hadFolders && previousFoldersFile.open(QIODevice::ReadOnly)) previousFolders = previousFoldersFile.readAll();
    previousFoldersFile.close();
    const bool writeFolders = hadFolders || !foldersById.isEmpty();
    const auto writeFoldersFile = [this](const QByteArray &bytes, QString *failure) {
        QDir().mkpath(QFileInfo(foldersPath()).absolutePath());
        QSaveFile file(foldersPath());
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) < 0 || !file.commit()) {
            if (failure) *failure = file.errorString();
            return false;
        }
        return true;
    };
    if (writeFolders) {
        QJsonObject foldersRoot;
        foldersRoot.insert(QStringLiteral("schemaVersion"), 1);
        foldersRoot.insert(QStringLiteral("profiles"), foldersById);
        const QByteArray bytes = QJsonDocument(foldersRoot).toJson(QJsonDocument::Indented);
        if (bytes != previousFolders && !writeFoldersFile(bytes, error)) return false;
    }
    const auto restoreFolders = [&]() {
        if (!writeFolders) return;
        if (hadFolders) writeFoldersFile(previousFolders, nullptr);
        else QFile::remove(foldersPath());
    };

    QJsonArray items;
    for (const MibProfileRecord &profile : profiles) {
        if (profile.type != MibProfileType::Custom && profile.type != MibProfileType::Folder) continue;
        QJsonObject object;
        object.insert(QStringLiteral("id"), profile.id);
        object.insert(QStringLiteral("name"), profile.name);
        object.insert(QStringLiteral("type"), profile.type == MibProfileType::Folder
            ? QStringLiteral("folder") : QStringLiteral("custom"));
        if (MibProfileIsManifest(profile)) {
            object.insert(QStringLiteral("manifestVersion"), 1);
            object.insert(QStringLiteral("roots"), membersToJson(profile.roots));
            object.insert(QStringLiteral("scope"), scopesToJson(profile.orderedScopes));
            if (!profile.providerPins.isEmpty())
                object.insert(QStringLiteral("pins"), pinsToJson(profile.providerPins));
            if (!profile.unresolvedLegacyModules.isEmpty())
                object.insert(QStringLiteral("unresolvedLegacyModules"),
                              stringsToJson(uniqueSorted(profile.unresolvedLegacyModules)));
        } else {
            if (profile.type == MibProfileType::Custom)
                object.insert(QStringLiteral("modules"), stringsToJson(uniqueSorted(profile.explicitModules)));
            else object.insert(QStringLiteral("directory"), profile.directory);
            object.insert(QStringLiteral("includeStandardBase"), profile.includeStandardBase);
            if (!profile.providerPins.isEmpty())
                object.insert(QStringLiteral("providerPins"), pinsToJson(profile.providerPins));
            if (!profile.members.isEmpty()) object.insert(QStringLiteral("members"), membersToJson(profile.members));
            if (!profile.unresolvedLegacyModules.isEmpty())
                object.insert(QStringLiteral("unresolvedLegacyModules"),
                              stringsToJson(uniqueSorted(profile.unresolvedLegacyModules)));
        }
        items.append(object);
    }
    QJsonObject root;
    root.insert(QStringLiteral("schemaVersion"), 5);
    root.insert(QStringLiteral("ordinaryProfilesMigrated"), true);
    root.insert(QStringLiteral("profiles"), items);
    QDir().mkpath(QFileInfo(filePath).absolutePath());
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly) ||
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented)) < 0 || !file.commit()) {
        if (error) *error = file.errorString();
        restoreFolders();
        return false;
    }
    return true;
}

bool MibProfileRepository::ordinaryProfileMigrationComplete() const
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) return false;
    return QJsonDocument::fromJson(file.readAll()).object()
        .value(QStringLiteral("ordinaryProfilesMigrated")).toBool();
}

MibProfileService::MibProfileService(MibProfileRepository value)
    : repository(std::move(value))
{
    const bool migrated = repository.ordinaryProfileMigrationComplete();
    reload();
    if (!migrated) {
        for (MibProfileRecord profile : std::as_const(builtInProfiles)) {
            profile.type = MibProfileType::Custom;
            if (std::none_of(customProfiles.cbegin(), customProfiles.cend(),
                [&profile](const auto &existing) { return existing.id == profile.id; }))
                customProfiles.append(profile);
        }
        QString ignored;
        persist(&ignored);
    }
    // An earlier version of this built-in listed LLDP-MIB, which the application does not
    // ship. Profiles migrated from it were left with that module unresolved, which would
    // be reported on every launch. Remove only that stale entry from the built-in profile;
    // nothing else is touched, and the repair does nothing the second time.
    bool repaired = false;
    for (QList<MibProfileRecord> *profiles : {&customProfiles, &folderProfiles})
        for (MibProfileRecord &profile : *profiles)
            if (profile.id == MibProfileDefinitions::standardsId() &&
                profile.unresolvedLegacyModules.removeAll(QStringLiteral("LLDP-MIB")) > 0)
                repaired = true;
    if (repaired) {
        QString ignored;
        persist(&ignored);
    }
    builtInProfiles.clear();
}

bool MibProfileService::reload(QString *error)
{
    QString detail;
    const QList<MibProfileRecord> loaded = repository.load(&detail);
    customProfiles.clear(); folderProfiles.clear();
    for (const MibProfileRecord &profile : loaded)
        (profile.type == MibProfileType::Folder ? folderProfiles : customProfiles).append(profile);
    if (error) *error = detail;
    return detail.isEmpty();
}

bool MibProfileService::persist(QString *error) const
{
    QList<MibProfileRecord> stored = customProfiles;
    stored.append(folderProfiles);
    return repository.save(stored, error);
}

QList<MibProfileRecord> MibProfileService::profiles() const
{
    QList<MibProfileRecord> result = builtInProfiles;
    result.append(automaticProfiles);
    result.append(customProfiles);
    result.append(folderProfiles);
    return result;
}

bool MibProfileService::setAutomaticProfiles(const QList<MibProfileRecord> &profiles)
{
    const auto same = [](const MibProfileRecord &a, const MibProfileRecord &b) {
        return a.id == b.id && a.name == b.name && a.roots == b.roots && a.orderedScopes == b.orderedScopes;
    };
    if (profiles.size() == automaticProfiles.size() &&
        std::equal(profiles.cbegin(), profiles.cend(), automaticProfiles.cbegin(), same))
        return false;
    automaticProfiles = profiles;
    return true;
}

const MibProfileRecord *MibProfileService::find(const QString &id) const
{
    for (const MibProfileRecord &profile : builtInProfiles)
        if (profile.id == id) return &profile;
    for (const MibProfileRecord &profile : customProfiles)
        if (profile.id == id) return &profile;
    for (const MibProfileRecord &profile : folderProfiles)
        if (profile.id == id) return &profile;
    for (const MibProfileRecord &profile : automaticProfiles)
        if (profile.id == id) return &profile;
    return nullptr;
}

bool MibProfileService::isBuiltIn(const QString &id)
{
    Q_UNUSED(id)
    return false;
}

QString MibProfileService::create(const QString &name, QString *error)
{
    if (name.trimmed().isEmpty()) { if (error) *error = QObject::tr("Profile name is required"); return {}; }
    MibProfileRecord record{QUuid::createUuid().toString(QUuid::WithoutBraces),
                            name.trimmed(), MibProfileType::Custom, {}, false};
    record.manifestVersion = 1;
    customProfiles.append(record);
    if (!persist(error)) { customProfiles.removeLast(); return {}; }
    return record.id;
}

QString MibProfileService::duplicate(const QString &id, const QString &name, QString *error)
{
    const MibProfileRecord *source = find(id);
    if (!source) { if (error) *error = QObject::tr("Profile not found"); return {}; }
    MibProfileRecord copy = *source;
    copy.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    copy.name = name.trimmed().isEmpty() ? source->name + QObject::tr(" Copy") : name.trimmed();
    copy.type = MibProfileType::Custom;
    customProfiles.append(copy);
    if (!persist(error)) { customProfiles.removeLast(); return {}; }
    return copy.id;
}

bool MibProfileService::rename(const QString &id, const QString &name, QString *error)
{
    if (isBuiltIn(id) || name.trimmed().isEmpty()) return false;
    for (MibProfileRecord &profile : customProfiles) if (profile.id == id) {
        const QString old = profile.name; profile.name = name.trimmed();
        if (!persist(error)) { profile.name = old; return false; }
        return true;
    }
    return false;
}

bool MibProfileService::remove(const QString &id, QString *error)
{
    if (isBuiltIn(id)) return false;
    for (qsizetype i = 0; i < customProfiles.size(); ++i) if (customProfiles[i].id == id) {
        const MibProfileRecord removed = customProfiles.takeAt(i);
        if (!persist(error)) { customProfiles.insert(i, removed); return false; }
        return true;
    }
    return false;
}

bool MibProfileService::update(const MibProfileRecord &value, QString *error)
{
    if (isBuiltIn(value.id) || value.type != MibProfileType::Custom) return false;
    if (!MibProfileIsManifest(value)) {
        if (error) *error = QObject::tr(
            "Profiles must use exact physical files; legacy identity membership requires migration");
        return false;
    }
    for (MibProfileRecord &profile : customProfiles) if (profile.id == value.id) {
        const MibProfileRecord old = profile; profile = value;
        profile.explicitModules = MibProfileMemberIdentities(profile.roots);
        if (!persist(error)) { profile = old; return false; }
        return true;
    }
    return false;
}

bool MibProfileService::importCustomProfile(const QString &stableId, const QString &name,
                                             const QStringList &modules, QString *error)
{
    if (stableId.isEmpty() || isBuiltIn(stableId)) return false;
    for (MibProfileRecord &profile : customProfiles) if (profile.id == stableId) {
        const MibProfileRecord old = profile;
        profile.name = name; profile.type = MibProfileType::Custom;
        if (MibProfileIsManifest(profile)) {
            const QStringList exactIdentities = MibProfileMemberIdentities(profile.roots);
            for (const QString &identity : modules)
                if (!exactIdentities.contains(identity) &&
                    !profile.unresolvedLegacyModules.contains(identity))
                    profile.unresolvedLegacyModules.append(identity);
            profile.unresolvedLegacyModules = uniqueSorted(profile.unresolvedLegacyModules);
        } else if (profile.members.isEmpty() && profile.unresolvedLegacyModules.isEmpty()) {
            profile.explicitModules = uniqueSorted(modules);
        } else {
            const QStringList exactIdentities = MibProfileMemberIdentities(profile.members);
            for (const QString &identity : modules)
                if (!exactIdentities.contains(identity) &&
                    !profile.unresolvedLegacyModules.contains(identity))
                    profile.unresolvedLegacyModules.append(identity);
            profile.unresolvedLegacyModules = uniqueSorted(profile.unresolvedLegacyModules);
            profile.explicitModules = uniqueSorted(exactIdentities +
                                                   profile.unresolvedLegacyModules);
        }
        if (!persist(error)) { profile = old; return false; }
        return true;
    }
    MibProfileRecord profile;
    profile.id = stableId; profile.name = name; profile.type = MibProfileType::Custom;
    profile.explicitModules = uniqueSorted(modules);
    customProfiles.append(profile);
    if (!persist(error)) { customProfiles.removeLast(); return false; }
    return true;
}

bool MibProfileService::migrateLegacyProfiles(const MibDependencyIndex &index, QString *error)
{
    const QList<MibProfileRecord> previous = customProfiles;
    bool changed = false;
    for (MibProfileRecord &profile : customProfiles) {
        if (MibProfileIsManifest(profile)) {
            QStringList stillUnresolved;
            for (const QString &identity : std::as_const(profile.unresolvedLegacyModules)) {
                const auto providers = index.providersFor(identity);
                const auto exact = providers.size() == 1
                    ? MibProfileMembersFromFiles({providers.first().canonicalPath})
                    : QList<MibProfileMember>{};
                if (exact.size() != 1 || exact.first().sha256 != providers.first().sha256) {
                    stillUnresolved.append(identity);
                    continue;
                }
                if (std::none_of(profile.roots.cbegin(), profile.roots.cend(), [&exact](const auto &root) {
                        return canonicalFilePath(root.canonicalPath).compare(
                            canonicalFilePath(exact.first().canonicalPath), Qt::CaseInsensitive) == 0;
                    })) profile.roots.append(exact.first());
                const QString parent = canonicalFilePath(QFileInfo(exact.first().canonicalPath).absolutePath());
                if (std::none_of(profile.orderedScopes.cbegin(), profile.orderedScopes.cend(),
                    [&parent](const auto &scope) {
                        return scope.canonicalPath.compare(parent, Qt::CaseInsensitive) == 0;
                    })) profile.orderedScopes.append({QStringLiteral("collection:") +
                        QDir::fromNativeSeparators(QDir(index.libraryRoot()).relativeFilePath(parent)), parent});
            }
            stillUnresolved = uniqueSorted(stillUnresolved);
            if (stillUnresolved != profile.unresolvedLegacyModules) {
                profile.unresolvedLegacyModules = stillUnresolved;
                profile.explicitModules = MibProfileMemberIdentities(profile.roots);
                changed = true;
            }
            continue;
        }
        const MibProfileRecord before = profile;
        QList<MibProfileMember> roots;
        QStringList unresolved = profile.unresolvedLegacyModules;
        bool broadGeneratedStandards = false;
        if (profile.members.isEmpty()) {
            QStringList identities = profile.explicitModules;
            if (profile.includeStandardBase) identities.append(MibProfileDefinitions::standardsModules());
            for (const QString &identity : uniqueSorted(identities)) {
                const auto providers = index.providersFor(identity);
                if (providers.size() != 1) { unresolved.append(identity); continue; }
                const auto exact = MibProfileMembersFromFiles({providers.first().canonicalPath});
                if (exact.size() != 1 || exact.first().sha256 != providers.first().sha256)
                    unresolved.append(identity);
                else if (std::none_of(roots.cbegin(), roots.cend(), [&exact](const auto &root) {
                    return canonicalFilePath(root.canonicalPath).compare(
                        canonicalFilePath(exact.first().canonicalPath), Qt::CaseInsensitive) == 0;
                })) roots.append(exact.first());
            }
        } else {
            int standardsCount = 0;
            for (const auto &member : profile.members)
                if (QDir::fromNativeSeparators(member.canonicalPath).contains(
                        QStringLiteral("/Standards/"), Qt::CaseInsensitive)) ++standardsCount;
            broadGeneratedStandards = standardsCount >= 20 && standardsCount < profile.members.size() &&
                !profile.unresolvedLegacyModules.isEmpty();
            if (broadGeneratedStandards) {
                for (const auto &member : profile.members)
                    if (!QDir::fromNativeSeparators(member.canonicalPath)
                            .contains(QStringLiteral("/Standards/"), Qt::CaseInsensitive))
                        roots.append(member);
            } else {
                QSet<QString> imported;
                const auto pathKey = [](const QString &path) {
                    const QString value = QDir::fromNativeSeparators(canonicalFilePath(path));
#ifdef Q_OS_WIN
                    return value.toLower();
#else
                    return value;
#endif
                };
                // files() returns the list by value, and iterating a temporary list makes a private
                // copy that is destroyed when the loop ends. Hand out pointers only into a list that
                // outlives every use of them.
                const QList<MibDependencyFileRecord> indexFiles = index.files();
                const auto indexedRecord = [&indexFiles](const MibProfileMember &member)
                    -> const MibDependencyFileRecord * {
                    for (const auto &record : indexFiles)
                        if (canonicalFilePath(record.canonicalPath).compare(
                                canonicalFilePath(member.canonicalPath), Qt::CaseInsensitive) == 0 &&
                            record.sha256 == member.sha256) return &record;
                    return nullptr;
                };
                for (const auto &member : profile.members)
                    if (const auto *record = indexedRecord(member))
                        for (const auto &imports : record->importsByModule) imported.unite(QSet<QString>(
                            imports.cbegin(), imports.cend()));
                QSet<QString> reachablePaths;
                for (const auto &member : profile.members)
                    if (std::none_of(member.identities.cbegin(), member.identities.cend(),
                        [&imported](const QString &identity) { return imported.contains(identity); })) {
                        roots.append(member);
                        reachablePaths.insert(pathKey(member.canonicalPath));
                    }
                bool progress = true;
                while (progress) {
                    progress = false;
                    QStringList needed;
                    for (const auto &member : profile.members)
                        if (reachablePaths.contains(pathKey(member.canonicalPath)))
                            if (const auto *record = indexedRecord(member))
                                for (const auto &imports : record->importsByModule) needed.append(imports);
                    for (const QString &identity : uniqueSorted(needed)) {
                        QList<MibProfileMember> matches;
                        for (const auto &member : profile.members)
                            if (member.identities.contains(identity)) matches.append(member);
                        if (matches.size() == 1 && !reachablePaths.contains(
                                pathKey(matches.first().canonicalPath))) {
                            reachablePaths.insert(pathKey(matches.first().canonicalPath));
                            progress = true;
                        }
                    }
                }
                for (const auto &member : profile.members)
                    if (!reachablePaths.contains(pathKey(member.canonicalPath)))
                        roots.append(member);
            }
        }
        QList<MibProfileScope> scopes;
        const auto appendScope = [&scopes, &index](const QString &path) {
            const QString canonical = canonicalFilePath(path);
            if (canonical.isEmpty() || std::any_of(scopes.cbegin(), scopes.cend(), [&canonical](const auto &scope) {
                    return scope.canonicalPath.compare(canonical, Qt::CaseInsensitive) == 0;
                })) return;
            QString relative = QDir(index.libraryRoot()).relativeFilePath(canonical);
            scopes.append({QStringLiteral("collection:") + QDir::fromNativeSeparators(relative), canonical});
        };
        QString standardsScope;
        for (const auto &member : profile.members) {
            const QString normalized = QDir::fromNativeSeparators(member.canonicalPath);
            const qsizetype marker = normalized.indexOf(QStringLiteral("/Standards/"), 0,
                                                         Qt::CaseInsensitive);
            if (marker >= 0) standardsScope = normalized.left(marker + 10);
        }
        for (const auto &root : roots) {
            const QString parent = QFileInfo(root.canonicalPath).absolutePath();
            if (!QDir::fromNativeSeparators(parent).endsWith(QStringLiteral("/Unassigned"),
                                                              Qt::CaseInsensitive))
                appendScope(parent);
        }
        if (!standardsScope.isEmpty()) appendScope(standardsScope);
        profile.manifestVersion = 1;
        profile.roots = roots;
        profile.orderedScopes = scopes;
        profile.unresolvedLegacyModules = uniqueSorted(unresolved);
        profile.explicitModules = MibProfileMemberIdentities(roots);
        profile.members.clear();
        profile.includeStandardBase = false;
        profile.directory.clear();
        if (broadGeneratedStandards)
            profile.unresolvedLegacyModules.clear();
        changed |= profile.manifestVersion != before.manifestVersion;
    }
    if (!changed) return true;
    if (persist(error)) return true;
    customProfiles = previous;
    return false;
}

bool MibProfileService::requiresLegacyConversion() const
{
    const auto pending = [](const MibProfileRecord &profile) {
        return !MibProfileIsManifest(profile);
    };
    return std::any_of(customProfiles.cbegin(), customProfiles.cend(), pending) ||
           std::any_of(folderProfiles.cbegin(), folderProfiles.cend(), pending);
}

bool MibProfileService::hasUnresolvedLegacyResidue() const
{
    return std::any_of(customProfiles.cbegin(), customProfiles.cend(),
        [](const MibProfileRecord &profile) {
            return MibProfileIsManifest(profile) && !profile.unresolvedLegacyModules.isEmpty();
        });
}

int MibProfileService::refreshRootHash(const QString &path, const QString &sha256, QString *error)
{
    const QString wanted = QFileInfo(path).canonicalFilePath();
    if (wanted.isEmpty() || sha256.isEmpty()) return 0;
    const auto samePhysicalFile = [&wanted](const QString &candidate) {
        const QString other = QFileInfo(candidate).canonicalFilePath();
#ifdef Q_OS_WIN
        return !other.isEmpty() && other.compare(wanted, Qt::CaseInsensitive) == 0;
#else
        return !other.isEmpty() && other == wanted;
#endif
    };
    int updated = 0;
    for (QList<MibProfileRecord> *profiles : {&customProfiles, &folderProfiles})
        for (MibProfileRecord &profile : *profiles)
            for (MibProfileMember &root : profile.roots)
                if (root.sha256 != sha256 && samePhysicalFile(root.canonicalPath)) {
                    root.sha256 = sha256;
                    ++updated;
                }
    if (updated > 0) persist(error);
    return updated;
}

bool MibProfileService::migrateLegacyFolderProfiles(QString *error)
{
    if (folderProfiles.isEmpty()) return true;
    const QList<MibProfileRecord> previousCustom = customProfiles;
    const QList<MibProfileRecord> previousFolders = folderProfiles;
    for (MibProfileRecord profile : std::as_const(folderProfiles)) {
        QStringList files;
        QDirIterator iterator(profile.directory, QDir::Files | QDir::Readable,
                              QDirIterator::Subdirectories);
        while (iterator.hasNext()) {
            const QString path = iterator.next();
            if (MibCandidateFilter::accepts(QFileInfo(path).fileName())) files.append(path);
        }
        profile.members = MibProfileMembersFromFiles(files);
        profile.roots = profile.members;
        profile.members.clear();
        profile.explicitModules = MibProfileMemberIdentities(profile.roots);
        profile.manifestVersion = 1;
        profile.orderedScopes = {{QStringLiteral("folder:") + QDir::fromNativeSeparators(profile.directory),
                                  canonicalFilePath(profile.directory)}};
        profile.directory.clear();
        profile.type = MibProfileType::Custom;
        customProfiles.append(profile);
    }
    folderProfiles.clear();
    if (!persist(error)) {
        customProfiles = previousCustom;
        folderProfiles = previousFolders;
        return false;
    }
    return true;
}

bool MibProfileService::addFiles(const QString &id, const QStringList &files,
                                 MibProfileMemberReason reason, QString *error)
{
    const MibProfileRecord *current = find(id);
    if (!current || current->type != MibProfileType::Custom || isBuiltIn(id)) return false;
    if (files.isEmpty()) {
        if (error) *error = QObject::tr("No unambiguous exact MIB files were selected");
        return false;
    }
    QStringList diagnostics;
    const QList<MibProfileMember> additions = MibProfileMembersFromFiles(files, reason, &diagnostics);
    if (!diagnostics.isEmpty() && additions.isEmpty()) {
        if (error) *error = diagnostics.join(QStringLiteral("; "));
        return false;
    }
    MibProfileRecord changed = *current;
    for (const auto &member : additions) {
        const auto existing = std::find_if(changed.roots.cbegin(), changed.roots.cend(),
            [&member](const auto &candidate) {
#ifdef Q_OS_WIN
                return candidate.canonicalPath.compare(member.canonicalPath, Qt::CaseInsensitive) == 0;
#else
                return candidate.canonicalPath == member.canonicalPath;
#endif
            });
        if (existing == changed.roots.cend()) changed.roots.append(member);
        const QString parent = canonicalFilePath(QFileInfo(member.canonicalPath).absolutePath());
        if (std::none_of(changed.orderedScopes.cbegin(), changed.orderedScopes.cend(),
            [&parent](const auto &scope) {
                return scope.canonicalPath.compare(parent, Qt::CaseInsensitive) == 0;
            })) changed.orderedScopes.append({QStringLiteral("collection:") +
                QDir::fromNativeSeparators(parent), parent});
    }
    changed.explicitModules = MibProfileMemberIdentities(changed.roots);
    for (const auto &member : additions)
        for (const QString &identity : member.identities)
            changed.unresolvedLegacyModules.removeAll(identity);
    return update(changed, error);
}

bool MibProfileService::addFolder(const QString &id, const QString &folder, QString *error)
{
    QStringList files;
    QDirIterator iterator(folder, QDir::Files | QDir::Readable, QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QString path = iterator.next();
        if (MibCandidateFilter::accepts(QFileInfo(path).fileName())) files.append(path);
    }
    return addFiles(id, files, MibProfileMemberReason::Added, error);
}

bool MibProfileService::followFolders(const QString &id, const QStringList &folders, QString *error)
{
    const MibProfileRecord *current = find(id);
    if (!current || current->type != MibProfileType::Custom || isBuiltIn(id)) return false;
    MibProfileRecord changed = *current;
    for (const QString &folder : folders) {
        const QString path = canonicalFilePath(folder);
        if (path.isEmpty() || !QFileInfo(path).isDir()) {
            if (error) *error = QObject::tr("%1 is not a folder").arg(QDir::toNativeSeparators(folder));
            return false;
        }
        const bool known = std::any_of(changed.folders.cbegin(), changed.folders.cend(),
            [&path](const MibProfileFolder &existing) {
                return MibPathIsInsideFolder(path, existing.canonicalPath) &&
                       MibPathIsInsideFolder(existing.canonicalPath, path);
            });
        if (!known) changed.folders.append({path, {}});
    }
    if (changed.folders == current->folders) return true;
    return update(changed, error);
}

bool MibProfileService::unfollowFolders(const QString &id, const QStringList &folders, QString *error)
{
    const MibProfileRecord *current = find(id);
    if (!current || current->type != MibProfileType::Custom || isBuiltIn(id)) return false;
    MibProfileRecord changed = *current;
    changed.folders.erase(std::remove_if(changed.folders.begin(), changed.folders.end(),
        [&folders](const MibProfileFolder &folder) {
            return std::any_of(folders.cbegin(), folders.cend(), [&folder](const QString &path) {
                return MibPathIsInsideFolder(path, folder.canonicalPath) &&
                       MibPathIsInsideFolder(folder.canonicalPath, path);
            });
        }), changed.folders.end());
    if (changed.folders == current->folders) return true;
    return update(changed, error);
}

bool MibProfileService::pinProvider(const QString &id, const QString &module, const MibProviderPin &pin, QString *error)
{
    const MibProfileRecord *current = find(id);
    if (!current || current->type != MibProfileType::Custom || isBuiltIn(id)) {
        if (error) *error = QObject::tr("Only a Custom profile can choose a copy of a module.");
        return false;
    }
    if (module.isEmpty() || pin.canonicalPath.isEmpty()) {
        if (error) *error = QObject::tr("A module and a file are needed to choose a copy.");
        return false;
    }
    MibProfileRecord changed = *current;
    changed.providerPins.insert(module, pin);
    return update(changed, error);
}

bool MibProfileService::unpinProvider(const QString &id, const QString &module, QString *error)
{
    const MibProfileRecord *current = find(id);
    if (!current || current->type != MibProfileType::Custom || isBuiltIn(id) || !current->providerPins.contains(module))
        return false;
    MibProfileRecord changed = *current;
    changed.providerPins.remove(module);
    return update(changed, error);
}

bool MibProfileService::setFilesExcluded(const QString &id, const QStringList &files, bool excluded,
                                         QString *error)
{
    const MibProfileRecord *current = find(id);
    if (!current || current->type != MibProfileType::Custom || isBuiltIn(id)) return false;
    MibProfileRecord changed = *current;
    for (const QString &file : files) {
        for (MibProfileFolder &folder : changed.folders) {
            if (!MibPathIsInsideFolder(file, folder.canonicalPath)) continue;
            const auto same = [&file](const QString &path) {
                return MibPathIsInsideFolder(file, path) && MibPathIsInsideFolder(path, file);
            };
            const bool listed = std::any_of(folder.excludedFiles.cbegin(), folder.excludedFiles.cend(), same);
            if (excluded && !listed) folder.excludedFiles.append(file);
            if (!excluded && listed)
                folder.excludedFiles.erase(std::remove_if(folder.excludedFiles.begin(),
                                                          folder.excludedFiles.end(), same),
                                           folder.excludedFiles.end());
            folder.excludedFiles.sort();
        }
    }
    if (changed.folders == current->folders) return true;
    return update(changed, error);
}

MibProfileModuleAdditionResult MibAddModulesToEditableProfile(
    MibProfileService &service, const QString &profileId, const QStringList &moduleIdentities)
{
    MibProfileModuleAdditionResult result;
    const MibProfileRecord *current = service.find(profileId);
    if (!current) return result;
    if (current->type != MibProfileType::Custom) {
        result.status = MibProfileModuleAdditionStatus::ReadOnly;
        return result;
    }

    MibProfileRecord updated = *current;
    for (const QString &value : moduleIdentities) {
        const QString identity = value.trimmed();
        if (identity.isEmpty() || updated.explicitModules.contains(identity)) continue;
        updated.explicitModules.append(identity);
        result.addedModules.append(identity);
    }
    result.addedModules = uniqueSorted(result.addedModules);
    if (result.addedModules.isEmpty()) {
        result.status = MibProfileModuleAdditionStatus::Unchanged;
        return result;
    }
    if (!service.update(updated, &result.error)) {
        result.status = MibProfileModuleAdditionStatus::PersistenceFailed;
        result.addedModules.clear();
        return result;
    }
    result.status = MibProfileModuleAdditionStatus::Updated;
    return result;
}

MibProfileDependencyRefreshResult MibResolveProfileDependencies(
    const MibProfileRecord &profile, const MibDependencyIndex &index)
{
    MibProfileDependencyRefreshResult result;
    result.profile = profile;
    QMap<QString, MibProfileMember> selected;
    for (const auto &member : profile.members)
        for (const QString &identity : member.identities)
            selected.insert(identity, member);

    bool changed = true;
    while (changed) {
        changed = false;
        QMap<QString, QSet<QString>> requestedFromFolders;
        for (auto it = selected.cbegin(); it != selected.cend(); ++it) {
            const QString identity = it.key();
            const MibProfileMember &member = it.value();
            QStringList dependencies;
            for (const auto &provider : index.providersFor(identity)) {
                if (QFileInfo(provider.canonicalPath).canonicalFilePath().compare(
                        QFileInfo(member.canonicalPath).canonicalFilePath(), Qt::CaseInsensitive) == 0 &&
                    provider.sha256 == member.sha256) {
                    dependencies = provider.imports;
                    break;
                }
            }
            for (const QString &dependency : dependencies)
                if (!selected.contains(dependency))
                    requestedFromFolders[dependency].insert(
                        QFileInfo(member.canonicalPath).canonicalPath());
        }

        for (auto request = requestedFromFolders.cbegin(); request != requestedFromFolders.cend(); ++request) {
            const QString identity = request.key();
            QList<MibIndexedProvider> candidates = index.providersFor(identity);
            QList<MibIndexedProvider> contextual;
            for (const QString &source : request.value()) {
                const auto local = index.providersFor(identity, QFileInfo(source).absolutePath(),
                                                       MibCatalogProviderScope::ExactFolder);
                for (const auto &provider : local)
                    if (std::none_of(contextual.cbegin(), contextual.cend(), [&provider](const auto &item) {
                        return item.canonicalPath.compare(provider.canonicalPath, Qt::CaseInsensitive) == 0 &&
                               item.sha256 == provider.sha256;
                    })) contextual.append(provider);
            }
            if (contextual.size() == 1) candidates = contextual;
            else if (!contextual.isEmpty()) candidates.clear();
            if (candidates.size() != 1) continue;
            const auto &provider = candidates.first();
            MibProfileMember member{provider.canonicalPath, provider.sha256, {},
                                    MibProfileMemberReason::Dependency};
            for (const auto &record : index.files())
                if (record.canonicalPath.compare(provider.canonicalPath, Qt::CaseInsensitive) == 0) {
                    member.identities = record.importsByModule.keys();
                    break;
                }
            if (member.identities.isEmpty()) member.identities.append(identity);
            result.profile.members.append(member);
            result.addedDependencies.append(member);
            for (const QString &provided : member.identities) selected.insert(provided, member);
            changed = true;
        }
    }

    QSet<QString> unresolved;
    for (auto it = selected.cbegin(); it != selected.cend(); ++it) {
        for (const auto &provider : index.providersFor(it.key())) {
            if (provider.canonicalPath.compare(it->canonicalPath, Qt::CaseInsensitive) != 0 ||
                provider.sha256 != it->sha256) continue;
            for (const QString &dependency : provider.imports)
                if (!selected.contains(dependency)) unresolved.insert(dependency);
            break;
        }
    }
    result.unresolved = unresolved.values();
    result.unresolved.sort(Qt::CaseSensitive);
    result.profile.unresolvedLegacyModules = result.unresolved;
    return result;
}

QList<MibProfileRecord> MibBuildAutomaticProfiles(const QString &libraryRoot,
                                                  const QList<MibDependencyFileRecord> &files)
{
    QList<MibProfileRecord> result;
    const QString root = QFileInfo(libraryRoot).canonicalFilePath();
    if (root.isEmpty()) return result;
    const QDir rootDir(root);
    QMap<QString, MibProfileRecord> byId;
    for (const MibDependencyFileRecord &file : files) {
        if (file.sha256.isEmpty() || file.importsByModule.isEmpty()) continue;   // unreadable or no module
        const QString relative = QDir::fromNativeSeparators(rootDir.relativeFilePath(file.canonicalPath));
        if (relative.startsWith(QLatin1String("../")) || relative == QLatin1String("..")) continue;   // outside the Library
        const QStringList parts = relative.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        if (parts.size() < 3) continue;   // needs Vendor/Product/file
        const QString vendor = parts.at(0), product = parts.at(1);
        if (vendor.startsWith(QLatin1Char('.')) || product.startsWith(QLatin1Char('.'))) continue;
        if (vendor.compare(QLatin1String("Standards"), Qt::CaseInsensitive) == 0 ||
            vendor.compare(QLatin1String("Unassigned"), Qt::CaseInsensitive) == 0) continue;
        const QString id = QStringLiteral("auto:%1/%2").arg(vendor, product);
        MibProfileRecord &profile = byId[id];
        if (profile.id.isEmpty()) {
            profile.id = id;
            profile.name = QStringLiteral("%1 / %2").arg(vendor, product);
            profile.type = MibProfileType::Automatic;
            profile.manifestVersion = 1;
            const QString productDir = QFileInfo(rootDir.filePath(vendor + QLatin1Char('/') + product)).canonicalFilePath();
            const QString vendorDir = QFileInfo(rootDir.filePath(vendor)).canonicalFilePath();
            // The product folder first, then the vendor folder (files shared by a vendor's products).
            profile.orderedScopes = {{QStringLiteral("folder:") + vendor + QLatin1Char('/') + product, productDir},
                                     {QStringLiteral("folder:") + vendor, vendorDir}};
        }
        MibProfileMember member;
        member.canonicalPath = file.canonicalPath;
        member.sha256 = file.sha256;
        member.identities = file.importsByModule.keys();
        member.identities.sort();
        member.reason = MibProfileMemberReason::Added;
        profile.roots.append(member);
    }
    for (auto it = byId.begin(); it != byId.end(); ++it) {
        MibProfileRecord profile = it.value();
        std::sort(profile.roots.begin(), profile.roots.end(), [](const MibProfileMember &a, const MibProfileMember &b) {
            return a.canonicalPath < b.canonicalPath;
        });
        profile.explicitModules = MibProfileMemberIdentities(profile.roots);
        result.append(profile);
    }
    std::sort(result.begin(), result.end(), [](const MibProfileRecord &a, const MibProfileRecord &b) {
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });
    return result;
}
