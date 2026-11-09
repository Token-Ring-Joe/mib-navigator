#ifndef TRAPHISTORYSTORE_H
#define TRAPHISTORYSTORE_H

#include "traprecord.h"

class TrapHistoryStore
{
public:
    static constexpr int DefaultMaximumRecords = 1000;
    explicit TrapHistoryStore(int maximumRecords = DefaultMaximumRecords);

    int count() const;
    int maximumRecords() const;
    const QList<TrapRecord> &records() const;
    const TrapRecord *recordAt(int index) const;
    void append(TrapRecord record);
    void clear();
    void setMaximumRecords(int maximumRecords);

private:
    void trim();
    int maximum;
    quint64 nextRecordId = 1;
    QList<TrapRecord> storedRecords;
};

#endif
