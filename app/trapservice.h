#ifndef TRAPSERVICE_H
#define TRAPSERVICE_H

#include "trapdecoder.h"
#include "traphistorystore.h"

#include <QObject>

class ITrapReceiver
{
public:
    virtual ~ITrapReceiver() = default;
    virtual bool start() = 0;
    virtual void stop() = 0;
    virtual bool isRunning() const = 0;
};

class TrapService : public QObject
{
    Q_OBJECT
public:
    explicit TrapService(int maximumRecords = TrapHistoryStore::DefaultMaximumRecords,
                         QObject *parent = nullptr);
    ~TrapService() override;

    bool start(ITrapReceiver *receiver);
    void stop();
    bool isRunning() const;
    bool receive(const Pdu &pdu, const TrapEndpoint &endpoint,
                 const QDateTime &received = QDateTime::currentDateTime());
    const TrapHistoryStore &history() const;
    void clear();
    void setMaximumRecords(int maximumRecords);

signals:
    void recordAdded(quint64 recordId);
    void historyReset();
    void receiverFailed();

private:
    ITrapReceiver *activeReceiver = nullptr;
    TrapDecoder decoder;
    TrapHistoryStore store;
};

#endif
