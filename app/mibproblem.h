#ifndef MIBPROBLEM_H
#define MIBPROBLEM_H

#include <QList>
#include <QString>
#include <QStringList>

// A single thing that went wrong (or deserves a note) while building a MIB
// Environment. MIB Navigator keeps loading what it can; these records are how it
// tells the user what was skipped, what was degraded, and why.
enum class MibProblemSeverity {
    Error,   // something the user chose could not be loaded at all
    Warning, // loaded, but some definitions may be missing or unresolved
    Note     // informational only; does not count as a problem
};

struct MibProblem {
    MibProblemSeverity severity = MibProblemSeverity::Warning;
    QString category;       // short label, e.g. "Missing dependency"
    QString module;         // module identity, or the file name when there is none
    QString file;           // original (not staged) file path, if known
    int line = 0;           // 1-based line in `file`, 0 when unknown
    QString why;            // plain-language cause
    QString effect;         // what will not work, or may not work
    QStringList requestedBy;
    QString suggestion;     // what the user can do, may be empty
    QString parserTag;      // libsmi diagnostic tag when it came from the parser
};

struct MibProblemSummary {
    int errors = 0;
    int warnings = 0;
    int notes = 0;
    bool hasProblems() const { return errors > 0 || warnings > 0; }
};

inline MibProblemSummary MibSummarizeProblems(const QList<MibProblem> &problems)
{
    MibProblemSummary summary;
    for (const MibProblem &problem : problems) {
        switch (problem.severity) {
        case MibProblemSeverity::Error: ++summary.errors; break;
        case MibProblemSeverity::Warning: ++summary.warnings; break;
        case MibProblemSeverity::Note: ++summary.notes; break;
        }
    }
    return summary;
}

inline QString MibProblemSeverityText(MibProblemSeverity severity)
{
    switch (severity) {
    case MibProblemSeverity::Error: return QStringLiteral("Error");
    case MibProblemSeverity::Warning: return QStringLiteral("Warning");
    case MibProblemSeverity::Note: return QStringLiteral("Note");
    }
    return {};
}

#endif
