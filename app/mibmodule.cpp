/*
    Copyright (C) 2004-2011 Martin Jolicoeur (snmpb1@gmail.com) 

    This file is part of the SnmpB project 
    (http://sourceforge.net/projects/snmpb)

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#include <memory>
#include <stdio.h>
#include <string.h>
#include <qfileinfo.h>
#include <qdir.h>
#include <qmessagebox.h> 
#include <qtextstream.h>
#include <QStringBuilder>
#include <QElapsedTimer>
#include <QCryptographicHash>
#include <QMutex>
#include <QMutexLocker>
#include <algorithm>

#include "mibmodule.h"
#include "mibenvironmentextractor.h"
#include "mibenvironmentregistry.h"
#include "mibproblemreport.h"
#include "mibcollection.h"
#include "miblibrary.h"
#include "diagnosticlogger.h"
#include "agent.h"
#include "preferences.h"
#include "mibservice.h"
#include "mibservice_internal.h"
#include "mibengine.h"
#include "mibprofile.h"
#include "mibcandidatefilter.h"
#include "mibruntimeparser.h"
#include "mibruntimestage.h"

namespace {
QString currentLibraryRoot()
{
    QSettings settings;
    return MibCollection::configuredRoot(settings);
}

QString canonicalRuntimePath(const QString &path)
{
    const QString canonical = QFileInfo(path).canonicalFilePath();
    return QDir::cleanPath(canonical.isEmpty() ? QFileInfo(path).absoluteFilePath() : canonical);
}

bool sameRuntimePath(const QString &a, const QString &b)
{
#ifdef Q_OS_WIN
    return canonicalRuntimePath(a).compare(canonicalRuntimePath(b), Qt::CaseInsensitive) == 0;
#else
    return canonicalRuntimePath(a) == canonicalRuntimePath(b);
#endif
}

}

LoadedMibModule::LoadedMibModule(MibModuleRecord moduleRecord)
    : record(std::move(moduleRecord))
{
    name = record.name;
    path = record.path;
}

void LoadedMibModule::PrintProperties(QString& text)
{
    // Create a table and add elements ...
    text = MibModule::tr("<table border=\"1\" cellpadding=\"0\" cellspacing=\"0\" align=\"left\">");
    
    // Add the name
    text += MibModule::tr("<tr><td><b>Name:</b></td><td><font color=#009000><b>%1</b></font></td>")
            .arg(record.name);
    
    // Add last revision
    if(record.lastRevision.isValid())
        text += MibModule::tr("<tr><td><b>Last revision:</b></td><td>%1</td></tr>")
                .arg(record.lastRevision.toString(Qt::ISODate));
    
    // Add the description
    text += MibModule::tr("<tr><td><b>Description:</b></td><td><font face=fixed color=blue>");
    text += Qt::convertFromPlainText (record.description);
    text += MibModule::tr("</font></td></tr>");
    
    // Add root node name
    if (!record.rootName.isEmpty())
        text += MibModule::tr("<tr><td><b>Root node:</b></td><td>%1</td>")
                .arg(record.rootName);
    
    // Add required modules
    text += MibModule::tr("<tr><td><b>Requires:</b></td><td><font color=red>");
    text += record.imports.join(QStringLiteral("<br>"));
    text += MibModule::tr("</font></td></tr>");
    
    // Add organization
    text += MibModule::tr("<tr><td><b>Organization:</b></td><td>");
    text += Qt::convertFromPlainText (record.organization);
    text += MibModule::tr("</td></tr>");
    
    // Add contact info
    text += MibModule::tr("<tr><td><b>Contact Info:</b></td><td><font face=fixed>");
    text += Qt::convertFromPlainText (record.contactInfo);
    text += MibModule::tr("</font></td></tr>");
             
    text += QString("</table>");
}

QString LoadedMibModule::GetMibLanguage() const
{
    return record.language;
}

static MibModule *CurrentModuleObject = NULL;

MibModule::MibModule(Snmpb *snmpb)
    : s(snmpb), dependencyIndex(MibDependencyIndex::forLibraryRoot(currentLibraryRoot()))
{
    QElapsedTimer phase; phase.start();
    DiagnosticLogger::log("MIB", tr("dependency index path=\"%1\"").arg(
        QDir::toNativeSeparators(dependencyIndex.path())));
    QString dependencyIndexError;
    const bool dependencyIndexLoaded = dependencyIndex.load(&dependencyIndexError);
    DiagnosticLogger::log("MIB", tr("dependency index load status=%1 records=%2 generation=%3%4")
        .arg(MibDependencyIndexLoadStatusText(dependencyIndex.loadStatus()))
        .arg(dependencyIndex.files().size()).arg(dependencyIndex.generation())
        .arg(dependencyIndex.loadDiagnostic().isEmpty() ? QString()
            : tr(" reason=\"%1\"").arg(dependencyIndex.loadDiagnostic())));
    DiagnosticLogger::log("Startup", tr("dependency index load complete elapsed_ms=%1 records=%2")
        .arg(phase.elapsed()).arg(dependencyIndex.files().size()));
    Q_UNUSED(dependencyIndexLoaded);
    // Must be connected before call to InitLib ...
    connect(this, SIGNAL ( LogError(QString) ),
            s->MainUI()->LogOutput, SLOT ( append (QString) ));

    CurrentModuleObject = this;
    phase.restart();
    InitLib(0);
    DiagnosticLogger::log("Startup", tr("libsmi initialization complete elapsed_ms=%1").arg(phase.elapsed()));
    phase.restart(); ReadMibPaths(); RebuildCandidateList();
    DiagnosticLogger::log("Startup", tr("MIB candidate enumeration complete elapsed_ms=%1 candidates=%2 stale=%3")
        .arg(phase.elapsed()).arg(Total.size()).arg(dependencyIndexStale ? QStringLiteral("yes") : QStringLiteral("no")));
    phase.restart(); RegenerateSmiConf();
    RebuildLoadedList(); RebuildUnloadedList();
    DiagnosticLogger::log("Startup", tr("requested MIB load complete elapsed_ms=%1 requested=%2 loaded=%3")
        .arg(phase.elapsed()).arg(0).arg(Loaded.size()));

    connect( this, SIGNAL( StopAgentTimer() ), 
             s->AgentObj(), SLOT( StopTimer() ));
    environmentManager=std::make_unique<MibEnvironmentManager>(
        [this](const MibEffectivePlan &plan){return BuildEnvironment(plan);},this,
        MibEnvironmentCache::DefaultByteBudget,QStringLiteral("patched-libsmi-%1/engine-policy-1")
            .arg(MibEngine::instance().libraryVersion()));
    connect(environmentManager.get(),&MibEnvironmentManager::buildStarted,this,
        [this](quint64,const QString &name){emit profileRuntimeBuildStarted(name);});
    connect(environmentManager.get(),&MibEnvironmentManager::buildCompleted,this,
        [this](quint64,const QString &profileId,MibEnvironmentPtr environment,
               QStringList loadedModules,bool cacheHit,bool partial){
            const auto plan=requestedPlans.value(profileId);
            currentEnvironment=environment;activeProfilePlan=plan;hasActiveProfilePlan=true;
            s->MibLoaderObj()->SetEnvironment(environment,loadedModules);
            emit profileRuntimeReady(profileId,plan,environment,loadedModules,cacheHit,partial);
        });
    connect(environmentManager.get(),&MibEnvironmentManager::buildFailed,this,
        [this](quint64,const QString &profileId,const QString &error){emit profileRuntimeFailed(profileId,error);});
}

void MibModule::ShowModuleInfo()
{
    // Legacy Available/Loaded Modules presentation was removed. Module metadata
    // is presented by the Environment-backed inspector and MIB Catalog.
}

// For sorting total module list based on name
bool compareModule(QStringList s1, QStringList s2)
{
    return s1[0] < s2[0];
}

static void NormalErrorHdlr(char *path, int line, int severity, 
                            char *msg, char *tag)
{
    (void)line; (void)tag;

    if (severity <= 1)
        CurrentModuleObject->SendLogError(MibModule::tr("ERROR(%1) loading %2: %3")
                                          .arg(severity).arg(path).arg(msg));
}

namespace {
// While an Environment is being built, keep what libsmi says about each file
// (message, line, severity) instead of discarding it, so the user can be told what
// went wrong and where. Builds are serialized by MibEngine, so one collector is enough.
QMutex parserDiagnosticMutex;
QList<MibDiagnosticRecord> parserDiagnosticRecords;
bool parserDiagnosticsActive = false;
}

static void CollectingErrorHdlr(char *path, int line, int severity, char *msg, char *tag)
{
    NormalErrorHdlr(path, line, severity, msg, tag);
    QMutexLocker lock(&parserDiagnosticMutex);
    if (!parserDiagnosticsActive) return;
    MibDiagnosticRecord record;
    record.severity = severity;
    record.line = line;
    record.sourcePath = path ? QString::fromLocal8Bit(path) : QString();
    record.message = msg ? QString::fromLocal8Bit(msg) : QString();
    record.tag = tag ? QString::fromLocal8Bit(tag) : QString();
    parserDiagnosticRecords.append(record);
}

namespace {
class ParserDiagnosticCapture
{
public:
    ParserDiagnosticCapture() : savedFlags(smiGetFlags())
    {
        {
            QMutexLocker lock(&parserDiagnosticMutex);
            parserDiagnosticRecords.clear();
            parserDiagnosticsActive = true;
        }
        smiSetFlags(savedFlags | SMI_FLAG_ERRORS);
        smiSetErrorHandler(CollectingErrorHdlr);
        smiSetErrorLevel(5);   // errors and warnings; parsing itself is unaffected
    }
    ~ParserDiagnosticCapture() { finish({}); }
    // Stops capturing and returns the records with staged paths mapped back to the
    // user's original files.
    QList<MibDiagnosticRecord> finish(const QMap<QString, QString> &stagedToOriginal)
    {
        if (finished) return {};
        finished = true;
        smiSetFlags(savedFlags);
        smiSetErrorHandler(NormalErrorHdlr);
        smiSetErrorLevel(0);
        QMutexLocker lock(&parserDiagnosticMutex);
        parserDiagnosticsActive = false;
        QList<MibDiagnosticRecord> records = std::move(parserDiagnosticRecords);
        parserDiagnosticRecords.clear();
        for (MibDiagnosticRecord &record : records) {
            const QString key =
#ifdef Q_OS_WIN
                QDir::fromNativeSeparators(record.sourcePath).toLower();
#else
                QDir::fromNativeSeparators(record.sourcePath);
#endif
            const auto original = stagedToOriginal.constFind(key);
            if (original != stagedToOriginal.cend()) record.sourcePath = original.value();
        }
        return records;
    }
private:
    int savedFlags;
    bool finished = false;
};
}

static bool MibFilenameFilter(const QString& filename)
{
    // This is all futile. FIXME
    // To machine code, file extension says literally nothing about its content.
    // To a human, it *might* vaguely identify the content-type, possibly fooling the human.
    // But code like this function should not exist.
    // Instead, try to load every readable file as MIB, and back off gracefully.

    return MibCandidateFilter::accepts(filename);
}

void MibModule::RebuildTotalList()
{
    RebuildCandidateList();
}

MibModule::~MibModule()
{
    environmentManager.reset();
    CurrentModuleObject=nullptr;
    MibEngine::instance().shutdown();
}

#if 0 // Historical compile-all implementation retained only as migration reference.
void MibModule::RebuildTotalListLegacyCompileAll()
{
    DiagnosticLogger::log("MIB", "MIB search-path resolution and MIB/PIB loading begin");
    /* Enable error reporting */
    // Inventory metadata is user-visible; retain DESCRIPTION, ORGANIZATION,
    // CONTACT-INFO, and REFERENCE text while scanning configured modules.
    smiSetFlags((smiGetFlags() | SMI_FLAG_ERRORS) & ~SMI_FLAG_NODESCR);
    smiSetErrorHandler(NormalErrorHdlr);
    smiSetErrorLevel(3);

    std::unique_ptr<char, decltype(&std::free)>
        smipath{ smiGetPath(), &std::free };
    QStringList
        smipaths = QString(smipath.get()).split(SMI_PATH_SEPARATOR);
   
    Total.clear();
    KnownModuleNames.clear();
    AvailableRecords.clear();
    QStringList errored_files;
    for (int i = 0; i < smipaths.size(); ++i)
    {
        QDir d(smipaths[i], "", QDir::Unsorted, QDir::Files | QDir::Readable);
        QStringList list = d.entryList();
        for (QStringListIterator it(list); it.hasNext(); )
        {
            const QString& fn = it.next();
            if (MibFilenameFilter(fn))
            {
                // Load each module and build a list of possible root oids
                // This is used for module auto-loading on mib walk
                QStringList module;

                // If a module has a fatal error, ignore it
                ErrorWhileLoading = false;
                char *mod = smiLoadModule(fn.toLatin1());
                SmiModule *smiModule = mod?smiGetModule(mod):NULL;
                if (ErrorWhileLoading == true)
                {
                    errored_files << d.absoluteFilePath(fn);
                    continue;
                }

                module += QFileInfo(fn.toLatin1()).fileName();

                if (smiModule)
                {
                    const MibService service;
                    for (const MibModuleRecord &record :
                         service.modulesFromFile(d.absoluteFilePath(fn))) {
                        if (!KnownModuleNames.contains(record.name))
                            KnownModuleNames.append(record.name);
                        const auto duplicate = std::find_if(AvailableRecords.cbegin(),
                            AvailableRecords.cend(), [&record](const MibModuleRecord &item) {
                                return item.name == record.name && item.path == record.path;
                            });
                        if (duplicate == AvailableRecords.cend()) AvailableRecords.append(record);
                    }
                    SmiNode *node = smiGetModuleIdentityNode(smiModule);
                    if (node)
                        module += smiRenderOID(node->oidlen, 
                                               node->oid, SMI_RENDER_NUMERIC);

                    for(node = smiGetFirstNode(smiModule, SMI_NODEKIND_NODE); 
                        node; node = smiGetNextNode(node, SMI_NODEKIND_NODE))
                    {
                        if (node->decl == SMI_DECL_VALUEASSIGNMENT)
                            module += smiRenderOID(node->oidlen, 
                                                   node->oid, SMI_RENDER_NUMERIC);
                    }
                }
                Total.append(module);
            }
        }
    }

    /* warn if there're MIBs which failed to load */
    if (!errored_files.empty())
    {
        std::sort(errored_files.begin(), errored_files.end());

        QMessageBox::warning (s->MainUI()->MIBTree, tr("MIB loading errors"),
                              tr("%n MIB files failed to load. See Log for details.",
                                 nullptr, errored_files.size()),
                              QMessageBox::Ok, Qt::NoButton);
    }

    std::sort(Total.begin(), Total.end(), compareModule);
}
#endif

