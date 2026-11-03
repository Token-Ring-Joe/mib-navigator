#include "watchservice.h"

#include "mibvaluesemantics.h"
#include "snmpreport.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <QPointer>
#include <QRunnable>
#include <QThread>

namespace {
const char *const kUptimeOid = "1.3.6.1.2.1.1.3.0";

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

quint64 WatchService::add(const WatchSpec &spec)
{
    WatchRow row;
    row.id = nextId++;
    row.spec = spec;
    row.spec.intervalSeconds = qBound(1, spec.intervalSeconds, 3600);
    row.objectLabel = spec.oid;
    row.status = tr("Waiting for the first answer");
    row.nextDueMs = 0;   // poll as soon as the timer next fires
    rows.insert(row.id, row);
    order.append(row.id);
    emit rowAdded(row.id);
    return row.id;
}

void WatchService::remove(quint64 id)
{
    if (!rows.remove(id)) return;
    order.removeAll(id);
    emit rowRemoved(id);
}

void WatchService::clear()
{
    const QList<quint64> all = order;
    for (quint64 id : all) remove(id);
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
