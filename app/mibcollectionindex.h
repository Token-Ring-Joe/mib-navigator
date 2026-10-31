#ifndef MIBCOLLECTIONINDEX_H
#define MIBCOLLECTIONINDEX_H

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

// An object-level index of every MIB file in the collection, built by reading the text of the
// files. Nothing is loaded into the MIB parser, so it covers the whole collection and not just
// the active MIB Profile, and a MIB the parser rejects is still searchable.
//
// It deliberately stays tolerant: it only needs the shape "name <macro> ... ::= { parent n }",
// so syntax errors elsewhere in a file do not hide the definitions around them. An OID it cannot
// work out (a missing import, a circular definition) is reported as unresolved, never guessed.

struct MibCollectionEntry
{
    QString name;
    QString module;
    QString kind;       // "Object", "Node", "Notification", "Module identity", "Type", ...
    QString oid;        // dotted numeric OID, empty when it could not be resolved
    QString file;
    int line = 0;
    QString unresolved; // why the OID is empty, when it is
};

struct MibCollectionSearchResult
{
    QList<MibCollectionEntry> entries;
    bool truncated = false;
    // "exact", or for a numeric OID with no exact definition, the closest definition above it.
    QString note;
};

class MibCollectionIndex
{
public:
    struct OidItem { QString name; int number = -1; QString module; };
    struct Definition
    {
        QString name;
        QString kind;
        int line = 0;
        QList<OidItem> oid;   // the "{ ... }" components; empty for types
        QString enterprise;   // TRAP-TYPE only
        int trapNumber = -1;  // TRAP-TYPE only
    };
    struct ParsedModule
    {
        QString name;
        QHash<QString, QString> imports;   // imported symbol -> module it comes from
        QList<Definition> definitions;
    };
    struct ParsedFile
    {
        QString path;
        QList<ParsedModule> modules;
    };

    // Reads one file's text. Never fails: whatever definitions can be found are returned.
    static ParsedFile parse(const QString &path, const QByteArray &text);

    void setFiles(const QList<ParsedFile> &files);
    int fileCount() const { return filesIndexed; }
    int moduleCount() const;
    int definitionCount() const { return entries.size(); }
    int unresolvedCount() const;

    // Search by numeric OID ("1.3.6.1.2.1.2.2.1.10.1", with or without a leading dot),
    // qualified name ("IF-MIB::ifInOctets" or "IF-MIB.ifInOctets"), or name / part of a name.
    MibCollectionSearchResult search(const QString &query, int limit = 500) const;
    // Every module that defines this exact name (used to find the right module for a result).
    QStringList modulesDefining(const QString &name) const;

private:
    void resolveAll(const QList<ParsedFile> &files);

    QList<MibCollectionEntry> entries;
    QHash<QString, QList<int>> byLowerName;   // lower-case name -> entry indexes
    QHash<QString, int> byOid;                // oid -> first entry index
    QMap<QString, QList<int>> byModule;
    int filesIndexed = 0;
};

#endif
