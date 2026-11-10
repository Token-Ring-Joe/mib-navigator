#ifndef MIBPROBLEMREPORT_H
#define MIBPROBLEMREPORT_H

#include <QDir>
#include <QFileInfo>
#include <QMap>
#include <QPair>
#include <QSet>

#include "mibeffectiveplan.h"
#include "mibproblem.h"
#include "mibrecords.h"
#include "mibruntimeparser.h"

// Builds the user-facing list of problems for one Environment build. The build
// itself never stops at these; the report is how the user finds out what was
// skipped or degraded, and why, so they can fix it or carry on knowing what may
// not work.
struct MibProblemInput {
    MibEffectivePlan plan;
    // libsmi diagnostics captured during the build. sourcePath must already be the
    // original file path, not the staged copy.
    QList<MibDiagnosticRecord> parserDiagnostics;
    // Planned or requested identities that did not end up loaded.
    QStringList failedIdentities;
    QStringList loadedIdentities;
    QList<MibExplicitRootLoadResult> rootOutcomes;
};

namespace MibProblemReportDetail {

inline QString fileLabel(const QString &path)
{
    return path.isEmpty() ? QString() : QFileInfo(path).fileName();
}

inline QString providerPath(const MibEffectivePlan &plan, const QString &identity)
{
    for (const auto &file : plan.runtimeFiles)
        if (file.identities.contains(identity)) return file.canonicalPath;
    return {};
}

inline QString joinNames(const QStringList &names)
{
    return names.join(QStringLiteral(", "));
}

// libsmi severities: 0-1 are real errors (syntax errors, a module or import that
// cannot be found), 2-3 are standards/style problems that rarely stop a MIB from
// working (standard MIBs trigger several), 4-5 are warnings.
inline bool isParserError(int severity) { return severity <= 1; }
inline bool isParserMinor(int severity) { return severity == 2 || severity == 3; }
inline bool isParserWarning(int severity) { return severity == 4 || severity == 5; }

}

