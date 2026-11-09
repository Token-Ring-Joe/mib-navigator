#include "mibdependencyindex.h"
#include "mibprofile.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <iostream>

namespace {
bool check(bool value, const char *message)
{ if (!value) std::cerr << "FAIL: " << message << '\n'; return value; }

QString touch(const QDir &root, const QString &relative)
{
    const QString path = root.filePath(relative);
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path); file.open(QIODevice::WriteOnly); file.write("x");
    return QFileInfo(path).canonicalFilePath();
}

MibDependencyFileRecord record(const QString &path, const QStringList &modules, const QString &sha = "aa")
{
    MibDependencyFileRecord r;
    r.canonicalPath = path; r.sha256 = sha; r.filename = QFileInfo(path).fileName();
    for (const QString &m : modules) r.importsByModule.insert(m, {});
    return r;
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    bool ok = true;
    QTemporaryDir dir; ok &= check(dir.isValid(), "temporary directory");
    const QDir root(QFileInfo(dir.path()).canonicalFilePath());

    const QList<MibDependencyFileRecord> files{
        record(touch(root, "Standards/IF-MIB"), {"IF-MIB"}),                                  // reserved
        record(touch(root, "standards/lower-MIB"), {"LOWER-MIB"}),                           // reserved, any case
        record(touch(root, "Unassigned/Mystery-MIB"), {"MYSTERY-MIB"}),                      // reserved
        record(touch(root, "Acme/Switch/a.mib"), {"ACME-A"}),
        record(touch(root, "Acme/Switch/deeper/b.mib"), {"ACME-B", "ACME-B2"}),              // any depth; two modules in one file
        record(touch(root, "Acme/Router/c.mib"), {"ACME-C"}),
        record(touch(root, "Acme/shared.mib"), {"ACME-SHARED"}),                             // vendor level: belongs to no product
        record(touch(root, "loose.mib"), {"LOOSE"}),                                         // loose in the Library root
        record(touch(root, ".cache/Prod/h.mib"), {"HIDDEN"}),                                // hidden folder
        record(touch(root, "Acme/Switch/unreadable.mib"), {"ACME-U"}, QString()),            // no hash: could not be read
        record(touch(root, "Acme/Switch/nomodule.txt"), {}),                                 // declares no module
        record(touch(root, "Beta/Prod/d.mib"), {"BETA-D"}),
        record("/somewhere/else/Gamma/Prod/e.mib", {"OUTSIDE"}),                             // not in this Library
    };
    const QList<MibProfileRecord> profiles = MibBuildAutomaticProfiles(root.path(), files);

    QStringList names; for (const auto &p : profiles) names << p.name;
    ok &= check(names == QStringList({"Acme / Router", "Acme / Switch", "Beta / Prod"}),
                "one profile per Vendor/Product folder, sorted; Standards, Unassigned, hidden, loose and outside files make none");
    const MibProfileRecord &sw = profiles.at(1);
    ok &= check(sw.id == "auto:Acme/Switch" && sw.type == MibProfileType::Automatic && MibProfileIsManifest(sw),
                "a stable id from the folder path, the Automatic type, and an exact (manifest) profile");
    ok &= check(sw.roots.size() == 2 && sw.roots.at(0).canonicalPath.endsWith("Acme/Switch/a.mib") &&
                sw.roots.at(1).canonicalPath.endsWith("Acme/Switch/deeper/b.mib") &&
                sw.roots.at(1).identities == QStringList({"ACME-B", "ACME-B2"}),
                "its roots are the product folder's files at any depth, with every module a file declares; unreadable and module-less files are left out");
    ok &= check(sw.explicitModules == QStringList({"ACME-A", "ACME-B", "ACME-B2"}), "its module list follows the roots");
    ok &= check(sw.orderedScopes.size() == 2 && sw.orderedScopes.at(0).canonicalPath == root.filePath("Acme/Switch") &&
                sw.orderedScopes.at(1).canonicalPath == root.filePath("Acme"),
                "it looks in the product folder first, then the vendor folder for files its products share");
    {
        const auto again = MibBuildAutomaticProfiles(root.path(), files);
        bool same = again.size() == profiles.size();
        for (int i = 0; same && i < again.size(); ++i)
            same = again.at(i).id == profiles.at(i).id && again.at(i).roots == profiles.at(i).roots;
        ok &= check(same, "building twice gives the same answer");
    }
    ok &= check(MibBuildAutomaticProfiles("/no/such/folder", files).isEmpty() && MibBuildAutomaticProfiles(root.path(), {}).isEmpty(),
                "a missing Library or no files gives no profiles");

    // ---- how the service treats them ------------------------------------------------------------------------
    const QString repository = dir.filePath("profiles-v1.json");
    MibProfileService service{MibProfileRepository(repository)};
    const int builtIns = service.profiles().size();
    ok &= check(service.setAutomaticProfiles(profiles) && service.profiles().size() == builtIns + 3, "the service lists them");
    ok &= check(!service.setAutomaticProfiles(profiles), "setting the same list again reports no change");
    ok &= check(service.find("auto:Acme/Router") && service.find("auto:Acme/Router")->name == "Acme / Router", "they can be found by id");
    ok &= check(!service.rename("auto:Acme/Router", "My name") && !service.remove("auto:Acme/Router") &&
                service.find("auto:Acme/Router")->name == "Acme / Router", "they cannot be renamed or deleted");
    ok &= check(!service.addFiles("auto:Acme/Router", {touch(root, "Acme/Router/new.mib")}) &&
                service.find("auto:Acme/Router")->roots.size() == 1, "files cannot be added to one by hand");
    MibProfileRecord edited = *service.find("auto:Acme/Router"); edited.name = "Hacked";
    ok &= check(!service.update(edited) && service.find("auto:Acme/Router")->name == "Acme / Router", "they cannot be updated");
    const QString custom = service.create("Mine");
    ok &= check(!custom.isEmpty() && service.persist(), "a Custom profile can still be created and saved next to them");
    QFile saved(repository);
    ok &= check(saved.open(QIODevice::ReadOnly) && !saved.readAll().contains("auto:") , "Automatic profiles are never written to disk");

    // The folder changes: a file is removed and a product folder disappears.
    const QList<MibDependencyFileRecord> after{files.mid(3, 3)};
    ok &= check(service.setAutomaticProfiles(MibBuildAutomaticProfiles(root.path(), after)) &&
                !service.find("auto:Beta/Prod") && service.find("auto:Acme/Switch")->roots.size() == 2 && service.find(custom),
                "when the folder changes the profiles follow it, and Custom profiles are untouched");
    return ok ? 0 : 1;
}
