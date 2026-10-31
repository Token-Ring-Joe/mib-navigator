#ifndef REQUESTLOG_H
#define REQUESTLOG_H

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QString>

// A record of every request the application makes and how it ended, so a failed request is never
// silent and can be pasted into a bug report or a ticket. It holds no secrets: the identity is
// "SNMPv2c" or "SNMPv3 user (authPriv)", never a community or a passphrase.
enum class RequestOutcome { Running, Succeeded, Failed, TimedOut, Refused, Stopped };

struct RequestLogEntry
{
    quint64 id = 0;
    QDateTime started;
    QString operation;     // "Get", "Walk", ...
    QString target;        // "10.1.1.20:161"
    QString identity;      // "SNMPv2c" or "SNMPv3 operator (authPriv)"
    QString oid;           // what was asked for
    RequestOutcome outcome = RequestOutcome::Running;
    QString detail;        // plain-English result or reason it failed
    int objects = 0;
    qint64 durationMs = 0;
};

QString RequestOutcomeText(RequestOutcome outcome);
// Works out how a request ended from the text the application shows for it.
RequestOutcome ClassifyRequestOutcome(const QString &resultHtml);
QString RequestLogAsText(const QList<RequestLogEntry> &entries);

class RequestLog : public QObject
{
    Q_OBJECT
public:
    explicit RequestLog(QObject *parent = nullptr, int limit = 1000);
    quint64 begin(const QString &operation, const QString &target, const QString &identity, const QString &oid);
    // outcome Running means "work it out from the text".
    void finish(quint64 id, const QString &resultHtml, int objects);
    void clear();
    const QList<RequestLogEntry> &entries() const { return list; }
    const RequestLogEntry *entry(quint64 id) const;

signals:
    void entryAdded(quint64 id);
    void entryChanged(quint64 id);
    void cleared();

private:
    QList<RequestLogEntry> list;
    quint64 nextId = 1;
    int maxEntries;
};

#endif
