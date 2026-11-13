#include "mibenvironmentdiskcache.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>
#include <algorithm>

// QDataStream finds these through argument-dependent lookup, so they live in the global
// namespace (internal linkage). Every field of every record is written; see the note in
// mibenvironment.h.
static QDataStream &operator<<(QDataStream &s, const MibEnvironmentValue &v)
{
    return s << qint32(v.baseType) << v.isSigned << qint64(v.signedValue) << quint64(v.unsignedValue)
             << v.bytes << v.oid << v.canonicalText;
}
static QDataStream &operator>>(QDataStream &s, MibEnvironmentValue &v)
{
    qint32 type = 0; qint64 sv = 0; quint64 uv = 0;
    s >> type >> v.isSigned >> sv >> uv >> v.bytes >> v.oid >> v.canonicalText;
    v.baseType = static_cast<MibEnvironmentBaseType>(type); v.signedValue = sv; v.unsignedValue = uv;
    return s;
}
static QDataStream &operator<<(QDataStream &s, const MibEnvironmentConstraint &c)
{ return s << c.minimum << c.maximum << c.isSizeConstraint; }
static QDataStream &operator>>(QDataStream &s, MibEnvironmentConstraint &c)
{ return s >> c.minimum >> c.maximum >> c.isSizeConstraint; }
static QDataStream &operator<<(QDataStream &s, const MibEnvironmentNamedValue &n) { return s << n.name << n.value; }
static QDataStream &operator>>(QDataStream &s, MibEnvironmentNamedValue &n) { return s >> n.name >> n.value; }
static QDataStream &operator<<(QDataStream &s, const MibEnvironmentFinding &f)
{ return s << qint32(f.kind) << f.moduleIdentity << f.plannedProviderPath << f.actualProviderPath << f.detail; }
static QDataStream &operator>>(QDataStream &s, MibEnvironmentFinding &f)
{
    qint32 kind = 0;
    s >> kind >> f.moduleIdentity >> f.plannedProviderPath >> f.actualProviderPath >> f.detail;
    f.kind = static_cast<MibEnvironmentFindingKind>(kind);
    return s;
}
static QDataStream &operator<<(QDataStream &s, const MibEnvironmentTypeRecord &t)
{
    return s << t.id << t.moduleIdentity << t.name << qint32(t.baseType) << qint32(t.declaration)
             << t.parentTypeId << t.ancestry << t.displayHint << t.units << t.description << t.reference
             << qint32(t.status) << t.constraints << t.namedValues << t.defaultValue;
}
static QDataStream &operator>>(QDataStream &s, MibEnvironmentTypeRecord &t)
{
    qint32 base = 0, declaration = 0, status = 0;
    s >> t.id >> t.moduleIdentity >> t.name >> base >> declaration >> t.parentTypeId >> t.ancestry
      >> t.displayHint >> t.units >> t.description >> t.reference >> status >> t.constraints
      >> t.namedValues >> t.defaultValue;
    t.baseType = static_cast<MibEnvironmentBaseType>(base); t.declaration = declaration; t.status = status;
    return s;
}
static QDataStream &operator<<(QDataStream &s, const MibEnvironmentRevisionRecord &r) { return s << r.date << r.description; }
static QDataStream &operator>>(QDataStream &s, MibEnvironmentRevisionRecord &r) { return s >> r.date >> r.description; }
static QDataStream &operator<<(QDataStream &s, const MibEnvironmentImportRecord &r) { return s << r.moduleIdentity << r.symbol; }
static QDataStream &operator>>(QDataStream &s, MibEnvironmentImportRecord &r) { return s >> r.moduleIdentity >> r.symbol; }
static QDataStream &operator<<(QDataStream &s, const MibEnvironmentModuleRecord &m)
{
    return s << m.identity << qint32(m.language) << m.plannedProviderPath << m.actualProviderPath
             << m.rawProviderSha256 << m.imports << m.revisions << m.lastUpdated << m.organization
             << m.contactInfo << m.description << m.reference << m.rootOid << m.rootName << m.findings;
}
static QDataStream &operator>>(QDataStream &s, MibEnvironmentModuleRecord &m)
{
    qint32 language = 0;
    s >> m.identity >> language >> m.plannedProviderPath >> m.actualProviderPath >> m.rawProviderSha256
      >> m.imports >> m.revisions >> m.lastUpdated >> m.organization >> m.contactInfo >> m.description
      >> m.reference >> m.rootOid >> m.rootName >> m.findings;
    m.language = static_cast<MibEnvironmentLanguage>(language);
    return s;
}
static QDataStream &operator<<(QDataStream &s, const MibEnvironmentIndexObject &o)
{ return s << o.oid << o.qualifiedName << o.typeId << qint32(o.baseType); }
static QDataStream &operator>>(QDataStream &s, MibEnvironmentIndexObject &o)
{
    qint32 base = 0;
    s >> o.oid >> o.qualifiedName >> o.typeId >> base;
    o.baseType = static_cast<MibEnvironmentBaseType>(base);
    return s;
}
static QDataStream &operator<<(QDataStream &s, const MibEnvironmentNodeRecord &n)
{
    return s << n.oid << n.oidParts << n.name << n.moduleIdentity << n.qualifiedName << qint32(n.kind)
             << qint32(n.declaration) << n.parentOid << n.childOids << qint32(n.access) << qint32(n.status)
             << n.syntaxName << n.typeId << qint32(n.baseType) << n.textualConventionId
             << n.textualConventionAncestry << n.displayHint << n.units << n.description << n.reference
             << n.constraints << n.namedValues << n.defaultValue << n.tableOid << n.rowOid << n.columnOids
             << qint32(n.indexKind) << n.indexObjects << n.implied << n.creatable << n.augmentsRowOid
             << n.notificationObjectOids << n.trapEnterpriseOid << qint32(n.trapGeneric) << qint32(n.trapSpecific);
}
static QDataStream &operator>>(QDataStream &s, MibEnvironmentNodeRecord &n)
{
    qint32 kind = 0, declaration = 0, access = 0, status = 0, base = 0, indexKind = 0, generic = 0, specific = 0;
    s >> n.oid >> n.oidParts >> n.name >> n.moduleIdentity >> n.qualifiedName >> kind >> declaration
      >> n.parentOid >> n.childOids >> access >> status >> n.syntaxName >> n.typeId >> base
      >> n.textualConventionId >> n.textualConventionAncestry >> n.displayHint >> n.units >> n.description
      >> n.reference >> n.constraints >> n.namedValues >> n.defaultValue >> n.tableOid >> n.rowOid
      >> n.columnOids >> indexKind >> n.indexObjects >> n.implied >> n.creatable >> n.augmentsRowOid
      >> n.notificationObjectOids >> n.trapEnterpriseOid >> generic >> specific;
    n.kind = static_cast<MibEnvironmentNodeKind>(kind); n.declaration = declaration;
    n.access = static_cast<MibEnvironmentAccess>(access); n.status = static_cast<MibEnvironmentStatusCode>(status);
    n.baseType = static_cast<MibEnvironmentBaseType>(base); n.indexKind = static_cast<MibEnvironmentIndexKind>(indexKind);
    n.trapGeneric = generic; n.trapSpecific = specific;
    return s;
}
static QDataStream &operator<<(QDataStream &s, const MibEnvironmentTelemetry &t)
{
    return s << qint64(t.moduleCount) << qint64(t.nodeCount) << qint64(t.typeCount) << quint64(t.ownedUtf16Characters)
             << quint64(t.enumRecordCount) << quint64(t.constraintCount) << quint64(t.tableIndexRecordCount)
             << quint64(t.notificationCount) << quint64(t.approximateOwnedBytes) << qint64(t.extractionMilliseconds);
}
static QDataStream &operator>>(QDataStream &s, MibEnvironmentTelemetry &t)
{
    qint64 modules = 0, nodes = 0, types = 0, extraction = 0;
    quint64 chars = 0, enums = 0, constraints = 0, indexes = 0, notifications = 0, bytes = 0;
    s >> modules >> nodes >> types >> chars >> enums >> constraints >> indexes >> notifications >> bytes >> extraction;
    t.moduleCount = modules; t.nodeCount = nodes; t.typeCount = types; t.ownedUtf16Characters = chars;
    t.enumRecordCount = enums; t.constraintCount = constraints; t.tableIndexRecordCount = indexes;
    t.notificationCount = notifications; t.approximateOwnedBytes = bytes; t.extractionMilliseconds = extraction;
    return s;
}
static QDataStream &operator<<(QDataStream &s, const MibExplicitRootLoadResult &r)
{ return s << r.identity << qint32(r.status) << r.physicalPath << r.diagnostic << r.success; }
static QDataStream &operator>>(QDataStream &s, MibExplicitRootLoadResult &r)
{
    qint32 status = 0;
    s >> r.identity >> status >> r.physicalPath >> r.diagnostic >> r.success;
    r.status = static_cast<MibExplicitRootLoadStatus>(status);
    return s;
}
static QDataStream &operator<<(QDataStream &s, const MibProblem &p)
{
    return s << qint32(p.severity) << p.category << p.module << p.file << qint32(p.line) << p.why << p.effect
             << p.requestedBy << p.suggestion << p.parserTag;
}
static QDataStream &operator>>(QDataStream &s, MibProblem &p)
{
    qint32 severity = 0, line = 0;
    s >> severity >> p.category >> p.module >> p.file >> line >> p.why >> p.effect >> p.requestedBy
      >> p.suggestion >> p.parserTag;
    p.severity = static_cast<MibProblemSeverity>(severity); p.line = line;
    return s;
}