void MibModule::RebuildCandidateList()
{
    auto engineOperation=MibEngine::instance().beginOperation(QStringLiteral("candidate-list"));
    const QString libraryRoot = currentLibraryRoot();
    if (!sameRuntimePath(dependencyIndex.libraryRoot(), libraryRoot)) {
        dependencyIndex = MibDependencyIndex::forLibraryRoot(libraryRoot);
        QString loadError;
        dependencyIndex.load(&loadError);
        if (!loadError.isEmpty())
            DiagnosticLogger::log("MIB", tr("Library-owned dependency index load failed: %1")
                                  .arg(loadError));
    }
    const QStringList paths = MibCollection(libraryRoot).runtimeSearchPaths();
    const MibDependencyInspection inspection = dependencyIndex.inspect(paths);
    bool stale = inspection.stale();
    if (dependencyIndex.generation() == 0 || inspection.stale()) {
        QString updateError;
        const MibDependencyScanResult update = dependencyIndex.update(paths, &updateError);
        if (updateError.isEmpty()) stale = false;
        DiagnosticLogger::log("MIB", tr(
            "current Library dependency index synchronized generation=%1 scanned=%2 reused=%3 deleted=%4%5")
            .arg(dependencyIndex.generation()).arg(update.scanned).arg(update.reused)
            .arg(update.deleted).arg(updateError.isEmpty() ? QString() : tr(" error=\"%1\"").arg(updateError)));
    }
    dependencyIndexStale = stale; Total.clear(); KnownModuleNames.clear(); AvailableRecords.clear();
    QSet<QString> candidatePaths;
    for (const MibPhysicalCandidate &candidate : inspection.candidates) {
        Total.append(QStringList{candidate.filename});
        candidatePaths.insert(QFileInfo(candidate.canonicalPath).canonicalFilePath().toLower());
    }
    for (const MibDependencyFileRecord &file : dependencyIndex.files()) {
        const QString canonicalPath = QFileInfo(file.canonicalPath).canonicalFilePath().toLower();
        if (!candidatePaths.contains(canonicalPath) || file.checkState != QStringLiteral("verified")) continue;
        for (auto it = file.importsByModule.begin(); it != file.importsByModule.end(); ++it) {
            MibModuleRecord record; record.name = it.key(); record.path = file.canonicalPath;
            record.imports = it.value(); AvailableRecords.append(record);
            if (!KnownModuleNames.contains(record.name)) KnownModuleNames.append(record.name);
        }
    }
    std::sort(Total.begin(), Total.end(), compareModule);
}

