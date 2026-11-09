#include "mibcollection.h"

#include "mibcandidatefilter.h"

#include <QCryptographicHash>
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QSet>

namespace {
QByteArray digest(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly)
        ? QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256)
        : QByteArray();
}

bool copySafely(const QString &source, const QString &destination,
                MibCollectionResult *result, bool standard)
{
    if (QFileInfo::exists(destination)) {
        if (!digest(source).isEmpty() && digest(source) == digest(destination)) {
            ++result->identicalSkipped;
            return true;
        }
        result->conflicts.append(destination);
        return true;
    }
    if (!QFile::copy(source, destination)) {
        result->error = QObject::tr("Could not copy %1 to %2").arg(source, destination);
        return false;
    }
    if (standard) ++result->standardsCopied;
    else ++result->importedCopied;
    return true;
}

}

MibCollection::MibCollection(QString value)
    : root(QDir::cleanPath(value.isEmpty() ? defaultRoot() : value)) {}

QString MibCollection::defaultRoot()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))
        .filePath(QStringLiteral("MIB Navigator/MIBs"));
}

QString MibCollection::legacyManagedRoot()
{
    if (QCoreApplication::instance()) {
        const QString isolated = QCoreApplication::instance()
            ->property("snmpb.isolatedStateRoot").toString();
        if (!isolated.isEmpty()) return QDir(isolated).filePath(QStringLiteral("mibs"));
    }
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
        .filePath(QStringLiteral("mibs"));
}

QString MibCollection::internalStateRoot()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
        .filePath(QStringLiteral("SnmpB/mibs"));
}

QString MibCollection::configuredRoot(QSettings &settings)
{
    return QDir::cleanPath(settings.value(QStringLiteral("mib-library/root"),
                                           defaultRoot()).toString());
}

bool MibCollection::setConfiguredRoot(QSettings &settings, const QString &value,
                                      const QStringList &bundledPaths,
                                      MibCollectionResult *output)
{
    MibCollectionResult result;
    const QString cleaned = QDir::cleanPath(value.trimmed());
    if (cleaned.isEmpty() || !QDir().mkpath(cleaned)) {
        result.error = QObject::tr("The selected MIB Library Location cannot be created");
    } else {
        QFileInfo rootInfo(cleaned);
        if (!rootInfo.isDir() || !rootInfo.isWritable())
            result.error = QObject::tr("The selected MIB Library Location is not writable");
        else
            result = MibCollection(cleaned).initialize(bundledPaths);
    }
    if (result.success) {
        settings.setValue(QStringLiteral("mib-library/root"), cleaned);
        settings.setValue(QStringLiteral("mib-library/migration-v1-complete"), true);
        settings.sync();
        if (settings.status() != QSettings::NoError) {
            result.success = false;
            result.error = QObject::tr("The MIB Library Location could not be saved");
        }
    }
    if (output) *output = result;
    return result.success;
}

QString MibCollection::standardsPath() const { return QDir(root).filePath("Standards"); }
QString MibCollection::unassignedPath() const { return QDir(root).filePath("Unassigned"); }
QString MibCollection::prototypeProfilesPath() const { return QDir(root).filePath("Profiles"); }

QStringList MibCollection::runtimeSearchPaths() const
{
    QStringList paths{standardsPath(), unassignedPath()};
    const QDir rootDirectory(root);
    for (const QFileInfo &entry : rootDirectory.entryInfoList(
             QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable, QDir::Name | QDir::IgnoreCase)) {
        if (entry.fileName().compare(QStringLiteral("Standards"), Qt::CaseInsensitive) == 0 ||
            entry.fileName().compare(QStringLiteral("Unassigned"), Qt::CaseInsensitive) == 0 ||
            entry.fileName().compare(QStringLiteral("Library"), Qt::CaseInsensitive) == 0)
            continue;
        paths.append(QDir::cleanPath(entry.absoluteFilePath()));
    }
    const QString prototypeLibrary = QDir(root).filePath(QStringLiteral("Library"));
    if (QDir(prototypeLibrary).exists()) paths.append(prototypeLibrary);
    QDirIterator descendants(root, QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable,
                             QDirIterator::Subdirectories);
    while (descendants.hasNext()) paths.append(QDir::cleanPath(descendants.next()));
    paths.removeDuplicates();
    return paths;
}