inline QList<MibProblem> MibBuildProblemReport(const MibProblemInput &input)
{
    using namespace MibProblemReportDetail;
    const MibEffectivePlan &plan = input.plan;
    QList<MibProblem> problems;
    QList<QPair<int, QString>> missingDependencyProblems;   // problem index, missing identity
    QSet<QString> reportedIdentities;          // identities already explained by a plan finding
    const QSet<QString> loaded(input.loadedIdentities.cbegin(), input.loadedIdentities.cend());

    // ---- findings from planning: what could not be found, was duplicated, or changed
    for (const MibPlanResolutionDiagnostic &finding : plan.resolutionDiagnostics) {
        MibProblem problem;
        problem.requestedBy = finding.requesters;
        switch (finding.tier) {
        case MibPlanResolutionTier::ModifiedRoot:
            problem.severity = MibProblemSeverity::Note;
            problem.category = QObject::tr("Modified");
            problem.module = finding.identity;
            problem.file = finding.scope;
            problem.why = finding.reason;
            problem.effect = QObject::tr("The current file on disk is loaded, not the version first added.");
            break;
        case MibPlanResolutionTier::InvalidRoot:
            problem.severity = MibProblemSeverity::Error;
            problem.category = QObject::tr("MIB not loaded");
            problem.module = finding.identity;
            problem.file = finding.scope;
            problem.why = finding.reason;
            problem.effect = QObject::tr("Nothing from this file is in the Tree.");
            problem.suggestion = QObject::tr("Restore or re-add the file, or remove it from the profile.");
            reportedIdentities.insert(finding.identity);
            break;
        case MibPlanResolutionTier::ListedNotFound:
            problem.severity = MibProblemSeverity::Warning;
            problem.category = QObject::tr("Listed MIB not in the Library");
            problem.module = finding.identity;
            problem.why = finding.reason.isEmpty()
                ? QObject::tr("The profile lists %1 but the MIB Library has no file that declares it.")
                      .arg(finding.identity)
                : finding.reason;
            problem.effect = QObject::tr("%1 is not loaded, so its objects will not appear in the Tree.")
                .arg(finding.identity);
            problem.suggestion = QObject::tr("Add a file that declares %1 to the Library, or remove it "
                                             "from the profile.").arg(finding.identity);
            reportedIdentities.insert(finding.identity);
            break;
        case MibPlanResolutionTier::FolderNotFound:
            problem.severity = MibProblemSeverity::Warning;
            problem.category = QObject::tr("Followed folder not found");
            problem.module = QFileInfo(finding.identity).fileName();
            problem.file = finding.identity;
            problem.why = QObject::tr("The profile follows the folder %1, which does not exist. It may "
                                      "have been renamed, moved or deleted.")
                              .arg(QDir::toNativeSeparators(finding.identity));
            problem.effect = QObject::tr("No MIBs from that folder are loaded.");
            problem.suggestion = QObject::tr("Restore the folder, or remove it from the profile on "
                                             "MIB Library > Profiles.");
            break;
        case MibPlanResolutionTier::Unresolved:
        case MibPlanResolutionTier::OutOfScope: {
            problem.severity = MibProblemSeverity::Warning;
            problem.category = QObject::tr("Missing dependency");
            const QString requester = finding.requesters.isEmpty() ? QObject::tr("A selected MIB")
                                                                   : joinNames(finding.requesters);
            problem.module = finding.requesters.isEmpty() ? finding.identity : requester;
            problem.file = finding.requesters.isEmpty() ? QString()
                                                        : providerPath(plan, finding.requesters.first());
            if (finding.tier == MibPlanResolutionTier::Unresolved) {
                problem.why = QObject::tr("%1 imports %2, which is not in your MIB Library.")
                    .arg(requester, finding.identity);
                problem.suggestion = QObject::tr("Add %1 to the Library (Catalog tab or import a file), "
                                                 "or ignore this if you do not need those definitions.")
                    .arg(finding.identity);
            } else {
                QStringList where;
                for (const auto &candidate : finding.candidates) where.append(candidate.canonicalPath);
                problem.why = QObject::tr("%1 imports %2, which exists only outside this profile's "
                                          "folders: %3").arg(requester, finding.identity, where.join(QStringLiteral("; ")));
                problem.suggestion = QObject::tr("Add that folder to the profile's scope, or add the file to "
                                                 "the profile.");
            }
            problem.effect = QObject::tr("Definitions that use %1 may be unresolved or shown as plain numbers.")
                .arg(finding.identity);
            reportedIdentities.insert(finding.identity);
            missingDependencyProblems.append({static_cast<int>(problems.size()), finding.identity});
            break;
        }
        case MibPlanResolutionTier::Ambiguous: {
            problem.severity = MibProblemSeverity::Warning;
            problem.category = QObject::tr("Conflicting definitions");
            problem.module = finding.identity;
            problem.file = providerPath(plan, finding.identity);
            problem.why = finding.reason;
            problem.effect = QObject::tr("The selected file supplies this module. If it is not the one you meant, "
                                         "definitions can differ.");
            problem.suggestion = QObject::tr("Remove or rename the copy you do not want, or pin the one you do.");
            break;
        }
        case MibPlanResolutionTier::NewestRevision:
            problem.severity = MibProblemSeverity::Note;
            problem.category = QObject::tr("Conflicting definitions (resolved)");
            problem.module = finding.identity;
            problem.file = providerPath(plan, finding.identity);
            problem.why = finding.reason;
            problem.effect = QObject::tr("Nothing to do: the newest revision is used. The older copies are ignored.");
            break;
        case MibPlanResolutionTier::ProviderConflict:
            problem.severity = MibProblemSeverity::Warning;
            problem.category = QObject::tr("Conflicting file");
            problem.module = finding.identity;
            problem.why = finding.reason;
            problem.effect = QObject::tr("That file was not loaded to avoid defining a module twice.");
            reportedIdentities.insert(finding.identity);
            break;
        case MibPlanResolutionTier::StalePin:
            problem.severity = MibProblemSeverity::Warning;
            problem.category = QObject::tr("Pinned file unavailable");
            problem.module = finding.identity;
            problem.file = providerPath(plan, finding.identity);
            problem.why = finding.reason;
            problem.effect = QObject::tr("A different file than the pinned one was used.");
            problem.suggestion = QObject::tr("Re-pin the file you want, or remove the pin.");
            break;
        default:
            continue;
        }
        problems.append(problem);
    }

    // ---- what the parser itself said, grouped by file, with line numbers
    QMap<QString, QList<MibDiagnosticRecord>> byFile;
    for (const MibDiagnosticRecord &record : input.parserDiagnostics)
        byFile[record.sourcePath].append(record);

    // The parser also says "failed to locate MIB module `X'" for a missing import. The plan
    // already reported that in plain language, so use the parser only for the line number.
    const auto mentionsReportedIdentity = [&reportedIdentities](const QString &message) {
        for (const QString &identity : reportedIdentities)
            if (message.contains(QStringLiteral("`%1'").arg(identity))) return identity;
        return QString();
    };
    for (const auto &entry : missingDependencyProblems) {
        MibProblem &problem = problems[entry.first];
        const auto records = byFile.constFind(problem.file);
        if (records == byFile.cend()) continue;
        for (const MibDiagnosticRecord &record : records.value())
            if (record.message.contains(QStringLiteral("`%1'").arg(entry.second))) {
                problem.line = record.line;
                break;
            }
    }

    for (auto it = byFile.cbegin(); it != byFile.cend(); ++it) {
        const QString path = it.key();
        QString identity;
        for (const auto &file : plan.runtimeFiles)
            if (file.canonicalPath == path && !file.identities.isEmpty()) { identity = file.identities.first(); break; }
        if (identity.isEmpty()) identity = fileLabel(path);
        int shownErrors = 0, hiddenErrors = 0, shownMinor = 0, hiddenMinor = 0;
        int shownWarnings = 0, hiddenWarnings = 0;
        for (const MibDiagnosticRecord &record : it.value()) {
            if (!mentionsReportedIdentity(record.message).isEmpty()) continue;   // already explained
            if (MibProblemReportDetail::isParserError(record.severity)) {
                if (shownErrors >= 15) { ++hiddenErrors; continue; }
                ++shownErrors;
                MibProblem problem;
                problem.severity = MibProblemSeverity::Warning;
                problem.category = QObject::tr("Parse error");
                problem.module = identity;
                problem.file = path;
                problem.line = record.line;
                problem.parserTag = record.tag;
                problem.why = record.message;
                problem.effect = QObject::tr("The parser reported an error here. Definitions at or after this "
                                             "point may be missing or wrong.");
                problem.suggestion = QObject::tr("Open the file at this line to correct it, or continue "
                                                 "knowing those definitions may not work.");
                problems.append(problem);
            } else if (MibProblemReportDetail::isParserMinor(record.severity)) {
                if (shownMinor >= 5) { ++hiddenMinor; continue; }
                ++shownMinor;
                MibProblem problem;
                problem.severity = MibProblemSeverity::Note;
                problem.category = QObject::tr("Standards issue");
                problem.module = identity;
                problem.file = path;
                problem.line = record.line;
                problem.parserTag = record.tag;
                problem.why = record.message;
                problem.effect = QObject::tr("The MIB does not strictly follow the standard here. It normally "
                                             "still works.");
                problems.append(problem);
            } else if (MibProblemReportDetail::isParserWarning(record.severity)) {
                if (shownWarnings >= 5) { ++hiddenWarnings; continue; }
                ++shownWarnings;
                MibProblem problem;
                problem.severity = MibProblemSeverity::Note;
                problem.category = QObject::tr("Parser warning");
                problem.module = identity;
                problem.file = path;
                problem.line = record.line;
                problem.parserTag = record.tag;
                problem.why = record.message;
                problem.effect = QObject::tr("A warning only. It does not stop the MIB from working.");
                problems.append(problem);
            }
        }
        if (hiddenErrors > 0) {
            MibProblem problem;
            problem.severity = MibProblemSeverity::Warning;
            problem.category = QObject::tr("Parse error");
            problem.module = identity;
            problem.file = path;
            problem.why = QObject::tr("%n more parser error(s) in this file were not listed.", nullptr, hiddenErrors);
            problem.effect = QObject::tr("Fix the errors above first; later ones are often caused by earlier ones.");
            problems.append(problem);
        }
        if (hiddenMinor > 0 || hiddenWarnings > 0) {
            MibProblem problem;
            problem.severity = MibProblemSeverity::Note;
            problem.category = QObject::tr("More parser messages");
            problem.module = identity;
            problem.file = path;
            QStringList parts;
            if (hiddenMinor > 0) parts.append(QObject::tr("%n more standards issue(s)", nullptr, hiddenMinor));
            if (hiddenWarnings > 0) parts.append(QObject::tr("%n warning(s)", nullptr, hiddenWarnings));
            problem.why = QObject::tr("%1 more for this file are not shown here.").arg(parts.join(QObject::tr(" and ")));
            problem.effect = QObject::tr("These do not stop a MIB from working. Open the file in the editor and "
                                         "run Verify to see every message.");
            problems.append(problem);
        }
    }

    // ---- modules that were planned but did not load for some other reason
    for (const QString &identity : input.failedIdentities) {
        if (reportedIdentities.contains(identity) || loaded.contains(identity)) continue;
        const QString path = providerPath(plan, identity);
        if (path.isEmpty()) continue;   // not a file we were asked to load
        MibProblem problem;
        problem.severity = MibProblemSeverity::Error;
        problem.category = QObject::tr("MIB failed to load");
        problem.module = identity;
        problem.file = path;
        for (const auto &outcome : input.rootOutcomes)
            if (outcome.identity == identity && !outcome.success && !outcome.diagnostic.isEmpty())
                problem.why = outcome.diagnostic;
        const auto firstError = byFile.constFind(path);
        if (firstError != byFile.cend())
            for (const auto &record : firstError.value())
                if (MibProblemReportDetail::isParserError(record.severity)) {
                    if (problem.why.isEmpty())
                        problem.why = QObject::tr("The parser could not load it: %1").arg(record.message);
                    problem.line = record.line;
                    break;
                }
        if (problem.why.isEmpty())
            problem.why = QObject::tr("The parser could not load this file.");
        problem.effect = QObject::tr("Nothing from %1 is in the Tree.").arg(identity);
        problem.suggestion = QObject::tr("Open the file in the editor and run Verify to see why.");
        problems.append(problem);
    }
    return problems;
}

#endif
