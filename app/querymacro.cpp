#include "querymacro.h"
#include <QObject>
#include <QRegularExpression>
#include <QSet>
#include <QUuid>

namespace {
bool fail(QString *error, const QString &message) { if (error) *error = message; return false; }
bool validId(const QString &id) {
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9_-]{1,128}$"));
    return pattern.match(id).hasMatch();
}
bool readable(const MibEnvironmentNodeRecord &node) {
    return node.access == MibEnvironmentAccess::ReadOnly || node.access == MibEnvironmentAccess::ReadWrite ||
        node.access == MibEnvironmentAccess::Install || node.access == MibEnvironmentAccess::InstallNotify;
}
}
QString QueryMacroNewId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
QString QueryMacroNumericOid(const QString &text, bool suffix)
{
    QString value = text.trimmed();
    if (!suffix && value.startsWith('.')) value.remove(0, 1);
    if (value.isEmpty() || value.size() > 1408) return {};
    const QStringList parts = value.split('.');
    if (parts.size() > 128 || (!suffix && parts.size() < 2)) return {};
    QStringList normalized;
    for (const QString &part : parts) {
        if (part.isEmpty() || part.size() > 10) return {};
        for (QChar c : part) if (c < u'0' || c > u'9') return {};
        bool ok = false; const quint64 number = part.toULongLong(&ok);
        if (!ok || number > 0xffffffffULL) return {};
        normalized.append(QString::number(number));
    }
    if (!suffix && (normalized[0].toUInt() > 2 ||
        (normalized[0] != "2" && normalized[1].toUInt() > 39))) return {};
    return normalized.join('.');
}
bool ValidateQueryMacroEntry(const QueryMacroEntry &entry, QString *error)
{
    if (error) error->clear();
    if (!validId(entry.id) || entry.label.size() > 512)
        return fail(error, QObject::tr("Entry ID or label is invalid."));
    if (QueryMacroNumericOid(entry.oid).isEmpty() || QueryMacroNumericOid(entry.oid) != entry.oid)
        return fail(error, QObject::tr("Enter a complete numeric OID with at most 128 numbers (0..4294967295)."));
    if (entry.kind == QueryMacroKind::Scalar || entry.kind == QueryMacroKind::ExactInstance) {
        if (!entry.columns.isEmpty() || !entry.fixedInstances.isEmpty() || entry.instancePolicy != QueryMacroInstancePolicy::All)
            return fail(error, QObject::tr("Scalar and exact-instance entries cannot contain table settings."));
        if (entry.kind == QueryMacroKind::Scalar && entry.oid.count('.') >= 127)
            return fail(error, QObject::tr("The scalar OID leaves no room for its .0 instance."));
        return true;
    }
    if (entry.kind != QueryMacroKind::Table || entry.columns.isEmpty() || entry.columns.size() > 256)
        return fail(error, QObject::tr("A table group needs between 1 and 256 column OIDs."));
    QSet<QString> columns;
    for (const QString &oid : entry.columns) {
        if (QueryMacroNumericOid(oid).isEmpty() || QueryMacroNumericOid(oid) != oid ||
            oid.count('.') >= 127 || columns.contains(oid))
            return fail(error, QObject::tr("Table columns must be unique numeric OIDs with room for an instance."));
        columns.insert(oid);
    }
    if (entry.instancePolicy != QueryMacroInstancePolicy::All &&
        entry.instancePolicy != QueryMacroInstancePolicy::ChooseWhenRun && entry.instancePolicy != QueryMacroInstancePolicy::Fixed)
        return fail(error, QObject::tr("Unknown table instance policy."));
    if (entry.instancePolicy != QueryMacroInstancePolicy::Fixed)
        return entry.fixedInstances.isEmpty() || fail(error, QObject::tr("Only Fixed instances may contain saved suffixes."));
    if (entry.fixedInstances.isEmpty() || entry.fixedInstances.size() > 4096)
        return fail(error, QObject::tr("Fixed instances needs between 1 and 4096 numeric suffixes."));
    QSet<QString> suffixes;
    for (const QString &suffix : entry.fixedInstances) {
        if (QueryMacroNumericOid(suffix, true).isEmpty() || QueryMacroNumericOid(suffix, true) != suffix || suffixes.contains(suffix))
            return fail(error, QObject::tr("Fixed suffixes must be unique numeric sequences, one per line."));
        for (const QString &column : entry.columns)
            if (column.count('.') + suffix.count('.') + 2 > 128)
                return fail(error, QObject::tr("A column plus fixed suffix exceeds the 128-number OID limit."));
        suffixes.insert(suffix);
    }
    return true;
}
bool ValidateQueryMacros(const QList<QueryMacroDefinition> &macros, QString *error)
{
    if (error) error->clear();
    if (macros.size() > QueryMacroMaximumDefinitions) return fail(error, QObject::tr("Too many macros (maximum 256)."));
    QSet<QString> ids; int totalEntries = 0;
    for (const auto &macro : macros) {
        if (!validId(macro.id) || ids.contains(macro.id) || macro.name.trimmed().isEmpty() || macro.name.size() > 256)
            return fail(error, QObject::tr("Each macro needs a unique ID and a name of 1 to 256 characters."));
        ids.insert(macro.id);
        if (macro.entries.size() > QueryMacroMaximumEntries || (totalEntries += macro.entries.size()) > 8192)
            return fail(error, QObject::tr("Too many entries (1024 per macro, 8192 in total)."));
        QSet<QString> entryIds;
        for (const auto &entry : macro.entries) {
            if (entryIds.contains(entry.id)) return fail(error, QObject::tr("An entry ID is duplicated."));
            entryIds.insert(entry.id);
            if (!ValidateQueryMacroEntry(entry, error)) return false;
        }
    }
    return true;
}
std::optional<QueryMacroEntry> QueryMacroEntryForObject(const MibEnvironmentPtr &environment,
                                                       const QString &oid, QString *error)
{
    if (error) error->clear();
    const auto *node = environment ? environment->nodeByOid(oid) : nullptr;
    if (!node) { fail(error, QObject::tr("The selected Profile does not define this object. Add a complete exact-instance OID instead.")); return {}; }
    QueryMacroEntry entry; entry.id = QueryMacroNewId(); entry.label = node->qualifiedName;
    entry.oid = node->oid;
    if (node->kind == MibEnvironmentNodeKind::Scalar && readable(*node)) entry.kind = QueryMacroKind::Scalar;
    else if (node->kind == MibEnvironmentNodeKind::Column && readable(*node)) {
        entry.kind = QueryMacroKind::Table; entry.oid = node->rowOid; entry.columns = {node->oid};
        if (const auto *row = environment->nodeByOid(node->rowOid)) entry.label = row->qualifiedName;
    } else if (node->kind == MibEnvironmentNodeKind::Row || node->kind == MibEnvironmentNodeKind::Table) {
        const MibEnvironmentNodeRecord *row = node;
        if (node->kind == MibEnvironmentNodeKind::Table) {
            row = nullptr;
            for (const QString &child : node->childOids) {
                const auto *candidate = environment->nodeByOid(child);
                if (candidate && candidate->kind == MibEnvironmentNodeKind::Row) { row = candidate; break; }
            }
        }
        if (!row) { fail(error, QObject::tr("No table row definition is available.")); return {}; }
        entry.kind = QueryMacroKind::Table; entry.oid = row->oid; entry.label = row->qualifiedName;
        for (const QString &column : row->columnOids) {
            const auto *candidate = environment->nodeByOid(column);
            if (candidate && readable(*candidate)) entry.columns.append(column);
        }
    } else { fail(error, QObject::tr("Choose a readable scalar, column, row or table.")); return {}; }
    if (!ValidateQueryMacroEntry(entry, error)) return {};
    return entry;
}
