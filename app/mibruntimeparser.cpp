#include "mibruntimeparser.h"

#include "mibengine.h"
#include "smi.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <algorithm>
#include <condition_variable>
#include <mutex>

namespace {
QString canonicalPath(const QString &path)
{
    const QString normalized = QDir::fromNativeSeparators(path);
    const QString canonical = QFileInfo(normalized).canonicalFilePath();
    return QDir::cleanPath(canonical.isEmpty() ? QFileInfo(normalized).absoluteFilePath()
                                               : canonical);
}

bool pathWithin(const QString &path, const QString &directory)
{
    QString root = QDir::fromNativeSeparators(canonicalPath(directory));
    const QString candidate = QDir::fromNativeSeparators(canonicalPath(path));
    if (!root.endsWith('/')) root += '/';
#ifdef Q_OS_WIN
    return candidate.startsWith(root, Qt::CaseInsensitive);
#else
    return candidate.startsWith(root, Qt::CaseSensitive);
#endif
}

QString fileSha256(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly)
        ? QString::fromLatin1(QCryptographicHash::hash(file.readAll(),
              QCryptographicHash::Sha256).toHex())
        : QString();
}
}

QStringList MibExplicitRootLoadBatch::failedIdentities() const
{
    QStringList result;
    for (const auto &root : roots) if (!root.success) result.append(root.identity);
    return result;
}

MibRuntimeParserResetResult MibRuntimeParser::reset(
    const MibRuntimePathConfiguration &paths,
    const std::function<void()> &restoreApplicationConfiguration)
{
    MibRuntimeParserResetResult result;
    if (!paths.isValid()) {
        result.error = paths.diagnostics().join(QStringLiteral("; "));
        return result;
    }
    MibEngine &engine = MibEngine::instance();
    if (!engine.isWorkerThread()) {
        std::mutex mutex;
        std::condition_variable done;
        bool complete = false;
        engine.submit([&] {
            result = reset(paths, restoreApplicationConfiguration);
            std::lock_guard lock(mutex);
            complete = true;
            done.notify_one();
        });
        std::unique_lock lock(mutex);
        done.wait(lock, [&] { return complete; });
        return result;
    }

    auto operation = engine.beginOperation(QStringLiteral("profile-runtime-parser-reset"));
    const int requiredFlags = smiGetFlags() | SMI_FLAG_ERRORS;
    smiExit();
    smiInit(nullptr);
    smiSetFlags(requiredFlags);
    result.appliedPaths = paths.orderedPaths();
    const QByteArray encoded = result.appliedPaths.join(QDir::listSeparator()).toLocal8Bit();
    if (smiSetPath(encoded.constData()) != 0) {
        result.error = QStringLiteral("libsmi rejected the Profile runtime search path");
        return result;
    }
    if (restoreApplicationConfiguration) restoreApplicationConfiguration();
    result.restoredFlags = smiGetFlags();
    result.success = true;
    return result;
}

MibExplicitRootLoadBatch MibRuntimeParser::loadExplicitRoots(
    const MibProfileRuntimeConfiguration &configuration,
    const MibRuntimePathConfiguration &paths)
{
    MibEngine &engine = MibEngine::instance();
    if (!engine.isWorkerThread()) {
        MibExplicitRootLoadBatch result;
        std::mutex mutex;
        std::condition_variable done;
        bool complete = false;
        engine.submit([&] {
            result = loadExplicitRoots(configuration, paths);
            std::lock_guard lock(mutex);
            complete = true;
            done.notify_one();
        });
        std::unique_lock lock(mutex);
        done.wait(lock, [&] { return complete; });
        return result;
    }

    auto operation = engine.beginOperation(QStringLiteral("profile-explicit-root-loading"));
    MibExplicitRootLoadBatch batch;
    for (const QString &identity : configuration.explicitRoots()) {
        MibExplicitRootLoadResult result;
        result.identity = identity;
        const auto aliasIt = configuration.rootAliases().constFind(identity);
        if (aliasIt != configuration.rootAliases().cend()) {
            const MibRuntimeRootAlias &alias = aliasIt.value();
            result.physicalPath = canonicalPath(alias.canonicalPath);
            const auto authorized = std::find_if(paths.entries().cbegin(), paths.entries().cend(),
                [&alias, &result](const MibRuntimePathEntry &entry) {
                    return entry.collectionId == alias.collectionId &&
                        pathWithin(result.physicalPath, entry.canonicalPath);
                });
            if (alias.identity != identity || authorized == paths.entries().cend()) {
                result.status = MibExplicitRootLoadStatus::UnauthorizedAlias;
                result.diagnostic = QStringLiteral(
                    "Alias for %1 is outside its authorized runtime collection: %2")
                    .arg(identity, result.physicalPath);
                batch.roots.append(result);
                continue;
            }
            const QFileInfo file(result.physicalPath);
            if (!file.isFile() || !file.isReadable()) {
                result.status = MibExplicitRootLoadStatus::MissingAliasFile;
                result.diagnostic = QStringLiteral("Alias file for %1 is missing: %2")
                    .arg(identity, result.physicalPath);
                batch.roots.append(result);
                continue;
            }
            const QString currentHash = fileSha256(result.physicalPath);
            if (currentHash.isEmpty() || currentHash != alias.sha256) {
                result.status = MibExplicitRootLoadStatus::AliasContentChanged;
                result.diagnostic = QStringLiteral("Alias file for %1 changed after planning: %2")
                    .arg(identity, result.physicalPath);
                batch.roots.append(result);
                continue;
            }
            if (smiGetModule(identity.toLocal8Bit().constData())) {
                result.status = MibExplicitRootLoadStatus::AlreadyLoaded;
                result.success = true;
                batch.roots.append(result);
                continue;
            }
            const QByteArray encoded = QDir::toNativeSeparators(result.physicalPath).toLocal8Bit();
            const char *loadedName = smiLoadModule(encoded.constData());
            if (smiGetModule(identity.toLocal8Bit().constData())) {
                result.status = MibExplicitRootLoadStatus::LoadedByAlias;
                result.success = true;
            } else if (loadedName) {
                result.status = MibExplicitRootLoadStatus::RequestedIdentityNotDeclared;
                result.diagnostic = QStringLiteral("Alias file %1 did not declare requested identity %2")
                    .arg(result.physicalPath, identity);
            } else {
                result.status = MibExplicitRootLoadStatus::ParserError;
                result.diagnostic = QStringLiteral("libsmi could not parse alias file for %1: %2")
                    .arg(identity, result.physicalPath);
            }
        } else if (smiGetModule(identity.toLocal8Bit().constData())) {
            result.status = MibExplicitRootLoadStatus::AlreadyLoaded;
            result.success = true;
        } else {
            const QByteArray encoded = identity.toLocal8Bit();
            smiLoadModule(encoded.constData());
            if (smiGetModule(encoded.constData())) {
                result.status = MibExplicitRootLoadStatus::LoadedByIdentity;
                result.success = true;
            } else {
                result.status = MibExplicitRootLoadStatus::NativeLookupFailed;
                result.diagnostic = QStringLiteral("Native libsmi lookup failed for explicit root %1")
                    .arg(identity);
            }
        }
        batch.roots.append(result);
    }
    return batch;
}