MibProfileDependencyCheck MibModule::CheckProfileDependencies(
    const QString &profileId, const QStringList &explicitModules,
    bool includeStandardBase, QString *error)
{
    auto engineOperation=MibEngine::instance().beginOperation(QStringLiteral("dependency-check"));
    QElapsedTimer totalTimer;
    totalTimer.start();
    const MibDependencyScanResult scan = RefreshDependencyIndex(error);
    QStringList roots = explicitModules;
    if (includeStandardBase) roots.append(MibProfileDefinitions::standardsModules());
    roots.removeDuplicates(); roots.sort(Qt::CaseInsensitive);
    const QString signature = MibDependencyIndex::profileSignature(explicitModules, includeStandardBase);
    if (!scan.changed && dependencyIndex.profileCheckCurrent(profileId, signature)) {
        const MibProfileDependencyCheck cached = dependencyIndex.profileCheck(profileId);
        DiagnosticLogger::log("MIB", tr("Dependency check profile=%1 files-scanned=0 reused=%2 cache=reused inspection-ms=%3 graph-ms=0 semantic-ms=0 total-ms=%4")
            .arg(profileId).arg(scan.reused).arg(scan.elapsedMsecs).arg(totalTimer.elapsed()));
        emit inventoryChanged();
        return cached;
    }
    QElapsedTimer semanticTimer;
    semanticTimer.start();

    QSet<QString> loading;
    const bool libraryWide = profileId == QStringLiteral("mib-library");
    std::function<MibDependencyLoadAttempt(const QString &, const QString &)> loadIndexed;
    loadIndexed = [this, libraryWide, &loading, &loadIndexed](const QString &path, const QString &expected) {
        MibDependencyLoadAttempt attempt;
        if (libraryWide && dependencyIndex.semanticallyVerified(expected)) {
            attempt.success = true; attempt.loadedModuleNames = {expected}; return attempt;
        }
        if (smiIsLoaded(expected.toLocal8Bit().constData())) {
            attempt.success = true; attempt.loadedModuleNames = {expected}; return attempt;
        }
        if (loading.contains(expected)) { attempt.diagnostic = tr("Circular provider load"); return attempt; }
        loading.insert(expected);
        for (const QString &dependency : dependencyIndex.imports(expected)) {
            if (smiIsLoaded(dependency.toLocal8Bit().constData())) continue;
            const auto provider = dependencyIndex.provider(dependency);
            if (provider.status == MibProviderStatus::Found) loadIndexed(provider.path, dependency);
        }
        ErrorWhileLoading = false;
        char *canonicalName = smiLoadModule(QDir::toNativeSeparators(path).toLocal8Bit().constData());
        const QList<MibModuleRecord> fromFile = MibService().modulesFromFile(path);
        for (const MibModuleRecord &record : fromFile) attempt.loadedModuleNames.append(record.name);
        attempt.success = canonicalName && attempt.loadedModuleNames.contains(expected) && !ErrorWhileLoading;
        if (!attempt.success) attempt.diagnostic = ErrorWhileLoading
            ? tr("libsmi reported a parser/semantic error")
            : tr("Provider did not produce the requested module identity");
        loading.remove(expected); return attempt;
    };
    const MibDependencyCheckResult result = MibBoundedDependencyLoader().load(roots, dependencyIndex, loadIndexed);
    MibProfileDependencyCheck check;
    check.profileSignature = signature;
    check.indexGeneration = dependencyIndex.generation(); check.effectiveModules = result.loaded;
    check.dependencies = result.dependencies; check.checkedUtc = QDateTime::currentDateTimeUtc();
    check.elapsedMsecs = scan.elapsedMsecs + result.elapsedMsecs;
    for (const MibDependencyFailure &failure : result.failures) {
        check.unresolved.append(failure.moduleName);
        check.failureSummaries.append(tr("%1: %2%3").arg(failure.moduleName,
            MibDependencyFailureText(failure.kind), failure.detail.isEmpty() ? QString() : tr(" — %1").arg(failure.detail)));
    }
    for (const QString &module : result.loaded) dependencyIndex.recordVerification(module, true);
    for (const MibDependencyFailure &failure : result.failures)
        dependencyIndex.recordVerification(failure.moduleName, false, failure.detail);
    dependencyIndex.setProfileCheck(profileId, check);
    if (!dependencyIndex.save(error)) {
        DiagnosticLogger::log("MIB", tr("dependency index save failed path=\"%1\" reason=\"%2\"")
            .arg(QDir::toNativeSeparators(dependencyIndex.path()), error ? *error : QString()));
        return check;
    }
    DiagnosticLogger::log("MIB", tr("dependency index save records=%1 generation=%2 path=\"%3\"")
        .arg(dependencyIndex.files().size()).arg(dependencyIndex.generation())
        .arg(QDir::toNativeSeparators(dependencyIndex.path())));
    // Verification changes which indexed identities are projected into the
    // current-session Inventory/Profile models. Rebuild that projection now;
    // otherwise newly learned aliases only appear after process restart.
    RebuildCandidateList();
    DiagnosticLogger::log("MIB", tr("Dependency check profile=%1 files-scanned=%2 reused=%3 deleted=%4 modules=%5 unresolved=%6 elapsed-ms=%7")
        .arg(profileId).arg(scan.scanned).arg(scan.reused).arg(scan.deleted)
        .arg(check.effectiveModules.size()).arg(check.unresolved.size()).arg(check.elapsedMsecs));
    DiagnosticLogger::log("MIB", tr("Dependency check phases profile=%1 inspection-ms=%2 semantic-ms=%3 total-ms=%4 cache=updated")
        .arg(profileId).arg(scan.elapsedMsecs).arg(semanticTimer.elapsed()).arg(totalTimer.elapsed()));
    Q_UNUSED(libraryWide);
    emit inventoryChanged();
    return check;
}

