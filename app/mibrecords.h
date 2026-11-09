#ifndef MIBRECORDS_H
#define MIBRECORDS_H

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

struct MibRevisionRecord
{
    QDateTime date;
    QString description;
};

struct MibModuleRecord
{
    QString name;
    QString path;
    QString language;
    QString organization;
    QString contactInfo;
    QString description;
    QString reference;
    QString rootOid;
    QString rootName;
    QDateTime lastRevision;
    QList<MibRevisionRecord> revisions;
    QStringList imports;
    bool loaded = false;
};

struct MibDiagnosticRecord
{
    int severity = 0;
    QString tag;
    QString module;
    QString sourcePath;
    int line = 0;
    QString message;
    QString rawText;
    quint64 operationId = 0;
};

enum class MibLoadStatus { Success, Partial, Failure };

struct MibLoadResult
{
    quint64 operationId = 0;
    QStringList requestedModules;
    QStringList loadedModules;
    QStringList alreadyLoadedModules;
    QStringList unavailableModules;
    QList<MibDiagnosticRecord> diagnostics;
    MibLoadStatus status = MibLoadStatus::Success;
};

struct MibTreeNodeRecord
{
    QString oid;
    QString name;
    QString moduleName;
    int nodeKind = 0;
    QString access;
    QString status;
    QString typeName;
    QString baseType;
    QString displayHint;
    QStringList ranges;
    QStringList namedValues;
    QString units;
    QString description;
    QString reference;
    QList<MibTreeNodeRecord> children;
};

#endif
