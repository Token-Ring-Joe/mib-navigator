#ifndef SNMPWALKASYNCRUNNER_H
#define SNMPWALKASYNCRUNNER_H

#include "snmpwalkoperation.h"

#include <QObject>
#include <QThread>
#include <climits>
#include <memory>

struct SnmpWalkShared;

// Runs a SnmpWalkOperation on a worker thread. Objects reach the owner's thread while the walk
// is still running: `progress` delivers them in order, then `completed` reports how it ended
// (always after the last `progress`). The worker waits when the owner falls behind, so a fast
// agent cannot pile unbounded work onto the GUI thread.
class SnmpWalkAsyncRunner : public QObject
{
    Q_OBJECT
public:
    // How long the destructor waits for a request that is still in flight before it gives up on
    // the worker. A worker is only checked for cancellation between requests, so this is about
    // one request timeout; the thread is then left to finish and clean up by itself.
    static constexpr unsigned long kShutdownWaitMs = 3000;
    // Most objects handed to the owner in one `progress`. The owner's thread handles other events
    // (a click on Stop, repainting) between chunks, so a fast agent cannot keep it busy.
    static constexpr int kDeliveryChunk = 250;

    explicit SnmpWalkAsyncRunner(QObject *parent = nullptr);
    ~SnmpWalkAsyncRunner() override;
    bool start(const SnmpRequestContext &context, const Oid &root,
               std::unique_ptr<ISnmpTransport> transport,
               const SnmpWalkLimits &limits = {});
    void cancel();
    bool isRunning() const;
    // True from start() until `completed` has been emitted. The worker thread may need a moment
    // after that to exit; start() waits for it.
    bool isBusy() const { return thread && !resultDelivered; }
    bool wait(unsigned long milliseconds = ULONG_MAX);
    void setShutdownWaitMs(unsigned long milliseconds) { shutdownWaitMs = milliseconds; }

signals:
    void progress(const QVector<Vb> &varbinds);
    void completed(const SnmpWalkResult &result);

private:
    void drain();

    unsigned long shutdownWaitMs = kShutdownWaitMs;
    bool resultDelivered = false;
    bool resultWaiting = false;      // the walk is over; `completed` follows the last chunk
    SnmpWalkResult waitingResult;
    QThread *thread = nullptr;
    std::shared_ptr<SnmpCancellationToken> cancellation;
    std::shared_ptr<SnmpWalkShared> shared;
};

#endif
