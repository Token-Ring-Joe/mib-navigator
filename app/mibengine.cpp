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
    // Steady-clock deadline in milliseconds since the clock's epoch; 0 means "not armed".
    std::atomic<long long> deadlineMs{0};
    std::atomic<bool> timedOut{false};
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
namespace {
long long steadyNowMs(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
// Completion state shared with the queued marker, so a caller that stops waiting leaves nothing
// dangling for the worker to write to later.
struct DrainState{std::mutex mutex;std::condition_variable done;bool complete=false;};
}
void MibEngine::armShutdownDeadline(std::chrono::milliseconds budget)
{
    const long long deadline=steadyNowMs()+std::max<long long>(1,budget.count());
    d->deadlineMs.store(deadline<1?1:deadline);
}
bool MibEngine::shutdownTimedOut()const{return d->timedOut.load();}
void MibEngine::clearShutdownDeadline(){d->deadlineMs.store(0);d->timedOut.store(false);}
bool MibEngine::waitForQueuedMarker(std::function<void(std::function<void()>)> enqueue)
{
    auto state=std::make_shared<DrainState>();
    enqueue([state]{std::lock_guard lock(state->mutex);state->complete=true;state->done.notify_all();});
    std::unique_lock lock(state->mutex);
    const long long deadline=d->deadlineMs.load();
    if(deadline==0){state->done.wait(lock,[&]{return state->complete;});return true;}
    const auto until=std::chrono::steady_clock::time_point(std::chrono::milliseconds(deadline));
    if(state->done.wait_until(lock,until,[&]{return state->complete;}))return true;
    d->timedOut.store(true);return false;
}
void MibEngine::drain()
{
    if(isWorkerThread())return;
    waitForQueuedMarker([this](std::function<void()> marker){submit(std::move(marker));});
}
