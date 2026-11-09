#include "miblibrary.h"
#include "mibcollection.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QSet>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QUrl>
#include <algorithm>

namespace {
QString normalizedModule(QString value) { return value.trimmed(); }

MibLibraryStatus classify(const QString &name,
                          const QMap<QString, MibLibraryStatus> &known,
                          const MibCatalog &catalog)
{
    if (known.contains(name)) return known.value(name);
    return catalog.find(name) ? MibLibraryStatus::Available
                              : MibLibraryStatus::Unresolved;
}

MibDependencyNode visit(const QString &name,
    const QMap<QString, MibLibraryStatus> &known, const MibCatalog &catalog,
    QSet<QString> *visiting, QSet<QString> *planned, QStringList *ordered,
    QStringList *unresolved)
{
    MibDependencyNode node;
    node.moduleName = name;
    node.status = classify(name, known, catalog);
    if (visiting->contains(name)) { node.cycle = true; return node; }
    const MibCatalogEntry *entry = catalog.find(name);
    if (!entry) {
        if (node.status == MibLibraryStatus::Unresolved && !unresolved->contains(name))
            unresolved->append(name);
        return node;
    }
    visiting->insert(name);
    QStringList imports = entry->imports;
    imports.removeDuplicates();
    std::sort(imports.begin(), imports.end());
    for (const QString &dependency : imports)
        node.dependencies.append(visit(dependency, known, catalog, visiting,
                                       planned, ordered, unresolved));
    visiting->remove(name);
    if (node.status == MibLibraryStatus::Available && !planned->contains(name)) {
        planned->insert(name);
        ordered->append(name);
    }
    return node;
}
}

int MibValidationErrorLevel(MibValidationLevel level)
{
    switch (level) {
    case MibValidationLevel::Errors: return 3;
    case MibValidationLevel::ErrorsAndWarnings: return 5;
    case MibValidationLevel::FullReview: return 9;
    }
    return 5;
}

bool MibValidationRecursive(MibValidationLevel level)
{
    return level == MibValidationLevel::FullReview;
}

MibImportScanner::Result MibImportScanner::scan(const QByteArray &content)
{
    Result result;
    QString text = QString::fromUtf8(content);
    text.remove(QRegularExpression(QStringLiteral("--[^\\r\\n]*")));
    QRegularExpression moduleRe(
        QStringLiteral("(?mi)^\\s*([A-Za-z][A-Za-z0-9-]*)\\s+(?:PIB-)?DEFINITIONS\\s*::="));
    auto modules = moduleRe.globalMatch(text);
    QList<QPair<QString, int>> declarations;
    while (modules.hasNext()) {
        const auto declaration = modules.next();
        const QString name = normalizedModule(declaration.captured(1));
        if (!result.moduleNames.contains(name)) result.moduleNames.append(name);
        declarations.append({name, declaration.capturedStart()});
    }
    const QRegularExpression updatedRe(
        QStringLiteral("\\bLAST-UPDATED\\s+\"([0-9]{8,14}Z)\""),
        QRegularExpression::CaseInsensitiveOption);
    const auto updated = updatedRe.match(text);
    if (updated.hasMatch()) result.revision = updated.captured(1);
    QRegularExpression startRe(QStringLiteral("\\bIMPORTS\\b"),
                               QRegularExpression::CaseInsensitiveOption);
    auto starts = startRe.globalMatch(text);
    while (starts.hasNext()) {
        const auto match = starts.next();
        const int end = text.indexOf(';', match.capturedEnd());
        if (end < 0) { result.malformedImports = true; break; }
        const QString block = text.mid(match.capturedEnd(), end - match.capturedEnd());
        QRegularExpression fromRe(QStringLiteral("\\bFROM\\s+([A-Za-z][A-Za-z0-9-]*)"),
                                  QRegularExpression::CaseInsensitiveOption);
        auto from = fromRe.globalMatch(block);
        while (from.hasNext()) {
            const QString dependency = normalizedModule(from.next().captured(1));
            if (!result.imports.contains(dependency)) result.imports.append(dependency);
        }
    }
    for (int i = 0; i < declarations.size(); ++i) {
        const int begin = declarations[i].second;
        const int end = i + 1 < declarations.size() ? declarations[i + 1].second : text.size();
        const QString moduleText = text.mid(begin, end - begin); QStringList direct;
        auto moduleStarts = startRe.globalMatch(moduleText);
        while (moduleStarts.hasNext()) {
            const auto match = moduleStarts.next(); const int blockEnd = moduleText.indexOf(';', match.capturedEnd());
            if (blockEnd < 0) break;
            auto from = QRegularExpression(QStringLiteral("\\bFROM\\s+([A-Za-z][A-Za-z0-9-]*)"),
                QRegularExpression::CaseInsensitiveOption).globalMatch(moduleText.mid(match.capturedEnd(), blockEnd - match.capturedEnd()));
            while (from.hasNext()) { const QString dependency = normalizedModule(from.next().captured(1)); if (!direct.contains(dependency)) direct.append(dependency); }
        }
        QStringList semantic;
        QString semanticText = moduleText;
        semanticText.remove(QRegularExpression(QStringLiteral("\"(?:\"\"|[^\"])*\""),
            QRegularExpression::DotMatchesEverythingOption));
        auto semanticModules = QRegularExpression(
            QStringLiteral("(?m)^\\s*(?:SUPPORTS|MODULE)\\s+([A-Za-z][A-Za-z0-9-]*)\\s*$"),
            QRegularExpression::CaseInsensitiveOption).globalMatch(semanticText);
        while (semanticModules.hasNext()) {
            const QString dependency = normalizedModule(semanticModules.next().captured(1));
            static const QSet<QString> currentModuleClauses{
                QStringLiteral("MANDATORY-GROUPS"), QStringLiteral("GROUP"),
                QStringLiteral("OBJECT"), QStringLiteral("WRITE-SYNTAX"),
                QStringLiteral("MIN-ACCESS"), QStringLiteral("DESCRIPTION")};
            if (dependency != declarations[i].first &&
                !currentModuleClauses.contains(dependency.toUpper()) &&
                !semantic.contains(dependency))
                semantic.append(dependency);
        }
        semantic.sort(Qt::CaseSensitive);
        result.semanticDependenciesByModule.insert(declarations[i].first, semantic);
        for (const QString &dependency : std::as_const(semantic))
            if (!direct.contains(dependency)) direct.append(dependency);
        direct.sort(Qt::CaseSensitive);
        result.importsByModule.insert(declarations[i].first, direct);
    }
    return result;
}

