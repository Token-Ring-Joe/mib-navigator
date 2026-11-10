#include "diagnosticlogger.h"
#include "udpportowner.h"

#include <QApplication>
#include <QFile>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTextEdit>
#include <iostream>
#include <thread>
#include <memory>

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
    std::thread worker([] {
        DiagnosticLogger::log("SNMP++", "library first\r\nlibrary second\ncommunity=library-secret\n<literal-not-markup>");
    });
    worker.join(); deliverQueuedCalls();
    const auto displayedLines = displayed.toPlainText().split('\n');
    bool libraryLinesPrefixed = true; int libraryLines = 0;
    const QRegularExpression prefix(QStringLiteral(R"(^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9:.]+[+-][0-9]{2}:[0-9]{2} .+ \[SNMP\+\+\] )"));
    for (const auto &line : displayedLines) if (line.contains("[SNMP++]")) {
        ++libraryLines; libraryLinesPrefixed &= prefix.match(line).hasMatch();
    }
    ok &= check(libraryLines == 4 && libraryLinesPrefixed &&
                displayed.toPlainText().contains("<literal-not-markup>") &&
                !displayed.toPlainText().contains("library-secret"),
                "multiline worker library logs have ISO timestamps on every line, safe literal text and redaction");
    auto destroyed = std::make_unique<QTextEdit>();
    DiagnosticLogger::attachLogWidget(destroyed.get());
    DiagnosticLogger::log("Test", "message for destroyed recipient");
    destroyed.reset();
    DiagnosticLogger::attachLogWidget(&displayed); deliverQueuedCalls();
    ok &= check(!displayed.toPlainText().contains("message for destroyed recipient"),
                "queued logging cannot move a destroyed recipient's message to another widget");
    ok &= check(!DiagnosticLogger::redactLibraryMessage("password: a secret, with; spaces\nand another line").contains("secret") &&
                !DiagnosticLogger::redactLibraryMessage("USMUserTable: New key: (AA BB CC DD)").contains("AA BB"),
                "native unquoted/multiline library secrets never reach the diagnostic logger");
    const QString protectedKey = DiagnosticLogger::redact("USMUserTable: New key: (AA BB CC DD)");
    ok &= check(!protectedKey.contains("AA BB") && protectedKey.contains("[REDACTED]") &&
                !DiagnosticLogger::redact("password=\"a secret with spaces\"").contains("with spaces") &&
                DiagnosticLogger::redact("Could not generate localized key: Unsupported auth protocol")
                    .contains("Unsupported auth protocol"),
                "SNMP++ key/quoted-secret redaction preserves useful failure explanations");
    const QString timestamp = DiagnosticLogger::structuredTimestamp();
    ok &= check(QRegularExpression(
        QStringLiteral("^\\d{4}-\\d{2}-\\d{2}T\\d{2}:\\d{2}:\\d{2}\\.\\d{3}[+-]\\d{2}:\\d{2}$"))
        .match(timestamp).hasMatch(), "local ISO timestamp has milliseconds and numeric offset");
    ok &= check(QDateTime::fromString(timestamp, Qt::ISODateWithMs).offsetFromUtc() ==
        QDateTime::currentDateTime().offsetFromUtc(), "timestamp uses current OS local offset");
    const QString path = DiagnosticLogger::logFilePath();
    DiagnosticLogger::log("Test", "stale-session-marker");
    DiagnosticLogger::shutdown();
    QFile log(path);
    ok &= check(log.open(QIODevice::ReadOnly), "diagnostic log creation");
    const QByteArray contents = log.readAll();
    ok &= check(contents.contains("PROCESS START") && contents.contains("[REDACTED]"),
                "startup logging and redaction marker");
    ok &= check(!contents.contains("public") && !contents.contains("hunter2") &&
                !contents.contains("alpha") && !contents.contains("beta"),
                "secret leaked to diagnostic log");

    app.setProperty("snmpb.isolatedStateRoot", temporary.filePath("isolated"));
    ok &= check(DiagnosticLogger::initialize("isolated", app.arguments()) &&
                DiagnosticLogger::logFilePath().startsWith(temporary.filePath("isolated/logs/")),
                "isolated logger stays inside the isolated state directory without an override");
    QTextEdit nextSession;
    DiagnosticLogger::attachLogWidget(&nextSession); deliverQueuedCalls();
    ok &= check(!displayed.toPlainText().contains("stale-session-marker") &&
                !nextSession.toPlainText().contains("stale-session-marker"),
                "shutdown/reinitialize rejects delivery from the previous diagnostic session");
    DiagnosticLogger::shutdown();

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
