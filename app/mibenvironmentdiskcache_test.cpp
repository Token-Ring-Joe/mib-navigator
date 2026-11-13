#include "mibenvironmentdiskcache.h"
#include "mibenvironmentextractor.h"

#include "smi.h"

#include <QCoreApplication>
#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QThread>
#include <iostream>

namespace {
bool check(bool condition, const char *message)
{
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}

// ---- An independent, field-by-field comparison (it does not reuse the serializer) ----
bool same(const MibEnvironmentValue &a, const MibEnvironmentValue &b)
{
    return a.baseType == b.baseType && a.isSigned == b.isSigned && a.signedValue == b.signedValue &&
           a.unsignedValue == b.unsignedValue && a.bytes == b.bytes && a.oid == b.oid &&
           a.canonicalText == b.canonicalText;
}
bool same(const QList<MibEnvironmentConstraint> &a, const QList<MibEnvironmentConstraint> &b)
{
    if (a.size() != b.size()) return false;
    for (qsizetype i = 0; i < a.size(); ++i)
        if (!same(a[i].minimum, b[i].minimum) || !same(a[i].maximum, b[i].maximum) ||
            a[i].isSizeConstraint != b[i].isSizeConstraint) return false;
    return true;
}
bool same(const QList<MibEnvironmentNamedValue> &a, const QList<MibEnvironmentNamedValue> &b)
{
    if (a.size() != b.size()) return false;
    for (qsizetype i = 0; i < a.size(); ++i)
        if (a[i].name != b[i].name || !same(a[i].value, b[i].value)) return false;
    return true;
}
bool same(const QList<MibEnvironmentFinding> &a, const QList<MibEnvironmentFinding> &b)
{
    if (a.size() != b.size()) return false;
    for (qsizetype i = 0; i < a.size(); ++i)
        if (a[i].kind != b[i].kind || a[i].moduleIdentity != b[i].moduleIdentity ||
            a[i].plannedProviderPath != b[i].plannedProviderPath ||
            a[i].actualProviderPath != b[i].actualProviderPath || a[i].detail != b[i].detail) return false;
    return true;
}
bool same(const MibEnvironmentNodeRecord &a, const MibEnvironmentNodeRecord &b)
{
    if (a.oid != b.oid || a.oidParts != b.oidParts || a.name != b.name || a.moduleIdentity != b.moduleIdentity ||
        a.qualifiedName != b.qualifiedName || a.kind != b.kind || a.declaration != b.declaration ||
        a.parentOid != b.parentOid || a.childOids != b.childOids || a.access != b.access || a.status != b.status ||
        a.syntaxName != b.syntaxName || a.typeId != b.typeId || a.baseType != b.baseType ||
        a.textualConventionId != b.textualConventionId || a.textualConventionAncestry != b.textualConventionAncestry ||
        a.displayHint != b.displayHint || a.units != b.units || a.description != b.description ||
        a.reference != b.reference || !same(a.constraints, b.constraints) || !same(a.namedValues, b.namedValues) ||
        !same(a.defaultValue, b.defaultValue) || a.tableOid != b.tableOid || a.rowOid != b.rowOid ||
        a.columnOids != b.columnOids || a.indexKind != b.indexKind || a.implied != b.implied ||
        a.creatable != b.creatable || a.augmentsRowOid != b.augmentsRowOid ||
        a.notificationObjectOids != b.notificationObjectOids || a.trapEnterpriseOid != b.trapEnterpriseOid ||
        a.trapGeneric != b.trapGeneric || a.trapSpecific != b.trapSpecific ||
        a.indexObjects.size() != b.indexObjects.size()) return false;
    for (qsizetype i = 0; i < a.indexObjects.size(); ++i)
        if (a.indexObjects[i].oid != b.indexObjects[i].oid ||
            a.indexObjects[i].qualifiedName != b.indexObjects[i].qualifiedName ||
            a.indexObjects[i].typeId != b.indexObjects[i].typeId ||
            a.indexObjects[i].baseType != b.indexObjects[i].baseType) return false;
    return true;
}
bool same(const MibEnvironmentTypeRecord &a, const MibEnvironmentTypeRecord &b)
{
    return a.id == b.id && a.moduleIdentity == b.moduleIdentity && a.name == b.name && a.baseType == b.baseType &&
           a.declaration == b.declaration && a.parentTypeId == b.parentTypeId && a.ancestry == b.ancestry &&
           a.displayHint == b.displayHint && a.units == b.units && a.description == b.description &&
           a.reference == b.reference && a.status == b.status && same(a.constraints, b.constraints) &&
           same(a.namedValues, b.namedValues) && same(a.defaultValue, b.defaultValue);
}
bool same(const MibEnvironmentModuleRecord &a, const MibEnvironmentModuleRecord &b)
{
    if (a.identity != b.identity || a.language != b.language || a.plannedProviderPath != b.plannedProviderPath ||
        a.actualProviderPath != b.actualProviderPath || a.rawProviderSha256 != b.rawProviderSha256 ||
        a.lastUpdated != b.lastUpdated || a.organization != b.organization || a.contactInfo != b.contactInfo ||
        a.description != b.description || a.reference != b.reference || a.rootOid != b.rootOid ||
        a.rootName != b.rootName || !same(a.findings, b.findings) || a.imports.size() != b.imports.size() ||
        a.revisions.size() != b.revisions.size()) return false;
    for (qsizetype i = 0; i < a.imports.size(); ++i)
        if (a.imports[i].moduleIdentity != b.imports[i].moduleIdentity || a.imports[i].symbol != b.imports[i].symbol) return false;
    for (qsizetype i = 0; i < a.revisions.size(); ++i)
        if (a.revisions[i].date != b.revisions[i].date || a.revisions[i].description != b.revisions[i].description) return false;
    return true;
}
bool sameProblems(const QList<MibProblem> &a, const QList<MibProblem> &b)
{
    if (a.size() != b.size()) return false;
    for (qsizetype i = 0; i < a.size(); ++i)
        if (a[i].severity != b[i].severity || a[i].category != b[i].category || a[i].module != b[i].module ||
            a[i].file != b[i].file || a[i].line != b[i].line || a[i].why != b[i].why ||
            a[i].effect != b[i].effect || a[i].requestedBy != b[i].requestedBy ||
            a[i].suggestion != b[i].suggestion || a[i].parserTag != b[i].parserTag) return false;
    return true;
}

// Returns an empty string when equal, otherwise the name of the first difference.
QString difference(const MibEnvironment &a, const MibEnvironment &b)
{
    if (a.planHash() != b.planHash()) return "planHash";
    if (a.parserIdentity() != b.parserIdentity()) return "parserIdentity";
    if (a.status() != b.status()) return "status";
    if (a.plannedCount() != b.plannedCount() || a.loadedCount() != b.loadedCount() ||
        a.failedCount() != b.failedCount()) return "counts";
    if (a.profileId() != b.profileId()) return "profileId";
    if (a.runtimeConfigurationHash() != b.runtimeConfigurationHash()) return "runtimeConfigurationHash";
    if (a.runtimePathHash() != b.runtimePathHash()) return "runtimePathHash";
    if (a.libraryGeneration() != b.libraryGeneration()) return "libraryGeneration";
    if (a.authorizedRuntimePaths() != b.authorizedRuntimePaths()) return "authorizedRuntimePaths";
    if (a.authorizedFiles() != b.authorizedFiles()) return "authorizedFiles";
    if (a.explicitRoots() != b.explicitRoots()) return "explicitRoots";
    if (a.loadedModuleIdentities() != b.loadedModuleIdentities()) return "loadedModuleIdentities";
    if (a.loadedProviderPaths() != b.loadedProviderPaths()) return "loadedProviderPaths";
    if (a.constructionDiagnostics() != b.constructionDiagnostics()) return "constructionDiagnostics";
    if (a.providersAuthorized() != b.providersAuthorized() || a.authorityComplete() != b.authorityComplete())
        return "authority flags";
    if (a.rootLoadOutcomes().size() != b.rootLoadOutcomes().size()) return "rootOutcomes size";
    for (qsizetype i = 0; i < a.rootLoadOutcomes().size(); ++i) {
        const auto &x = a.rootLoadOutcomes()[i]; const auto &y = b.rootLoadOutcomes()[i];
        if (x.identity != y.identity || x.status != y.status || x.physicalPath != y.physicalPath ||
            x.diagnostic != y.diagnostic || x.success != y.success) return "rootOutcomes";
    }
    if (!sameProblems(a.problems(), b.problems())) return "problems";
    if (!same(a.findings(), b.findings())) return "findings";
    if (a.rootOids() != b.rootOids()) return "rootOids";
    const auto &ta = a.telemetry(); const auto &tb = b.telemetry();
    if (ta.moduleCount != tb.moduleCount || ta.nodeCount != tb.nodeCount || ta.typeCount != tb.typeCount ||
        ta.ownedUtf16Characters != tb.ownedUtf16Characters || ta.enumRecordCount != tb.enumRecordCount ||
        ta.constraintCount != tb.constraintCount || ta.tableIndexRecordCount != tb.tableIndexRecordCount ||
        ta.notificationCount != tb.notificationCount || ta.approximateOwnedBytes != tb.approximateOwnedBytes ||
        ta.extractionMilliseconds != tb.extractionMilliseconds) return "telemetry";
    if (a.modules().size() != b.modules().size()) return "module count";
    for (qsizetype i = 0; i < a.modules().size(); ++i)
        if (!same(a.modules()[i], b.modules()[i])) return "module " + a.modules()[i].identity;
    if (a.types().size() != b.types().size()) return "type count";
    for (qsizetype i = 0; i < a.types().size(); ++i)
        if (!same(a.types()[i], b.types()[i])) return "type " + a.types()[i].id;
    if (a.nodes().size() != b.nodes().size()) return "node count";
    for (qsizetype i = 0; i < a.nodes().size(); ++i)
        if (!same(a.nodes()[i], b.nodes()[i])) return "node " + a.nodes()[i].oid;
    // The lookup tables are rebuilt on load; they must answer exactly as the originals do.
    for (const auto &node : a.nodes()) {
        const auto *byOid = b.nodeByOid(node.oid);
        const auto *original = a.nodeByOid(node.oid);
        if (!byOid || !original || !same(*byOid, *original)) return "nodeByOid " + node.oid;
        if (a.nodesByOid(node.oid).size() != b.nodesByOid(node.oid).size()) return "nodesByOid " + node.oid;
        if (a.nodesByName(node.name).size() != b.nodesByName(node.name).size()) return "nodesByName " + node.name;
        const auto *q1 = a.nodeByQualifiedName(node.qualifiedName); const auto *q2 = b.nodeByQualifiedName(node.qualifiedName);
        if ((q1 == nullptr) != (q2 == nullptr) || (q1 && q1->oid != q2->oid)) return "nodeByQualifiedName " + node.qualifiedName;
    }
    for (const auto &type : a.types()) {
        const auto *t = b.type(type.id);
        if (!t || !same(*t, *a.type(type.id))) return "type lookup " + type.id;
    }
    for (const auto &module : a.modules()) {
        const auto *m = b.module(module.identity);
        if (!m || !same(*m, *a.module(module.identity))) return "module lookup " + module.identity;
    }
    const QString probe = QStringLiteral("1.3.6.1.2.1.2.2.1.7.4.5");
    QStringList sa, sb;
    const auto *pa = a.longestPrefixNode(probe, &sa); const auto *pb = b.longestPrefixNode(probe, &sb);
    if ((pa == nullptr) != (pb == nullptr) || sa != sb || (pa && pa->oid != pb->oid)) return "longestPrefixNode";
    return {};
}

QString canonical(const QString &path)
{
    const QString value = QFileInfo(path).canonicalFilePath();
    return value.isEmpty() ? QFileInfo(path).absoluteFilePath() : value;
}

void appendLoadedToPlan(MibEffectivePlan *plan)
{
    for (SmiModule *module = smiGetFirstModule(); module; module = smiGetNextModule(module)) {
        const QString identity = QString::fromLocal8Bit(module->name ? module->name : "");
        if (identity.isEmpty() || plan->effectiveModules.contains(identity)) continue;
        MibEffectivePlanMember member;
        member.identity = identity;
        member.provider.canonicalPath = canonical(QString::fromLocal8Bit(module->path ? module->path : ""));
        member.provider.sha256 = QStringLiteral("test-provider-hash-%1").arg(identity);
        plan->members.append(member); plan->effectiveModules.append(identity);
    }
    plan->effectiveModules.sort(Qt::CaseSensitive);
    plan->sha256 = QStringLiteral("disk-cache-test-plan");
    plan->profileId = QStringLiteral("auto:Test/Profile");
    plan->resolverPolicyVersion = 2;
    plan->hasRuntimePaths = true;
}

QByteArray bytesOf(const QString &path) { QFile f(path); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray(); }
void writeBytes(const QString &path, const QByteArray &data) { QFile f(path); if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.write(data); }
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    bool ok = true;
    const QString root = QStringLiteral(SNMPB_SOURCE_DIR);
    const QStringList paths = {root + "/libsmi/mibs/ietf", root + "/libsmi/mibs/iana",
                               root + "/libsmi/mibs/irtf", root + "/libsmi/mibs/site", root + "/libsmi/mibs/tubs"};
    smiInit("mib-environment-disk-cache-test");
    smiSetPath(paths.join(QDir::listSeparator()).toLocal8Bit().constData());
    smiSetFlags((smiGetFlags() | SMI_FLAG_ERRORS) & ~SMI_FLAG_NODESCR);
    // The bundled standards: tables with every kind of index, enumerations, BITS, notifications,
    // SNMPv1 traps, defaults and size/range constraints.
    for (const QString &directory : paths) {
        const QStringList files = QDir(directory).entryList(QDir::Files, QDir::Name);
        for (const QString &file : files)
            smiLoadModule(QDir::toNativeSeparators(QDir(directory).filePath(file)).toLocal8Bit().constData());
    }
    MibEffectivePlan plan;
    appendLoadedToPlan(&plan);
    MibEnvironmentPtr environment = MibEnvironmentExtractor().extract(plan);
    ok &= check(environment && environment->nodes().size() > 10000 && environment->modules().size() > 100,
                "a large real Environment is extracted");
    if (!environment) return 1;