bool MibCatalog::parse(const QByteArray &json, MibCatalog *catalog, QString *error)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    const QJsonObject root = document.object();
    if (parseError.error != QJsonParseError::NoError || root.value("version").toInt() != 1 ||
        !root.value("entries").isArray()) {
        if (error) *error = QStringLiteral("Catalog must be valid version 1 JSON");
        return false;
    }
    QList<MibCatalogEntry> parsed;
    for (const QJsonValue &value : root.value("entries").toArray()) {
        const QJsonObject object = value.toObject();
        MibCatalogEntry entry;
        entry.sourceId = object.value("sourceId").toString().trimmed();
        entry.sourceName = object.value("sourceName").toString().trimmed();
        entry.category = object.value("category").toString().trimmed();
        entry.moduleName = object.value("moduleName").toString().trimmed();
        entry.revision = object.value("revision").toString().trimmed();
        entry.url = object.value("url").toString().trimmed();
        entry.filename = object.value("filename").toString().trimmed();
        entry.sha256 = object.value("sha256").toString().toLower();
        entry.licenseUrl = object.value("licenseUrl").toString().trimmed();
        for (const QJsonValue &item : object.value("imports").toArray())
            entry.imports.append(item.toString().trimmed());
        const QUrl url(entry.url);
        if (entry.sourceId.isEmpty() || entry.moduleName.isEmpty() ||
            entry.filename.isEmpty() || entry.filename != QFileInfo(entry.filename).fileName() ||
            entry.filename.contains(QRegularExpression(QStringLiteral("[<>:\"/\\\\|?*]"))) ||
            !url.isValid() || (url.scheme() != "https" && url.scheme() != "http") ||
            (!entry.sha256.isEmpty() &&
             !QRegularExpression("^[0-9a-f]{64}$").match(entry.sha256).hasMatch())) {
            if (error) *error = QStringLiteral("Invalid catalog entry for %1").arg(entry.moduleName);
            return false;
        }
        parsed.append(entry);
    }
    if (catalog) catalog->items = parsed;
    return true;
}

