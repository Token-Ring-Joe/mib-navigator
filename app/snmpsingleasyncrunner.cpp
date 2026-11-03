#include "snmpsingleasyncrunner.h"

#include <QDeadlineTimer>

namespace
{
class SingleWorker : public QObject
{
    Q_OBJECT
public:
    SingleWorker(const SnmpRequestContext &context, const QVector<Vb> &varbinds,
                 std::unique_ptr<ISnmpTransport> transport)
        : operation(context, varbinds), transport(std::move(transport)) {}
public slots:
    void run() { emit finished(operation.execute(*transport)); }
signals:
    void finished(const SnmpSingleResult &result);
private:
    SnmpSingleOperation operation;
    std::unique_ptr<ISnmpTransport> transport;
};
}

SnmpSingleAsyncRunner::SnmpSingleAsyncRunner(QObject *parent) : QObject(parent)
{
    qRegisterMetaType<SnmpSingleResult>();
}

SnmpSingleAsyncRunner::~SnmpSingleAsyncRunner()
{
    if (thread && !wait(shutdownWaitMs)) {
        // The worker is stuck in its request. Leave it to finish and delete its own thread:
        // nothing it does from here on reaches this object.
        QObject::disconnect(thread, nullptr, this, nullptr);
        connect(thread, &QThread::finished, thread, &QObject::deleteLater);
        thread = nullptr;
    } else if (thread) {
        delete thread;
        thread = nullptr;
    }
}

bool SnmpSingleAsyncRunner::start(const SnmpRequestContext &context, const QVector<Vb> &varbinds,
                                  std::unique_ptr<ISnmpTransport> transport)
{
    if (!transport || isBusy())
        return false;
    // The previous request has reported its result; let its thread finish exiting first.
    if (thread && !wait(2000))
        return false;
    resultDelivered = false;
    thread = new QThread;
    auto *worker = new SingleWorker(context, varbinds, std::move(transport));
    worker->moveToThread(thread);
    connect(thread, &QThread::started, worker, &SingleWorker::run);
    connect(worker, &SingleWorker::finished, this, [this](const SnmpSingleResult &result) {
        resultDelivered = true;
        emit completed(result);
    });
    connect(worker, &SingleWorker::finished, thread, &QThread::quit);
    connect(worker, &SingleWorker::finished, worker, &QObject::deleteLater);
    // Captures its own thread: after a new request has started, this must not touch the new one.
    connect(thread, &QThread::finished, this, [this, finishedThread = thread]() {
        finishedThread->deleteLater();
        if (thread == finishedThread)
            thread = nullptr;
    });
    thread->start();
    return true;
}

bool SnmpSingleAsyncRunner::isRunning() const
{
    return thread && thread->isRunning();
}

bool SnmpSingleAsyncRunner::wait(unsigned long milliseconds)
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

#include "snmpsingleasyncrunner.moc"