MibDependencyScanResult MibModule::RefreshDependencyIndex(QString *error)
{
    auto engineOperation=MibEngine::instance().beginOperation(QStringLiteral("dependency-refresh"));
    const QString libraryRoot = currentLibraryRoot();
    if (!sameRuntimePath(dependencyIndex.libraryRoot(), libraryRoot)) {
        dependencyIndex = MibDependencyIndex::forLibraryRoot(libraryRoot);
        if (!dependencyIndex.load(error)) return {};
    }
    const QStringList paths = MibCollection(libraryRoot).runtimeSearchPaths();
    const MibDependencyScanResult result = dependencyIndex.update(paths, error);
    RebuildCandidateList();
    return result;
}

MibProfileDependencyCheck MibModule::CachedProfileDependencies(
    const QString &profileId, const QString &signature) const
{
    return !dependencyIndexStale && dependencyIndex.profileCheckCurrent(profileId, signature)
        ? dependencyIndex.profileCheck(profileId) : MibProfileDependencyCheck{};
}

QStringList MibModule::AvailableModuleNames() const
{
    QStringList result = KnownModuleNames;
    for (const LoadedMibModule &module : Loaded)
        if (!result.contains(module.name)) result.append(module.name);
    result.sort(Qt::CaseInsensitive);
    return result;
}

