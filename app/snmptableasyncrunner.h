#ifndef SNMPTABLEASYNCRUNNER_H
#define SNMPTABLEASYNCRUNNER_H

#include "snmptableoperation.h"

#include <QObject>
#include <QThread>
#include <memory>

class SnmpTableAsyncRunner : public QObject
{
    Q_OBJECT
public:
    explicit SnmpTableAsyncRunner(QObject *parent = nullptr);
    ~SnmpTableAsyncRunner() override;
    bool start(const SnmpRequestContext &context, const SnmpTablePlan &plan,
               std::unique_ptr<ISnmpTransport> transport);
    void cancel();
    bool isRunning() const;
    // True from start() until the result has been delivered (the thread may still be exiting).
    bool isBusy() const { return thread && !resultDelivered; }
    bool wait(unsigned long milliseconds = ULONG_MAX);

signals:
    void completed(const SnmpTableResult &result);

private:
    QThread *thread = nullptr;
    std::shared_ptr<SnmpCancellationToken> cancellation;
    bool resultDelivered = false;
};

#endif
