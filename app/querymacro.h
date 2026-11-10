#ifndef QUERYMACRO_H
#define QUERYMACRO_H

#include "mibenvironment.h"
#include <optional>

// Definitions contain intent only. OIDs are durable; labels never retarget them.
enum class QueryMacroKind { Scalar, ExactInstance, Table };
enum class QueryMacroInstancePolicy { All, ChooseWhenRun, Fixed };
struct QueryMacroEntry {
    QString id;
    QueryMacroKind kind = QueryMacroKind::ExactInstance;
    QString label;
    QString oid; // scalar object, complete exact instance, or table row
    QStringList columns;
    QueryMacroInstancePolicy instancePolicy = QueryMacroInstancePolicy::All;
    QStringList fixedInstances;
    bool operator==(const QueryMacroEntry &v) const {
        return id == v.id && kind == v.kind && label == v.label && oid == v.oid &&
            columns == v.columns && instancePolicy == v.instancePolicy && fixedInstances == v.fixedInstances;
    }
};
struct QueryMacroDefinition {
    QString id;
    QString name;
    QList<QueryMacroEntry> entries;
    bool operator==(const QueryMacroDefinition &v) const {
        return id == v.id && name == v.name && entries == v.entries;
    }
};
inline constexpr int QueryMacroMaximumDefinitions = 256;
inline constexpr int QueryMacroMaximumEntries = 1024;
inline constexpr qint64 QueryMacroMaximumStoreBytes = 8 * 1024 * 1024;
QString QueryMacroNewId();
QString QueryMacroNumericOid(const QString &text, bool suffix = false);
bool ValidateQueryMacroEntry(const QueryMacroEntry &entry, QString *error);
bool ValidateQueryMacros(const QList<QueryMacroDefinition> &macros, QString *error);
// Only copied immutable metadata is consulted; this never queries or loads MIBs.
std::optional<QueryMacroEntry> QueryMacroEntryForObject(const MibEnvironmentPtr &environment,
                                                       const QString &oid, QString *error);
#endif
