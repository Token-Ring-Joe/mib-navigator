#include "diagnosticlogger.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QOperatingSystemVersion>
#include <QPointer>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTextEdit>
#include <QThread>
#include <QUuid>
#include <QWidget>
#include <QEvent>
#include <cstdio>

namespace {
QMutex mutex;
QFile *file = nullptr;
QString path;
QString sessionId;
QPointer<QTextEdit> logWidget;
DiagnosticLogger *instance = nullptr;
QtMessageHandler previousHandler = nullptr;
thread_local bool handlingMessage = false;

QString severity(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg: return QStringLiteral("Debug");
    case QtInfoMsg: return QStringLiteral("Info");
    case QtWarningMsg: return QStringLiteral("Warning");
    case QtCriticalMsg: return QStringLiteral("Critical");
    case QtFatalMsg: return QStringLiteral("Fatal");
    }
    return QStringLiteral("Qt");
}

void qtMessageHandler(QtMsgType type, const QMessageLogContext &context,
                      const QString &message)
{
    if (!handlingMessage) {
        handlingMessage = true;
        QString location;
        if (context.file)
            location = QStringLiteral(" (%1:%2)").arg(
                QString::fromLocal8Bit(context.file)).arg(context.line);
        DiagnosticLogger::log(QStringLiteral("Qt/%1").arg(severity(type)),
                              message + location, type != QtDebugMsg);
        handlingMessage = false;
    }
    const QByteArray local = message.toLocal8Bit();
    std::fprintf(stderr, "%s\n", local.constData());
    std::fflush(stderr);
    if (type == QtFatalMsg) std::abort();
}
}

bool DiagnosticLogger::initialize(const QString &version,
                                  const QStringList &arguments,
                                  const QString &directoryOverride)
{
    QMutexLocker lock(&mutex);
    if (file) return true;
    const QString directory = directoryOverride.isEmpty() ?
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) +
            QStringLiteral("/logs") : directoryOverride;
    if (!QDir().mkpath(directory)) return false;
    QDir logDir(directory);
    const QFileInfoList oldLogs = logDir.entryInfoList(
        {QStringLiteral("MIB-Navigator-*.log")}, QDir::Files, QDir::Time);
    for (int i = 19; i < oldLogs.size(); ++i)
        QFile::remove(oldLogs.at(i).absoluteFilePath());
    sessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    path = logDir.filePath(QStringLiteral("MIB-Navigator-%1-PID%2.log")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz")))
        .arg(QCoreApplication::applicationPid()));
    file = new QFile(path);
    if (!file->open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Append)) {
        delete file; file = nullptr; path.clear(); return false;
    }
    if (!instance) instance = new DiagnosticLogger;
    previousHandler = qInstallMessageHandler(qtMessageHandler);
    lock.unlock();
    log(QStringLiteral("Startup"), QStringLiteral("PROCESS START"));
    log(QStringLiteral("Startup"), QStringLiteral("session=%1 pid=%2 version=%3 Qt=%4")
        .arg(sessionId).arg(QCoreApplication::applicationPid()).arg(version,
             QString::fromLatin1(qVersion())));
    log(QStringLiteral("Startup"), QStringLiteral("executable=%1 OS=%2")
        .arg(QCoreApplication::applicationFilePath(),
             QOperatingSystemVersion::current().name()));
    log(QStringLiteral("Startup"), QStringLiteral("commandLine=%1")
        .arg(redact(arguments.join(QLatin1Char(' ')))));
    return true;
}

void DiagnosticLogger::shutdown()
{
    log(QStringLiteral("Shutdown"), QStringLiteral("process normal exit"), false);
    QMutexLocker lock(&mutex);
    qInstallMessageHandler(previousHandler);
    previousHandler = nullptr;
    if (file) { file->flush(); file->close(); delete file; file = nullptr; }
    logWidget.clear(); path.clear(); sessionId.clear();
}

void DiagnosticLogger::log(const QString &category, const QString &message,
                           bool mirrorToUi)
{
    const QString safe = redact(message);
    const QString line = QStringLiteral("%1 [%2] [tid=%3] [%4] %5\n")
        .arg(structuredTimestamp(), sessionId)
        .arg(reinterpret_cast<quintptr>(QThread::currentThreadId()), 0, 16)
        .arg(category, safe);
    {
        QMutexLocker lock(&mutex);
        if (file) { file->write(line.toUtf8()); file->flush(); }
    }
    if (mirrorToUi && logWidget) {
        const QString display = line.trimmed();
        QMetaObject::invokeMethod(logWidget.data(), [display]() {
            if (logWidget) logWidget->append(display.toHtmlEscaped());
        }, Qt::QueuedConnection);
    }
}

QString DiagnosticLogger::redact(const QString &message)
{
    QString result = message;
    static const QRegularExpression assignment(
        QStringLiteral("(?i)(community|password|passphrase|authsecret|privacysecret|localizedkey)\\s*[:=]\\s*([^\\s,;]+)"));
    result.replace(assignment, QStringLiteral("\\1=[REDACTED]"));
    static const QRegularExpression option(
        QStringLiteral("(?i)(--(?:community|password|passphrase|auth-secret|privacy-secret))\\s+([^\\s]+)"));
    result.replace(option, QStringLiteral("\\1 [REDACTED]"));
    return result;
}

QString DiagnosticLogger::logFilePath() { QMutexLocker lock(&mutex); return path; }
QString DiagnosticLogger::logDirectory() { return QFileInfo(logFilePath()).absolutePath(); }
void DiagnosticLogger::attachLogWidget(QTextEdit *widget) { logWidget = widget; }
void DiagnosticLogger::clearDisplayedLog()
{
    if (!logWidget) return;
    QMetaObject::invokeMethod(logWidget.data(), [] {
        if (logWidget) logWidget->clear();
    }, Qt::QueuedConnection);
}

QString DiagnosticLogger::structuredTimestamp(const QDateTime &dateTime)
{
    const QDateTime local = dateTime.toLocalTime();
    const int offset = local.offsetFromUtc();
    const QChar sign = offset < 0 ? QLatin1Char('-') : QLatin1Char('+');
    const int absoluteOffset = qAbs(offset);
    return QStringLiteral("%1%2%3:%4")
        .arg(local.toString(QStringLiteral("yyyy-MM-dd'T'HH:mm:ss.zzz")))
        .arg(sign)
        .arg(absoluteOffset / 3600, 2, 10, QLatin1Char('0'))
        .arg((absoluteOffset % 3600) / 60, 2, 10, QLatin1Char('0'));
}

void DiagnosticLogger::installMainWindowLifecycle(QWidget *window)
{
    if (!window || !instance) return;
    window->installEventFilter(instance);
    QObject::connect(window, &QObject::destroyed, instance, [] {
        DiagnosticLogger::log(QStringLiteral("UI"),
                              QStringLiteral("main window destroyed"), false);
    });
}

bool DiagnosticLogger::eventFilter(QObject *watched, QEvent *event)
{
    Q_UNUSED(watched);
    switch (event->type()) {
    case QEvent::Show: log(QStringLiteral("UI"), QStringLiteral("main window shown")); break;
    case QEvent::Hide: log(QStringLiteral("UI"), QStringLiteral("main window hidden")); break;
    case QEvent::Close: log(QStringLiteral("UI"), QStringLiteral("main window close requested")); break;
    default: break;
    }
    return false;
}