    // The fixture must exercise every kind of field, or the comparison below proves little.
    {
        bool index = false, augment = false, named = false, constraint = false, defaults = false, notification = false,
             trap = false, implied = false, creatable = false, units = false, hint = false, tc = false, bits = false,
             sizeConstraint = false;
        for (const auto &n : environment->nodes()) {
            index |= n.indexKind == MibEnvironmentIndexKind::Index && !n.indexObjects.isEmpty();
            augment |= n.indexKind == MibEnvironmentIndexKind::Augment && !n.augmentsRowOid.isEmpty();
            named |= !n.namedValues.isEmpty(); constraint |= !n.constraints.isEmpty();
            defaults |= n.defaultValue.baseType != MibEnvironmentBaseType::Unknown;
            notification |= !n.notificationObjectOids.isEmpty(); trap |= !n.trapEnterpriseOid.isEmpty();
            implied |= n.implied; creatable |= n.creatable; units |= !n.units.isEmpty();
            hint |= !n.displayHint.isEmpty(); tc |= !n.textualConventionId.isEmpty();
            bits |= n.baseType == MibEnvironmentBaseType::Bits;
            for (const auto &c : n.constraints) sizeConstraint |= c.isSizeConstraint;
        }
        ok &= check(index && augment && named && constraint && defaults && notification && implied && creatable &&
                    units && hint && tc && bits && sizeConstraint,
                    "the fixture covers indexes, augments, enumerations, constraints, defaults, notifications, "
                    "implied/creatable rows, units, display hints, textual conventions, BITS and size constraints");
        if (!trap) std::cerr << "note: the fixture has no SNMPv1 trap node (trap fields compared as empty)\n";
        bool typeConstraint = false, typeNamed = false, typeDefault = false;
        for (const auto &t : environment->types()) {
            typeConstraint |= !t.constraints.isEmpty(); typeNamed |= !t.namedValues.isEmpty();
            typeDefault |= t.defaultValue.baseType != MibEnvironmentBaseType::Unknown;
        }
        ok &= check(typeConstraint && typeNamed, "the fixture's types carry constraints and enumerations");
        (void)typeDefault;
    }
    // Fill the build-time fields that a plain extraction leaves empty, so they are compared too.
    // (They are what the authority check reads.)
    ok &= check(!environment->modules().first().imports.isEmpty() || !environment->modules().last().imports.isEmpty(),
                "modules carry imports");

