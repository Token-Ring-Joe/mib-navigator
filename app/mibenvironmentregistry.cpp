#include "mibenvironmentregistry.h"
#include <QMutex>
namespace { QMutex mutex; MibEnvironmentPtr current; }
void MibEnvironmentRegistry::publish(MibEnvironmentPtr value) { QMutexLocker lock(&mutex); current=std::move(value); }
bool MibEnvironmentRegistry::isUsableMaterialization(const MibEnvironmentPtr &value)
{
    return value && (value->plannedCount() == 0 || value->failedCount() < value->plannedCount());
}
bool MibEnvironmentRegistry::publishMaterialization(MibEnvironmentPtr value)
{
    if (!isUsableMaterialization(value)) return false;
    publish(std::move(value));
    return true;
}
MibEnvironmentPtr MibEnvironmentRegistry::active() { QMutexLocker lock(&mutex); return current; }
