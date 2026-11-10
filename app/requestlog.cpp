#include "requestlog.h"

#include <QTextDocumentFragment>

QString RequestOutcomeText(RequestOutcome outcome)
{
    switch (outcome) {
    case RequestOutcome::Running: return QObject::tr("Running");
    case RequestOutcome::Succeeded: return QObject::tr("OK");
    case RequestOutcome::Failed: return QObject::tr("Failed");
    case RequestOutcome::TimedOut: return QObject::tr("Timeout");
    case RequestOutcome::Refused: return QObject::tr("Refused by device");
    case RequestOutcome::Stopped: return QObject::tr("Stopped");
    }
    return {};
}

RequestOutcome ClassifyRequestOutcome(const QString &html)
{
    const QString plain = QTextDocumentFragment::fromHtml(html).toPlainText();
    if (plain.contains(QLatin1String("Timeout:")) || plain.startsWith(QLatin1String("Timeout")))
        return RequestOutcome::TimedOut;
    // An SNMPv3 report: the device answered, but refused to carry the request out.
    if (plain.contains(QLatin1String("usmStats")) || plain.contains(QLatin1String("snmpUnknown")) ||
        plain.contains(QLatin1String("snmpInvalid")) || plain.contains(QLatin1String("SNMPv3 report")))
        return RequestOutcome::Refused;
    if (html.contains(QLatin1String("query stopped")) || plain.contains(QLatin1String("query stopped")) ||
        plain.startsWith(QLatin1String("Stopped")))
        return RequestOutcome::Stopped;
    if (html.contains(QLatin1String("color=red"))) return RequestOutcome::Failed;
    return RequestOutcome::Succeeded;
}

QString RequestLogAsText(const QList<RequestLogEntry> &entries)
{
    QString text = QObject::tr("Time\tOperation\tTarget\tIdentity\tObject\tResult\tObjects\tTook (ms)\tDetail\n");
    for (const RequestLogEntry &e : entries)
        text += QStringLiteral("%1\t%2\t%3\t%4\t%5\t%6\t%7\t%8\t%9\n")
            .arg(e.started.toString(Qt::ISODateWithMs), e.operation, e.target, e.identity, e.oid,
                 RequestOutcomeText(e.outcome)).arg(e.objects).arg(e.durationMs)
            .arg(QString(e.detail).replace(QLatin1Char('\n'), QLatin1Char(' ')).replace(QLatin1Char('\t'), QLatin1Char(' ')));
    return text;
}

RequestLog::RequestLog(QObject *parent, int limit) : QObject(parent), maxEntries(qMax(1, limit)) {}

quint64 RequestLog::begin(const QString &operation, const QString &target, const QString &identity,
                          const QString &oid)
{
    RequestLogEntry e;
    e.id = nextId++;
    e.started = QDateTime::currentDateTime();
    e.operation = operation; e.target = target; e.identity = identity; e.oid = oid;
    list.append(e);
    while (list.size() > maxEntries) list.removeFirst();
    emit entryAdded(e.id);
    return e.id;
}

const RequestLogEntry *RequestLog::entry(quint64 id) const
{
    for (const RequestLogEntry &e : list) if (e.id == id) return &e;
    return nullptr;
}

void RequestLog::finish(quint64 id, const QString &resultHtml, int objects, RequestOutcome outcome)
{
    for (RequestLogEntry &e : list) {
        if (e.id != id) continue;
        if (e.outcome != RequestOutcome::Running) return;   // finished already
        e.outcome = outcome == RequestOutcome::Running ? ClassifyRequestOutcome(resultHtml) : outcome;
        e.detail = QTextDocumentFragment::fromHtml(resultHtml).toPlainText().simplified();
        e.detail.remove(QStringLiteral("-----"));
        e.detail = e.detail.simplified();
        e.objects = objects;
        e.durationMs = e.started.msecsTo(QDateTime::currentDateTime());
        emit entryChanged(id);
        return;
    }
}

void RequestLog::clear()
{
    list.clear();
    emit cleared();
}
