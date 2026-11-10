#include "querymacroservice.h"
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QTemporaryDir>
#include <cstdio>
namespace {
int failures = 0;
void check(bool ok, const char *why) { if (!ok) { std::fprintf(stderr, "FAIL: %s\n", why); ++failures; } }
void write(const QString &path, const QByteArray &bytes) { QFile f(path); if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size()) qFatal("Fixture write failed"); }
QByteArray read(const QString &path) { QFile f(path); if (!f.open(QIODevice::ReadOnly)) qFatal("Fixture read failed"); return f.readAll(); }
QueryMacroEntry exact(QString oid = "1.3.6.1.2.1.1.1.0") { QueryMacroEntry e; e.id = QueryMacroNewId(); e.oid = oid; e.label = "Unresolved numeric instance"; return e; }
}
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv); QTemporaryDir dir; check(dir.isValid(), "temporary fixture");
    const QString path = dir.filePath("private/query-macros-v1.json");
    QueryMacroService service(path);
    check(service.editable() && service.definitions().isEmpty() && !service.dirty() && !QFileInfo::exists(dir.filePath("private")), "missing store loads offline without filesystem mutation");
    check(service.save() && !QFileInfo::exists(path), "no-op empty save creates no file");
    const QString first = service.create(); service.rename(first, "MIB-II Essentials");
    auto scalar = exact("1.3.6.1.2.1.1.1"); scalar.kind = QueryMacroKind::Scalar;
    auto table = exact("1.3.6.1.2.1.2.2.1"); table.kind = QueryMacroKind::Table;
    table.columns = {"1.3.6.1.2.1.2.2.1.2", "1.3.6.1.2.1.2.2.1.8"};
    table.instancePolicy = QueryMacroInstancePolicy::Fixed; table.fixedInstances = {"1", "4.101.116.104.49", "192.0.2.1"};
    const auto raw = exact();
    check(service.putEntry(first, scalar) && service.putEntry(first, table) && service.putEntry(first, raw), "mixed scalar/table/exact definitions");
    check(service.moveEntry(first, raw.id, -1), "entry order changes");
    const QString second = service.duplicate(first);
    const auto a = service.definition(first), b = service.definition(second);
    check(a && b && a->id != b->id && a->entries.size() == b->entries.size() && a->entries[0].id != b->entries[0].id, "duplication regenerates all owned IDs");
    check(service.move(second, -1) && service.save() && !service.dirty(), "save ordered definitions atomically");
    const QByteArray good = read(path);
    for (const QByteArray key : {"target", "credential", "profileId", "password", "polling"}) check(!good.contains('"' + key + '"'), "store contains no connection/credential/Profile fields");
    QueryMacroService restart(path);
    check(restart.definitions() == service.definitions() && restart.definitions().first().id == second, "restart preserves all values, IDs and order");
    check(restart.save() && read(path) == good, "no-op save preserves bytes");
    restart.rename(first, ""); check(!restart.save() && restart.dirty() && read(path) == good, "invalid working name remains editable and cannot replace good file");
    restart.rename(first, "Unsaved retry");
    { QLockFile lock(path + ".lock"); check(lock.tryLock(), "hold competing writer lock"); check(!restart.save() && restart.dirty() && read(path) == good, "busy writer leaves working edits and durable file intact"); }
    check(restart.save() && !restart.dirty(), "retry saves retained working edits");
    QueryMacroService stale(path), other(path); other.rename(first, "Other process"); check(other.save(), "independent writer commits");
    const auto external = read(path); stale.rename(first, "Keep my changes");
    check(!stale.save() && stale.dirty() && stale.definition(first)->name == "Keep my changes" && read(path) == external, "external changes are not overwritten");
    check(!stale.reload() && stale.dirty(), "reload cannot silently discard working edits");
    check(stale.reload(true) && stale.definition(first)->name == "Other process", "explicit discard/reload adopts external state");
    check(stale.remove(second) && stale.save(), "delete is persisted");
    QueryMacroService removedRestart(path); check(removedRestart.definitions().size() == 1 && !removedRestart.definition(second), "deleted ID stays deleted after restart");
    const QByteArray one = read(path); const auto originalRoot = QJsonDocument::fromJson(one).object();
    QList<QByteArray> bad{QByteArray("{broken"), QByteArray("[]"), QByteArray()};
    auto root = originalRoot; root.insert("version", 2); bad.append(QJsonDocument(root).toJson());
    root = originalRoot; root.insert("target", "do-not-drop-this-field"); bad.append(QJsonDocument(root).toJson());
    root = originalRoot; auto array = root.value("macros").toArray(); array.append(array.first()); root.insert("macros", array); bad.append(QJsonDocument(root).toJson());
    root = originalRoot; array = root.value("macros").toArray(); auto macro = array.first().toObject(); auto entries = macro.value("entries").toArray(); auto entry = entries.first().toObject();
    entry.insert("kind", "future-read-kind"); entries[0] = entry; macro.insert("entries", entries); array[0] = macro; root.insert("macros", array); bad.append(QJsonDocument(root).toJson());
    for (const auto &bytes : bad) {
        write(path, bytes); QueryMacroService protectedStore(path);
        check(!protectedStore.editable() && !protectedStore.error().isEmpty() && protectedStore.create().isEmpty() && !protectedStore.save() && read(path) == bytes,
              "malformed/newer/unknown schema bytes are never replaced with empty definitions");
    }
    write(path, QByteArray(QueryMacroMaximumStoreBytes + 1, ' ')); QueryMacroService excessive(path);
    check(!excessive.editable() && !excessive.save() && QFileInfo(path).size() == QueryMacroMaximumStoreBytes + 1, "oversized store remains preserved");
    write(path, one); check(excessive.reload(true) && excessive.definitions().size() == 1, "repaired file can be reloaded explicitly");
    write(dir.filePath("block"), "regular file"); QueryMacroService failed(dir.filePath("block/macros.json"));
    const QString pending = failed.create(); check(!pending.isEmpty() && !failed.save() && failed.dirty() && failed.definition(pending), "failed directory creation retains working definition");
    check(QueryMacroNumericOid(" .1.03.6.1.000 ") == "1.3.6.1.0" && QueryMacroNumericOid("4.101.116.104.49", true) == "4.101.116.104.49", "numeric canonicalization and compound suffixes");
    for (const QString &invalid : {QString("1"), QString("3.1"), QString("1.40"), QString("1.3..6"), QString("1.3.-1"), QString("1.3.4294967296"), QString("1.3.sysDescr")})
        check(QueryMacroNumericOid(invalid).isEmpty(), "invalid numeric identity rejected");
    QString error; table.fixedInstances = {"1", "1"}; check(!ValidateQueryMacroEntry(table, &error), "duplicate fixed suffix rejected");
    table.instancePolicy = QueryMacroInstancePolicy::ChooseWhenRun; table.fixedInstances.clear(); check(ValidateQueryMacroEntry(table, &error), "choose-at-run intent saves without discovery");
    table.instancePolicy = QueryMacroInstancePolicy::All; check(ValidateQueryMacroEntry(table, &error), "all-instances intent saves without discovery");
    table.columns.append(table.columns.first()); check(!ValidateQueryMacroEntry(table, &error), "duplicate column rejected");
    auto maximum = exact("1.3." + QStringList(126, "1").join('.'));
    check(ValidateQueryMacroEntry(maximum, &error), "complete 128-number exact instance accepted");
    maximum.kind = QueryMacroKind::Scalar; check(!ValidateQueryMacroEntry(maximum, &error), "scalar reserves one number for .0");
    QueryMacroService large(dir.filePath("large.json"));
    for (int i = 0; i < 100; ++i) {
        const auto id = large.create(); large.rename(id, QString("Macro %1").arg(i));
        for (int j = 0; j < 10; ++j) check(large.putEntry(id, exact(QString("1.3.6.1.4.1.64993.%1.%2.0").arg(i).arg(j))), "large definition insertion");
    }
    check(large.save(), "100 macros/1000 entries save"); QueryMacroService largeRestart(large.path());
    check(largeRestart.definitions() == large.definitions(), "large ordered definitions survive restart exactly");
    check(!QueryMacroEntryForObject({}, "1.3.6.1.2.1.1.1", &error) && !error.isEmpty(), "unknown metadata cannot infer scalar/table semantics");
    return failures ? 1 : 0;
}
