#include "traphistorystore.h"

#include <algorithm>

TrapHistoryStore::TrapHistoryStore(int maximumRecords)
    : maximum(std::max(1, maximumRecords)) {}

int TrapHistoryStore::count() const { return storedRecords.size(); }
int TrapHistoryStore::maximumRecords() const { return maximum; }
const QList<TrapRecord> &TrapHistoryStore::records() const { return storedRecords; }

const TrapRecord *TrapHistoryStore::recordAt(int index) const
{
    return index >= 0 && index < storedRecords.size() ? &storedRecords.at(index) : nullptr;
}

void TrapHistoryStore::append(TrapRecord record)
{
    if (record.recordId == 0)
        record.recordId = nextRecordId++;
    storedRecords.append(std::move(record));
    trim();
}

void TrapHistoryStore::clear() { storedRecords.clear(); }

void TrapHistoryStore::setMaximumRecords(int maximumRecords)
{
    maximum = std::max(1, maximumRecords);
    trim();
}

void TrapHistoryStore::trim()
{
    const int excess = storedRecords.size() - maximum;
    if (excess > 0)
        storedRecords.erase(storedRecords.begin(), storedRecords.begin() + excess);
}
