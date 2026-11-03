#ifndef MIBPROFILE_H
#define MIBPROFILE_H

#include "miblibrary.h"

#include <QList>
#include <QMap>
#include <QStringList>

class MibDependencyIndex;
struct MibDependencyFileRecord;

// Automatic profiles are derived from the Library's Vendor/Product folders. They are never saved
// and cannot be edited member by member: the folder is the single source of truth.
enum class MibProfileType { Standards, Custom, Folder, Automatic };

struct MibProviderPin {
    QString canonicalPath;
    QString sha256;
    QString reason;
};

struct MibProfileScope {
    QString id;
    QString canonicalPath;
    bool operator==(const MibProfileScope &other) const {
        return id == other.id && canonicalPath == other.canonicalPath;
    }
};

enum class MibProfileMemberReason { Added, Dependency };
enum class MibProfileMemberState { Current, Missing, Changed, CatalogStale };

struct MibProfileMember {
    QString canonicalPath;
    QString sha256;
    QStringList identities;
    MibProfileMemberReason reason = MibProfileMemberReason::Added;
    bool operator==(const MibProfileMember &other) const {
        return canonicalPath == other.canonicalPath && sha256 == other.sha256 &&
            identities == other.identities && reason == other.reason;
    }
};

// A folder a Custom profile follows: every MIB file in it, at any depth, is a member except the
// files the user excluded. Membership is worked out from the Library index whenever the profile
// is planned, so MIBs later added to or removed from the folder change the profile.
struct MibProfileFolder {
    QString canonicalPath;
    QStringList excludedFiles;   // canonical paths of files inside the folder that are left out
    bool operator==(const MibProfileFolder &other) const {
        return canonicalPath == other.canonicalPath && excludedFiles == other.excludedFiles;
    }
};

struct MibProfileRecord {
    QString id;
    QString name;
    MibProfileType type = MibProfileType::Custom;
    QStringList explicitModules;
    bool includeStandardBase = false;
    QString directory;
    QMap<QString, MibProviderPin> providerPins;
    QList<MibProfileMember> members;
    QStringList unresolvedLegacyModules;
    int manifestVersion = 0;
    QList<MibProfileMember> roots;
    QList<MibProfileScope> orderedScopes;
    QList<MibProfileFolder> folders;   // Custom profiles only; see MibProfileFolder
};

MibProfileMemberState MibProfileMemberCurrentState(const MibProfileMember &member);
QList<MibProfileMember> MibProfileMembersFromFiles(
    const QStringList &files, MibProfileMemberReason reason = MibProfileMemberReason::Added,
    QStringList *diagnostics = nullptr);
QStringList MibProfileMemberIdentities(const QList<MibProfileMember> &members);
bool MibProfileRequiresExactMigration(const MibProfileRecord &profile);
bool MibProfileIsManifest(const MibProfileRecord &profile);
// Where imports are looked up first: the followed folders (in the order they were added), then
// the profile's other scopes.
QStringList MibProfileScopePaths(const MibProfileRecord &profile);
// True when `path` is the folder itself or lies anywhere below it.
bool MibPathIsInsideFolder(const QString &path, const QString &folder);
// The Library files a followed folder contributes (MIB files only: unreadable files and files
// that declare no module are skipped). Excluded files are left out unless `includeExcluded`.
QList<MibProfileMember> MibProfileFolderMembers(const MibProfileFolder &folder,
                                                const QList<MibDependencyFileRecord> &files,
                                                bool includeExcluded = false);
// The profile with each followed folder's files added to its roots (exact roots keep their own
// recorded hash). Followed folders that no longer exist are listed in `missingFolders`.
MibProfileRecord MibProfileWithFolderFiles(const MibProfileRecord &profile,
                                           const QList<MibDependencyFileRecord> &files,
                                           QStringList *missingFolders = nullptr);

class MibProfileDefinitions
{
public:
    static QString standardsId();
    static QStringList standardsModules();
    static QList<MibProfileRecord> builtIns();
    static QString validCurrentId(const QString &savedId,
                                  const QList<MibProfileRecord> &profiles);
};

