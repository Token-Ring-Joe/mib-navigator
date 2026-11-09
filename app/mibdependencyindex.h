#ifndef MIBDEPENDENCYINDEX_H
#define MIBDEPENDENCYINDEX_H

#include <QDateTime>
#include <QList>
#include <QMap>
#include <QStringList>
#include <functional>

struct MibDependencyFileRecord {
    QString canonicalPath;
    int searchPathPrecedence = -1;
    QString filename;
    qint64 size = -1;
    qint64 modifiedMsecs = -1;
    QString sha256;
    QMap<QString, QStringList> importsByModule;
    QString checkState;
    QString diagnostic;
    QDateTime lastCheckedUtc;
};

struct MibDependencyScanResult {
    int scanned = 0;
    int reused = 0;
    int deleted = 0;
    qint64 elapsedMsecs = 0;
    bool changed = false;
};

struct MibPhysicalCandidate {
    QString canonicalPath;
    int searchPathPrecedence = -1;
    QString filename;
    qint64 size = -1;
    qint64 modifiedMsecs = -1;
    bool indexed = false;
    bool changed = true;
};
struct MibDependencyInspection {
    QList<MibPhysicalCandidate> candidates;
    int unchanged = 0;
    int newOrChanged = 0;
    int deleted = 0;
    qint64 elapsedMsecs = 0;
    bool stale() const { return newOrChanged > 0 || deleted > 0; }
};

struct MibRuntimeRequestNormalization {
    QStringList identities;
    int inputCount = 0;
    int legacyFilenameCount = 0;
    int identityCount = 0;
    int unresolvedCount = 0;
    int duplicateCount = 0;
};

enum class MibDependencyIndexLoadStatus {
    NotLoaded, Loaded, Missing, EmptyFile, MalformedJson, UnsupportedSchema,
    LibraryMismatch, ReadError
};

enum class MibProviderStatus { Found, Missing, Ambiguous };
struct MibProviderResolution {
    MibProviderStatus status = MibProviderStatus::Missing;
    QString path;
    QStringList alternatives;
};

struct MibIndexedProvider {
    QString moduleName;
    QString canonicalPath;
    int searchPathPrecedence = -1;
    QString filename;
    QString sha256;
    QString origin;
    QStringList imports;
    QString checkState;
};

enum class MibCatalogProviderScope { ExactFolder, Subtree, Global };

struct MibProfileDependencyCheck {
    QString profileSignature;
    quint64 indexGeneration = 0;
    QStringList effectiveModules;
    QStringList dependencies;
    QStringList unresolved;
    QStringList failureSummaries;
    QDateTime checkedUtc;
    qint64 elapsedMsecs = 0;
};

class MibDependencyIndex
{
public:
    explicit MibDependencyIndex(QString path = {});
    MibDependencyIndex(QString path, QString libraryRoot);
    static QString defaultPath();
    static QString pathForLibraryRoot(const QString &libraryRoot);
    static MibDependencyIndex forLibraryRoot(const QString &libraryRoot);
    bool load(QString *error = nullptr);
    bool save(QString *error = nullptr) const;
    MibDependencyScanResult update(const QStringList &searchPaths, QString *error = nullptr);
    MibDependencyInspection inspect(const QStringList &searchPaths) const;
    MibProviderResolution provider(const QString &moduleName) const;
    QList<MibIndexedProvider> providersFor(const QString &moduleName) const;
    QList<MibIndexedProvider> providersFor(const QString &moduleName,
        const QString &folder, MibCatalogProviderScope scope) const;
    QStringList imports(const QString &moduleName) const;
    bool semanticallyVerified(const QString &moduleName) const;
    QStringList moduleNames() const;
    QList<MibDependencyFileRecord> files() const { return records; }
    quint64 generation() const { return currentGeneration; }
    QString path() const { return filePath; }
    QString libraryRoot() const { return ownedLibraryRoot; }
    bool ownsSnapshot(const QString &libraryRoot, quint64 generation) const;
    MibDependencyIndexLoadStatus loadStatus() const { return currentLoadStatus; }
    QString loadDiagnostic() const { return currentLoadDiagnostic; }
    void setProfileCheck(const QString &profileId, const MibProfileDependencyCheck &check);
    MibProfileDependencyCheck profileCheck(const QString &profileId) const;
    bool profileCheckCurrent(const QString &profileId, const QString &signature) const;
    static QString profileSignature(const QStringList &modules, bool includeStandardBase);
    void recordVerification(const QString &moduleName, bool success, const QString &diagnostic = {});
private:
    void rebuildProviders();
    QString filePath;
    QString ownedLibraryRoot;
    QList<MibDependencyFileRecord> records;
    QMap<QString, QList<int>> providers;
    QMap<QString, MibProfileDependencyCheck> profileChecks;
    quint64 currentGeneration = 0;
    MibDependencyIndexLoadStatus currentLoadStatus = MibDependencyIndexLoadStatus::NotLoaded;
    QString currentLoadDiagnostic;
};

struct MibEffectivePlan;

QString MibDependencyIndexLoadStatusText(MibDependencyIndexLoadStatus status);

enum class MibDependencyFailureKind {
    MissingProvider, AmbiguousProvider, ParserSemanticFailure, DependencyUnresolved
};
struct MibDependencyFailure {
    QString moduleName;
    MibDependencyFailureKind kind = MibDependencyFailureKind::DependencyUnresolved;
    QString detail;
};
struct MibDependencyLoadAttempt {
    bool success = false;
    QStringList loadedModuleNames;
    QString diagnostic;
};
struct MibDependencyCheckResult {
    QStringList requested;
    QStringList loaded;
    QStringList dependencies;
    QList<MibDependencyFailure> failures;
    int passes = 0;
    int noProgressStops = 0;
    qint64 elapsedMsecs = 0;
};

class MibBoundedDependencyLoader
{
public:
    using LoadFile = std::function<MibDependencyLoadAttempt(
        const QString &physicalPath, const QString &expectedModule)>;
    MibDependencyCheckResult load(const QStringList &roots,
                                  const MibDependencyIndex &index,
                                  const LoadFile &loadFile) const;
    MibDependencyCheckResult load(const MibEffectivePlan &plan,
                                  const LoadFile &loadFile) const;
};

QString MibDependencyFailureText(MibDependencyFailureKind kind);
QStringList MibDeclaredIdentitiesForCandidate(const QString &candidate,
                                              const MibDependencyIndex &index);
MibRuntimeRequestNormalization MibNormalizeRuntimeRequests(
    const QStringList &requests, const MibDependencyIndex &index);

#endif
