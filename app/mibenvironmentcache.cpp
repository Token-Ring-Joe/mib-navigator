#include "mibenvironmentcache.h"
#include <QCryptographicHash>
#include <QDir>

QString MibEnvironmentCacheKey::stableId() const
{
    return QStringLiteral("%1|providers:%2|p%3|policy%4|e%5|b%6|%7")
        .arg(planSha256,providerSetSha256).arg(planSchemaVersion).arg(providerPolicyVersion)
        .arg(environmentSchemaVersion).arg(environmentBuilderVersion).arg(parserIdentity);
}

MibEnvironmentCacheKey MibEnvironmentCacheKey::fromPlan(const MibEffectivePlan &plan,
                                                          const QString &parser)
{
    MibEnvironmentCacheKey key; key.planSha256 = plan.sha256; key.parserIdentity = parser;
    QByteArray providers;for(const auto &member:plan.members){providers+=member.identity.toUtf8();providers+='\0';
        providers+=QDir::fromNativeSeparators(member.provider.canonicalPath).toUtf8();providers+='\0';
        providers+=member.provider.sha256.toUtf8();providers+='\n';}
    providers+=plan.runtimeConfiguration.sha256().toUtf8();
    key.providerSetSha256=QString::fromLatin1(QCryptographicHash::hash(providers,QCryptographicHash::Sha256).toHex());
    return key;
}

size_t qHash(const MibEnvironmentCacheKey &key, size_t seed) noexcept
{ return qHash(key.stableId(), seed); }

MibEnvironmentCache::MibEnvironmentCache(quint64 byteBudget,CostEstimator estimator)
    :budget(byteBudget),estimate(estimator?std::move(estimator):[](const MibEnvironment &environment){
        return qMax<quint64>(sizeof(MibEnvironment),environment.telemetry().approximateOwnedBytes);}){}
void MibEnvironmentCache::touch(const MibEnvironmentCacheKey &key)
{ lru.removeAll(key); lru.append(key); }
MibEnvironmentPtr MibEnvironmentCache::find(const MibEnvironmentCacheKey &key)
{
    auto it=entries.find(key); if(it==entries.end())return {}; touch(key); return it->environment;
}
bool MibEnvironmentCache::contains(const MibEnvironmentCacheKey &key)const{return entries.contains(key);}
bool MibEnvironmentCache::insert(const MibEnvironmentCacheKey &key,MibEnvironmentPtr environment)
{
    if(!environment)return false;
    const quint64 cost=estimate(*environment);
    if(cost>budget)return false;
    if(auto it=entries.find(key);it!=entries.end()){bytes-=it->bytes;entries.erase(it);lru.removeAll(key);}
    while(bytes+cost>budget&&!lru.isEmpty()){
        const auto victim=lru.takeFirst();const auto it=entries.find(victim);
        if(it!=entries.end()){bytes-=it->bytes;entries.erase(it);}
    }
    entries.insert(key,{std::move(environment),cost});bytes+=cost;touch(key);return true;
}
