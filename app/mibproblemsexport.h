#ifndef MIBPROBLEMSEXPORT_H
#define MIBPROBLEMSEXPORT_H

#include "csvformat.h"
#include "mibproblem.h"
#include <QSaveFile>

// A presentation/export snapshot only. The published Environment owns the report.
inline QString MibProblemsAsCsv(const QList<MibProblem> &problems, const QList<int> &order)
{
    QList<QStringList> records{{"Severity", "Module", "Problem", "Why", "What it affects",
                               "File", "Line", "Needed by", "What you can do", "Parser tag"}};
    for (int index : order) {
        if (index < 0 || index >= problems.size()) continue;
        const auto &p = problems.at(index);
        records.append({MibProblemSeverityText(p.severity), p.module, p.category, p.why, p.effect,
            p.file, p.line > 0 ? QString::number(p.line) : QString(),
            p.requestedBy.join(QStringLiteral("; ")), p.suggestion, p.parserTag});
    }
    return FormatCsv(records);
}

inline bool SaveMibProblemsCsv(const QString &path, const QString &snapshot, QString *error)
{
    QSaveFile file(path);
    const QByteArray bytes = snapshot.toUtf8();
    if (!file.open(QIODevice::WriteOnly)) { if (error) *error = file.errorString(); return false; }
    if (file.write(bytes) != bytes.size()) {
        if (error) *error = file.errorString();
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) { if (error) *error = file.errorString(); return false; }
    if (error) error->clear();
    return true;
}
#endif
