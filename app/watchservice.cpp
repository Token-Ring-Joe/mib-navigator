#include "watchservice.h"

#include "mibvaluesemantics.h"
#include "snmpreport.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QLockFile>
#include <QMetaObject>
#include <QPointer>
#include <QRunnable>
#include <QThread>

namespace {
const char *const kUptimeOid = "1.3.6.1.2.1.1.3.0";
constexpr int kMaximumDefinitions = 10000;
constexpr qint64 kMaximumStoreBytes = 16 * 1024 * 1024;

bool validDefinition(const WatchSpec &spec)
{
    static const QRegularExpression numeric(QStringLiteral(R"(^[0-9]+(?:\.[0-9]+)+$)"));
    if (spec.target.trimmed().isEmpty() || spec.credentialKey.isEmpty() ||
        spec.intervalSeconds < 1 || spec.intervalSeconds > 3600 ||
        !numeric.match(spec.oid).hasMatch()) return false;
    for (const auto &part : spec.oid.split('.')) {
        bool ok = false; const auto value = part.toULongLong(&ok);
        if (!ok || value > 0xffffffffULL) return false;
    }
    return Oid(spec.oid.toLatin1().constData()).valid();
}

WatchSample::Kind kindFor(int syntax)
{
    switch (syntax) {
    case sNMP_SYNTAX_INT32: return WatchSample::Kind::Integer;
    case sNMP_SYNTAX_GAUGE32: return WatchSample::Kind::Gauge;
    case sNMP_SYNTAX_CNTR32: return WatchSample::Kind::Counter32;
    case sNMP_SYNTAX_CNTR64: return WatchSample::Kind::Counter64;
    case sNMP_SYNTAX_TIMETICKS: return WatchSample::Kind::TimeTicks;
    default: return WatchSample::Kind::Other;
    }
}

bool octetsObject(const MibEnvironmentNodeRecord *node)
{
    if (!node) return false;
    return node->units.compare(QLatin1String("octets"), Qt::CaseInsensitive) == 0 ||
           node->name.endsWith(QLatin1String("Octets"), Qt::CaseInsensitive);
}

// Runs one GET (the object plus sysUpTime.0) and reduces the answer to a PollResult.
// Executes on a worker thread; it touches nothing owned by the main thread.
WatchService::PollResult pollOnce(ISnmpTransport &transport, const SnmpRequestConfig &config,
                                  const QString &oidText, const MibEnvironmentPtr &environment,
                                  qint64 nowMs)
{
    WatchService::PollResult result;
    result.sample.monotonicMs = nowMs;
    const Oid oid(oidText.toLatin1().constData());
    if (!oid.valid()) { result.sample.error = QObject::tr("The watched OID is not valid."); return result; }

    const auto request = [&](bool withUptime) {
        SnmpTransportRequest r;
        r.operation = SnmpTransportOperation::Get;
        r.pdu.set_type(sNMP_PDU_GET);
        Vb first(oid); r.pdu += first;
        if (withUptime) { Vb up{Oid(kUptimeOid)}; r.pdu += up; }
        return transport.execute(r);
    };
    SnmpTransportResult answer = request(true);
    // An SNMPv1 device that does not know sysUpTime rejects the whole request naming varbind 2.
    if (answer.status == SnmpOperationStatus::SnmpError && answer.snmpErrorIndex == 2)
        answer = request(false);

    switch (answer.status) {
    case SnmpOperationStatus::Success: break;
    case SnmpOperationStatus::Timeout:
        result.sample.error = QObject::tr("Timeout: no response from the device");
        return result;
    case SnmpOperationStatus::SnmpError:
        result.sample.error = QObject::tr("The device refused the request: %1")
            .arg(QString::fromLatin1(Snmp::error_msg(answer.snmpErrorStatus)));
        return result;
    default:
        result.sample.error = answer.transportStatus != 0
            ? ExplainSnmpTransportFailure(answer.transportStatus, config.securityName)
            : QObject::tr("The request could not be sent");
        return result;
    }

    const QString report = ExplainSnmpReport(answer.pdu, config.securityName);
    if (!report.isEmpty()) { result.sample.error = report; return result; }
    if (answer.pdu.get_vb_count() < 1) { result.sample.error = QObject::tr("The device returned nothing"); return result; }

    Vb vb; answer.pdu.get_vb(vb, 0);
    Oid answered; vb.get_oid(answered);
    const MibResolvedObject resolved = ResolveMibObject(environment, answered);
    const MibValueCheck check = CheckMibValue(resolved, vb);
    result.objectLabel = RenderMibOid(environment, answered);
    result.octetCounter = octetsObject(resolved.node);
    result.received = check.rawValue == check.receivedType
        ? check.receivedType : QStringLiteral("%1: %2").arg(check.receivedType, check.rawValue);
    result.warnings = check.warnings.join(QStringLiteral("  "));

    const int syntax = int(vb.get_syntax());
    if (syntax == sNMP_SYNTAX_NOSUCHOBJECT || syntax == sNMP_SYNTAX_NOSUCHINSTANCE ||
        syntax == sNMP_SYNTAX_ENDOFMIBVIEW) {
        result.sample.error = QObject::tr("The device does not have this object (%1)").arg(check.receivedType);
        return result;
    }
    result.value = RenderMibValue(resolved, vb);
    result.sample.ok = true;
    result.sample.rawValue = check.rawValue;
    result.sample.kind = kindFor(syntax);
    switch (result.sample.kind) {
    case WatchSample::Kind::Integer: {
        long v = 0; if (vb.get_value(v) == SNMP_CLASS_SUCCESS) { result.sample.number = v; result.sample.numeric = true; } break; }
    case WatchSample::Kind::Gauge:
    case WatchSample::Kind::TimeTicks: {
        unsigned long v = 0; if (vb.get_value(v) == SNMP_CLASS_SUCCESS) { result.sample.number = qint64(v); result.sample.numeric = true; } break; }
    case WatchSample::Kind::Counter32: {
        unsigned long v = 0; if (vb.get_value(v) == SNMP_CLASS_SUCCESS) { result.sample.counter = v; result.sample.numeric = true; } break; }
    case WatchSample::Kind::Counter64: {
        Counter64 v; if (vb.get_value(v) == SNMP_CLASS_SUCCESS) {
            result.sample.counter = (quint64(v.high()) << 32) | quint64(v.low()); result.sample.numeric = true; } break; }
    case WatchSample::Kind::Other: break;
    }
    if (answer.pdu.get_vb_count() >= 2) {
        Vb up; answer.pdu.get_vb(up, 1);
        unsigned long ticks = 0;
        if (int(up.get_syntax()) == sNMP_SYNTAX_TIMETICKS && up.get_value(ticks) == SNMP_CLASS_SUCCESS) {
            result.sample.hasUptime = true; result.sample.uptimeTicks = ticks;
        }
    }
    return result;
}

class PollTask final : public QRunnable
{
public:
    PollTask(QPointer<WatchService> service, quint64 id, SnmpRequestConfig config, QString oid,
             WatchService::TransportFactory factory, MibEnvironmentPtr environment, qint64 nowMs)
        : service(std::move(service)), id(id), config(std::move(config)), oid(std::move(oid)),
          factory(std::move(factory)), environment(std::move(environment)), nowMs(nowMs) {}
    void run() override
    {
        WatchService::PollResult result;
        std::unique_ptr<ISnmpTransport> transport = factory(config);
        if (!transport) { result.sample.error = QObject::tr("No SNMP session could be created"); }
        else result = pollOnce(*transport, config, oid, environment, nowMs);
        QPointer<WatchService> target = service;
        if (!target) return;
        const quint64 rowId = id;
        QMetaObject::invokeMethod(target.data(), "applyResultQueued", Qt::QueuedConnection,
                                  Q_ARG(quint64, rowId), Q_ARG(WatchService::PollResult, result));
    }
private:
    QPointer<WatchService> service;
    quint64 id;
    SnmpRequestConfig config;
    QString oid;
    WatchService::TransportFactory factory;
    MibEnvironmentPtr environment;
    qint64 nowMs;
};
}

