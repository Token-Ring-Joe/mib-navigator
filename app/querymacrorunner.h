#ifndef QUERYMACRORUNNER_H
#define QUERYMACRORUNNER_H

#include "querymacro.h"
#include "snmpinstanceasyncrunner.h"
#include "snmpsingleasyncrunner.h"
#include "snmptableasyncrunner.h"
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
    // Table group: the rows received so far (a value copy; no parser pointers).
    bool isTable = false;
    SnmpTableResult table;
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
    // Lets the user pick instances for "Choose when run" tables. Receives the table's label, the
    // discovered suffixes and their decoded index text; returning nothing (or an empty list)
    // cancels the whole run before any retrieval starts and leaves earlier results intact.
    using InstanceChooser = std::function<std::optional<QStringList>(const QString &label,
        const QStringList &instances, const QStringList &decoded)>;
    void setInstanceChooser(InstanceChooser chooser) { instanceChooser = std::move(chooser); }
    // What the request in flight is, for the request log (never contains secrets).
    QString requestDescription() const { return requestLabel; }
    bool isBusy() const { return busy; }
    bool requestInFlight() const { return !inFlight.isEmpty(); }
    const QueryMacroRunSnapshot &snapshot() const { return result; }
    void cancel();
    void shutdown();
signals:
    // Instance discovery for "Choose when run" tables began; results are not touched until it ends.
    void selectionStarted();
    // The user cancelled or discovery was stopped before any retrieval: nothing was replaced.
    void abandoned();
    void started();
    void changed();
    void completed();
    void requestStarted(const QStringList &oids);
    // Per actual request, including unsuccessful adaptive probes/retries.
    void requestFinished(const QString &detail, int values, bool failed, bool timeout, bool refused);
private:
    struct TableJob {
        int group = 0;
        SnmpTablePlan plan;
        QString label;
        bool choose = false;
        QStringList instances;   // empty: the whole table; otherwise one exact-row read each
        int next = 0;
    };
    void beginRun();
    void selectNext();
    void receiveInstances(const SnmpInstanceResult &discovered);
    void receiveTable(const SnmpTableResult &table);
    void startTableRequest();
    void abandon();
    void advance();
    void receive(const SnmpSingleResult &response);
    void finish(QueryMacroRunState state, const QString &detail = {});
    void failReads(const QVector<int> &indexes, const QString &problem);
    SnmpSingleAsyncRunner worker;
    SnmpTableAsyncRunner tableWorker;
    SnmpInstanceAsyncRunner instanceWorker;
    InstanceChooser instanceChooser;
    std::optional<SnmpRequestContext> tableContext;
    QList<TableJob> tableJobs;
    QString requestLabel;
    bool selecting = false;
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