QStringList MibModule::LoadedModuleNames() const
{
    QStringList result;
    for (const LoadedMibModule &module : Loaded) result.append(module.name);
    return result;
}

MibModuleRecord MibModule::ModuleMetadata(const QString &moduleName, const QString &localPath)
{
    auto engineOperation=MibEngine::instance().beginOperation(QStringLiteral("module-metadata"));
    SmiModule *module = smiGetModule(moduleName.toLocal8Bit().constData());
    if (!module && !localPath.isEmpty()) {
        char *loaded = smiLoadModule(QFileInfo(localPath).fileName().toLocal8Bit().constData());
        module = loaded ? smiGetModule(loaded) : nullptr;
    }
    return SnapshotMibModule(module);
}

MibEffectivePlan MibModule::BuildEffectivePlan(const MibProfileRecord &profile) const
{
    return MibPlanForProfile(profile, dependencyIndex);
}

QStringList MibModule::LoadEffectivePlan(const MibEffectivePlan &plan,
                                         QList<MibExplicitRootLoadResult> *outcomes)
{
    auto engineOperation=MibEngine::instance().beginOperation(QStringLiteral("load-effective-plan"));
    const MibExplicitRootLoadBatch loaded = MibRuntimeParser::loadExplicitRoots(
        plan.runtimeConfiguration, plan.runtimePaths);
    if (outcomes) *outcomes = loaded.roots;
    QStringList unavailable = loaded.failedIdentities();
    unavailable.removeDuplicates();
    unavailable.sort(Qt::CaseSensitive);
    for (const auto &root : loaded.roots) {
        if (!root.success) {
            emit LogError(root.diagnostic);
            DiagnosticLogger::log("MIB", root.diagnostic);
        } else {
            DiagnosticLogger::log("MIB", tr(
                "Explicit Profile root materialized identity=%1 method=%2%3")
                .arg(root.identity).arg(static_cast<int>(root.status))
                .arg(root.physicalPath.isEmpty() ? QString() : tr(" path=%1").arg(root.physicalPath)));
        }
    }
    return unavailable;
}

