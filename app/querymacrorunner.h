#ifndef QUERYMACRORUNNER_H
#define QUERYMACRORUNNER_H

#include "querymacro.h"
#include "snmpsingleasyncrunner.h"
#include <QDateTime>
#include <functional>

enum class QueryMacroRunState { Running, Complete, Partial, Failed, Stopped, Refused };
struct QueryMacroRead {
    QString id, label, oid, problem;
    std::optional<Vb> value;
    QDateTime received;
    bool finished = false;
};
struct QueryMacroRunGroup {
    QString id, name, problem;
    QList<QueryMacroRead> reads;
};
// Published values contain no credentials, parser pointers or retained model indexes.
struct QueryMacroRunSnapshot {
    QString name, target, credential, profile, detail;
    QDateTime started;
    QueryMacroRunState state = QueryMacroRunState::Running;
    MibEnvironmentPtr environment;
    QList<QueryMacroRunGroup> groups;
    int requests = 0;
    int valueCount() const;
    int problemCount() const;
    QString summary() const;
};

// Non-widget orchestration. Each GET uses the existing single-operation worker.
// Agent reserves foreground Query ownership for this service's entire lifecycle.
class QueryMacroRunner final : public QObject {
    Q_OBJECT
public:
    using TransportFactory = std::function<std::unique_ptr<ISnmpTransport>(const SnmpRequestConfig &)>;
    explicit QueryMacroRunner(QObject *parent = nullptr);
    ~QueryMacroRunner() override;
    bool start(const QueryMacroDefinition &definition, const SnmpRequestContext &context,
               const QString &target, const QString &credential, const QString &profile,
               TransportFactory factory, QString *error);
    bool isBusy() const { return busy; }
    bool requestInFlight() const { return !inFlight.isEmpty(); }
    const QueryMacroRunSnapshot &snapshot() const { return result; }
    void cancel();
    void shutdown();
signals:
    void started();
    void changed();
    void completed();
    void requestStarted(const QStringList &oids);
    // Per actual request, including unsuccessful adaptive probes/retries.
    void requestFinished(const QString &detail, int values, bool failed, bool timeout, bool refused);
private:
    void advance();
    void receive(const SnmpSingleResult &response);
    void finish(QueryMacroRunState state, const QString &detail = {});
    void failReads(const QVector<int> &indexes, const QString &problem);
    SnmpSingleAsyncRunner worker;
    std::optional<SnmpRequestContext> context;
    TransportFactory factory;
    QueryMacroRunSnapshot result;
    QList<QVector<int>> pending;
    QVector<int> inFlight;
    bool busy = false, stopped = false, shuttingDown = false;
    quint64 generation = 0;
    int batchLimit = 8;
};
#endif