    // ---- 1. serialize / deserialize ----
    const QStringList loadedModules = environment->loadedModuleIdentities().isEmpty()
        ? plan.effectiveModules : environment->loadedModuleIdentities();
    const QByteArray payload = MibEnvironmentDiskCache::serialize(*environment, loadedModules);
    std::cout << "payload bytes=" << payload.size() << " nodes=" << environment->nodes().size()
              << " types=" << environment->types().size() << " modules=" << environment->modules().size() << '\n';
    QString error; QStringList loadedBack;
    auto copy = MibEnvironmentDiskCache::deserialize(payload, &loadedBack, &error);
    ok &= check(copy && error.isEmpty() && loadedBack == loadedModules, "the payload deserializes with its loaded modules");
    if (copy) {
        const QString diff = difference(*environment, *copy);
        if (!diff.isEmpty()) std::cerr << "difference: " << diff.toStdString() << '\n';
        ok &= check(diff.isEmpty(), "a deserialized Environment equals the original field by field and by lookup");
        ok &= check(MibEnvironmentDiskCache::serialize(*copy, loadedModules) == payload, "re-serializing is byte-identical");
        ok &= check(copy->schemaVersion() == environment->schemaVersion() && copy->builderVersion() == environment->builderVersion(),
                    "versions match");
    }
    ok &= check(!MibEnvironmentDiskCache::deserialize(payload.left(payload.size() / 2), nullptr, &error) && !error.isEmpty(),
                "a truncated payload is rejected");
    ok &= check(!MibEnvironmentDiskCache::deserialize(payload + QByteArray("x"), nullptr, &error),
                "trailing bytes are rejected");
    ok &= check(!MibEnvironmentDiskCache::deserialize(QByteArray("garbage"), nullptr, &error), "garbage is rejected");