MibCollectionResult MibCollection::initialize(const QStringList &bundledPaths,
                                               const QString &legacyRoot) const
{
    MibCollectionResult result;
    for (const QString &path : {standardsPath(), unassignedPath()})
        if (!QDir().mkpath(path)) {
            result.error = QObject::tr("Could not create required MIB directory: %1").arg(path);
            return result;
        }
    QSet<QString> baselineHashes;
    for (const QString &path : bundledPaths) {
        QDirIterator files(path, QDir::Files | QDir::Readable);
        QStringList sources;
        while (files.hasNext()) sources.append(files.next());
        // The configured collection may be below a source directory (notably
        // in tests and portable installs).  Snapshot candidates before making
        // destination directories so iterator mutation cannot skip sources.
        for (const QString &source : sources) {
            if (!MibCandidateFilter::accepts(QFileInfo(source).fileName())) continue;
            baselineHashes.insert(QString::fromLatin1(digest(source)));
            if (!copySafely(source, QDir(standardsPath()).filePath(QFileInfo(source).fileName()),
                            &result, true)) return result;
        }
    }
    const QString legacyDownloaded = legacyRoot.isEmpty() ? QString()
        : QDir(legacyRoot).filePath(QStringLiteral("downloaded"));
    if (!legacyDownloaded.isEmpty() && QDir(legacyDownloaded).exists()) {
        QDirIterator files(legacyDownloaded, QDir::Files | QDir::Readable);
        while (files.hasNext()) {
            const QString source = files.next();
            if (!MibCandidateFilter::accepts(QFileInfo(source).fileName()) ||
                baselineHashes.contains(QString::fromLatin1(digest(source)))) continue;
            if (!copySafely(source, QDir(unassignedPath()).filePath(QFileInfo(source).fileName()),
                            &result, false)) return result;
        }
    }
    // Conservatively transition the development prototype by copying its
    // library contents into the new trees. Originals are deliberately kept.
    const QList<QPair<QString, QString>> prototypeTrees{
        {QDir(root).filePath(QStringLiteral("Library/Standards")), standardsPath()},
        {QDir(root).filePath(QStringLiteral("Library/Imported")), unassignedPath()}};
    for (const auto &tree : prototypeTrees) {
        if (!QDir(tree.first).exists()) continue;
        QDirIterator files(tree.first, QDir::Files | QDir::Readable,
                           QDirIterator::Subdirectories);
        while (files.hasNext()) {
            const QString source = files.next();
            if (!MibCandidateFilter::accepts(QFileInfo(source).fileName())) continue;
            const QString destination = QDir(tree.second).filePath(
                QDir(tree.first).relativeFilePath(source));
            if (!QDir().mkpath(QFileInfo(destination).absolutePath()) ||
                !copySafely(source, destination, &result, tree.second == standardsPath()))
                return result;
        }
    }
    result.success = true;
    return result;
}

MibCollectionResult MibCollection::importFiles(const QStringList &sourceFiles) const
{
    MibCollectionResult result;
    if (!QDir().mkpath(unassignedPath())) {
        result.error = QObject::tr("Could not create the MIB Library import directory: %1")
            .arg(unassignedPath());
        return result;
    }
    for (const QString &source : sourceFiles) {
        const QFileInfo info(source);
        if (!info.isFile() || !info.isReadable() ||
            !MibCandidateFilter::accepts(info.fileName())) {
            result.error = QObject::tr("Not a readable MIB file: %1").arg(source);
            return result;
        }
        if (!copySafely(source, QDir(unassignedPath()).filePath(info.fileName()),
                        &result, false)) return result;
    }
    result.success = true;
    return result;
}