bool lessThanLoadedMibModule(const LoadedMibModule &lm1,
                             const LoadedMibModule &lm2)
{
    return lm1.name < lm2.name;
}

void MibModule::RebuildLoadedList()
{
    auto engineOperation=MibEngine::instance().beginOperation(QStringLiteral("loaded-list"));
    Loaded.clear();

    for (SmiModule *mod = smiGetFirstModule();
         mod;
         mod = smiGetNextModule(mod) )
    {
        LoadedMibModule lmodule(SnapshotMibModule(mod));
        Loaded.append(lmodule);

    }
    
    std::sort(Loaded.begin(), Loaded.end(), lessThanLoadedMibModule);
}

void MibModule::RebuildUnloadedList()
{
    Unloaded.clear();
    
    for(int i = 0; i < Total.count(); ++i)
    {
        QString current = Total[i][0];

        bool found = false;
        const QStringList declarations = MibDeclaredIdentitiesForCandidate(current, dependencyIndex);
        for(int j = 0; j < Loaded.count(); j++)
        { 
            if (QFileInfo(Loaded[j].path).fileName().compare(current, Qt::CaseInsensitive) == 0 ||
                declarations.contains(Loaded[j].name))
            {
                found = true;
                break;
            }
        }
        if (!found) {
            Unloaded.append(current);
        }
    }
}

void MibModule::AddModule()
{
    DiagnosticLogger::log("MIB", QStringLiteral("Legacy module editor ignored; use a Profile"));
}

void MibModule::RemoveModule()
{
    DiagnosticLogger::log("MIB", QStringLiteral("Legacy module editor ignored; use a Profile"));
}

void MibModule::ReadMibPaths()
{
    auto engineOperation=MibEngine::instance().beginOperation(QStringLiteral("read-search-paths"));
    // read in MIB path from config
    QStringList paths;
    QSettings settings;
    int size = settings.beginReadArray("mibpaths");
    for (int i = 0; i < size; ++i) {
        settings.setArrayIndex(i);
        paths << settings.value("dir").toString();
    }

    QSettings rootSettings;
    const MibCollection collection(MibCollection::configuredRoot(rootSettings));
    for (const QString &path : collection.runtimeSearchPaths())
        if (!paths.contains(path)) paths.append(path);

    smiSetPath(paths.join(SMI_PATH_SEPARATOR).toLocal8Bit().data());
}

