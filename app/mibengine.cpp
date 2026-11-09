#include "mibengine.h"
#include <algorithm>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <thread>
namespace { thread_local int engineDepth=0; }

class MibEngine::Private
{
public:
    std::recursive_mutex mutex;
    std::atomic<int> active{0};
    std::atomic<int> maximum{0};
    std::atomic<quint64> operationId{1};
    std::mutex queueMutex;
    std::condition_variable queueReady;
    std::deque<std::function<void()>> queue;
    bool stopping=false;
    std::thread worker;
};

MibEngine::MibEngine():d(std::make_unique<Private>())
{
    d->worker=std::thread([this]{for(;;){std::function<void()> work;{
        std::unique_lock lock(d->queueMutex);d->queueReady.wait(lock,[this]{return d->stopping||!d->queue.empty();});
        if(d->stopping&&d->queue.empty())return;work=std::move(d->queue.front());d->queue.pop_front();}work();}});
}
MibEngine::~MibEngine(){drain();{std::lock_guard lock(d->queueMutex);d->stopping=true;}d->queueReady.notify_one();if(d->worker.joinable())d->worker.join();}
MibEngine &MibEngine::instance(){static MibEngine engine;return engine;}
MibEngine::Operation::Operation(MibEngine *owner):engine(owner)
{
    engine->d->mutex.lock();
    ++engineDepth;
    const int current=engineDepth==1?engine->d->active.fetch_add(1)+1:engine->d->active.load();
    int observed=engine->d->maximum.load();
    while(current>observed&&!engine->d->maximum.compare_exchange_weak(observed,current)){}
}
MibEngine::Operation::Operation(Operation &&other)noexcept:engine(other.engine){other.engine=nullptr;}
MibEngine::Operation::~Operation(){if(engine){if(engineDepth==1)engine->d->active.fetch_sub(1);--engineDepth;engine->d->mutex.unlock();}}
MibEngine::Operation MibEngine::beginOperation(const QString &name){Q_UNUSED(name);return Operation(this);}
int MibEngine::maximumConcurrentOperations()const{return d->maximum.load();}
void MibEngine::resetConcurrencyMetrics(){d->maximum.store(0);}
void MibEngine::submit(std::function<void()> operation)
{{std::lock_guard lock(d->queueMutex);if(d->stopping)return;d->queue.push_back(std::move(operation));}d->queueReady.notify_one();}
bool MibEngine::isWorkerThread()const{return std::this_thread::get_id()==d->worker.get_id();}
void MibEngine::drain()
{
    if(isWorkerThread())return;std::mutex mutex;std::condition_variable done;bool complete=false;
    submit([&]{std::lock_guard lock(mutex);complete=true;done.notify_one();});
    std::unique_lock lock(mutex);done.wait(lock,[&]{return complete;});
}