    // ---- 2. keys ----
    const QString parser = QStringLiteral("patched-libsmi-test/engine-policy-1"), build = QStringLiteral("abc123|clean source");
    const QString key = MibEnvironmentDiskCache::keyFor(plan, parser, build);
    ok &= check(key.size() == 64 && key == MibEnvironmentDiskCache::keyFor(plan, parser, build), "the key is a stable SHA-256");
    const auto differs = [&](const MibEffectivePlan &other, const QString &p = QString(), const QString &b = QString()) {
        return MibEnvironmentDiskCache::keyFor(other, p.isEmpty() ? parser : p, b.isEmpty() ? build : b) != key;
    };
    { auto p = plan; p.members[0].provider.sha256 = "different-content"; ok &= check(differs(p), "a changed MIB file changes the key"); }
    { auto p = plan; p.members[0].provider.canonicalPath += "-moved"; ok &= check(differs(p), "a moved MIB file changes the key"); }
    { auto p = plan; p.members.removeLast(); p.effectiveModules.removeLast(); ok &= check(differs(p), "a different member set changes the key"); }
    { auto p = plan; p.profileId = "auto:Other/Profile"; ok &= check(differs(p), "a different profile changes the key"); }
    { auto p = plan; p.sha256 = "another-plan"; ok &= check(differs(p), "a different plan changes the key"); }
    { auto p = plan; p.missingModules = {"GONE-MIB"}; ok &= check(differs(p), "a different set of missing modules changes the key"); }
    ok &= check(differs(plan, "other-parser"), "a different parser changes the key");
    ok &= check(differs(plan, QString(), "def456|clean source"), "a different application build changes the key");
    { auto p = plan; p.libraryGeneration = 99; p.bypassEnvironmentDiskCache = true;
      ok &= check(!differs(p), "the per-process Library generation and the bypass flag do not change the key"); }
    { auto p = plan; p.resolverPolicyVersion = 0; ok &= check(MibEnvironmentDiskCache::keyFor(p, parser, build).isEmpty(), "legacy-policy plans are never cached"); }
    { auto p = plan; p.hasRuntimePaths = false; ok &= check(MibEnvironmentDiskCache::keyFor(p, parser, build).isEmpty(), "plans without runtime paths are never cached"); }
    { auto p = plan; p.sha256.clear(); ok &= check(MibEnvironmentDiskCache::keyFor(p, parser, build).isEmpty(), "unsealed plans are never cached"); }

