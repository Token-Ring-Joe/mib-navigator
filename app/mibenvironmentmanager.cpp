#include "mibenvironmentmanager.h"
#include "mibengine.h"
#include "mibenvironmentregistry.h"
#include <QPointer>
#include <QThread>

namespace {
bool authorityMatches(const MibEnvironmentPtr &environment, const MibEffectivePlan &plan)
{
    if (!plan.hasRuntimePaths)
        return MibEnvironmentRegistry::isUsableMaterialization(environment);
    QStringList requestedPaths;
    for (const auto &entry : plan.runtimePaths.entries()) requestedPaths.append(entry.canonicalPath);
    // A plan resolved from exact roots is an exact-file plan even when it has no files
    // (an empty profile, or one whose roots are all missing).
    const bool exactFileRuntime = !plan.runtimeConfiguration.authorizedFiles().isEmpty() ||
                                  plan.hasResolverInput;
    const bool runtimePathsMatch = environment && (exactFileRuntime
        ? !environment->authorizedRuntimePaths().isEmpty() &&
          environment->runtimePathHash() != plan.runtimePaths.sha256()
        : environment->runtimePathHash() == plan.runtimePaths.sha256() &&
          environment->authorizedRuntimePaths() == requestedPaths);
    return environment && environment->publishable() &&
        environment->profileId() == plan.profileId &&
        environment->planHash() == plan.sha256 &&
        environment->runtimeConfigurationHash() == plan.runtimeConfiguration.sha256() &&
        environment->libraryGeneration() == plan.runtimeConfiguration.libraryGeneration() &&
        runtimePathsMatch &&
        environment->explicitRoots() == plan.runtimeConfiguration.explicitRoots();
}
}

MibEnvironmentManager::MibEnvironmentManager(Builder builder,QObject *parent,quint64 cacheBudget,QString parserIdentity)
    :QObject(parent),build(std::move(builder)),environmentCache(cacheBudget),parserId(std::move(parserIdentity)){}
MibEnvironmentManager::~MibEnvironmentManager(){shutdown();}
quint64 MibEnvironmentManager::request(const MibEffectivePlan &plan,bool bypassCache)
{
    Q_ASSERT(QThread::currentThread()==thread());const quint64 requested=++generation;
    const auto key=MibEnvironmentCacheKey::fromPlan(plan,parserId);
    if(!bypassCache)if(auto cached=environmentCache.find(key);authorityMatches(cached,plan)){pending.reset();activeEnvironment=cached;MibEnvironmentRegistry::publish(cached);
        emit buildCompleted(requested,plan.profileId,cached,plan.effectiveModules,true,
            cached->status()==MibEnvironmentStatus::Partial);return requested;}
    Request request{requested,plan,key,bypassCache};if(inFlight)pending=std::move(request);else start(std::move(request));return requested;
}
void MibEnvironmentManager::start(Request request)
{
    inFlight=true;++builds;emit buildStarted(request.generation,request.plan.profileName);
    QPointer<MibEnvironmentManager> guard(this);Builder execute=build;
    MibEngine::instance().submit([guard,execute,request=std::move(request)]()mutable{
        MibEnvironmentBuildResult result;try{result=execute(request.plan);}
        catch(const std::exception &exception){result.error=QString::fromLocal8Bit(exception.what());}
        catch(...){result.error=QStringLiteral("Unexpected MIB Environment builder failure");}
        if(!guard)return;
        QMetaObject::invokeMethod(guard,[guard,request=std::move(request),result=std::move(result)]()mutable{
            if(guard)guard->deliver(request.generation,request.plan,request.key,std::move(result));},Qt::QueuedConnection);
    });
}
void MibEnvironmentManager::deliver(quint64 completed,const MibEffectivePlan &plan,
 const MibEnvironmentCacheKey &key,MibEnvironmentBuildResult result)
{
    inFlight=false;if(stopping)return;const bool usable=authorityMatches(result.environment,plan);
    if(usable)environmentCache.insert(key,result.environment);
    if(completed!=generation)emit staleResultDiscarded(completed,plan.profileId);
    else if(!usable)emit buildFailed(completed,plan.profileId,result.error.isEmpty()
        ? QStringLiteral("The MIB environment could not be built for this profile (the result did not match "
                         "the files the profile asked for)") : result.error);
    else {activeEnvironment=result.environment;MibEnvironmentRegistry::publishMaterialization(result.environment);
        emit buildCompleted(completed,plan.profileId,result.environment,result.loadedModules,false,
            result.environment->status()==MibEnvironmentStatus::Partial);}
    startPending();
}
void MibEnvironmentManager::startPending()
{
    if(stopping||!pending)return;auto request=std::move(*pending);pending.reset();
    if(!request.bypassCache)if(auto cached=environmentCache.find(request.key);authorityMatches(cached,request.plan)){activeEnvironment=cached;MibEnvironmentRegistry::publish(cached);
        emit buildCompleted(request.generation,request.plan.profileId,cached,request.plan.effectiveModules,true,
            cached->status()==MibEnvironmentStatus::Partial);return;}start(std::move(request));
}
void MibEnvironmentManager::shutdown()
{if(stopping)return;stopping=true;pending.reset();MibEngine::instance().drain();inFlight=false;}