class MibProfileRepository
{
public:
    explicit MibProfileRepository(QString path);
    QList<MibProfileRecord> load(QString *error = nullptr) const;
    bool save(const QList<MibProfileRecord> &profiles, QString *error = nullptr) const;
    bool ordinaryProfileMigrationComplete() const;
    QString path() const { return filePath; }
    // Followed folders are kept in a companion file next to the profile file, so a build that
    // predates folder members can read and rewrite the profile file without losing them.
    QString foldersPath() const;
private:
    QString filePath;
};

class MibProfileService
{
public:
    explicit MibProfileService(MibProfileRepository repository);
    bool reload(QString *error = nullptr);
    bool persist(QString *error = nullptr) const;
    QList<MibProfileRecord> profiles() const;
    const MibProfileRecord *find(const QString &id) const;
    QString create(const QString &name, QString *error = nullptr);
    QString duplicate(const QString &id, const QString &name, QString *error = nullptr);
    bool rename(const QString &id, const QString &name, QString *error = nullptr);
    bool remove(const QString &id, QString *error = nullptr);
    bool update(const MibProfileRecord &profile, QString *error = nullptr);
    bool importCustomProfile(const QString &stableId, const QString &name,
                             const QStringList &modules, QString *error = nullptr);
    bool requiresLegacyConversion() const;
    bool hasUnresolvedLegacyResidue() const;
    bool migrateLegacyProfiles(const MibDependencyIndex &index, QString *error = nullptr);
    // Compatibility conversion only: snapshots persisted legacy folder Profiles
    // once. It never refreshes current Profile membership.
    bool migrateLegacyFolderProfiles(QString *error = nullptr);
    bool addFiles(const QString &id, const QStringList &files,
                  MibProfileMemberReason reason = MibProfileMemberReason::Added,
                  QString *error = nullptr);
    bool addFolder(const QString &id, const QString &folder, QString *error = nullptr);
    // Followed folders (Custom profiles only). Adding a folder that is already followed changes
    // nothing; removing one also forgets its exclusions.
    bool followFolders(const QString &id, const QStringList &folders, QString *error = nullptr);
    bool unfollowFolders(const QString &id, const QStringList &folders, QString *error = nullptr);
    // Leaves files inside followed folders out of the profile, or takes them back in.
    bool setFilesExcluded(const QString &id, const QStringList &files, bool excluded,
                          QString *error = nullptr);
    // After the user saves a MIB from the editor, every profile root at that path
    // records the new content hash so the root is not later reported as modified.
    // Returns how many roots were updated.
    int refreshRootHash(const QString &path, const QString &sha256, QString *error = nullptr);
    // Replaces the derived Automatic profiles (see MibBuildAutomaticProfiles). They are held in
    // memory only. Returns true when the list changed.
    bool setAutomaticProfiles(const QList<MibProfileRecord> &profiles);
private:
    static bool isBuiltIn(const QString &id);
    MibProfileRepository repository;
    QList<MibProfileRecord> builtInProfiles = MibProfileDefinitions::builtIns();
    QList<MibProfileRecord> customProfiles;
    QList<MibProfileRecord> folderProfiles;
    QList<MibProfileRecord> automaticProfiles;
};

enum class MibProfileModuleAdditionStatus {
    Updated,
    Unchanged,
    Missing,
    ReadOnly,
    PersistenceFailed
};

struct MibProfileModuleAdditionResult {
    MibProfileModuleAdditionStatus status = MibProfileModuleAdditionStatus::Missing;
    QStringList addedModules;
    QString error;
};

// One Automatic profile for every Vendor/Product folder of the Library: files directly under
// <root>/<Vendor>/<Product>/ (at any depth below it). "Standards" and "Unassigned" at the first
// level are reserved: they take part in the Library but never become profiles. Files that sit
// directly in a vendor folder, or loose in the root, belong to no product and are not included
// in any profile; the vendor folder is searched for imports. Nothing here knows a vendor name.
QList<MibProfileRecord> MibBuildAutomaticProfiles(const QString &libraryRoot,
                                                  const QList<MibDependencyFileRecord> &files);

MibProfileModuleAdditionResult MibAddModulesToEditableProfile(
    MibProfileService &service, const QString &profileId, const QStringList &moduleIdentities);

struct MibProfileDependencyRefreshResult {
    MibProfileRecord profile;
    QList<MibProfileMember> addedDependencies;
    QStringList unresolved;
};

MibProfileDependencyRefreshResult MibResolveProfileDependencies(
    const MibProfileRecord &profile, const MibDependencyIndex &index);

#endif