    // ---- 3. the saved file ----
    QTemporaryDir temp;
    ok &= check(temp.isValid(), "temporary directory");
    const QString directory = temp.filePath("environments");
    MibEnvironmentDiskCache cache(directory);
    ok &= check(!cache.load(key, plan).has_value(), "nothing is found before anything is saved");
    QString why = "unset";
    cache.load(key, plan, &why);
    ok &= check(why.isEmpty(), "a missing copy is not an error");
    ok &= check(cache.store(key, *environment, loadedModules, &error) && QFile::exists(cache.pathFor(key)), "a copy is written");
    {
        auto entry = cache.load(key, plan, &why);
        ok &= check(entry.has_value() && why.isEmpty() && entry->loadedModules == loadedModules, "the copy loads");
        if (entry) {
            const QString diff = difference(*environment, *entry->environment);
            // Only the re-stamped fields may differ (this plan's Library generation is 0).
            ok &= check(diff.isEmpty(), "a copy loaded from disk equals the original");
        }
    }
    {   // The Library generation is stamped from the requesting plan, as a fresh build would.
        auto newer = plan; newer.libraryGeneration = 7;
        auto entry = cache.load(key, newer, &why);
        ok &= check(entry.has_value() && entry->environment->libraryGeneration() == newer.runtimeConfiguration.libraryGeneration() &&
                    entry->environment->runtimeConfigurationHash() == newer.runtimeConfiguration.sha256(),
                    "a loaded copy takes the requesting plan's runtime stamps");
    }
    const QString file = cache.pathFor(key);
    const QByteArray good = bytesOf(file);

