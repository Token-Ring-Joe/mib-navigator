#ifndef MIBENVIRONMENTCACHE_H
#define MIBENVIRONMENTCACHE_H

#include "mibeffectiveplan.h"
#include "mibenvironment.h"
#include <QHash>
#include <QList>
#include <functional>

struct MibEnvironmentCacheKey {
    QString planSha256;
    QString providerSetSha256;
    int planSchemaVersion = MibEffectivePlan::SchemaVersion;
    int providerPolicyVersion = MibEffectivePlan::PolicyVersion;
    int environmentSchemaVersion = MIB_ENVIRONMENT_SCHEMA_VERSION;
    int environmentBuilderVersion = MIB_ENVIRONMENT_BUILDER_VERSION;
    QString parserIdentity;
    bool operator==(const MibEnvironmentCacheKey &other) const {
        return planSha256==other.planSha256&&providerSetSha256==other.providerSetSha256&&planSchemaVersion==other.planSchemaVersion&&
            providerPolicyVersion==other.providerPolicyVersion&&
            environmentSchemaVersion==other.environmentSchemaVersion&&
            environmentBuilderVersion==other.environmentBuilderVersion&&parserIdentity==other.parserIdentity;
    }
    QString stableId() const;
    static MibEnvironmentCacheKey fromPlan(const MibEffectivePlan &, const QString &parserIdentity);
};

size_t qHash(const MibEnvironmentCacheKey &key, size_t seed = 0) noexcept;

class MibEnvironmentCache final {
public:
    static constexpr quint64 DefaultByteBudget = 128ULL * 1024ULL * 1024ULL;
    using CostEstimator=std::function<quint64(const MibEnvironment &)>;
    explicit MibEnvironmentCache(quint64 byteBudget = DefaultByteBudget,CostEstimator estimator={});
    MibEnvironmentPtr find(const MibEnvironmentCacheKey &);
    bool insert(const MibEnvironmentCacheKey &, MibEnvironmentPtr);
    bool contains(const MibEnvironmentCacheKey &) const;
    quint64 usedBytes() const { return bytes; }
    quint64 byteBudget() const { return budget; }
    qsizetype size() const { return entries.size(); }
private:
    struct Entry { MibEnvironmentPtr environment; quint64 bytes = 0; };
    void touch(const MibEnvironmentCacheKey &);
    quint64 budget;
    quint64 bytes = 0;
    QHash<MibEnvironmentCacheKey, Entry> entries;
    QList<MibEnvironmentCacheKey> lru;
    CostEstimator estimate;
};

#endif
