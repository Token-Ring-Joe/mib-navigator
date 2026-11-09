#ifndef SNMPINSTANCEASYNCRUNNER_H
#define SNMPINSTANCEASYNCRUNNER_H

#include "snmpinstanceoperation.h"
#include <QObject>
#include <QThread>
#include <memory>

class SnmpInstanceAsyncRunner : public QObject
{
    Q_OBJECT
public:
    explicit SnmpInstanceAsyncRunner(QObject *parent = nullptr);
    ~SnmpInstanceAsyncRunner() override;
    bool start(const SnmpRequestContext &context, const Oid &root,
               std::unique_ptr<ISnmpTransport> transport);
    void cancel();
    bool isRunning() const;
    bool wait(unsigned long milliseconds = ULONG_MAX);
signals:
    void completed(const SnmpInstanceResult &result);
private:
    QThread *thread = nullptr;
    std::shared_ptr<SnmpCancellationToken> cancellation;
};

#endif