Q_DECLARE_METATYPE(WatchService::PollResult)

WatchService::WatchService(ConfigBuilder builder, TransportFactory factory,
                           EnvironmentSource environmentSource, QObject *parent)
    : QObject(parent), buildConfig(std::move(builder)), makeTransport(std::move(factory)),
      environment(std::move(environmentSource))
{
    qRegisterMetaType<WatchService::PollResult>();
    pool.setMaxThreadCount(4);
    clock.start();
    connect(&timer, &QTimer::timeout, this, &WatchService::tick);
}

WatchService::~WatchService()
{
    timer.stop();
    pool.waitForDone(10000);
}

void WatchService::startTimer(int intervalMs) { timer.start(intervalMs); }

quint64 WatchService::add(const WatchSpec &spec, bool paused)
{
    WatchRow row;
    row.id = nextId++;
    row.spec = spec;
    row.spec.intervalSeconds = qBound(1, spec.intervalSeconds, 3600);
    if (order.size() >= kMaximumDefinitions || !validDefinition(row.spec)) return 0;
    row.objectLabel = spec.oid;
    row.paused = paused;
    row.status = paused ? tr("Paused") : tr("Waiting for the first answer");
    row.nextDueMs = 0;   // poll as soon as the timer next fires
    rows.insert(row.id, row);
    order.append(row.id);
    persistDefinitions();
    emit rowAdded(row.id);
    return row.id;
}