QByteArray MibCatalog::serialize() const
{
    QJsonArray entries;
    for (const MibCatalogEntry &entry : items) {
        QJsonObject object{{"sourceId", entry.sourceId}, {"sourceName", entry.sourceName},
            {"category", entry.category}, {"moduleName", entry.moduleName},
            {"revision", entry.revision}, {"url", entry.url},
            {"filename", entry.filename}, {"sha256", entry.sha256},
            {"licenseUrl", entry.licenseUrl}};
        QJsonArray imports; for (const QString &item : entry.imports) imports.append(item);
        object.insert("imports", imports); entries.append(object);
    }
    return QJsonDocument(QJsonObject{{"version", 1}, {"entries", entries}}).toJson();
}

const MibCatalogEntry *MibCatalog::find(const QString &moduleName) const
{
    for (const MibCatalogEntry &entry : items)
        if (entry.moduleName == moduleName) return &entry;
    return nullptr;
}

void MibCatalog::upsert(const MibCatalogEntry &entry)
{
    for (MibCatalogEntry &item : items) {
        if (item.moduleName == entry.moduleName && item.sourceId == entry.sourceId) {
            item = entry;
            return;
        }
    }
    items.append(entry);
}

QUrl IanaMibSourceProvider::indexUrl()
{
    return QUrl(QStringLiteral("https://www.iana.org/protocols"));
}

bool IanaMibSourceProvider::parseIndex(const QByteArray &html, MibCatalog *catalog,
                                       QString *error)
{
    const QString text = QString::fromUtf8(html);
    const int heading = text.indexOf(QStringLiteral("IANA-Maintained MIBs"), 0,
                                     Qt::CaseInsensitive);
    if (heading < 0) {
        if (error) *error = QStringLiteral("IANA-Maintained MIBs section was not found");
        return false;
    }
    const int sectionEnd = text.indexOf(QStringLiteral("iCalendar Element Registries"),
                                        heading, Qt::CaseInsensitive);
    const QString section = text.mid(heading, sectionEnd < 0 ? -1 : sectionEnd - heading);
    const QRegularExpression linkRe(
        QStringLiteral("<a[^>]+href=[\"']([^\"']+)[\"'][^>]*>\\s*"
                       "(IANA[A-Za-z0-9-]*MIB)\\s*</a>"),
        QRegularExpression::CaseInsensitiveOption);
    QList<MibCatalogEntry> entries;
    QSet<QString> seen;
    auto matches = linkRe.globalMatch(section);
    while (matches.hasNext()) {
        const auto match = matches.next();
        const QString module = match.captured(2).trimmed();
        QUrl url = QUrl(QStringLiteral("https://www.iana.org")).resolved(QUrl(match.captured(1)));
        if (url.scheme() != QStringLiteral("https") ||
            url.host().compare(QStringLiteral("www.iana.org"), Qt::CaseInsensitive) != 0 ||
            !url.path().startsWith(QStringLiteral("/assignments/")) || seen.contains(module))
            continue;
        // The registry index links to an assignment landing alias. IANA's
        // standalone module artifact is the same assignment path with its
        // slug appended; using it keeps retrieval HTTPS and avoids the
        // landing alias's currently insecure HTTP Location response.
        QStringList segments = url.path().split('/', Qt::SkipEmptyParts);
        if (segments.size() == 2 && segments.first() == QStringLiteral("assignments")) {
            QString path = url.path();
            if (!path.endsWith('/')) path += '/';
            url.setPath(path + segments.last());
        }
        seen.insert(module);
        MibCatalogEntry entry;
        entry.sourceId = id();
        entry.sourceName = displayName();
        entry.category = QStringLiteral("Standards MIB");
        entry.moduleName = module;
        entry.url = url.toString(QUrl::FullyEncoded);
        entry.filename = module + QStringLiteral(".mib");
        entries.append(entry);
    }
    if (entries.isEmpty()) {
        if (error) *error = QStringLiteral("IANA MIB index contained no valid module links");
        return false;
    }
    std::sort(entries.begin(), entries.end(), [](const auto &a, const auto &b) {
        return a.moduleName < b.moduleName;
    });
    if (catalog) catalog->setEntries(entries);
    return true;
}

