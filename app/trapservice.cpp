#include "trapservice.h"

TrapService::TrapService(int maximumRecords, QObject *parent)
    : QObject(parent), store(maximumRecords) {}

TrapService::~TrapService() { stop(); }

bool TrapService::start(ITrapReceiver *receiver)
{
    stop();
    if (!receiver || !receiver->start()) {
        emit receiverFailed();
        return false;
    }
    activeReceiver = receiver;
    return true;
}

void TrapService::stop()
{
    if (activeReceiver)
        activeReceiver->stop();
    activeReceiver = nullptr;
}

bool TrapService::isRunning() const
{
    return activeReceiver && activeReceiver->isRunning();
}

bool TrapService::receive(const Pdu &pdu, const TrapEndpoint &endpoint,
                          const QDateTime &received)
{
    TrapRecord record = decoder.decode(pdu, endpoint, received);
    if (!record.isValid())
        return false;
    store.append(std::move(record));
    emit recordAdded(store.records().last().recordId);
    return true;
}

const TrapHistoryStore &TrapService::history() const { return store; }

void TrapService::clear()
{
    store.clear();
    emit historyReset();
}

void TrapService::setMaximumRecords(int maximumRecords)
{
    const int oldCount = store.count();
    store.setMaximumRecords(maximumRecords);
    if (store.count() != oldCount)
        emit historyReset();
}