void WatchService::remove(quint64 id)
{
    if (!rows.remove(id)) return;
    order.removeAll(id);
    persistDefinitions();
    emit rowRemoved(id);
}

void WatchService::clear()
{
    const QList<quint64> all = order;
    const bool previous = restoring; restoring = true;
    for (quint64 id : all) remove(id);
    restoring = previous;
    persistDefinitions();
}

bool WatchService::setInterval(quint64 id, int seconds)
{
    auto it = rows.find(id);
    if (it == rows.end() || seconds < 1 || seconds > 3600) return false;
    if (it->spec.intervalSeconds == seconds) return true;
    it->spec.intervalSeconds = seconds;
    it->nextDueMs = clock.elapsed() + qint64(seconds) * 1000;
    persistDefinitions();
    emit rowChanged(id);
    return true;
}

void WatchService::setPaused(quint64 id, bool paused)
{
    auto it = rows.find(id);
    if (it == rows.end() || it->paused == paused) return;
    it->paused = paused;
    it->status = paused ? tr("Paused") : tr("Resuming");
    if (!paused) it->nextDueMs = 0;
    emit rowChanged(id);
}

const WatchRow *WatchService::row(quint64 id) const
{
    const auto it = rows.constFind(id);
    return it == rows.cend() ? nullptr : &it.value();
}

QList<quint64> WatchService::ids() const { return order; }
int WatchService::pollsInFlight() const { return outstanding; }

