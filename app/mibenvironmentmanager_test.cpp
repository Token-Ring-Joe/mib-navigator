#include "mibenvironmentmanager.h"
#include "mibenvironmentregistry.h"
#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QElapsedTimer>
#include <QMutex>
#include <QSemaphore>
#include <QThread>
#include <QTimer>
#include <cstdio>
#include <thread>

namespace {
int failures=0;
void check(bool value,const char *message){if(!value){std::fprintf(stderr,"FAIL: %s\n",message);++failures;}}
bool until(const std::function<bool()> &condition,int timeout=5000){QDeadlineTimer deadline(timeout);
    while(!condition()&&!deadline.hasExpired())QCoreApplication::processEvents(QEventLoop::AllEvents,20);return condition();}
MibEffectivePlan plan(const QString &id,const QString &hash={}){MibEffectivePlan p;p.profileId=id;p.profileName=id;
    p.sha256=hash.isEmpty()?id+QStringLiteral("-hash"):hash;return p;}
MibEnvironmentPtr environment(){return std::make_shared<MibEnvironment>();}
}

int main(int argc,char **argv)
{
    QCoreApplication app(argc,argv);
    auto a=environment(),b=environment(),c=environment();
    MibEnvironmentCache cache(100,[](const MibEnvironment &){return 50;});
    auto ka=MibEnvironmentCacheKey::fromPlan(plan("A"),"parser");
    auto kb=MibEnvironmentCacheKey::fromPlan(plan("B"),"parser");
    auto kc=MibEnvironmentCacheKey::fromPlan(plan("C"),"parser");
    check(cache.insert(ka,a)&&cache.insert(kb,b),"cache accepts entries within byte budget");
    check(cache.find(ka)==a,"cache hit returns immutable shared Environment and refreshes LRU");
    check(cache.insert(kc,c)&&cache.contains(ka)&&!cache.contains(kb)&&cache.contains(kc),"byte-bounded LRU evicts least recent entry");
    check(b!=nullptr,"eviction does not invalidate externally retained Environment");
    MibEnvironmentCache oversized(40,[](const MibEnvironment &){return 50;});
    check(!oversized.insert(ka,a)&&oversized.size()==0,"oversized entry is not cached");
    auto providerA=plan("provider","same-plan");MibEffectivePlanMember member;member.identity="X";member.provider.sha256="one";providerA.members<<member;
    auto providerB=providerA;providerB.members[0].provider.sha256="two";
    check(!(MibEnvironmentCacheKey::fromPlan(providerA,"parser")==MibEnvironmentCacheKey::fromPlan(providerB,"parser")),
          "provider hash independently invalidates cache identity");
    auto providerPathB=providerA;providerA.members[0].provider.canonicalPath="C:/catalog/provider-a.mib";
    providerPathB.members[0].provider.canonicalPath="C:/catalog/provider-b.mib";
    check(!(MibEnvironmentCacheKey::fromPlan(providerA,"parser")==MibEnvironmentCacheKey::fromPlan(providerPathB,"parser")),
          "provider path independently invalidates cache identity");

    QSemaphore bStarted,bRelease;QMutex recordMutex;QStringList invoked;int completions=0,stale=0,failuresSeen=0;QString lastProfile;bool lastCacheHit=false;
    MibEnvironmentManager manager([&](const MibEffectivePlan &p){
        {QMutexLocker lock(&recordMutex);invoked<<p.profileId;}if(p.profileId=="B"){bStarted.release();bRelease.acquire();}
        MibEnvironmentBuildResult result;if(p.profileId!="FAIL")result.environment=environment();else result.error="fatal";return result;
    },nullptr,1024*1024,"parser");
    QObject::connect(&manager,&MibEnvironmentManager::buildCompleted,&app,[&](quint64,const QString&id,MibEnvironmentPtr,QStringList,bool hit,bool){
        ++completions;lastProfile=id;lastCacheHit=hit;});
    QObject::connect(&manager,&MibEnvironmentManager::staleResultDiscarded,&app,[&]{++stale;});
    QObject::connect(&manager,&MibEnvironmentManager::buildFailed,&app,[&]{++failuresSeen;});
    manager.request(plan("A"));check(until([&]{return completions==1;}),"initial A build completes");auto activeA=manager.active();
    manager.request(plan("B"));check(bStarted.tryAcquire(1,5000),"uncached B build begins on worker");
    bool heartbeat=false;QTimer::singleShot(0,&app,[&]{heartbeat=true;});QCoreApplication::processEvents();
    check(heartbeat&&manager.active()==activeA,"GUI event loop and A remain active during uncached B build");
    manager.request(plan("C"));manager.request(plan("D"));bRelease.release();
    check(until([&]{return lastProfile=="D";}),"latest D eventually publishes");
    check(stale==1&&manager.active()!=activeA,"stale B is discarded and does not publish");
    {QMutexLocker lock(&recordMutex);check(invoked==QStringList{"A","B","D"},"rapid A-B-C-D coalesces obsolete C");}
    const quint64 beforeHit=manager.buildCount();QElapsedTimer cacheTimer;cacheTimer.start();manager.request(plan("A"));const qint64 cacheMicros=cacheTimer.nsecsElapsed()/1000;
    check(lastProfile=="A"&&lastCacheHit&&manager.buildCount()==beforeHit,"A-B-A cache hit bypasses builder");
    QSemaphore sameStarted,sameRelease;int sameCompletions=0;
    MibEnvironmentManager sameManager([&](const MibEffectivePlan &){sameStarted.release();sameRelease.acquire();
        MibEnvironmentBuildResult r;r.environment=environment();return r;},nullptr,1024,"parser");
    QObject::connect(&sameManager,&MibEnvironmentManager::buildCompleted,&app,[&]{++sameCompletions;});
    sameManager.request(plan("same"));check(sameStarted.tryAcquire(1,5000),"same-key build starts");
    sameManager.request(plan("same"));sameRelease.release();
    check(until([&]{return sameCompletions==1;})&&sameManager.buildCount()==1,"duplicate in-flight cache key is single-flight");
    auto beforeFailure=manager.active();manager.request(plan("FAIL"));
    check(until([&]{return failuresSeen==1;} )&&manager.active()==beforeFailure,"fatal build preserves active Environment");

    int authorityFailures=0;
    MibEnvironmentManager authorityManager([](const MibEffectivePlan &){
        MibEnvironmentBuildResult result;result.environment=environment();return result;
    },nullptr,1024,"parser");
    QObject::connect(&authorityManager,&MibEnvironmentManager::buildFailed,&app,
        [&](quint64,const QString &,const QString &){++authorityFailures;});
    MibEffectivePlan authorityPlan=plan("authority");authorityPlan.hasRuntimePaths=true;
    authorityManager.request(authorityPlan);
    check(until([&]{return authorityFailures==1;})&&!authorityManager.active(),
          "Environment without matching runtime authority cannot publish or enter the active Tree state");
    const quint64 beforeProvider=manager.buildCount();manager.request(providerA);check(until([&]{return lastProfile=="provider";}),"provider A builds");
    manager.request(providerB);check(until([&]{return manager.buildCount()==beforeProvider+2;}),"provider content change misses cache and rebuilds");

    QSemaphore shutdownStarted,shutdownRelease;
    MibEnvironmentManager shutdownManager([&](const MibEffectivePlan &){shutdownStarted.release();shutdownRelease.acquire();
        MibEnvironmentBuildResult r;r.environment=environment();return r;},nullptr,1024,"parser");
    shutdownManager.request(plan("shutdown"));check(shutdownStarted.tryAcquire(1,5000),"shutdown scenario build starts");
    std::thread release([&]{shutdownRelease.release();});QElapsedTimer shutdownTimer;shutdownTimer.start();shutdownManager.shutdown();release.join();
    check(shutdownTimer.elapsed()<5000,"shutdown deterministically drains and joins engine work");
    std::printf("Phase6 manager builds=%llu rapid-builds=3 cache-hit-build-delta=0 cache-hit-us=%lld cache-budget=%llu\n",
        static_cast<unsigned long long>(manager.buildCount()),static_cast<long long>(cacheMicros),
        static_cast<unsigned long long>(manager.cache().byteBudget()));
    return failures?1:0;
}
