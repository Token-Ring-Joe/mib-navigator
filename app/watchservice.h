#ifndef WATCHSERVICE_H
#define WATCHSERVICE_H

#include <QDateTime>
#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QMutex>
#include <QObject>
#include <QThreadPool>
#include <QTimer>
#include <functional>
#include <memory>

#include "mibenvironment.h"
#include "snmptransport.h"
#include "watchcalculator.h"

// Watch: repeat a GET on one object every few seconds and show how it moves.
//
// Each watched row captures *which device and which credential* when it is created, as the
// target text and a credential reference. Changing the connection bar afterwards never
// retargets a row. The credential is resolved again at every poll, so editing a saved
// credential applies to the next poll; deleting it makes the row report "credential missing".
struct WatchSpec
{
    QString target;          // as typed, for example "10.1.1.20" or "10.1.1.20:1161"
    QString credentialKey;   // ConnectionModel credential key
    QString oid;             // numeric OID including the instance
    int intervalSeconds = 5;
};

struct WatchRow
{
    quint64 id = 0;
    WatchSpec spec;
    QString objectLabel;     // MIB name plus instance when known, else the numeric OID
    bool octetCounter = false;
    bool paused = false;
    bool inFlight = false;
    qint64 nextDueMs = 0;
    WatchSample baseline;    // the last good answer, which the next one is compared against
    QString value;           // interpreted by the MIB
    QString received;        // "Counter32: 1234" as the device sent it
    QString warnings;        // MIB mismatches (see CheckMibValue)
    WatchChange change;
    QString status;          // "OK" or a plain-English reason the last poll failed
    QDateTime updated;
    int polls = 0;
    int failures = 0;
};

class WatchService : public QObject
{
    Q_OBJECT
public:
    // Turns (target, credential) into request settings, or an error message. Called on the
    // main thread at every poll.
    using ConfigBuilder = std::function<bool(const QString &target, const QString &credentialKey,
                                             SnmpRequestConfig *config, QString *error)>;
    using TransportFactory = std::function<std::unique_ptr<ISnmpTransport>(const SnmpRequestConfig &)>;
    using EnvironmentSource = std::function<MibEnvironmentPtr()>;

    // What a background poll hands back to the main thread.
    struct PollResult
    {
        WatchSample sample;
        QString objectLabel;
        bool octetCounter = false;
        QString value;
        QString received;
        QString warnings;
    };

    WatchService(ConfigBuilder builder, TransportFactory factory, EnvironmentSource environment,
                 QObject *parent = nullptr);
    ~WatchService() override;

    quint64 add(const WatchSpec &spec);
    void remove(quint64 id);
    void clear();
    void setPaused(quint64 id, bool paused);
    void pollNow(quint64 id);
    const WatchRow *row(quint64 id) const;
    QList<quint64> ids() const;
    // Polls whose answer has not arrived yet; tests use this to wait.
    int pollsInFlight() const;
    bool waitForPolls(int timeoutMs);
    void startTimer(int intervalMs = 250);

signals:
    void rowAdded(quint64 id);
    void rowChanged(quint64 id);
    void rowRemoved(quint64 id);

private slots:
    void applyResultQueued(quint64 id, const WatchService::PollResult &result) { applyResult(id, result); }

private:
    void tick();
    void startPoll(WatchRow &row);
    void applyResult(quint64 id, const PollResult &result);

    ConfigBuilder buildConfig;
    TransportFactory makeTransport;
    EnvironmentSource environment;
    QHash<quint64, WatchRow> rows;
    QList<quint64> order;
    quint64 nextId = 1;
    QThreadPool pool;
    QTimer timer;
    int outstanding = 0;
    QElapsedTimer clock;
};

#endif