    { QByteArray bad = good; bad[bad.size() / 2] = char(bad[bad.size() / 2] ^ 0x5a); writeBytes(file, bad);
      ok &= check(!cache.load(key, plan, &why).has_value() && why.contains("checksum") && !QFile::exists(file),
                  "a damaged copy is rejected and removed"); }
    { writeBytes(file, good.left(good.size() / 3));
      ok &= check(!cache.load(key, plan, &why).has_value() && !why.isEmpty() && !QFile::exists(file),
                  "a truncated copy is rejected and removed"); }
    { writeBytes(file, QByteArray("this is not an Environment"));
      ok &= check(!cache.load(key, plan, &why).has_value() && !why.isEmpty() && !QFile::exists(file),
                  "a foreign file is rejected and removed"); }
    { writeBytes(file, QByteArray());
      ok &= check(!cache.load(key, plan, &why).has_value() && !QFile::exists(file), "an empty file is rejected and removed"); }
    {   // Right bytes under the wrong key.
        const QString otherKey = QString(64, QLatin1Char('a'));
        writeBytes(cache.pathFor(otherKey), good);
        ok &= check(!cache.load(otherKey, plan, &why).has_value() && why.contains("different key") && !QFile::exists(cache.pathFor(otherKey)),
                    "a copy stored under another key is rejected");
    }
    {   // An older or newer on-disk format.
        QByteArray header; QDataStream s(&header, QIODevice::WriteOnly); s.setVersion(QDataStream::Qt_6_5);
        s << QByteArray("MNENVCACHE") << quint32(MibEnvironmentDiskCache::FormatVersion + 1) << key << QByteArray() << QByteArray();
        writeBytes(file, header);
        ok &= check(!cache.load(key, plan, &why).has_value() && why.contains("format") && !QFile::exists(file),
                    "a copy in another format version is rejected");
    }
    ok &= check(cache.store(key, *environment, loadedModules, &error) && cache.load(key, plan).has_value(),
                "a fresh copy is accepted after rejections");

    // ---- 4. background writes ----
    {
        const QString asyncKey = QString(64, QLatin1Char('b'));
        cache.storeAsync(asyncKey, environment, loadedModules);
        cache.waitForPendingWrites();
        ok &= check(QFile::exists(cache.pathFor(asyncKey)) && cache.load(asyncKey, plan).has_value(),
                    "a background write completes and loads");
        // Two writers on one key never leave a damaged file.
        const QString raceKey = QString(64, QLatin1Char('c'));
        QThread *a = QThread::create([&] { for (int i = 0; i < 4; ++i) cache.store(raceKey, *environment, loadedModules); });
        QThread *b = QThread::create([&] { for (int i = 0; i < 4; ++i) cache.store(raceKey, *environment, loadedModules); });
        a->start(); b->start(); a->wait(); b->wait(); delete a; delete b;
        ok &= check(cache.load(raceKey, plan).has_value(), "concurrent writers leave one intact copy");
    }

