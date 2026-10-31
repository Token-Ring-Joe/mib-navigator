#ifndef PROFILEDETECTOR_H
#define PROFILEDETECTOR_H

#include <QObject>
#include <QThreadPool>
#include <functional>
#include <memory>

#include "snmptransport.h"

// What a device said about itself, read once when the user clicks Detect.
struct DeviceIdentity
{
    bool ok = false;
    QString error;          // plain-English reason when it did not answer
    QString sysObjectId;    // "1.3.6.1.4.1...."
    QString sysDescr;
};

// Reads sysObjectID.0 and sysDescr.0 from one device. It never runs by itself: the only caller
// is the Detect button. One read at a time; the answer arrives as a signal on the main thread.
class ProfileDetector : public QObject
{
    Q_OBJECT
public:
    using ConfigBuilder = std::function<bool(const QString &target, const QString &credentialKey,
                                             SnmpRequestConfig *config, QString *error)>;
    using TransportFactory = std::function<std::unique_ptr<ISnmpTransport>(const SnmpRequestConfig &)>;

    ProfileDetector(ConfigBuilder builder, TransportFactory factory, QObject *parent = nullptr);
    ~ProfileDetector() override;

    // Returns false (and says why through error) when a read is already running or the target or
    // credential is not usable. Otherwise finished() follows.
    bool detect(const QString &target, const QString &credentialKey, QString *error);
    bool isRunning() const { return running; }
    // Reads the identity through a transport already in hand (also what the background read runs).
    static DeviceIdentity read(ISnmpTransport &transport, const SnmpRequestConfig &config);

signals:
    void finished(const QString &target, const DeviceIdentity &identity);

private:
    ConfigBuilder buildConfig;
    TransportFactory makeTransport;
    QThreadPool pool;
    bool running = false;
};

Q_DECLARE_METATYPE(DeviceIdentity)

#endif
