#include "mibdiagnosticcollector.h"

MibDiagnosticCollector *MibDiagnosticCollector::active = nullptr;

MibDiagnosticCollector::MibDiagnosticCollector(quint64 operationId,
                                               const QString &module)
    : operation(operationId), requestedModule(module) {}

MibDiagnosticCollector::~MibDiagnosticCollector() { finish(); }

void MibDiagnosticCollector::install(int errorLevel)
{
    Q_ASSERT(!active);
    active = this;
    installed = true;
    smiSetErrorHandler(handler);
    smiSetErrorLevel(errorLevel);
}

void MibDiagnosticCollector::finish(SmiErrorHandler *replacement, int replacementLevel)
{
    if (!installed)
        return;
    if (active == this)
        active = nullptr;
    smiSetErrorHandler(replacement);
    smiSetErrorLevel(replacementLevel);
    installed = false;
}

const QList<MibDiagnosticRecord> &MibDiagnosticCollector::diagnostics() const
{
    return captured;
}

QString MibDiagnosticCollector::rawText(const MibDiagnosticRecord &record)
{
    return QStringLiteral("%1:%2: [%3] %4")
        .arg(record.sourcePath).arg(record.line).arg(record.tag, record.message);
}

void MibDiagnosticCollector::handler(char *path, int line, int severity,
                                     char *message, char *tag)
{
    if (active)
        active->append(path, line, severity, message, tag);
}

void MibDiagnosticCollector::append(char *path, int line, int severity,
                                    char *message, char *tag)
{
    MibDiagnosticRecord record;
    record.severity = severity;
    record.tag = QString::fromLocal8Bit(tag ? tag : "");
    record.module = requestedModule;
    record.sourcePath = QString::fromLocal8Bit(path ? path : "");
    record.line = line;
    record.message = QString::fromLocal8Bit(message ? message : "");
    record.operationId = operation;
    record.rawText = rawText(record);
    captured.append(record);
}