bool MibModule::ApplyProfileRuntime(const MibEffectivePlan &plan, QString *error)
{
    if(error)error->clear();
    if(!environmentManager){if(error)*error=tr("MIB Environment worker is unavailable");return false;}
    requestedPlans.insert(plan.profileId,plan);latestRequestedPlan=plan;
    environmentManager->request(plan);
    return true;
}

void MibModule::RestoreRuntimeAfterEditorValidation()
{
    const MibEffectivePlan restorePlan=hasActiveProfilePlan?activeProfilePlan:latestRequestedPlan;
    if(environmentManager&&!restorePlan.sha256.isEmpty()){requestedPlans.insert(restorePlan.profileId,restorePlan);
        environmentManager->request(restorePlan,true);return;}
    Refresh();
}

MibEnvironmentBuildResult MibModule::BuildEnvironment(const MibEffectivePlan &plan)
{
    auto operation=MibEngine::instance().beginOperation(QStringLiteral("async-plan-build"));
    MibEnvironmentBuildResult result;
    if (!plan.authorityError.isEmpty()) {
        result.error = plan.authorityError;
        return result;
    }
    if (!plan.hasRuntimePaths || !plan.runtimePaths.isValid()) {
        result.error = !plan.hasRuntimePaths
            ? tr("Profile runtime path configuration is missing")
            : plan.runtimePaths.diagnostics().join(QStringLiteral("; "));
        return result;
    }
    if (!dependencyIndex.ownsSnapshot(currentLibraryRoot(),
                                      plan.runtimeConfiguration.libraryGeneration())) {
        result.error = tr("MIB Library changed after the Profile runtime configuration was created");
        return result;
    }
    const MibRuntimeStageResult stage = MibRuntimeStage::prepare(plan);
    if (!stage.success) { result.error = stage.error; return result; }
    MibEffectivePlan isolatedPlan = plan;
    isolatedPlan.runtimeConfiguration = stage.configuration;
    isolatedPlan.runtimePaths = stage.paths;
    const auto reset = MibRuntimeParser::reset(isolatedPlan.runtimePaths, [] {
        smiSetErrorHandler(NormalErrorHdlr);
        smiSetErrorLevel(0);
    });
    if (!reset.success) { result.error = reset.error; return result; }
    DiagnosticLogger::log("MIB", tr("Profile runtime parser reset paths=%1 path-hash=%2")
        .arg(reset.appliedPaths.size()).arg(isolatedPlan.runtimePaths.sha256()));
    QList<MibExplicitRootLoadResult> rootOutcomes;
    ParserDiagnosticCapture capture;
    const QStringList unavailable=LoadEffectivePlan(isolatedPlan,&rootOutcomes);
    const QList<MibDiagnosticRecord> parserDiagnostics =
        capture.finish(isolatedPlan.runtimeConfiguration.stagedToOriginal());
    // Candidates for "did not load". Modules that duplicate-provider or stale-pin
    // handling resolved anyway are loaded, so they are filtered out below and only
    // reported as warnings.
    QStringList candidates;
    candidates.append(plan.effectiveModules);
    candidates.append(plan.missingModules);
    candidates.append(plan.pinFailureModules);
    candidates.append(unavailable);
    candidates.removeDuplicates();
    QStringList missing;
    for(const QString &identity:std::as_const(candidates))
        if(!smiGetModule(identity.toLocal8Bit().constData()))missing.append(identity);
    for(SmiModule *module=smiGetFirstModule();module;module=smiGetNextModule(module))
        if(module->name)result.loadedModules.append(QString::fromLocal8Bit(module->name));
    result.loadedModules.removeDuplicates();result.loadedModules.sort(Qt::CaseSensitive);
    MibProblemInput problemInput;
    problemInput.plan=isolatedPlan;
    problemInput.parserDiagnostics=parserDiagnostics;
    problemInput.failedIdentities=missing;
    problemInput.loadedIdentities=result.loadedModules;
    problemInput.rootOutcomes=rootOutcomes;
    result.environment=MibEnvironmentExtractor().extract(isolatedPlan,missing,rootOutcomes,{},
                                                         MibBuildProblemReport(problemInput));
    if (!dependencyIndex.ownsSnapshot(currentLibraryRoot(),
                                      plan.runtimeConfiguration.libraryGeneration())) {
        result.environment.reset();
        result.error = tr("MIB Library generation changed during Environment construction");
        return result;
    }
    if (!result.environment || !result.environment->publishable()) {
        QString firstProvider;
        QString firstActualProvider;
        for (const auto &member : plan.members)
            if (!member.provider.canonicalPath.isEmpty()) {
                firstProvider = member.provider.canonicalPath;
                if (SmiModule *loaded = smiGetModule(member.identity.toLocal8Bit().constData());
                    loaded && loaded->path)
                    firstActualProvider = QString::fromLocal8Bit(loaded->path);
                break;
            }
        result.error=tr("MIB Environment authorization failed; parser modules=%1; first provider=%2; first actual=%3; runtime paths=%4; diagnostics=%5")
            .arg(result.loadedModules.size()).arg(firstProvider, firstActualProvider,
                isolatedPlan.runtimePaths.orderedPaths().join(QDir::listSeparator()),
                result.environment ? result.environment->constructionDiagnostics().join(QStringLiteral("; "))
                                   : QString());
    }
    return result;
}