namespace {
constexpr char kMagic[] = "MNENVCACHE";
constexpr char kPayloadTag[] = "MNENV-PAYLOAD";
constexpr int kStreamVersion = QDataStream::Qt_6_5;
QString fileSuffix() { return QStringLiteral(".mnenv"); }
}

MibEnvironmentDiskCache::MibEnvironmentDiskCache(const QString &directory, quint64 byteBudget, int maximumEntries)
    : directoryPath(directory), budget(byteBudget), maximumEntryCount(qMax(1, maximumEntries))
{
    writers.setMaxThreadCount(1);
}

MibEnvironmentDiskCache::~MibEnvironmentDiskCache() { writers.waitForDone(); }

void MibEnvironmentDiskCache::waitForPendingWrites() { writers.waitForDone(); }

QString MibEnvironmentDiskCache::defaultDirectory()
{
    if (QCoreApplication::instance()) {
        const QString isolated = QCoreApplication::instance()->property("snmpb.isolatedStateRoot").toString();
        if (!isolated.isEmpty()) return QDir(isolated).filePath(QStringLiteral("mibs/environments"));
    }
    return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
        .filePath(QStringLiteral("SnmpB/mibs/environments"));
}

QString MibEnvironmentDiskCache::keyFor(const MibEffectivePlan &plan, const QString &parserIdentity,
                                        const QString &buildFingerprint)
{
    // Policy 0 plans hash the per-process Library generation into their identity: their copies
    // would never be found again, so do not write any.
    if (plan.resolverPolicyVersion == 0 || plan.sha256.isEmpty() || !plan.hasRuntimePaths) return {};
    QByteArray bytes;
    const auto add = [&bytes](const QString &text) { bytes += text.toUtf8(); bytes += '\0'; };
    add(QStringLiteral("mnenv-disk-key"));
    add(QString::number(FormatVersion));
    add(QString::number(MIB_ENVIRONMENT_SCHEMA_VERSION));
    add(QString::number(MIB_ENVIRONMENT_BUILDER_VERSION));
    add(QString::number(MibEffectivePlan::SchemaVersion));
    add(QString::number(MibEffectivePlan::PolicyVersion));
    add(parserIdentity);
    add(buildFingerprint);
    add(plan.profileId);
    add(plan.sha256);
    add(MibProfileRuntimeConfigurationBuilder::contentSha256(plan.runtimeConfiguration));
    for (const auto &member : plan.members) {
        add(member.identity);
        add(QDir::fromNativeSeparators(member.provider.canonicalPath));
        add(member.provider.sha256);
    }
    for (const QString &list : {plan.effectiveModules.join(QLatin1Char('\n')),
                                plan.missingModules.join(QLatin1Char('\n')),
                                plan.pinFailureModules.join(QLatin1Char('\n'))})
        add(list);
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

QString MibEnvironmentDiskCache::pathFor(const QString &key) const
{
    return QDir(directoryPath).filePath(key + fileSuffix());
}

QByteArray MibEnvironmentDiskCache::serialize(const MibEnvironment &e, const QStringList &loadedModules)
{
    QByteArray payload;
    QDataStream s(&payload, QIODevice::WriteOnly);
    s.setVersion(kStreamVersion);
    s << QByteArray(kPayloadTag) << loadedModules
      << e.planSha256 << e.parserId << e.authorityProfileId << e.runtimeConfigurationSha256
      << e.runtimePathSha256 << quint64(e.authorityLibraryGeneration) << e.authorityPaths << e.authorityFiles
      << e.requestedRoots << e.rootOutcomes << e.actualLoadedIdentities << e.actualProviderPaths
      << e.authorityDiagnostics << e.problemList << e.loadedProvidersAuthorized << e.runtimeAuthorityComplete
      << qint32(e.constructionStatus) << qint64(e.plannedModuleCount) << qint64(e.loadedModuleCount)
      << qint64(e.failedModuleCount) << e.materializationFindings << e.moduleRecords << e.nodeRecords
      << e.typeRecords << e.metrics << e.roots;
    return payload;
}

std::shared_ptr<MibEnvironment> MibEnvironmentDiskCache::deserialize(const QByteArray &payload,
                                                                    QStringList *loadedModules, QString *error)
{
    const auto fail = [error](const QString &why) { if (error) *error = why; return std::shared_ptr<MibEnvironment>(); };
    QDataStream s(payload);
    s.setVersion(kStreamVersion);
    auto e = std::make_shared<MibEnvironment>();
    QByteArray tag; QStringList loaded; quint64 generation = 0;
    qint32 status = 0; qint64 planned = 0, loadedCount = 0, failed = 0;
    s >> tag >> loaded;
    if (tag != kPayloadTag) return fail(QStringLiteral("unrecognized payload"));
    s >> e->planSha256 >> e->parserId >> e->authorityProfileId >> e->runtimeConfigurationSha256
      >> e->runtimePathSha256 >> generation >> e->authorityPaths >> e->authorityFiles >> e->requestedRoots
      >> e->rootOutcomes >> e->actualLoadedIdentities >> e->actualProviderPaths >> e->authorityDiagnostics
      >> e->problemList >> e->loadedProvidersAuthorized >> e->runtimeAuthorityComplete >> status >> planned
      >> loadedCount >> failed >> e->materializationFindings >> e->moduleRecords >> e->nodeRecords
      >> e->typeRecords >> e->metrics >> e->roots;
    if (s.status() != QDataStream::Ok || !s.atEnd()) return fail(QStringLiteral("truncated or malformed payload"));
    e->authorityLibraryGeneration = generation;
    e->constructionStatus = static_cast<MibEnvironmentStatus>(status);
    e->plannedModuleCount = planned; e->loadedModuleCount = loadedCount; e->failedModuleCount = failed;

    // The lookup tables are derived from the records, exactly as the extractor derives them.
    for (qsizetype i = 0; i < e->moduleRecords.size(); ++i) e->moduleIndex.insert(e->moduleRecords[i].identity, i);
    for (qsizetype i = 0; i < e->typeRecords.size(); ++i)
        if (!e->typeIndex.contains(e->typeRecords[i].id)) e->typeIndex.insert(e->typeRecords[i].id, i);
    for (qsizetype i = 0; i < e->nodeRecords.size(); ++i) {
        const auto &node = e->nodeRecords[i];
        e->oidIndex[node.oid].append(i);
        if (!node.qualifiedName.isEmpty() && !e->qualifiedNameIndex.contains(node.qualifiedName))
            e->qualifiedNameIndex.insert(node.qualifiedName, i);
        if (!node.name.isEmpty()) e->unqualifiedNameIndex[node.name].append(i);
    }
    if (loadedModules) *loadedModules = loaded;
    return e;
}

bool MibEnvironmentDiskCache::store(const QString &key, const MibEnvironment &environment,
                                    const QStringList &loadedModules, QString *error) const
{
    const auto fail = [error](const QString &why) { if (error) *error = why; return false; };
    if (key.isEmpty()) return fail(QStringLiteral("no cache key"));
    if (!QDir().mkpath(directoryPath)) return fail(QStringLiteral("cannot create %1").arg(directoryPath));
    const QByteArray compressed = qCompress(serialize(environment, loadedModules), 1);
    const QByteArray digest = QCryptographicHash::hash(compressed, QCryptographicHash::Sha256);
    QSaveFile file(pathFor(key));
    if (!file.open(QIODevice::WriteOnly)) return fail(file.errorString());
    QDataStream s(&file);
    s.setVersion(kStreamVersion);
    s << QByteArray(kMagic) << quint32(FormatVersion) << key << digest << compressed;
    if (s.status() != QDataStream::Ok || !file.commit()) return fail(file.errorString());
    return true;
}

void MibEnvironmentDiskCache::storeAsync(const QString &key, MibEnvironmentPtr environment,
                                         const QStringList &loadedModules)
{
    if (key.isEmpty() || !environment) return;
    writers.start([this, key, environment, loadedModules]() {
        QElapsedTimer timer;
        timer.start();
        QString error;
        if (store(key, *environment, loadedModules, &error)) {
            if (logLine)
                logLine(QStringLiteral("Environment saved for the next start profile=%1 bytes=%2 ms=%3")
                            .arg(environment->profileId()).arg(QFileInfo(pathFor(key)).size()).arg(timer.elapsed()));
            prune();
        } else if (logLine) {
            logLine(QStringLiteral("Environment could not be saved profile=%1 reason=%2")
                        .arg(environment->profileId(), error));
        }
    });
}

std::optional<MibEnvironmentDiskCache::Entry> MibEnvironmentDiskCache::load(
    const QString &key, const MibEffectivePlan &plan, QString *why) const
{
    if (why) why->clear();
    if (key.isEmpty()) return std::nullopt;
    const QString path = pathFor(key);
    QFile file(path);
    if (!file.exists()) return std::nullopt;
    const auto reject = [&](const QString &reason) -> std::optional<Entry> {
        if (why) *why = reason;
        QFile::remove(path);
        return std::nullopt;
    };
    if (!file.open(QIODevice::ReadOnly)) return reject(QStringLiteral("cannot read the saved copy"));
    QDataStream s(&file);
    s.setVersion(kStreamVersion);
    QByteArray magic, digest, compressed; quint32 format = 0; QString storedKey;
    s >> magic >> format >> storedKey >> digest >> compressed;
    if (s.status() != QDataStream::Ok || magic != kMagic) return reject(QStringLiteral("not a saved Environment"));
    if (format != quint32(FormatVersion)) return reject(QStringLiteral("saved by a different format version"));
    if (storedKey != key) return reject(QStringLiteral("saved for a different key"));
    if (QCryptographicHash::hash(compressed, QCryptographicHash::Sha256) != digest)
        return reject(QStringLiteral("checksum mismatch (damaged file)"));
    const QByteArray payload = qUncompress(compressed);
    if (payload.isEmpty()) return reject(QStringLiteral("cannot decompress"));
    QString error; QStringList loaded;
    auto environment = deserialize(payload, &loaded, &error);
    if (!environment) return reject(error);
    // The Library generation is a per-process counter; the key already proves the inputs are the
    // same, so the copy takes this session's value, as a fresh build of this plan would.
    environment->authorityLibraryGeneration = plan.runtimeConfiguration.libraryGeneration();
    environment->runtimeConfigurationSha256 = plan.runtimeConfiguration.sha256();
    file.close();
    // Mark it recently used so pruning keeps what is actually used.
    QFile touch(path);
    if (touch.open(QIODevice::ReadWrite | QIODevice::ExistingOnly)) touch.setFileTime(QDateTime::currentDateTime(), QFileDevice::FileModificationTime);
    return Entry{std::const_pointer_cast<const MibEnvironment>(environment), loaded};
}

void MibEnvironmentDiskCache::remove(const QString &key) const
{
    if (!key.isEmpty()) QFile::remove(pathFor(key));
}

void MibEnvironmentDiskCache::prune() const
{
    QDir dir(directoryPath);
    if (!dir.exists()) return;
    const QDateTime staleBefore = QDateTime::currentDateTime().addSecs(-600);
    // Temporary files a crashed write left behind (QSaveFile names them "<file>.<random>").
    for (const QFileInfo &leftover : dir.entryInfoList({QStringLiteral("*.mnenv.*")}, QDir::Files))
        if (leftover.lastModified() < staleBefore) QFile::remove(leftover.absoluteFilePath());
    QFileInfoList entries = dir.entryInfoList({QStringLiteral("*.mnenv")}, QDir::Files, QDir::Time);
    quint64 total = 0;
    int kept = 0;
    for (const QFileInfo &entry : entries) {
        const quint64 size = quint64(entry.size());
        if (kept < maximumEntryCount && (kept == 0 || total + size <= budget)) { total += size; ++kept; continue; }
        QFile::remove(entry.absoluteFilePath());
    }
}