bool MibCatalogCache::save(const QString &path, const MibCatalog &catalog,
                           const MibCatalogCacheInfo &info, QString *error)
{
    MibCatalog checked;
    QString parseError;
    if (!MibCatalog::parse(catalog.serialize(), &checked, &parseError)) {
        if (error) *error = parseError;
        return false;
    }
    QDir().mkpath(QFileInfo(path).absolutePath());
    QJsonObject root = QJsonDocument::fromJson(catalog.serialize()).object();
    root.insert(QStringLiteral("refreshedAt"), info.refreshedAt.toUTC().toString(Qt::ISODate));
    root.insert(QStringLiteral("sourceId"), info.sourceId);
    root.insert(QStringLiteral("sourceUrl"), info.sourceUrl);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) ||
        file.write(QJsonDocument(root).toJson()) < 0 || !file.commit()) {
        if (error) *error = QStringLiteral("Could not atomically save catalog cache");
        return false;
    }
    return true;
}

bool MibCatalogCache::load(const QString &path, MibCatalog *catalog,
                           MibCatalogCacheInfo *info, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("Catalog cache is unavailable");
        return false;
    }
    const QByteArray bytes = file.readAll();
    if (!MibCatalog::parse(bytes, catalog, error)) return false;
    if (info) {
        const QJsonObject root = QJsonDocument::fromJson(bytes).object();
        info->refreshedAt = QDateTime::fromString(root.value("refreshedAt").toString(), Qt::ISODate);
        info->sourceId = root.value("sourceId").toString();
        info->sourceUrl = root.value("sourceUrl").toString();
    }
    return true;
}

MibDependencyPlan MibDependencyResolver::resolve(const QStringList &roots,
    const QMap<QString, MibLibraryStatus> &known, const MibCatalog &catalog) const
{
    MibDependencyPlan plan; QSet<QString> visiting, planned;
    QStringList sortedRoots = roots; sortedRoots.removeDuplicates();
    std::sort(sortedRoots.begin(), sortedRoots.end());
    for (const QString &root : sortedRoots)
        plan.roots.append(visit(root, known, catalog, &visiting, &planned,
                                &plan.orderedDownloads, &plan.unresolved));
    return plan;
}

MibLibraryService::MibLibraryService(QString value, QString state)
    : root(value.isEmpty() ? defaultRoot() : QDir::cleanPath(value)),
      stateRoot(state.isEmpty() ? MibCollection::legacyManagedRoot() : QDir::cleanPath(state)) {}

QString MibLibraryService::defaultRoot()
{
    QSettings settings;
    return MibCollection::configuredRoot(settings);
}
QString MibLibraryService::downloadedPath() const { return MibCollection(root).importedPath(); }
QString MibLibraryService::standardsPath() const { return MibCollection(root).standardsPath(); }
QString MibLibraryService::profilesPath() const { return root; }
QString MibLibraryService::metadataPath() const
{
    return QDir(stateRoot).filePath("metadata");
}

bool MibLibraryService::safeFilename(const QString &name)
{
    return !name.isEmpty() && name == QFileInfo(name).fileName() && name != "." && name != ".." &&
        !name.contains(QRegularExpression(QStringLiteral("[<>:\"/\\\\|?*]")));
}

