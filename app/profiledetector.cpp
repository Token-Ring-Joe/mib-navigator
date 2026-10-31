#include "profiledetector.h"

#include "snmpreport.h"

#include <QMetaObject>
#include <QPointer>
#include <QRunnable>

DeviceIdentity ProfileDetector::read(ISnmpTransport &transport, const SnmpRequestConfig &config)
{
    DeviceIdentity identity;
    SnmpTransportRequest request;
    request.operation = SnmpTransportOperation::Get;
    request.pdu.set_type(sNMP_PDU_GET);
    Vb objectId{Oid("1.3.6.1.2.1.1.2.0")}; request.pdu += objectId;
    Vb descr{Oid("1.3.6.1.2.1.1.1.0")}; request.pdu += descr;
    const SnmpTransportResult answer = transport.execute(request);

    switch (answer.status) {
    case SnmpOperationStatus::Success: break;
    case SnmpOperationStatus::Timeout:
        identity.error = QObject::tr("Timeout: no response from the device. Check the Target address and the credential.");
        return identity;
    case SnmpOperationStatus::SnmpError:
        identity.error = QObject::tr("The device refused the request: %1")
            .arg(QString::fromLatin1(Snmp::error_msg(answer.snmpErrorStatus)));
        return identity;
    default:
        identity.error = QObject::tr("The request could not be sent (%1)")
            .arg(QString::fromLatin1(Snmp::error_msg(answer.transportStatus)));
        return identity;
    }
    const QString report = ExplainSnmpReport(answer.pdu, config.securityName);
    if (!report.isEmpty()) { identity.error = report; return identity; }

    for (int i = 0; i < answer.pdu.get_vb_count(); ++i) {
        Vb vb; answer.pdu.get_vb(vb, i);
        const int syntax = int(vb.get_syntax());
        if (syntax == sNMP_SYNTAX_NOSUCHOBJECT || syntax == sNMP_SYNTAX_NOSUCHINSTANCE || syntax == sNMP_SYNTAX_ENDOFMIBVIEW)
            continue;
        Oid oid; vb.get_oid(oid);
        const QString name = QString::fromLatin1(oid.get_printable());
        if (name == QLatin1String("1.3.6.1.2.1.1.2.0") && syntax == sNMP_SYNTAX_OID) {
            Oid value; if (vb.get_value(value) == SNMP_CLASS_SUCCESS) identity.sysObjectId = QString::fromLatin1(value.get_printable());
        } else if (name == QLatin1String("1.3.6.1.2.1.1.1.0")) {
            identity.sysDescr = QString::fromLatin1(vb.get_printable_value());
        }
    }
    if (identity.sysObjectId.isEmpty()) {
        identity.error = QObject::tr("The device did not report its sysObjectID, so it cannot be matched to a profile.");
        return identity;
    }
    identity.ok = true;
    return identity;
}

ProfileDetector::ProfileDetector(ConfigBuilder builder, TransportFactory factory, QObject *parent)
    : QObject(parent), buildConfig(std::move(builder)), makeTransport(std::move(factory))
{
    qRegisterMetaType<DeviceIdentity>();
    pool.setMaxThreadCount(1);
}

ProfileDetector::~ProfileDetector() { pool.waitForDone(15000); }

bool ProfileDetector::detect(const QString &target, const QString &credentialKey, QString *error)
{
    if (running) { if (error) *error = tr("A detection is already running."); return false; }
    SnmpRequestConfig config;
    QString why;
    if (!buildConfig(target, credentialKey, &config, &why)) { if (error) *error = why; return false; }
    running = true;
    class Task final : public QRunnable
    {
    public:
        Task(QPointer<ProfileDetector> detector, SnmpRequestConfig config, QString target, TransportFactory factory)
            : detector(std::move(detector)), config(std::move(config)), target(std::move(target)), factory(std::move(factory)) {}
        void run() override
        {
            DeviceIdentity identity;
            if (std::unique_ptr<ISnmpTransport> transport = factory(config)) identity = ProfileDetector::read(*transport, config);
            else identity.error = QObject::tr("No SNMP session could be created");
            QPointer<ProfileDetector> self = detector;
            if (!self) return;
            const QString name = target;
            QMetaObject::invokeMethod(self.data(), [self, name, identity]() {
                if (!self) return;
                self->running = false;
                emit self->finished(name, identity);
            }, Qt::QueuedConnection);
        }
    private:
        QPointer<ProfileDetector> detector; SnmpRequestConfig config; QString target; TransportFactory factory;
    };
    pool.start(new Task(QPointer<ProfileDetector>(this), config, target, makeTransport));
    return true;
}
