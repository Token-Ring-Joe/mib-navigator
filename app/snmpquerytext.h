#ifndef SNMPQUERYTEXT_H
#define SNMPQUERYTEXT_H

#include "mibvaluesemantics.h"
#include "snmpsingleoperation.h"
#include "snmpwalkoperation.h"

#include <QList>
#include <QStringList>
#include <functional>

// One object for the Results table.
struct SnmpQueryRow
{
    MibResolvedObject resolved;
    Oid oid;
    Vb vb;
};

// What a finished (or ending) request shows. The same text feeds the Query pane, the status
// line above the Results table and the Log, for Get, GetNext, GetBulk and Walk alike.
struct SnmpQueryText
{
    QStringList paragraphs;      // appended to the Query pane in order; the last one ends the query
    QString statusHtml;          // one-line summary; empty means the last paragraph is the summary
    QList<SnmpQueryRow> rows;    // objects to add to the Results table
    int objects = 0;
    int requests = 0;
};

using SnmpObjectResolver = std::function<MibResolvedObject(const Oid &)>;

// "3: ifDescr.1    <value>" for one object, ready for the Query pane.
QString FormatObjectLine(int number, const MibResolvedObject &resolved, const Oid &oid, const Vb &vb);

// The answer to one Get, GetNext or GetBulk, including per-varbind "No Such Object" /
// "No Such Instance" lines and the varbind a device error points at. `stopped` is true when the
// user pressed Stop while the request was in flight.
SnmpQueryText FormatSingleResult(const SnmpSingleResult &result, const SnmpObjectResolver &resolve,
                                 bool stopped);

// The answer to one Set: the same lines as above, but device errors are explained in plain
// English (notWritable, wrongType, ...), the failing varbind is named by its error-index, and the
// text ends with "SNMP set finished" (there is no request/object total to show).
SnmpQueryText FormatSetResult(const SnmpSingleResult &result, const SnmpObjectResolver &resolve);

// How a Walk ended (the objects themselves were shown as they arrived).
SnmpQueryText FormatWalkEnd(const SnmpWalkResult &result);

#endif