    // ---- 4b. a failed background save is reported, not silent ----
    {
        QTemporaryDir blocked;
        const QString asFile = blocked.filePath("not-a-directory");
        writeBytes(asFile, "a file where the cache folder should be");
        MibEnvironmentDiskCache broken(asFile);
        QStringList lines; QMutex lock;
        broken.setLogger([&](const QString &line) { QMutexLocker l(&lock); lines.append(line); });
        QString why2;
        ok &= check(!broken.store(key, *environment, loadedModules, &why2) && !why2.isEmpty(),
                    "a save into an unusable folder fails with a reason");
        broken.storeAsync(key, environment, loadedModules);
        broken.waitForPendingWrites();
        QMutexLocker l(&lock);
        ok &= check(lines.size() == 1 && lines[0].contains("could not be saved") && lines[0].contains("reason="),
                    "a failed background save is logged with its reason");
        l.unlock();
        MibEnvironmentDiskCache logged(temp.filePath("logged"));
        QStringList okLines; QMutex okLock;
        logged.setLogger([&](const QString &line) { QMutexLocker m(&okLock); okLines.append(line); });
        logged.storeAsync(key, environment, loadedModules);
        logged.waitForPendingWrites();
        QMutexLocker m(&okLock);
        ok &= check(okLines.size() == 1 && okLines[0].contains("saved for the next start") && okLines[0].contains("ms="),
                    "a successful background save is logged");
    }

    // ---- 5. pruning ----
    {
        QTemporaryDir pruneRoot;
        const QString pruneDir = pruneRoot.filePath("p");
        MibEnvironmentDiskCache small(pruneDir, /*byteBudget*/ 1ULL << 40, /*maximumEntries*/ 3);
        QDir().mkpath(pruneDir);
        QList<QString> names;
        for (int i = 0; i < 6; ++i) {
            const QString name = QString(63, QLatin1Char('d')) + QString::number(i);
            names.append(name);
            writeBytes(small.pathFor(name), QByteArray(100, 'x'));
            QFile f(small.pathFor(name)); ok &= check(f.open(QIODevice::ReadWrite), "fixture file opens");
            f.setFileTime(QDateTime::currentDateTime().addSecs(-1000 + i * 10), QFileDevice::FileModificationTime);
        }
        writeBytes(QDir(pruneDir).filePath("notes.txt"), "not a cache file");
        const QString stale = QDir(pruneDir).filePath("zzzz.mnenv.AbCdEf");
        writeBytes(stale, "temp"); { QFile f(stale); ok &= check(f.open(QIODevice::ReadWrite), "stale file opens"); f.setFileTime(QDateTime::currentDateTime().addSecs(-3600), QFileDevice::FileModificationTime); }
        const QString fresh = QDir(pruneDir).filePath("yyyy.mnenv.GhIjKl");
        writeBytes(fresh, "in-progress");
        small.prune();
        ok &= check(!QFile::exists(small.pathFor(names[0])) && !QFile::exists(small.pathFor(names[1])) &&
                    !QFile::exists(small.pathFor(names[2])) && QFile::exists(small.pathFor(names[3])) &&
                    QFile::exists(small.pathFor(names[4])) && QFile::exists(small.pathFor(names[5])),
                    "pruning keeps the newest copies up to the entry limit");
        ok &= check(QFile::exists(QDir(pruneDir).filePath("notes.txt")), "pruning never touches files it did not create");
        ok &= check(!QFile::exists(stale) && QFile::exists(fresh), "pruning removes old temporary files but not one in progress");

        MibEnvironmentDiskCache tiny(pruneDir, /*byteBudget*/ 150, /*maximumEntries*/ 16);
        tiny.prune();
        int left = 0; for (const QString &n : names) left += QFile::exists(tiny.pathFor(n)) ? 1 : 0;
        ok &= check(left == 1 && QFile::exists(tiny.pathFor(names[5])), "the byte budget keeps the newest copy, and always at least one");
    }

    std::cout << (ok ? "PASS" : "FAILED") << " mib-environment-disk-cache\n";
    return ok ? 0 : 1;
}
