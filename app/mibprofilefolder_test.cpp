// Custom profiles that follow folders: membership worked out from the Library index (so the
// profile follows the folder), exclusions, missing folders, and the companion file that keeps
// followed folders out of the profile file an older build reads and rewrites.
#include "mibdependencyindex.h"
#include "mibeffectiveplan.h"
#include "mibproblemreport.h"
#include "mibprofile.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <iostream>

namespace {
bool check(bool value, const char *message)
{ if (!value) std::cerr << "FAIL: " << message << '\n'; return value; }

QString writeMib(const QDir &root, const QString &relative, const QString &module)
{
    const QString path = root.filePath(relative);
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return {};
    file.write(module.toUtf8() + " DEFINITIONS ::= BEGIN\nEND\n");
    file.close();
    return QFileInfo(path).canonicalFilePath();
}

MibDependencyFileRecord record(const QString &path, const QStringList &modules, const QString &sha = "aa")
{
    MibDependencyFileRecord r;
    r.canonicalPath = path; r.sha256 = sha; r.filename = QFileInfo(path).fileName();
    for (const QString &m : modules) r.importsByModule.insert(m, {});
    return r;
}

QByteArray readAll(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

bool writeAll(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

QStringList identities(const QList<MibProfileMember> &members)
{
    QStringList result;
    for (const MibProfileMember &member : members) result += member.identities;
    return result;
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    bool ok = true;
    QTemporaryDir temp; ok &= check(temp.isValid(), "temporary directory");
    const QDir root(QFileInfo(temp.path()).canonicalFilePath());

    // ---- which files a followed folder contributes ------------------------------------------------
    {
        QDir().mkpath(root.filePath("Acme/Lab"));   // a followed folder that exists
        const QString folder = root.filePath("Acme/Lab");
        const QList<MibDependencyFileRecord> files{
            record(folder + "/a.mib", {"ACME-A"}),
            record(folder + "/deep/er/b.mib", {"ACME-B", "ACME-B2"}),          // any depth, two modules
            record(folder + "/skip/c.mib", {"ACME-C"}),                         // in an excluded subfolder
            record(folder + "/d.mib", {"ACME-D"}),                              // excluded file
            record(folder + "/unreadable.mib", {"ACME-U"}, QString()),          // could not be read
            record(folder + "/notes.txt", {}),                                  // declares no module
            record(root.filePath("Acme/Lab2/e.mib"), {"ACME-E"}),               // sibling with a longer name
            record(root.filePath("Acme/other.mib"), {"ACME-OTHER"}),            // outside the folder
        };
        const MibProfileFolder followed{folder, {folder + "/d.mib", folder + "/skip"}};
        const QList<MibProfileMember> members = MibProfileFolderMembers(followed, files);
        ok &= check(identities(members) == QStringList({"ACME-A", "ACME-B", "ACME-B2"}),
                    "a followed folder contributes its MIB files at any depth, minus excluded files and "
                    "subfolders; unreadable and module-less files, siblings with a longer name and files "
                    "outside are left out");
        ok &= check(identities(MibProfileFolderMembers(followed, files, true)).contains("ACME-D"),
                    "excluded files can still be listed (to show them as excluded)");
        ok &= check(MibPathIsInsideFolder(folder, folder) && MibPathIsInsideFolder(folder + "/x", folder) &&
                    !MibPathIsInsideFolder(folder + "2/x", folder) && !MibPathIsInsideFolder(QString(), folder),
                    "inside-folder test is by whole path components");
#ifndef Q_OS_WIN
        ok &= check(!MibPathIsInsideFolder(folder.toUpper() + "/a.mib", folder),
                    "paths are case sensitive where the file system is");
#endif

        MibProfileRecord profile;
        profile.id = "custom.lab"; profile.name = "Lab"; profile.manifestVersion = 1;
        MibProfileMember exact; exact.canonicalPath = folder + "/a.mib"; exact.sha256 = "recorded";
        exact.identities = {"ACME-A"};
        profile.roots = {exact};
        profile.orderedScopes = {{"collection:x", root.filePath("Acme")}};
        profile.folders = {followed, {root.filePath("Gone/Folder"), {}}};
        QStringList missing;
        const MibProfileRecord expanded = MibProfileWithFolderFiles(profile, files, &missing);
        ok &= check(expanded.roots.size() == 2 && expanded.roots.at(0).sha256 == "recorded" &&
                    expanded.explicitModules == QStringList({"ACME-A", "ACME-B", "ACME-B2"}),
                    "folder files join the roots once; an exact root in the folder keeps its recorded hash");
        ok &= check(missing == QStringList{root.filePath("Gone/Folder")},
                    "a followed folder that no longer exists is reported, not silently dropped");
        ok &= check(MibProfileScopePaths(profile) ==
                    QStringList({folder, root.filePath("Gone/Folder"), root.filePath("Acme")}),
                    "imports are looked up in the followed folders first, then the other scopes");
    }

    // ---- the service: follow, exclude, unfollow, persist ------------------------------------------
    QDir().mkpath(root.filePath("Library/Extreme/Fabric Engine"));
    QDir().mkpath(root.filePath("Library/Aruba/AOS-CX"));
    const QString fabric = QFileInfo(root.filePath("Library/Extreme/Fabric Engine")).canonicalFilePath();
    const QString aruba = QFileInfo(root.filePath("Library/Aruba/AOS-CX")).canonicalFilePath();
    const QString profilesPath = root.filePath("state/profiles-v1.json");
    const QString foldersPath = root.filePath("state/profile-folders-v1.json");
    QString id;
    {
        MibProfileService service{MibProfileRepository(profilesPath)};
        QString error;
        id = service.create("Lab", &error);
        ok &= check(!id.isEmpty(), "a Custom profile is created");
        ok &= check(!QFile::exists(foldersPath), "no companion file is written while no profile follows a folder");
        ok &= check(service.followFolders(id, {fabric, aruba, fabric + "/"}, &error) &&
                    service.find(id)->folders.size() == 2,
                    "following folders adds each once (the same folder written differently is not added twice)");
        ok &= check(!service.followFolders(id, {root.filePath("Library/No Such Folder")}, &error) &&
                    !error.isEmpty() && service.find(id)->folders.size() == 2,
                    "following a folder that does not exist is refused with a reason and changes nothing");
        ok &= check(service.setFilesExcluded(id, {fabric + "/old.mib"}, true, &error) &&
                    service.find(id)->folders.at(0).excludedFiles == QStringList{fabric + "/old.mib"},
                    "a file inside a followed folder can be excluded");
        ok &= check(service.setFilesExcluded(id, {fabric + "/old.mib"}, true, &error) &&
                    service.find(id)->folders.at(0).excludedFiles.size() == 1,
                    "excluding the same file twice records it once");
        ok &= check(service.setFilesExcluded(id, {root.filePath("Elsewhere/x.mib")}, true, &error) &&
                    service.find(id)->folders.at(0).excludedFiles.size() == 1 &&
                    service.find(id)->folders.at(1).excludedFiles.isEmpty(),
                    "excluding a file that is in no followed folder changes nothing");
        const QString builtIn = MibProfileDefinitions::standardsId();
        ok &= check(!service.followFolders(builtIn, {fabric}, &error) &&
                    service.find(builtIn)->folders.isEmpty(),
                    "built-in profiles cannot follow folders");
        ok &= check(QFile::exists(foldersPath), "followed folders are saved in the companion file");
        const QJsonObject saved = QJsonDocument::fromJson(readAll(profilesPath)).object();
        ok &= check(saved.value("schemaVersion").toInt() == 5 && !readAll(profilesPath).contains("folders") &&
                    !readAll(profilesPath).contains("Fabric Engine"),
                    "the profile file keeps the format older builds read (no folder data in it)");
    }
    {
        MibProfileService reloaded{MibProfileRepository(profilesPath)};
        const MibProfileRecord *lab = reloaded.find(id);
        ok &= check(lab && lab->folders.size() == 2 && lab->folders.at(0).canonicalPath == fabric &&
                    lab->folders.at(0).excludedFiles == QStringList{fabric + "/old.mib"} &&
                    lab->folders.at(1).canonicalPath == aruba,
                    "followed folders and exclusions survive a restart, in order");
    }
    // An older build reads the profile file and rewrites it from what it understood (its own JSON,
    // without the companion file). The followed folders must still be there afterwards.
    {
        QJsonObject rewritten = QJsonDocument::fromJson(readAll(profilesPath)).object();
        QJsonArray items = rewritten.value("profiles").toArray();
        for (int i = 0; i < items.size(); ++i) {
            QJsonObject item = items.at(i).toObject();
            if (item.value("id").toString() != id) continue;
            item.insert("name", "Lab (renamed by older build)");
            items.replace(i, item);
        }
        rewritten.insert("profiles", items);
        ok &= check(writeAll(profilesPath, QJsonDocument(rewritten).toJson()), "older-build rewrite written");
        MibProfileService afterOldBuild{MibProfileRepository(profilesPath)};
        const MibProfileRecord *lab = afterOldBuild.find(id);
        ok &= check(lab && lab->name == "Lab (renamed by older build)" && lab->folders.size() == 2,
                    "an older build rewriting the profile file does not lose followed folders");
    }
    {
        MibProfileService service{MibProfileRepository(profilesPath)};
        QString error;
        ok &= check(service.setFilesExcluded(id, {fabric + "/old.mib"}, false, &error) &&
                    service.find(id)->folders.at(0).excludedFiles.isEmpty(),
                    "an excluded file can be taken back in");
        const QString copy = service.duplicate(id, "Lab copy", &error);
        ok &= check(!copy.isEmpty() && service.find(copy)->folders.size() == 2,
                    "duplicating a profile duplicates its followed folders");
        ok &= check(service.unfollowFolders(id, {aruba}, &error) && service.find(id)->folders.size() == 1 &&
                    service.find(id)->folders.at(0).canonicalPath == fabric,
                    "a followed folder can be removed");
        {
            MibProviderPin pin; pin.canonicalPath = fabric + "/old.mib"; pin.sha256 = "abc"; pin.reason = "chosen in test";
            ok &= check(service.pinProvider(id, "DUP-MIB", pin, &error) && service.find(id)->providerPins.contains("DUP-MIB"),
                        "a Custom profile can choose which copy of a module it uses");
            MibProfileService reloaded{MibProfileRepository(profilesPath)};
            ok &= check(reloaded.find(id) && reloaded.find(id)->providerPins.value("DUP-MIB").canonicalPath == pin.canonicalPath &&
                        reloaded.find(id)->folders.size() == 1, "the chosen copy is saved with the profile and survives a restart");
            ok &= check(!service.pinProvider("standards", "DUP-MIB", pin, &error) && !service.pinProvider(id, "", pin, &error),
                        "built-in profiles and incomplete choices are refused");
            ok &= check(service.unpinProvider(id, "DUP-MIB", &error) && service.find(id)->providerPins.isEmpty() &&
                        !service.unpinProvider(id, "DUP-MIB", &error), "a chosen copy can be released");
        }
        ok &= check(service.remove(copy, &error) &&
                    !readAll(foldersPath).contains(copy.toUtf8()),
                    "deleting a profile removes its followed folders from the companion file");
        // The profile file cannot be written: the companion file is put back as it was.
        const QByteArray foldersBefore = readAll(foldersPath);
        const QString blocked = root.filePath("blocked/profiles-v1.json");
        QDir().mkpath(blocked);   // a folder where the profile file should be
        writeAll(root.filePath("blocked/profile-folders-v1.json"), foldersBefore);
        MibProfileRepository blockedRepository(blocked);
        QList<MibProfileRecord> changed = service.profiles();
        for (MibProfileRecord &profile : changed) if (profile.id == id) profile.folders.append({aruba, {}});
        QString saveError;
        ok &= check(!blockedRepository.save(changed, &saveError) && !saveError.isEmpty() &&
                    readAll(root.filePath("blocked/profile-folders-v1.json")) == foldersBefore,
                    "when the profile file cannot be written, the companion file is restored");
    }
    // A damaged companion file: profiles still load, the problem is reported.
    {
        const QByteArray good = readAll(foldersPath);
        writeAll(foldersPath, "{not json");
        QString error;
        const QList<MibProfileRecord> loaded = MibProfileRepository(profilesPath).load(&error);
        bool found = false;
        for (const MibProfileRecord &profile : loaded) found = found || (profile.id == id && profile.folders.isEmpty());
        ok &= check(found && error.contains("profile-folders-v1.json"),
                    "an unreadable companion file leaves the profiles loadable and says so");
        writeAll(foldersPath, good);
    }

    // ---- end to end through the real index and planner --------------------------------------------
    {
        const QDir library(QFileInfo(root.filePath("Library")).canonicalFilePath());
        writeMib(library, "Extreme/Fabric Engine/rc.mib", "RAPID-CITY");
        writeMib(library, "Extreme/Fabric Engine/sub/vlan.mib", "RC-VLAN-MIB");
        writeMib(library, "Extreme/Fabric Engine/old.mib", "OLD-FABRIC-MIB");
        writeMib(library, "Aruba/AOS-CX/arubaos.mib", "ARUBAWIRED-MIB");
        writeMib(library, "Standards/SNMPv2-SMI", "SNMPv2-SMI");
        MibDependencyIndex index(root.filePath("state/index.json"), library.path());
        index.update({library.path()});
        MibProfileRecord lab;
        lab.id = "custom.e2e"; lab.name = "Lab"; lab.type = MibProfileType::Custom; lab.manifestVersion = 1;
        lab.folders = {{fabric, {}}};
        MibEffectivePlan plan = MibPlanForProfile(lab, index);
        ok &= check(plan.effectiveModules.contains("RAPID-CITY") && plan.effectiveModules.contains("RC-VLAN-MIB") &&
                    plan.effectiveModules.contains("OLD-FABRIC-MIB") && !plan.effectiveModules.contains("ARUBAWIRED-MIB"),
                    "planning a profile that follows a folder loads every MIB in it, and nothing else");
        const QString firstSha = plan.sha256;

        writeMib(library, "Extreme/Fabric Engine/new.mib", "NEW-FABRIC-MIB");
        index.update({library.path()});
        plan = MibPlanForProfile(lab, index);
        ok &= check(plan.effectiveModules.contains("NEW-FABRIC-MIB") && plan.sha256 != firstSha,
                    "a MIB dropped into the folder joins the profile at the next refresh (and the plan changes)");

        lab.folders.first().excludedFiles = {fabric + "/old.mib"};
        plan = MibPlanForProfile(lab, index);
        ok &= check(!plan.effectiveModules.contains("OLD-FABRIC-MIB") && plan.effectiveModules.contains("RAPID-CITY"),
                    "an excluded file is left out while the rest of the folder still loads");

        QFile::remove(library.filePath("Extreme/Fabric Engine/sub/vlan.mib"));
        index.update({library.path()});
        plan = MibPlanForProfile(lab, index);
        ok &= check(!plan.effectiveModules.contains("RC-VLAN-MIB"),
                    "a MIB removed from the folder leaves the profile at the next refresh");

        const QString emptyFolder = library.filePath("Extreme/Empty");
        QDir().mkpath(emptyFolder);
        lab.folders.append({emptyFolder, {}});
        const auto beforeEmptyRemoved = MibPlanForProfile(lab, index);
        QDir().rmdir(emptyFolder);
        const auto afterEmptyRemoved = MibPlanForProfile(lab, index);
        ok &= check(beforeEmptyRemoved.effectiveModules == afterEmptyRemoved.effectiveModules &&
                    beforeEmptyRemoved.sha256 != afterEmptyRemoved.sha256,
                    "a missing empty followed folder invalidates the cached Environment report");
        lab.folders.removeLast();

        lab.folders.append({library.filePath("Extreme/Renamed Away"), {}});
        plan = MibPlanForProfile(lab, index);
        bool reported = false;
        for (const MibPlanResolutionDiagnostic &finding : plan.resolutionDiagnostics)
            reported = reported || (finding.tier == MibPlanResolutionTier::FolderNotFound &&
                                    finding.identity == library.filePath("Extreme/Renamed Away"));
        ok &= check(reported && plan.effectiveModules.contains("RAPID-CITY"),
                    "a followed folder that is gone is reported and the rest of the profile still loads");
        MibProblemInput input; input.plan = plan;
        bool shown = false;
        for (const MibProblem &problem : MibBuildProblemReport(input))
            shown = shown || (problem.category == "Followed folder not found" &&
                              problem.severity == MibProblemSeverity::Warning &&
                              problem.why.contains("Renamed Away") && !problem.suggestion.isEmpty());
        ok &= check(shown, "the Problems tab explains a missing followed folder and what to do");

        MibProfileRecord exactOnly = lab; exactOnly.folders.clear();
        MibProfileMember root1; root1.canonicalPath = library.filePath("Aruba/AOS-CX/arubaos.mib");
        root1.identities = {"ARUBAWIRED-MIB"};
        for (const MibDependencyFileRecord &file : index.files())
            if (file.canonicalPath == root1.canonicalPath) root1.sha256 = file.sha256;
        exactOnly.roots = {root1};
        plan = MibPlanForProfile(exactOnly, index);
        ok &= check(plan.effectiveModules == QStringList{"ARUBAWIRED-MIB"},
                    "a profile with exact files only plans exactly as before");
    }
    {
        // A helper file with a MIB extension is called out; a real MIB with a parse error is not.
        QTemporaryDir files; ok &= check(files.isValid(), "temporary directory for bootstrap-file notes");
        const auto write = [&files](const char *name, const char *text) {
            QFile file(files.filePath(name)); file.open(QIODevice::WriteOnly); file.write(text); return files.filePath(name);
        };
        const QString helper = write("def.my", "-- DEFINITIONS ::= BEGIN is only a comment here\nproduct = 5\n");
        const QString real = write("real.my", "REAL-MIB DEFINITIONS ::= BEGIN\n x OBJECT IDENTIFIER ::= { iso 3 }\nEND\n");
        ok &= check(MibProblemReportDetail::lacksModuleDefinition(helper), "a file without a module header is a probable non-MIB");
        ok &= check(!MibProblemReportDetail::lacksModuleDefinition(real), "a file with a module header is a MIB");
        ok &= check(!MibProblemReportDetail::lacksModuleDefinition(files.filePath("missing.my")),
                    "an unreadable file is never called a non-MIB");
        for (const QString &path : {helper, real}) {
            MibProblemInput input;
            MibDiagnosticRecord record; record.severity = 1; record.sourcePath = path; record.line = 2;
            record.message = "syntax error, unexpected identifier"; input.parserDiagnostics = {record};
            bool noted = false, parseError = false;
            for (const MibProblem &problem : MibBuildProblemReport(input)) {
                noted = noted || (problem.category == "Probably not a MIB" && problem.severity == MibProblemSeverity::Note &&
                                  problem.file == path && problem.suggestion.contains("move it out"));
                parseError = parseError || problem.category == "Parse error";
            }
            ok &= check(parseError, "the parser error itself is still reported");
            ok &= check(noted == (path == helper), "the bootstrap-file note appears only for the file with no module");
        }
    }
    return ok ? 0 : 1;
}
