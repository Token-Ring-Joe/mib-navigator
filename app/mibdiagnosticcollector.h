#ifndef MIBDIAGNOSTICCOLLECTOR_H
#define MIBDIAGNOSTICCOLLECTOR_H

#include "mibrecords.h"
#include "smi.h"

class MibDiagnosticCollector
{
public:
    explicit MibDiagnosticCollector(quint64 operationId = 0,
                                    const QString &module = QString());
    ~MibDiagnosticCollector();
    void install(int errorLevel);
    void finish(SmiErrorHandler *replacement = nullptr, int replacementLevel = 0);
    const QList<MibDiagnosticRecord> &diagnostics() const;
    static QString rawText(const MibDiagnosticRecord &record);

private:
    static void handler(char *path, int line, int severity, char *message, char *tag);
    void append(char *path, int line, int severity, char *message, char *tag);
    quint64 operation;
    QString requestedModule;
    QList<MibDiagnosticRecord> captured;
    bool installed = false;
    static MibDiagnosticCollector *active;
};

#endif
