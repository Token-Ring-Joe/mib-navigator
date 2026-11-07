#include "mibengine.h"
#include "mibdiagnosticcollector.h"
#include "miblibrary.h"
#include "mibdependencyindex.h"
#include <QTemporaryDir>
#include <QFile>
#include <QCryptographicHash>
#include <QSet>
#include <cstdlib>
#include "smi.h"
#include <QElapsedTimer>
#include <QDir>
#include <condition_variable>
#include <mutex>

namespace {
void replaceSourceReference(QString &message, const QString &from, const QString &to)
{
    qsizetype position = 0;
    while ((position = message.indexOf(from, position)) >= 0) {
        const qsizetype end = position + from.size();
        const QChar next = end < message.size() ? message.at(end) : QChar();
        if (next.isLetterOrNumber() || next == '-' || next == '_' || next == '.') {
            position = end;
            continue;
        }
        message.replace(position, from.size(), to);
        position += to.size();
    }
}
}

QString MibEngine::libraryVersion() const { return QStringLiteral(SMI_VERSION_STRING); }
void MibEngine::initialize(const QString &searchPath,bool restart)
{
    if(!isWorkerThread()){std::mutex mutex;std::condition_variable done;bool complete=false;
        submit([&,searchPath,restart]{initialize(searchPath,restart);std::lock_guard lock(mutex);complete=true;done.notify_one();});
        std::unique_lock lock(mutex);done.wait(lock,[&]{return complete;});return;}
    auto operation=beginOperation(QStringLiteral("initialize"));
    if(restart) smiExit();
    smiInit(nullptr);
    if(!searchPath.isEmpty())smiSetPath(searchPath.toLocal8Bit().constData());
}
void MibEngine::shutdown(){if(!isWorkerThread()){std::mutex mutex;std::condition_variable done;bool complete=false;
    submit([&]{shutdown();std::lock_guard lock(mutex);complete=true;done.notify_one();});std::unique_lock lock(mutex);done.wait(lock,[&]{return complete;});return;}
    auto operation=beginOperation(QStringLiteral("shutdown"));smiExit();}

