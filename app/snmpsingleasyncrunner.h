#ifndef SNMPSINGLEASYNCRUNNER_H
#define SNMPSINGLEASYNCRUNNER_H

#include "snmpsingleoperation.h"

#include <QObject>
#include <QThread>
#include <climits>
#include <memory>

// Runs one SnmpSingleOperation on a worker thread and reports its result. A request cannot be
// cancelled once sent; Stop only changes how the result is labelled.
class SnmpSingleAsyncRunner : public QObject
{
    Q_OBJECT
public:
    // How long the destructor waits for a request still in flight before it leaves the worker to
    // finish and delete its own thread.
    static constexpr unsigned long kShutdownWaitMs = 3000;

    explicit SnmpSingleAsyncRunner(QObject *parent = nullptr);
    ~SnmpSingleAsyncRunner() override;
    bool start(const SnmpRequestContext &context, const QVector<Vb> &varbinds,
               std::unique_ptr<ISnmpTransport> transport);
    // True from start() until `completed` has been emitted.
    bool isBusy() const { return thread && !resultDelivered; }
    bool isRunning() const;
    bool wait(unsigned long milliseconds = ULONG_MAX);
    void setShutdownWaitMs(unsigned long milliseconds) { shutdownWaitMs = milliseconds; }

signals:
    void completed(const SnmpSingleResult &result);

private:
    unsigned long shutdownWaitMs = kShutdownWaitMs;
    bool resultDelivered = false;
    QThread *thread = nullptr;
};

#endif
