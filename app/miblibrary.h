#ifndef MIBLIBRARY_H
#define MIBLIBRARY_H

#include "mibrecords.h"

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QMap>
#include <QStringList>
#include <QUrl>
#include <functional>

enum class MibLibraryStatus {
    Standards, Installed, Available, Downloading, Failed, Invalid, Unresolved,
    Conflict
};

enum class MibValidationLevel { Errors, ErrorsAndWarnings, FullReview };
int MibValidationErrorLevel(MibValidationLevel level);
bool MibValidationRecursive(MibValidationLevel level);

struct MibCatalogEntry {
    QString sourceId;
    QString sourceName;
    QString category;
    QString moduleName;
    QString revision;
    QString url;
    QString filename;
    QString sha256;
    QString licenseUrl;
    QStringList imports;
};

struct MibLibraryRecord {
    QString moduleName;
    QString revision;
    QString sourceId;
    QString sourceName;
    QString sourceUrl;
    QString sourceFilename;
    QString localPath;
    QString sha256;
    QString validationMessage;
    QDateTime downloadedAt;
    MibLibraryStatus status = MibLibraryStatus::Unresolved;
};

struct MibDependencyNode {
    QString moduleName;
    MibLibraryStatus status = MibLibraryStatus::Unresolved;
    QList<MibDependencyNode> dependencies;
    bool cycle = false;
};

struct MibDependencyPlan {
    QList<MibDependencyNode> roots;
    QStringList orderedDownloads;
    QStringList unresolved;
};

class MibDownloadSessionState
{
public:
    void begin(const QString &moduleName) { failures.remove(moduleName); }
    void fail(const QString &moduleName, const QString &reason) { failures[moduleName] = reason; }
    void succeed(const QString &moduleName) { failures.remove(moduleName); }
    MibLibraryStatus status(const QString &moduleName, MibLibraryStatus base) const {
        return failures.contains(moduleName) ? MibLibraryStatus::Failed : base;
    }
    QString failureReason(const QString &moduleName) const { return failures.value(moduleName); }
private:
    QMap<QString, QString> failures;
};

class MibImportScanner
{
public:
    struct Result {
        QStringList moduleNames;
        QStringList imports;
        QMap<QString, QStringList> importsByModule;
        QMap<QString, QStringList> semanticDependenciesByModule;
        QString revision;
        bool malformedImports = false;
    };
    static Result scan(const QByteArray &content);
};

class MibCatalog
{
public:
    static bool parse(const QByteArray &json, MibCatalog *catalog,
                      QString *error = nullptr);
    QByteArray serialize() const;
    const QList<MibCatalogEntry> &entries() const { return items; }
    const MibCatalogEntry *find(const QString &moduleName) const;
    void setEntries(const QList<MibCatalogEntry> &value) { items = value; }
    void upsert(const MibCatalogEntry &entry);
private:
    QList<MibCatalogEntry> items;
};

class IanaMibSourceProvider
{
public:
    static QString id() { return QStringLiteral("iana"); }
    static QString displayName() { return QStringLiteral("IANA"); }
    static QUrl indexUrl();
    static bool parseIndex(const QByteArray &html, MibCatalog *catalog,
                           QString *error = nullptr);
};

struct MibCatalogCacheInfo {
    QDateTime refreshedAt;
    QString sourceId;
    QString sourceUrl;
};

class MibCatalogCache
{
public:
    static bool save(const QString &path, const MibCatalog &catalog,
                     const MibCatalogCacheInfo &info, QString *error = nullptr);
    static bool load(const QString &path, MibCatalog *catalog,
                     MibCatalogCacheInfo *info = nullptr, QString *error = nullptr);
};

class MibSourceProvider
{
public:
    virtual ~MibSourceProvider() = default;
    virtual QString id() const = 0;
    virtual QString displayName() const = 0;
    virtual QString category() const = 0;
    virtual MibCatalog catalog() const = 0;
};

class CatalogMibSourceProvider : public MibSourceProvider
{
public:
    CatalogMibSourceProvider(QString sourceId, QString name, QString sourceCategory,
                             MibCatalog sourceCatalog)
        : sourceId(std::move(sourceId)), name(std::move(name)),
          sourceCategory(std::move(sourceCategory)), sourceCatalog(std::move(sourceCatalog)) {}
    QString id() const override { return sourceId; }
    QString displayName() const override { return name; }
    QString category() const override { return sourceCategory; }
    MibCatalog catalog() const override { return sourceCatalog; }
private:
    QString sourceId, name, sourceCategory;
    MibCatalog sourceCatalog;
};

class MibDependencyResolver
{
public:
    MibDependencyPlan resolve(const QStringList &roots,
        const QMap<QString, MibLibraryStatus> &known,
        const MibCatalog &catalog) const;
};

class MibLibraryService
{
public:
    explicit MibLibraryService(QString root = {}, QString stateRoot = {});
    static QString defaultRoot();
    QString rootPath() const { return root; }
    QString downloadedPath() const;
    QString metadataPath() const;
    QString standardsPath() const;
    QString profilesPath() const;
    QList<MibLibraryRecord> inventory(const QStringList &bundledPaths,
                                      const MibCatalog &catalog = {},
                                      const QList<MibModuleRecord> &localModules = {}) const;
    bool install(const MibCatalogEntry &entry, const QByteArray &content,
                 const QStringList &bundledPaths, MibLibraryRecord *record,
                 QString *error = nullptr,
                 const std::function<bool(const QString &, QString *)> &validator = {});
private:
    static bool safeFilename(const QString &name);
    QString root;
    QString stateRoot;
};

class MibValidationStaging
{
public:
    static bool validate(const QByteArray &content, const QString &directory,
        const std::function<bool(const QString &)> &validator, QString *error = nullptr);
};

QString MibLibraryStatusText(MibLibraryStatus status);
QString MibLibraryOriginText(const MibLibraryRecord &record);

struct MibLibraryFileInfo {
    QString origin;
    QString revision;
    QString filename;
    QString localPath;
    QString provider;
    QString sourceUrl;
    QString timestamp;
    QString sha256;
    QString state;
    bool showProvider = false;
    bool showSourceUrl = false;
    bool showTimestamp = false;
    bool showSha256 = false;
    bool showState = false;
};
MibLibraryFileInfo MibLibraryFileInformation(const MibLibraryRecord &record,
                                             const QString &failureReason = {});

#endif
