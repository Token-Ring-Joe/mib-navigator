#include "querymacrorepository.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QObject>
#include <QSaveFile>
#include <QSet>

namespace {
bool fail(QString *error, const QString &message) { if (error) *error = message; return false; }
bool read(const QString &path, QByteArray *bytes, QueryMacroStoreSnapshot *snapshot, QString *error) {
    snapshot->exists = QFileInfo::exists(path); snapshot->sha256.clear(); bytes->clear();
    if (!snapshot->exists) return true;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return fail(error, QObject::tr("Cannot read macros: %1").arg(file.errorString()));
    *bytes = file.read(QueryMacroMaximumStoreBytes + 1);
    if (file.error() != QFile::NoError) return fail(error, QObject::tr("Cannot read macros: %1").arg(file.errorString()));
    if (bytes->size() > QueryMacroMaximumStoreBytes) return fail(error, QObject::tr("The macro store exceeds the 8 MiB limit."));
    snapshot->sha256 = QCryptographicHash::hash(*bytes, QCryptographicHash::Sha256);
    return true;
}
bool keys(const QJsonObject &object, const QStringList &required, const QStringList &optional = {}) {
    for (const auto &key : required) if (!object.contains(key)) return false;
    for (auto it = object.begin(); it != object.end(); ++it)
        if (!required.contains(it.key()) && !optional.contains(it.key())) return false;
    return true;
}
bool stringList(const QJsonValue &value, QStringList *out) {
    if (!value.isArray()) return false;
    for (const auto &v : value.toArray()) { if (!v.isString()) return false; out->append(v.toString()); }
    return true;
}
QJsonArray strings(const QStringList &values) { QJsonArray a; for (const auto &v : values) a.append(v); return a; }
}
QueryMacroRepository::QueryMacroRepository(QString file) : fileName(std::move(file)) {}
bool QueryMacroRepository::load(QList<QueryMacroDefinition> *definitions, QueryMacroStoreSnapshot *snapshot, QString *error) const
{
    if (error) error->clear();
    QByteArray bytes; QueryMacroStoreSnapshot current;
    if (!read(fileName, &bytes, &current, error)) return false;
    QList<QueryMacroDefinition> loaded;
    if (current.exists) {
        QJsonParseError parse; const auto document = QJsonDocument::fromJson(bytes, &parse);
        if (parse.error != QJsonParseError::NoError || !document.isObject())
            return fail(error, QObject::tr("The macro file is malformed. Its bytes have been preserved."));
        const auto root = document.object();
        if (!root.value("version").isDouble() || root.value("version").toDouble() != 1)
            return fail(error, QObject::tr("Unsupported macro file version. The file has been preserved."));
        if (!keys(root, {"version", "macros"}) || !root.value("macros").isArray())
            return fail(error, QObject::tr("Invalid macro file structure. The file has been preserved."));
        for (const auto &value : root.value("macros").toArray()) {
            if (!value.isObject()) return fail(error, QObject::tr("Invalid macro definition."));
            const auto object = value.toObject();
            if (!keys(object, {"id", "name", "entries"}) || !object.value("id").isString() ||
                !object.value("name").isString() || !object.value("entries").isArray())
                return fail(error, QObject::tr("Invalid macro fields."));
            QueryMacroDefinition macro; macro.id = object.value("id").toString(); macro.name = object.value("name").toString();
            for (const auto &v : object.value("entries").toArray()) {
                if (!v.isObject()) return fail(error, QObject::tr("Invalid macro entry."));
                const auto item = v.toObject(); const QString kind = item.value("kind").toString();
                QueryMacroEntry entry; entry.id = item.value("id").toString(); entry.label = item.value("label").toString();
                if (!item.value("id").isString() || (item.contains("label") && !item.value("label").isString()))
                    return fail(error, QObject::tr("Invalid macro entry ID/label."));
                QString oidKey; QStringList required{"id", "kind"};
                if (kind == "scalar") { entry.kind = QueryMacroKind::Scalar; oidKey = "objectOid"; }
                else if (kind == "exact") { entry.kind = QueryMacroKind::ExactInstance; oidKey = "instanceOid"; }
                else if (kind == "table") {
                    entry.kind = QueryMacroKind::Table; oidKey = "rowOid"; required += QStringList{"columns", "instancePolicy", "fixedInstances"};
                    const QString policy = item.value("instancePolicy").toString();
                    if (policy == "all") entry.instancePolicy = QueryMacroInstancePolicy::All;
                    else if (policy == "choose") entry.instancePolicy = QueryMacroInstancePolicy::ChooseWhenRun;
                    else if (policy == "fixed") entry.instancePolicy = QueryMacroInstancePolicy::Fixed;
                    else return fail(error, QObject::tr("Unknown table instance policy."));
                    if (!stringList(item.value("columns"), &entry.columns) || !stringList(item.value("fixedInstances"), &entry.fixedInstances))
                        return fail(error, QObject::tr("Invalid table columns or suffixes."));
                } else return fail(error, QObject::tr("Unknown macro entry kind."));
                required.append(oidKey);
                if (!keys(item, required, {"label"}) || !item.value(oidKey).isString())
                    return fail(error, QObject::tr("Unknown or invalid entry fields; the file is preserved."));
                entry.oid = item.value(oidKey).toString(); macro.entries.append(entry);
            }
            loaded.append(macro);
        }
        if (!ValidateQueryMacros(loaded, error)) return false;
    }
    *definitions = loaded; *snapshot = current; return true;
}
bool QueryMacroRepository::save(const QList<QueryMacroDefinition> &definitions, QueryMacroStoreSnapshot *snapshot, QString *error) const
{
    if (error) error->clear();
    if (!ValidateQueryMacros(definitions, error)) return false;
    QJsonArray macros;
    for (const auto &macro : definitions) {
        QJsonArray entries;
        for (const auto &entry : macro.entries) {
            QJsonObject item{{"id", entry.id}, {"label", entry.label}};
            if (entry.kind == QueryMacroKind::Scalar) { item.insert("kind", "scalar"); item.insert("objectOid", entry.oid); }
            else if (entry.kind == QueryMacroKind::ExactInstance) { item.insert("kind", "exact"); item.insert("instanceOid", entry.oid); }
            else {
                const QString policy = entry.instancePolicy == QueryMacroInstancePolicy::All ? "all" :
                    entry.instancePolicy == QueryMacroInstancePolicy::ChooseWhenRun ? "choose" : "fixed";
                item.insert("kind", "table"); item.insert("rowOid", entry.oid); item.insert("columns", strings(entry.columns));
                item.insert("instancePolicy", policy); item.insert("fixedInstances", strings(entry.fixedInstances));
            }
            entries.append(item);
        }
        macros.append(QJsonObject{{"id", macro.id}, {"name", macro.name}, {"entries", entries}});
    }
    const QByteArray bytes = QJsonDocument(QJsonObject{{"version", 1}, {"macros", macros}}).toJson();
    if (bytes.size() > QueryMacroMaximumStoreBytes) return fail(error, QObject::tr("The macro store exceeds the 8 MiB limit."));
    if (!QDir().mkpath(QFileInfo(fileName).absolutePath())) return fail(error, QObject::tr("Cannot create the macro storage folder."));
    QLockFile lock(fileName + ".lock");
    if (!lock.tryLock(0)) return fail(error, QObject::tr("Another process is saving macros. Working edits are kept; try Save again."));
    QByteArray existing; QueryMacroStoreSnapshot current;
    if (!read(fileName, &existing, &current, error)) return false;
    if (current.exists != snapshot->exists || current.sha256 != snapshot->sha256)
        return fail(error, QObject::tr("The macro file changed outside this editor. Working edits are kept. Reconcile them before reloading."));
    QSaveFile file(fileName); file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) return fail(error, QObject::tr("Cannot save macros: %1").arg(file.errorString()));
    if (!file.setPermissions(QFile::ReadOwner | QFile::WriteOwner) || file.write(bytes) != bytes.size() || !file.commit())
        return fail(error, QObject::tr("Cannot save macros atomically: %1. Working edits are kept.").arg(file.errorString()));
    snapshot->exists = true; snapshot->sha256 = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256); return true;
}