QList<MibLibraryRecord> MibLibraryService::inventory(const QStringList &bundledPaths,
                                                      const MibCatalog &catalog,
                                                      const QList<MibModuleRecord> &localModules) const
{
    QMap<QString, MibLibraryRecord> records;
    auto scan = [&](const QStringList &paths, MibLibraryStatus status, const QString &source) {
        for (const QString &path : paths) {
            QDirIterator files(path, QDir::Files | QDir::Readable, QDirIterator::Subdirectories);
            while (files.hasNext()) {
                const QFileInfo file(files.next());
                QFile input(file.absoluteFilePath()); if (!input.open(QIODevice::ReadOnly)) continue;
                const auto parsed = MibImportScanner::scan(input.readAll());
                for (const QString &name : parsed.moduleNames) {
                    MibLibraryRecord record; record.moduleName = name; record.localPath = file.absoluteFilePath();
                    record.sourceName = source; record.status = status;
                    if (records.contains(name) && records[name].localPath != record.localPath)
                        records[name].status = MibLibraryStatus::Conflict;
                    else records.insert(name, record);
                }
            }
        }
    };
    Q_UNUSED(bundledPaths)
    scan({standardsPath()}, MibLibraryStatus::Standards, QStringLiteral("Standards"));
    QStringList localPaths = MibCollection(root).runtimeSearchPaths();
    const QString standardsPrefix = QDir::cleanPath(standardsPath()) + QDir::separator();
    localPaths.erase(std::remove_if(localPaths.begin(), localPaths.end(),
        [&standardsPrefix, this](const QString &path) {
            const QString clean = QDir::cleanPath(path);
            return clean == standardsPath() || clean.startsWith(standardsPrefix, Qt::CaseInsensitive);
        }), localPaths.end());
    scan(localPaths, MibLibraryStatus::Installed, QStringLiteral("Local library"));
    for (const MibModuleRecord &module : localModules) {
        if (module.name.isEmpty() || module.path.isEmpty() || records.contains(module.name)) continue;
        MibLibraryRecord record;
        record.moduleName = module.name;
        record.localPath = module.path;
        record.revision = module.lastRevision.isValid()
            ? module.lastRevision.toUTC().toString(Qt::ISODate) : QString();
        record.sourceName = QStringLiteral("Local");
        record.status = MibLibraryStatus::Installed;
        records.insert(record.moduleName, record);
    }
    for (auto iterator = records.begin(); iterator != records.end(); ++iterator) {
        QFile metadata(QDir(metadataPath()).filePath(iterator.key() + ".json"));
        if (!metadata.open(QIODevice::ReadOnly)) continue;
        const QJsonObject object = QJsonDocument::fromJson(metadata.readAll()).object();
        iterator->revision = object.value("revision").toString();
        iterator->sourceId = object.value("sourceId").toString();
        iterator->sourceName = object.value("sourceName").toString(iterator->sourceName);
        iterator->sourceUrl = object.value("sourceUrl").toString();
        iterator->sourceFilename = object.value("sourceFilename").toString();
        iterator->sha256 = object.value("sha256").toString();
        iterator->downloadedAt = QDateTime::fromString(
            object.value("downloadedAt").toString(), Qt::ISODate);
    }
    for (const MibCatalogEntry &entry : catalog.entries()) if (!records.contains(entry.moduleName)) {
        MibLibraryRecord record; record.moduleName = entry.moduleName; record.revision = entry.revision;
        record.sourceId = entry.sourceId; record.sourceName = entry.sourceName;
        record.sourceUrl = entry.url; record.status = MibLibraryStatus::Available;
        records.insert(entry.moduleName, record);
    }
    return records.values();
}

bool MibLibraryService::install(const MibCatalogEntry &entry, const QByteArray &content,
    const QStringList &bundledPaths, MibLibraryRecord *record, QString *error,
    const std::function<bool(const QString &, QString *)> &validator)
{
    if (!safeFilename(entry.filename)) { if (error) *error = "Unsafe catalog filename"; return false; }
    const QByteArray checksum = QCryptographicHash::hash(content, QCryptographicHash::Sha256).toHex();
    if (!entry.sha256.isEmpty() && checksum != entry.sha256.toLatin1()) {
        if (error) *error = "SHA-256 checksum mismatch"; return false;
    }
    const auto scanned = MibImportScanner::scan(content);
    if (!scanned.moduleNames.contains(entry.moduleName)) {
        if (error) *error = "Downloaded module identity does not match catalog"; return false;
    }
    for (const MibLibraryRecord &item : inventory(bundledPaths))
        if (item.moduleName == entry.moduleName && item.status == MibLibraryStatus::Standards) {
            if (error) *error = "This module already exists in the Standards folder. Edit it there, or remove it first."; return false;
        }
    QDir().mkpath(downloadedPath()); QDir().mkpath(metadataPath());
    const QString cachePath = QDir(root).filePath("cache"); QDir().mkpath(cachePath);
    if (validator) {
        QTemporaryFile staged(QDir(cachePath).filePath("validate-XXXXXX.mib"));
        if (!staged.open() || staged.write(content) != content.size() || !staged.flush()) {
            if (error) *error = "Validation staging failed"; return false;
        }
        QString validationError;
        if (!validator(staged.fileName(), &validationError)) {
            if (error) *error = validationError.isEmpty() ? "libsmi validation failed" : validationError;
            return false;
        }
    }
    const QString target = QDir(downloadedPath()).filePath(entry.filename);
    if (QFileInfo::exists(target)) { if (error) *error = "Downloaded module already exists"; return false; }
    QSaveFile output(target);
    if (!output.open(QIODevice::WriteOnly) || output.write(content) != content.size() || !output.commit()) {
        if (error) *error = "Atomic module install failed"; return false;
    }
    const QDateTime now = QDateTime::currentDateTimeUtc();
    QJsonObject provenance{{"version", 1}, {"moduleName", entry.moduleName},
        {"revision", entry.revision}, {"sourceId", entry.sourceId},
        {"sourceName", entry.sourceName}, {"sourceUrl", entry.url},
        {"sourceFilename", entry.filename}, {"localPath", target},
        {"sha256", QString::fromLatin1(checksum)}, {"downloadedAt", now.toString(Qt::ISODate)}};
    QSaveFile metadata(QDir(metadataPath()).filePath(entry.moduleName + ".json"));
    if (!metadata.open(QIODevice::WriteOnly) || metadata.write(QJsonDocument(provenance).toJson()) < 0 ||
        !metadata.commit()) { QFile::remove(target); if (error) *error = "Provenance install failed"; return false; }
    if (record) { record->moduleName = entry.moduleName; record->revision = entry.revision;
        record->sourceId = entry.sourceId; record->sourceName = entry.sourceName;
        record->sourceUrl = entry.url; record->sourceFilename = entry.filename;
        record->localPath = target; record->sha256 = QString::fromLatin1(checksum);
        record->downloadedAt = now; record->status = MibLibraryStatus::Installed; }
    return true;
}