bool MibModule::ValidateModuleFile(const QString &path, QString *error,
                                   MibValidationLevel level)
{
    auto engineOperation=MibEngine::instance().beginOperation(QStringLiteral("module-validation"));
    DiagnosticLogger::log("MIB", QStringLiteral("Downloaded MIB validation begin file=%1")
                          .arg(path));
    const int savedFlags = smiGetFlags();
    int validationFlags = savedFlags | SMI_FLAG_ERRORS | SMI_FLAG_NODESCR;
    if (MibValidationRecursive(level)) validationFlags |= SMI_FLAG_RECURSIVE;
    else validationFlags &= ~SMI_FLAG_RECURSIVE;
    smiSetFlags(validationFlags);
    smiSetErrorHandler(NormalErrorHdlr);
    smiSetErrorLevel(MibValidationErrorLevel(level));
    char *module = smiLoadModule(QDir::toNativeSeparators(path).toLocal8Bit().constData());
    smiSetFlags(savedFlags);
    smiSetErrorHandler(NormalErrorHdlr);
    smiSetErrorLevel(3);
    if (!module) {
        if (error) *error = tr("libsmi validation failed; see MIB diagnostics and Log");
        DiagnosticLogger::log("MIB", QStringLiteral("Downloaded MIB validation failed file=%1")
                              .arg(path));
        return false;
    }
    DiagnosticLogger::log("MIB", QStringLiteral("Downloaded MIB validation succeeded module=%1")
                          .arg(QString::fromLocal8Bit(module)));
    return true;
}

void MibModule::Refresh()
{
    std::unique_ptr<char, decltype(&std::free)> old_smipath{nullptr,std::free};
    {auto operation=MibEngine::instance().beginOperation(QStringLiteral("refresh-old-path"));old_smipath.reset(smiGetPath());}
    ReadMibPaths();
    std::unique_ptr<char, decltype(&std::free)> new_smipath{nullptr,std::free};
    {auto operation=MibEngine::instance().beginOperation(QStringLiteral("refresh-new-path"));new_smipath.reset(smiGetPath());}

    if (QString(old_smipath.get()) != QString(new_smipath.get())) {
        RebuildCandidateList();
    }

    if (hasActiveProfilePlan) ApplyProfileRuntime(activeProfilePlan);
}

void MibModule::RescanPath()
{
    ReadMibPaths();
    RebuildCandidateList();
    RebuildLoadedList();
    RebuildUnloadedList();
    emit inventoryChanged();
}

void MibModule::InitLib(int restart)
{
    if (restart)
    {
        std::unique_ptr<char, decltype(&std::free)> smipath{nullptr,std::free};
        {auto operation=MibEngine::instance().beginOperation(QStringLiteral("parser-path-before-restart"));
         smipath.reset(smiGetPath());}
        MibEngine::instance().initialize(QString::fromLocal8Bit(smipath.get()),true);
        auto operation=MibEngine::instance().beginOperation(QStringLiteral("parser-lifecycle-configuration"));
        smiSetErrorHandler(NormalErrorHdlr);
        smiSetErrorLevel(0);
    }
    else
    {
        MibEngine::instance().initialize();
        auto operation=MibEngine::instance().beginOperation(QStringLiteral("parser-lifecycle-configuration"));
        smiSetFlags(smiGetFlags() | SMI_FLAG_ERRORS);
        smiSetErrorHandler(NormalErrorHdlr);
        smiSetErrorLevel(0);
        // Read in the libsmi rc script -- shouldn't be necessary anymore
        //smiReadConfig(s->GetSmiConfigFile().toLocal8Bit().data(), NULL);
    }
}

void MibModule::RegenerateSmiConf()
{
    auto engineOperation=MibEngine::instance().beginOperation(QStringLiteral("parser-configuration"));
    QFile smiconf(s->GetSmiConfigFile());
    if (!smiconf.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        QString err = tr("Unable to regenerate smi.conf!\nError opening file %1")
                .arg(smiconf.fileName());
        QMessageBox::critical(NULL, tr("MIB Navigator error"), err, QMessageBox::Ok);
        return;
    }

    // write out mibpaths
    QTextStream out(&smiconf);

    std::unique_ptr<char, decltype(&std::free)> smipath{ smiGetPath(), &std::free };
    QStringList mibpaths = QString(smipath.get()).split(SMI_PATH_SEPARATOR);
    out << "path " << mibpaths.join(SMI_PATH_SEPARATOR) << Qt::endl;

}
