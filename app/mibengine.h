#ifndef MIBENGINE_H
#define MIBENGINE_H

#include "mibrecords.h"
#include <QByteArray>
#include <QString>
#include <atomic>
#include <chrono>
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

    // Bounded shutdown. A parse cannot be interrupted, so a very slow or pathological MIB could
    // otherwise keep the application open on exit. Once armed, drain() and shutdown() stop
    // waiting when the budget is spent and shutdownTimedOut() becomes true; the caller must then
    // leave without destroying anything the still-running parse might touch (the application
    // saves its settings and exits the process). Unarmed, drain() waits as long as it takes.
    void armShutdownDeadline(std::chrono::milliseconds budget);
    bool shutdownTimedOut() const;
    // Disarms the deadline and clears the timed-out state. For tests; the application never
    // continues after a timed-out shutdown.
    void clearShutdownDeadline();

private:
    MibEngine();
    ~MibEngine();
    // Queues the marker the callback is given and waits for the worker to run it, honoring the
    // shutdown deadline. Returns false when the wait timed out.
    bool waitForQueuedMarker(std::function<void(std::function<void()>)> enqueue);
    class Private;
    std::unique_ptr<Private> d;
};

#endif
