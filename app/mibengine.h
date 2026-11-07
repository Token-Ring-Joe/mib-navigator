#ifndef MIBENGINE_H
#define MIBENGINE_H

#include "mibrecords.h"
#include <QByteArray>
#include <QString>
#include <atomic>
#include <memory>
#include <functional>

class MibDependencyIndex;

struct MibEngineValidationResult
{
    bool success = false;
    QList<MibDiagnosticRecord> diagnostics;
    QString stagingError;
    QString stagedSourcePath;
    qint64 elapsedMilliseconds = 0;
};

class MibEngine final
{
public:
    class Operation final
    {
    public:
        Operation(Operation &&other) noexcept;
        ~Operation();
        Operation(const Operation &) = delete;
        Operation &operator=(const Operation &) = delete;
    private:
        friend class MibEngine;
        explicit Operation(MibEngine *owner);
        MibEngine *engine = nullptr;
    };

    static MibEngine &instance();
    Operation beginOperation(const QString &name);
    MibEngineValidationResult validateSource(const QByteArray &content,
                                             const QStringList &libraryPaths,
                                             int errorLevel,
                                             bool recursive,
                                             const MibDependencyIndex &librarySnapshot);
    int maximumConcurrentOperations() const;
    void resetConcurrencyMetrics();
    QString libraryVersion() const;
    void initialize(const QString &searchPath = {}, bool restart = false);
    void shutdown();
    void submit(std::function<void()> operation);
    bool isWorkerThread() const;
    void drain();

private:
    MibEngine();
    ~MibEngine();
    class Private;
    std::unique_ptr<Private> d;
};

#endif
