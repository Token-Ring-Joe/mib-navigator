#include "snmpwalkasyncrunner.h"

#include <QDeadlineTimer>
#include <QMutex>
#include <QMutexLocker>
#include <QWaitCondition>

namespace
{
// Objects the worker may have delivered but the owner has not yet shown. Past this it waits.
constexpr int kMaxPendingObjects = 2000;
}

struct SnmpWalkShared
{
    QMutex lock;
    QWaitCondition drained;
    QVector<Vb> pending;
    bool wakePosted = false;
};

namespace
{
class WalkWorker : public QObject
{
    Q_OBJECT
public:
    WalkWorker(const SnmpRequestContext &context, const Oid &root, const SnmpWalkLimits &limits,
               std::unique_ptr<ISnmpTransport> transport,
               std::shared_ptr<SnmpCancellationToken> cancellation,
               std::shared_ptr<SnmpWalkShared> shared)
        : operation(context, root, limits), transport(std::move(transport)),
          cancellation(std::move(cancellation)), shared(std::move(shared)) {}
public slots:
    void run()
    {
        const auto sink = [this](const QVector<Vb> &found) {
            QMutexLocker locker(&shared->lock);
            shared->pending += found;
            if (!shared->wakePosted) {
                shared->wakePosted = true;
                emit wake();
            }
            while (shared->pending.size() > kMaxPendingObjects && !cancellation->isCancelled())
                shared->drained.wait(&shared->lock, 50);
        };
        emit finished(operation.execute(*transport, *cancellation, sink));
    }
signals:
    void wake();
    void finished(const SnmpWalkResult &result);
private:
    SnmpWalkOperation operation;
    std::unique_ptr<ISnmpTransport> transport;
    std::shared_ptr<SnmpCancellationToken> cancellation;
    std::shared_ptr<SnmpWalkShared> shared;
};
}

SnmpWalkAsyncRunner::SnmpWalkAsyncRunner(QObject *parent) : QObject(parent)
{
    qRegisterMetaType<SnmpWalkResult>();
}

SnmpWalkAsyncRunner::~SnmpWalkAsyncRunner()
{
    cancel();
    if (thread && !wait(shutdownWaitMs)) {
        // The worker is stuck in a request. Leave it to finish and delete its own thread:
        // nothing it does from here on reaches this object.
        QObject::disconnect(thread, nullptr, this, nullptr);
        connect(thread, &QThread::finished, thread, &QObject::deleteLater);
        thread = nullptr;
    } else if (thread) {
        delete thread;
        thread = nullptr;
    }
}

bool SnmpWalkAsyncRunner::start(const SnmpRequestContext &context, const Oid &root,
                                std::unique_ptr<ISnmpTransport> transport,
                                const SnmpWalkLimits &limits)
{
    if (!transport || isBusy())
        return false;
    // The previous walk has reported its result; let its thread finish exiting first.
    if (thread && !wait(2000))
        return false;
    resultDelivered = false;
    resultWaiting = false;
    cancellation = std::make_shared<SnmpCancellationToken>();
    shared = std::make_shared<SnmpWalkShared>();
    thread = new QThread;
    auto *worker = new WalkWorker(context, root, limits, std::move(transport), cancellation, shared);
    worker->moveToThread(thread);
    connect(thread, &QThread::started, worker, &WalkWorker::run);
    // Both are queued into this object's thread, in the order the worker emitted them, so the
    // last batch of objects is always shown before the result.
    connect(worker, &WalkWorker::wake, this, [this]() { drain(); });
    connect(worker, &WalkWorker::finished, this, [this](const SnmpWalkResult &result) {
        waitingResult = result;
        resultWaiting = true;
        drain();
    });
    connect(worker, &WalkWorker::finished, thread, &QThread::quit);
    connect(worker, &WalkWorker::finished, worker, &QObject::deleteLater);
    // Captures its own thread: after a new walk has started, this must not touch the new one.
    connect(thread, &QThread::finished, this, [this, finishedThread = thread]() {
        finishedThread->deleteLater();
        if (thread == finishedThread) {
            thread = nullptr;
            cancellation.reset();
        }
    });
    thread->start();
    return true;
}

// Hands the owner the next chunk of objects. If more are waiting, the next chunk is queued behind
// whatever else the owner has to do; once none are left and the walk is over, `completed` follows.
void SnmpWalkAsyncRunner::drain()
{
    QVector<Vb> batch;
    bool more = false;
    {
        QMutexLocker locker(&shared->lock);
        if (shared->pending.size() <= kDeliveryChunk) {
            batch.swap(shared->pending);
        } else {
            batch = shared->pending.mid(0, kDeliveryChunk);
            shared->pending.remove(0, kDeliveryChunk);
            more = true;
        }
        shared->wakePosted = more;   // while more is waiting, no extra wake-ups are needed
        shared->drained.wakeAll();
    }
    if (!batch.isEmpty())
        emit progress(batch);
    if (more) {
        QMetaObject::invokeMethod(this, [this]() { drain(); }, Qt::QueuedConnection);
    } else if (resultWaiting) {
        resultWaiting = false;
        resultDelivered = true;
        emit completed(waitingResult);
    }
}

void SnmpWalkAsyncRunner::cancel()
{
    if (cancellation)
        cancellation->cancel();
}

bool SnmpWalkAsyncRunner::isRunning() const
{
    return thread && thread->isRunning();
}

bool SnmpWalkAsyncRunner::wait(unsigned long milliseconds)
{
    if (!thread) return true;
    // See SnmpTableAsyncRunner::wait: the worker's queued request to quit cannot be delivered
    // while the owner is blocked here, so ask the thread to quit directly, and keep asking.
    const QDeadlineTimer deadline = milliseconds == ULONG_MAX
        ? QDeadlineTimer(QDeadlineTimer::Forever) : QDeadlineTimer(static_cast<qint64>(milliseconds));
    while (!thread->wait(25)) {
        thread->quit();
        if (deadline.hasExpired()) return false;
    }
    return true;
}

#include "snmpwalkasyncrunner.moc"
