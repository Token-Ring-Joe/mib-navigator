#ifndef MIBENVIRONMENTMANAGER_H
#define MIBENVIRONMENTMANAGER_H

#include "mibenvironmentcache.h"
#include <QObject>
#include <functional>
#include <optional>

struct MibEnvironmentBuildResult {
    MibEnvironmentPtr environment;
    QStringList loadedModules;
    QString error;
};

class MibEnvironmentManager final : public QObject {
    Q_OBJECT
public:
    using Builder=std::function<MibEnvironmentBuildResult(const MibEffectivePlan &)>;
    explicit MibEnvironmentManager(Builder builder, QObject *parent=nullptr,
        quint64 cacheBudget=MibEnvironmentCache::DefaultByteBudget,QString parserIdentity={});
    ~MibEnvironmentManager() override;
    quint64 request(const MibEffectivePlan &plan,bool bypassCache=false);
    void shutdown();
    MibEnvironmentPtr active()const{return activeEnvironment;}
    quint64 buildCount()const{return builds;}
    const MibEnvironmentCache &cache()const{return environmentCache;}
signals:
    void buildStarted(quint64 generation,QString profileName);
    void buildCompleted(quint64 generation,QString profileId,MibEnvironmentPtr environment,
                        QStringList loadedModules,bool cacheHit,bool partial);
    void buildFailed(quint64 generation,QString profileId,QString error);
    void staleResultDiscarded(quint64 generation,QString profileId);
private:
    struct Request {quint64 generation=0;MibEffectivePlan plan;MibEnvironmentCacheKey key;bool bypassCache=false;};
    void start(Request request);
    void startPending();
    void deliver(quint64 generation,const MibEffectivePlan &,const MibEnvironmentCacheKey &,
                 MibEnvironmentBuildResult);
    Builder build;
    MibEnvironmentCache environmentCache;
    std::optional<Request> pending;
    bool inFlight=false;
    quint64 generation=0;
    quint64 builds=0;
    bool stopping=false;
    MibEnvironmentPtr activeEnvironment;
    QString parserId;
};

#endif
