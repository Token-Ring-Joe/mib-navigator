#include "diagnosticlogger.h"
#include "udpportowner.h"

#include <QApplication>
#include <QFile>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTextEdit>
#include <iostream>

namespace {
bool check(bool condition, const char *message)
{
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}

// The logger delivers to the widget with queued calls. One processEvents() does
// not reliably flush them on every platform event loop (it does not on glib/xcb).
void deliverQueuedCalls()
{
    QCoreApplication::sendPostedEvents();
    QCoreApplication::processEvents();
}
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QTemporaryDir temporary;
    bool ok = check(temporary.isValid(), "temporary log directory");
    ok &= check(DiagnosticLogger::initialize("test", app.arguments(), temporary.path()),
                "logger initialization");
    QTextEdit displayed;
    DiagnosticLogger::attachLogWidget(&displayed);
    DiagnosticLogger::log("Test",
        "community=public password=hunter2 authSecret=alpha privacySecret=beta");
    deliverQueuedCalls();
    ok &= check(displayed.toPlainText().contains("[Test]"),
                "structured message appears in displayed log");
    DiagnosticLogger::clearDisplayedLog();
    deliverQueuedCalls();
    ok &= check(displayed.toPlainText().isEmpty(), "Clear Log clears displayed session text");
    DiagnosticLogger::log("Test", "message after clear");
    deliverQueuedCalls();
    ok &= check(displayed.toPlainText().contains("message after clear"),
                "messages continue after Clear Log");
    const QString timestamp = DiagnosticLogger::structuredTimestamp();
    ok &= check(QRegularExpression(
        QStringLiteral("^\\d{4}-\\d{2}-\\d{2}T\\d{2}:\\d{2}:\\d{2}\\.\\d{3}[+-]\\d{2}:\\d{2}$"))
        .match(timestamp).hasMatch(), "local ISO timestamp has milliseconds and numeric offset");
    ok &= check(QDateTime::fromString(timestamp, Qt::ISODateWithMs).offsetFromUtc() ==
        QDateTime::currentDateTime().offsetFromUtc(), "timestamp uses current OS local offset");
    const QString path = DiagnosticLogger::logFilePath();
    DiagnosticLogger::shutdown();
    QFile log(path);
    ok &= check(log.open(QIODevice::ReadOnly), "diagnostic log creation");
    const QByteArray contents = log.readAll();
    ok &= check(contents.contains("PROCESS START") && contents.contains("[REDACTED]"),
                "startup logging and redaction marker");
    ok &= check(!contents.contains("public") && !contents.contains("hunter2") &&
                !contents.contains("alpha") && !contents.contains("beta"),
                "secret leaked to diagnostic log");

    UdpPortOwner owner;
    owner.found = true; owner.processId = 78472;
    owner.processName = "mib-navigator.exe";
    owner.executablePath = "C:/MIB Navigator/mib-navigator.exe";
    const QString description = UdpPortOwnerLookup::conflictDescription(162, owner);
    ok &= check(description.contains("Another instance") &&
                description.contains("78472") && description.contains(owner.executablePath),
                "Windows owner diagnostic formatting");
    const QString unavailable = UdpPortOwnerLookup::conflictDescription(162, {});
    ok &= check(unavailable.contains("could not be determined"),
                "unavailable owner diagnostic");
    // This read-only lookup opens no socket and is safe regardless of whether
    // the chosen high port currently has an owner.
    const UdpPortOwner probe = UdpPortOwnerLookup::lookup(65534, false);
    Q_UNUSED(probe);
    return ok ? 0 : 1;
}
