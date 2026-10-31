#include "snmpinstanceasyncrunner.h"
#include <QDeadlineTimer>
#include <climits>

namespace {
class InstanceWorker : public QObject
{
    Q_OBJECT
public:
    InstanceWorker(const SnmpRequestContext &context, const Oid &root,
                   std::unique_ptr<ISnmpTransport> transport,
                   std::shared_ptr<SnmpCancellationToken> cancellation)
        : operation(context, root), transport(std::move(transport)),
          cancellation(std::move(cancellation)) {}
public slots:
    void run() { emit finished(operation.execute(*transport, *cancellation)); }
signals:
    void finished(const SnmpInstanceResult &result);
private:
    SnmpInstanceOperation operation;
    std::unique_ptr<ISnmpTransport> transport;
    std::shared_ptr<SnmpCancellationToken> cancellation;
};
}

SnmpInstanceAsyncRunner::SnmpInstanceAsyncRunner(QObject *parent) : QObject(parent)
{ qRegisterMetaType<SnmpInstanceResult>(); }
SnmpInstanceAsyncRunner::~SnmpInstanceAsyncRunner()
{
    cancel(); wait();
    if (thread) { delete thread; thread = nullptr; }
}
bool SnmpInstanceAsyncRunner::start(const SnmpRequestContext &context,
                                    const Oid &root,
                                    std::unique_ptr<ISnmpTransport> transport)
{
    if (isRunning() || !transport) return false;
    cancellation = std::make_shared<SnmpCancellationToken>();
    thread = new QThread;
    auto *worker = new InstanceWorker(context, root, std::move(transport), cancellation);
    worker->moveToThread(thread);
    connect(thread, &QThread::started, worker, &InstanceWorker::run);
    connect(worker, &InstanceWorker::finished, this,
            [this](const SnmpInstanceResult &result) { emit completed(result); });
    connect(worker, &InstanceWorker::finished, thread, &QThread::quit);
    connect(worker, &InstanceWorker::finished, worker, &QObject::deleteLater);
    connect(thread, &QThread::finished, this, [this]() {
        thread->deleteLater(); thread = nullptr; cancellation.reset();
    });
    thread->start(); return true;
}
void SnmpInstanceAsyncRunner::cancel() { if (cancellation) cancellation->cancel(); }
bool SnmpInstanceAsyncRunner::isRunning() const { return thread && thread->isRunning(); }
bool SnmpInstanceAsyncRunner::wait(unsigned long milliseconds)
{
    if (!thread) return true;
    // The worker asks its thread to quit through a queued call into the thread that owns
    // this runner. That call cannot run while the owner is blocked in here, so waiting
    // alone can deadlock (it did, intermittently, when the runner was destroyed before the
    // owner's event loop delivered the call). Ask the thread to quit directly, and keep
    // asking: a quit that arrives before the thread's event loop has started is ignored.
    const QDeadlineTimer deadline = milliseconds == ULONG_MAX
        ? QDeadlineTimer(QDeadlineTimer::Forever) : QDeadlineTimer(static_cast<qint64>(milliseconds));
    while (!thread->wait(25)) {
        thread->quit();
        if (deadline.hasExpired()) return false;
    }
    return true;
}

#include "snmpinstanceasyncrunner.moc"