bool WatchService::waitForPolls(int timeoutMs)
{
    QElapsedTimer waited; waited.start();
    while (outstanding > 0) {
        if (waited.elapsed() > timeoutMs) return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
    QCoreApplication::processEvents();
    return true;
}

void WatchService::pollNow(quint64 id)
{
    auto it = rows.find(id);
    if (it != rows.end() && !it->inFlight) startPoll(*it);
}

void WatchService::tick()
{
    const qint64 now = clock.elapsed();
    for (quint64 id : order) {
        auto it = rows.find(id);
        if (it == rows.end() || it->paused || it->inFlight || it->nextDueMs > now) continue;
        startPoll(*it);
    }
}

void WatchService::startPoll(WatchRow &row)
{
    const qint64 now = clock.elapsed();
    row.nextDueMs = now + qint64(row.spec.intervalSeconds) * 1000;
    SnmpRequestConfig config;
    QString error;
    if (!buildConfig(row.spec.target, row.spec.credentialKey, &config, &error)) {
        row.status = error.isEmpty() ? tr("The target or credential is not usable") : error;
        ++row.failures;
        row.updated = QDateTime::currentDateTime();
        emit rowChanged(row.id);
        return;
    }
    config.contextName = row.spec.contextName;
    row.inFlight = true;
    ++outstanding;
    pool.start(new PollTask(QPointer<WatchService>(this), row.id, config, row.spec.oid,
                            makeTransport, environment ? environment() : MibEnvironmentPtr{}, now));
}

// Queued from the worker thread.
void WatchService::applyResult(quint64 id, const PollResult &result)
{
    --outstanding;
    auto it = rows.find(id);
    if (it == rows.end()) return;   // the row was removed while its poll was running
    WatchRow &row = *it;
    row.inFlight = false;
    ++row.polls;
    row.updated = QDateTime::currentDateTime();
    if (!result.sample.ok) {
        ++row.failures;
        row.status = result.sample.error;
        row.change = {};
    } else {
        if (!result.objectLabel.isEmpty()) row.objectLabel = result.objectLabel;
        row.octetCounter = result.octetCounter;
        row.value = result.value;
        row.received = result.received;
        row.warnings = result.warnings;
        row.change = ComputeWatchChange(row.baseline, result.sample, row.octetCounter);
        row.baseline = result.sample;
        row.status = tr("OK");
    }
    emit rowChanged(id);
}


void WatchService::setStoreError(const QString &error)
{
    if (storeError == error) return;
    storeError = error;
    emit persistenceErrorChanged(storeError);
}

bool WatchService::restoreDefinitions(const QString &fileName)
{
    if (!definitionsFile.isEmpty() || !order.isEmpty()) return false;
    definitionsFile = fileName;
    QFile file(fileName);
    if (!file.exists()) return true;
    const auto failed = [this, &fileName](const QString &reason) {
        storeBlocked = true;
        setStoreError(tr("Saved watches could not be loaded: %1. The original file is kept at %2. Fix it and restart before saving changes.")
                      .arg(reason, fileName));
        return false;
    };
    if (!file.open(QIODevice::ReadOnly)) return failed(file.errorString());
    const QByteArray bytes = file.read(kMaximumStoreBytes + 1);
    if (file.error() != QFileDevice::NoError) return failed(file.errorString());
    if (bytes.size() > kMaximumStoreBytes) return failed(tr("file is too large"));
    QJsonParseError parse;
    const auto document = QJsonDocument::fromJson(bytes, &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject())
        return failed(tr("invalid JSON"));
    const auto object = document.object();
    if (object.value("schemaVersion") != QJsonValue(1) || !object.value("watches").isArray())
        return failed(tr("unsupported or incomplete format"));
    const auto array = object.value("watches").toArray();
    if (array.size() > kMaximumDefinitions) return failed(tr("too many definitions"));
    QList<WatchSpec> specs;
    for (const auto &value : array) {
        if (!value.isObject()) return failed(tr("invalid watch definition"));
        const auto item = value.toObject();
        for (const char *key : {"target", "credential", "oid", "context"})
            if (!item.value(QLatin1String(key)).isString()) return failed(tr("invalid watch definition"));
        WatchSpec spec;
        spec.target = item.value("target").toString();
        spec.credentialKey = item.value("credential").toString();
        spec.oid = item.value("oid").toString();
        spec.contextName = item.value("context").toString();
        const auto interval = item.value("intervalSeconds");
        if (!interval.isDouble() || interval.toDouble() != interval.toInt(-1))
            return failed(tr("invalid polling interval"));
        spec.intervalSeconds = interval.toInt();
        if (!validDefinition(spec)) return failed(tr("invalid watch definition"));
        specs.append(spec);
    }
    storeExisted = true;
    storedBytes = bytes;
    restoring = true;
    for (const auto &spec : specs) add(spec, true);
    restoring = false;
    setStoreError({});
    return true;
}

void WatchService::persistDefinitions()
{
    if (definitionsFile.isEmpty() || restoring || storeBlocked) return;
    QJsonArray definitions;
    for (quint64 id : order) {
        const WatchSpec spec = rows.value(id).spec;
        definitions.append(QJsonObject{{"target", spec.target}, {"credential", spec.credentialKey},
            {"oid", spec.oid}, {"context", spec.contextName}, {"intervalSeconds", spec.intervalSeconds}});
    }
    const QByteArray bytes = QJsonDocument(QJsonObject{{"schemaVersion", 1}, {"watches", definitions}})
                             .toJson(QJsonDocument::Indented);
    const auto failed = [this](const QString &reason) {
        setStoreError(tr("Watches remain available this session but could not be saved to %1: %2")
                      .arg(definitionsFile, reason));
    };
    if (!QDir().mkpath(QFileInfo(definitionsFile).absolutePath())) {
        failed(tr("the settings folder could not be created")); return;
    }
    if (bytes.size() > kMaximumStoreBytes) { failed(tr("definitions exceed the file size limit")); return; }
    QLockFile lock(definitionsFile + QStringLiteral(".lock"));
    if (!lock.tryLock(0)) { failed(tr("another process is saving watches; try another edit shortly")); return; }
    QFile previous(definitionsFile);
    if (previous.exists() != storeExisted ||
        (storeExisted && (!previous.open(QIODevice::ReadOnly) || previous.read(kMaximumStoreBytes + 1) != storedBytes))) {
        storeBlocked = true;
        failed(tr("the file changed outside this session; restart to reload it before saving"));
        return;
    }
    previous.close();
    if (storeExisted && bytes == storedBytes) { setStoreError({}); return; }
    QSaveFile file(definitionsFile);
    if (!file.open(QIODevice::WriteOnly)) { failed(file.errorString()); return; }
    if (!file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
        failed(file.errorString()); file.cancelWriting(); return;
    }
    if (file.write(bytes) != bytes.size()) { failed(file.errorString()); file.cancelWriting(); return; }
    if (!file.commit()) { failed(file.errorString()); return; }
    storedBytes = bytes;
    storeExisted = true;
    setStoreError({});
}
