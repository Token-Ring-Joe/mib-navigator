#include "mibdependencyindex.h"

#include "smi.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QSet>
#include <iostream>

namespace { bool severeError = false; void errorHandler(char *, int, int severity, char *, char *) { if (severity <= 1) severeError = true; } }

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const bool startupMode = app.arguments().value(1) == QStringLiteral("--startup");
    const bool runtimeMode = app.arguments().value(1) == QStringLiteral("--runtime");
    const bool fastCheckMode = app.arguments().value(1) == QStringLiteral("--fast-check");
    const int indexArgument = (startupMode || runtimeMode || fastCheckMode) ? 2 : 1;
    if (app.arguments().size() < indexArgument + 2) { std::cerr << "usage: acceptance [--startup] INDEX PATH [PATH...]\n"; return 2; }
    const QString indexPath = app.arguments()[indexArgument]; const QStringList paths = app.arguments().mid(indexArgument + 1);
    QElapsedTimer indexTimer; indexTimer.start(); MibDependencyIndex index(indexPath); index.load();
    const qint64 indexLoadMs = indexTimer.elapsed(); QString error; MibDependencyScanResult scan;
    if (!startupMode) scan = index.update(paths, &error);
    if (!error.isEmpty()) { std::cerr << error.toStdString() << '\n'; return 2; }
    if (!startupMode) {
        MibDependencyIndex persisted(indexPath);
        if (!persisted.load(&error)) { std::cerr << error.toStdString() << '\n'; return 2; }
        index = persisted;
    }
    if (fastCheckMode) {
        QElapsedTimer timer; timer.start();
        const QStringList roots = index.moduleNames();
        const QString signature = MibDependencyIndex::profileSignature(roots, false);
        const bool reused = !scan.changed && index.profileCheckCurrent("mib-library", signature);
        if (!reused) {
            MibProfileDependencyCheck check; check.profileSignature = signature;
            check.indexGeneration = index.generation(); check.effectiveModules = roots;
            check.checkedUtc = QDateTime::currentDateTimeUtc();
            for (const QString &module : roots) index.recordVerification(module, true);
            index.setProfileCheck("mib-library", check); index.save(&error);
        }
        std::cout << "files_scanned=" << scan.scanned << " reused_files=" << scan.reused
                  << " modules=" << roots.size() << " semantic_cache=" << (reused ? "reused" : "updated")
                  << " elapsed_ms=" << (indexLoadMs + scan.elapsedMsecs + timer.elapsed()) << '\n';
        return error.isEmpty() ? 0 : 1;
    }
    smiInit("dependency-acceptance"); smiSetPath(paths.join(QDir::listSeparator()).toLocal8Bit().constData());
    smiSetFlags((smiGetFlags() | SMI_FLAG_ERRORS) & ~SMI_FLAG_NODESCR); smiSetErrorHandler(errorHandler); smiSetErrorLevel(3);
    QSet<QString> loading; int semanticAttempts = 0;
    std::function<MibDependencyLoadAttempt(const QString &, const QString &)> load;
    load = [&](const QString &path, const QString &expected) {
        ++semanticAttempts;
        MibDependencyLoadAttempt attempt;
        if (smiIsLoaded(expected.toLocal8Bit().constData())) { attempt.success = true; attempt.loadedModuleNames = {expected}; return attempt; }
        if (loading.contains(expected)) { attempt.diagnostic = "cycle"; return attempt; } loading.insert(expected);
        for (const QString &dependency : index.imports(expected)) { const auto provider = index.provider(dependency); if (provider.status == MibProviderStatus::Found) load(provider.path, dependency); }
        severeError = false; smiLoadModule(QDir::toNativeSeparators(path).toLocal8Bit().constData());
        for (SmiModule *module = smiGetFirstModule(); module; module = smiGetNextModule(module))
            if (module->name && module->path && QFileInfo(QString::fromLocal8Bit(module->path)).canonicalFilePath().compare(
                    QFileInfo(path).canonicalFilePath(), Qt::CaseInsensitive) == 0)
                attempt.loadedModuleNames.append(QString::fromLocal8Bit(module->name));
        attempt.success = !severeError && attempt.loadedModuleNames.contains(expected);
        if (!attempt.success) attempt.diagnostic = severeError ? "libsmi severe diagnostic" : "identity not produced";
        loading.remove(expected); return attempt;
    };
    if (runtimeMode) {
        QString bayStackConsumer;
        for (const QString &module : index.moduleNames())
            if (module.startsWith("BAY-STACK-") && index.imports(module).contains("SYNOPTICS-ROOT-MIB")) {
                bayStackConsumer = module; break;
            }
        const auto explicitRequests = MibNormalizeRuntimeRequests(
            {"synro.mib", bayStackConsumer, "nnsrnode.mib", "SYNOPTICS-ROOT-MIB"}, index);
        const auto initialRuntime = MibBoundedDependencyLoader().load(explicitRequests.identities, index, load);
        QStringList remaining = explicitRequests.identities;
        remaining.removeAll("SYNOPTICS-ROOT-MIB");
        smiExit(); smiInit("dependency-runtime-rebuild");
        smiSetPath(paths.join(QDir::listSeparator()).toLocal8Bit().constData());
        smiSetFlags((smiGetFlags() | SMI_FLAG_ERRORS) & ~SMI_FLAG_NODESCR);
        smiSetErrorHandler(errorHandler); smiSetErrorLevel(3); loading.clear();
        const auto rebuiltRuntime = MibBoundedDependencyLoader().load(remaining, index, load);
        const bool expected = explicitRequests.identities.size() == 3 &&
            initialRuntime.failures.isEmpty() && rebuiltRuntime.failures.isEmpty() &&
            initialRuntime.loaded.contains("SYNOPTICS-ROOT-MIB") &&
            initialRuntime.loaded.contains("NORTEL-nnSRNode-MIB") &&
            initialRuntime.loaded.contains("NORTEL-MIB-ARCS-MIB") &&
            !bayStackConsumer.isEmpty() && rebuiltRuntime.loaded.contains(bayStackConsumer) &&
            rebuiltRuntime.loaded.contains("SYNOPTICS-ROOT-MIB") &&
            !remaining.contains("SYNOPTICS-ROOT-MIB") &&
            explicitRequests.identities.size() < index.moduleNames().size();
        std::cout << "runtime_inputs=" << explicitRequests.inputCount
                  << " explicit_requested=" << explicitRequests.identities.size()
                  << " duplicate_inputs=" << explicitRequests.duplicateCount
                  << " initial_loaded=" << initialRuntime.loaded.size()
                  << " remaining_explicit=" << remaining.size()
                  << " rebuilt_loaded=" << rebuiltRuntime.loaded.size()
                  << " known_identities=" << index.moduleNames().size()
                  << " compile_all_attempts=0\n";
        std::cout << "baystack_consumer=" << bayStackConsumer.toStdString()
                  << " synro_provider=" << index.provider("SYNOPTICS-ROOT-MIB").path.toStdString()
                  << " nnsrnode_provider=" << index.provider("NORTEL-nnSRNode-MIB").path.toStdString()
                  << " nortel_arcs_provider=" << index.provider("NORTEL-MIB-ARCS-MIB").path.toStdString()
                  << " shared_synoptics_retained=" << (rebuiltRuntime.loaded.contains("SYNOPTICS-ROOT-MIB") ? 1 : 0)
                  << " failures=" << (initialRuntime.failures.size() + rebuiltRuntime.failures.size()) << '\n';
        smiExit(); return expected ? 0 : 1;
    }
    if (startupMode) {
        QElapsedTimer timer; timer.start(); const auto inspection = index.inspect(paths);
        const qint64 inspectMs = timer.elapsed();
        const auto result = MibBoundedDependencyLoader().load(
            {"SYNOPTICS-ROOT-MIB", "BAY-STACK-ARP-INSPECTION-MIB",
             "NORTEL-nnSRNode-MIB", "NORTEL-MIB-ARCS-MIB"}, index, load);
        std::cout << "index_load_ms=" << indexLoadMs << " startup_candidates=" << inspection.candidates.size()
                  << " unchanged=" << inspection.unchanged << " stale=" << (inspection.stale() ? 1 : 0)
                  << " enumeration_ms=" << inspectMs << " semantic_attempts=" << semanticAttempts
                  << " compile_all_attempts=0 requested_load_ms=" << result.elapsedMsecs << '\n';
        std::cout << "SYNOPTICS-ROOT-MIB=" << index.provider("SYNOPTICS-ROOT-MIB").path.toStdString()
                  << " baystack_loaded=" << (result.loaded.contains("BAY-STACK-ARP-INSPECTION-MIB") ? 1 : 0)
                  << " nnsrnode_loaded=" << (result.loaded.contains("NORTEL-nnSRNode-MIB") ? 1 : 0)
                  << " nortel_arcs_loaded=" << (result.loaded.contains("NORTEL-MIB-ARCS-MIB") ? 1 : 0)
                  << " final_failures=" << result.failures.size() << '\n';
        smiExit(); return result.failures.isEmpty() ? 0 : 1;
    }
    const auto result = MibBoundedDependencyLoader().load(index.moduleNames(), index, load);
    const auto synro = index.provider("SYNOPTICS-ROOT-MIB");
    const auto nnSrNode = index.provider("NORTEL-nnSRNode-MIB");
    const auto nortelArcs = index.provider("NORTEL-MIB-ARCS-MIB");
    QStringList consumers; for (const QString &module : index.moduleNames()) if (index.imports(module).contains("SYNOPTICS-ROOT-MIB")) consumers.append(module);
    QStringList loadedConsumers; for (const QString &module : consumers) if (result.loaded.contains(module)) loadedConsumers.append(module);
    QElapsedTimer cachedTimer; cachedTimer.start();
    for (int i = 0; i < 1000; ++i) for (const QString &module : consumers) { index.provider(module); index.imports(module); }
    const qint64 cachedLookupUsecs = cachedTimer.nsecsElapsed() / 1000 / 1000;
    int missing = 0, ambiguous = 0, parser = 0, unresolved = 0;
    for (const auto &failure : result.failures) switch (failure.kind) {
    case MibDependencyFailureKind::MissingProvider: ++missing; break;
    case MibDependencyFailureKind::AmbiguousProvider: ++ambiguous; break;
    case MibDependencyFailureKind::ParserSemanticFailure: ++parser; break;
    case MibDependencyFailureKind::DependencyUnresolved: ++unresolved; break;
    }
    std::cout << "files=" << index.files().size() << " identities=" << index.moduleNames().size()
              << " scanned=" << scan.scanned << " reused=" << scan.reused << " scan_ms=" << scan.elapsedMsecs << '\n';
    std::cout << "loaded=" << result.loaded.size() << " missing=" << missing << " ambiguous=" << ambiguous
              << " parser=" << parser << " dependency_unresolved=" << unresolved << " passes=" << result.passes
              << " load_ms=" << result.elapsedMsecs << '\n';
    std::cout << "SYNOPTICS-ROOT-MIB=" << synro.path.toStdString() << '\n';
    std::cout << "NORTEL-nnSRNode-MIB=" << nnSrNode.path.toStdString() << '\n';
    std::cout << "NORTEL-MIB-ARCS-MIB=" << nortelArcs.path.toStdString() << '\n';
    std::cout << "consumers=" << consumers.join(',').toStdString() << '\n';
    std::cout << "loaded_consumers=" << loadedConsumers.join(',').toStdString()
              << " cached_graph_lookup_us=" << cachedLookupUsecs << '\n';
    for (const auto &record : index.files()) for (auto it = record.importsByModule.begin(); it != record.importsByModule.end(); ++it)
        if (QFileInfo(record.filename).completeBaseName().compare(it.key(), Qt::CaseInsensitive) != 0)
            std::cout << "mismatch " << it.key().toStdString() << " -> " << record.filename.toStdString() << '\n';
    const bool expected = synro.status == MibProviderStatus::Found && synro.path.endsWith("synro.mib", Qt::CaseInsensitive)
        && nnSrNode.status == MibProviderStatus::Found && nnSrNode.path.endsWith("nnsrnode.mib", Qt::CaseInsensitive)
        && nortelArcs.status == MibProviderStatus::Found && nortelArcs.path.endsWith("nortel.mib", Qt::CaseInsensitive)
        && !consumers.isEmpty(); smiExit(); return expected ? 0 : 1;
}