MibEngineValidationResult MibEngine::validateSource(const QByteArray &content,
    const QStringList &libraryPaths, int errorLevel, bool recursive,
    const MibDependencyIndex &librarySnapshot)
{
    if (!isWorkerThread()) {
        MibEngineValidationResult result;
        std::mutex mutex;
        std::condition_variable done;
        bool complete = false;
        submit([&, content, errorLevel, recursive, libraryPaths, librarySnapshot] {
            result = validateSource(content, libraryPaths, errorLevel, recursive, librarySnapshot);
            std::lock_guard lock(mutex);
            complete = true;
            done.notify_one();
        });
        std::unique_lock lock(mutex);
        done.wait(lock, [&] { return complete; });
        return result;
    }
    auto operation = beginOperation(QStringLiteral("editor-validation"));
    QElapsedTimer timer;
    timer.start();
    MibEngineValidationResult result;
    const int savedFlags = smiGetFlags();
    const std::unique_ptr<char, decltype(&std::free)> savedPathBytes(smiGetPath(), std::free);
    const QString savedPath = QString::fromLocal8Bit(savedPathBytes.get());
    QTemporaryDir temporary;
    if (!temporary.isValid()) {
        result.stagingError = QStringLiteral("Could not create verification workspace");
    } else {
        // Read-only verification snapshot of the Library's provider policy. Its
        // index/aliases are private scratch data, never another persisted Library.
        MibDependencyIndex index = librarySnapshot;
        index.update(libraryPaths, &result.stagingError, false);
        if (result.stagingError.isEmpty()) {
            QMap<QString, QString> provenance;
            const auto source = MibImportScanner::scan(content);
            QSet<QString> required;
            std::function<void(const QString &)> visit = [&](const QString &name) {
                if (required.contains(name) || source.moduleNames.contains(name)) return;
                required.insert(name);
                for (const auto &dependency : index.imports(name)) visit(dependency);
            };
            for (const auto &imports : source.importsByModule)
                for (const auto &dependency : imports) visit(dependency);
            const QString aliases = temporary.filePath(QStringLiteral("aliases"));
            QDir().mkpath(aliases);
            for (const QString &name : index.moduleNames()) {
                if (source.moduleNames.contains(name)) continue;
                const auto provider = index.provider(name);
                if (provider.status != MibProviderStatus::Found) {
                    if (required.contains(name) && provider.status == MibProviderStatus::Ambiguous) {
                        MibDiagnosticRecord diagnostic;
                        diagnostic.severity = 2;
                        diagnostic.tag = QStringLiteral("editor-provider-conflict");
                        diagnostic.module = name;
                        diagnostic.message = QStringLiteral("Conflicting Library providers for %1: %2")
                            .arg(name, provider.alternatives.join(QStringLiteral(", ")));
                        result.diagnostics.append(diagnostic);
                    }
                    continue;
                }
                const QString alias = QDir(aliases).filePath(name);
                QString expectedHash;
                for (const auto &candidate : index.providersFor(name))
                    if (candidate.canonicalPath == provider.path) expectedHash = candidate.sha256;
                QFile copy(alias);
                const bool copied = QFile::copy(provider.path, alias) && copy.open(QIODevice::ReadOnly);
                const QString actualHash = copied ? QString::fromLatin1(
                    QCryptographicHash::hash(copy.readAll(), QCryptographicHash::Sha256).toHex()) : QString();
                copy.close();
                if (!copied || actualHash != expectedHash) {
                    QFile::remove(alias);
                    if (required.contains(name)) {
                        result.stagingError = QStringLiteral("Could not snapshot Library provider %1; refresh the Library and retry")
                            .arg(provider.path);
                        break;
                    }
                    continue;
                }
                provenance.insert(alias, provider.path);
            }
            if (result.stagingError.isEmpty()) {
                result.success = MibValidationStaging::validate(content, temporary.path(),
                    [&](const QString &path) {
                        result.stagedSourcePath = path;
                        for (const auto &identity : source.moduleNames) {
                            const QString alias = QDir(aliases).filePath(identity);
                            if (!QFile::copy(path, alias)) {
                                result.stagingError = QStringLiteral("Could not stage editor identity %1").arg(identity);
                                return false;
                            }
                            provenance.insert(alias, path);
                        }
                        // The current document always wins over any saved copy,
                        // and no module previously loaded for Query participates.
                        initialize(aliases, true);
                        int flags = savedFlags | SMI_FLAG_ERRORS | SMI_FLAG_NODESCR;
                        if (recursive) flags |= SMI_FLAG_RECURSIVE;
                        else flags &= ~SMI_FLAG_RECURSIVE;
                        smiSetFlags(flags);
                        static std::atomic<quint64> operationId{1};
                        MibDiagnosticCollector collector(operationId.fetch_add(1), QStringLiteral("editor"));
                        collector.install(errorLevel);
                        const bool loaded = smiLoadModule(QDir::toNativeSeparators(path).toLocal8Bit().constData()) != nullptr;
                        collector.finish(nullptr, 0);
                        result.diagnostics.append(collector.diagnostics());
                        initialize(savedPath, true);
                        smiSetPath(savedPath.toLocal8Bit().constData());
                        smiSetFlags(savedFlags);
                        return loaded;
                    }, &result.stagingError);
            }
            for (auto &diagnostic : result.diagnostics) {
                const QString path = QDir::fromNativeSeparators(diagnostic.sourcePath);
                diagnostic.sourcePath = provenance.value(path, path);
                for (auto it = provenance.cbegin(); it != provenance.cend(); ++it) {
                    const QStringList forms{it.key(), QDir::toNativeSeparators(it.key())};
                    for (const auto &form : forms)
                        replaceSourceReference(diagnostic.message, form, it.value());
                }
                diagnostic.rawText = MibDiagnosticCollector::rawText(diagnostic);
            }
        }
    }
    if (!result.stagingError.isEmpty()) {
        MibDiagnosticRecord diagnostic;
        diagnostic.severity = 2;
        diagnostic.tag = QStringLiteral("validation-staging");
        diagnostic.message = result.stagingError;
        diagnostic.rawText = MibDiagnosticCollector::rawText(diagnostic);
        result.diagnostics.append(diagnostic);
    }
    result.elapsedMilliseconds = timer.elapsed();
    return result;
}