QString MibLibraryStatusText(MibLibraryStatus status)
{
    switch (status) {
    case MibLibraryStatus::Standards: return "Standards";
    case MibLibraryStatus::Installed: return "Downloaded / Installed";
    case MibLibraryStatus::Available: return "Available";
    case MibLibraryStatus::Downloading: return "Downloading";
    case MibLibraryStatus::Failed: return "Failed";
    case MibLibraryStatus::Invalid: return "Invalid";
    case MibLibraryStatus::Conflict: return "Conflict";
    default: return "Unresolved";
    }
}

QString MibLibraryOriginText(const MibLibraryRecord &record)
{
    if (record.status == MibLibraryStatus::Standards) return QStringLiteral("Standards");
    if (record.sourceId.compare(IanaMibSourceProvider::id(), Qt::CaseInsensitive) == 0 ||
        record.sourceName.compare(QStringLiteral("IANA"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("IANA");
    if (!record.localPath.isEmpty()) return QStringLiteral("Imported");
    return record.sourceName.isEmpty() ? QStringLiteral("Catalog") : record.sourceName;
}

MibLibraryFileInfo MibLibraryFileInformation(const MibLibraryRecord &record,
                                              const QString &failureReason)
{
    const QString unavailable = QStringLiteral("—");
    MibLibraryFileInfo info;
    info.origin = MibLibraryOriginText(record);
    info.revision = record.revision.isEmpty() ? unavailable : record.revision;
    info.filename = record.sourceFilename.isEmpty()
        ? QFileInfo(record.localPath).fileName() : record.sourceFilename;
    if (info.filename.isEmpty()) info.filename = unavailable;
    info.localPath = record.localPath.isEmpty() ? unavailable : record.localPath;
    info.provider = record.sourceName;
    info.sourceUrl = record.sourceUrl;
    info.timestamp = record.downloadedAt.isValid()
        ? record.downloadedAt.toLocalTime().toString(Qt::ISODate) : QString();
    info.sha256 = record.sha256;
    info.showProvider = record.status != MibLibraryStatus::Standards && !info.provider.isEmpty();
    info.showSourceUrl = !info.sourceUrl.isEmpty();
    info.showTimestamp = record.downloadedAt.isValid();
    info.showSha256 = !info.sha256.isEmpty();
    info.showState = !failureReason.isEmpty() ||
        record.status == MibLibraryStatus::Downloading ||
        record.status == MibLibraryStatus::Failed || record.status == MibLibraryStatus::Invalid ||
        record.status == MibLibraryStatus::Conflict;
    info.state = failureReason.isEmpty() ? MibLibraryStatusText(record.status)
                                        : QObject::tr("Failed: %1").arg(failureReason);
    return info;
}

bool MibValidationStaging::validate(const QByteArray &content, const QString &directory,
    const std::function<bool(const QString &)> &validator, QString *error)
{
    QDir().mkpath(directory);
    QTemporaryFile staged(QDir(directory).filePath("mib-navigator-verify-XXXXXX.mib"));
    if (!staged.open() || staged.write(content) != content.size() || !staged.flush()) {
        if (error) *error = QStringLiteral("Could not stage current editor content");
        return false;
    }
    return validator && validator(staged.fileName());
}
