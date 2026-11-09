#include "mibdependencyindex.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <iostream>

namespace {
bool check(bool value, const char *message) { if (!value) std::cerr << "FAIL: " << message << '\n'; return value; }
bool writeFile(const QString &path, const QByteArray &content) { QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(content) == content.size(); }
QByteArray mib(const QString &name, const QString &dependency = {}) {
    QByteArray content = name.toUtf8() + " DEFINITIONS ::= BEGIN\n";
    if (!dependency.isEmpty()) content += "IMPORTS item FROM " + dependency.toUtf8() + ";\n";
    return content + "END\n";
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv); bool ok = true; QTemporaryDir temp; QDir dir(temp.path());
    for (int i = 0; i < 450; ++i)
        writeFile(dir.filePath(QStringLiteral("VENDOR-%1.mib").arg(i, 3, 10, QLatin1Char('0'))),
                  mib(QStringLiteral("VENDOR-%1-MIB").arg(i)));
    const QString missingPath = dir.filePath("missing/dependency-index-v1.json");
    MibDependencyIndex missing(missingPath); missing.load(); int semanticLoads = 0;
    const auto firstRun = missing.inspect({temp.path()});
    ok &= check(firstRun.candidates.size() == 450 && firstRun.newOrChanged == 450,
                "first-run startup cheaply enumerates 400+ candidates");
    ok &= check(semanticLoads == 0 && missing.files().isEmpty() && !QFile::exists(missingPath),
                "missing index does not compile candidates or write during startup read");
    const QString indexPath = dir.filePath("dependency-index-v1.json"); MibDependencyIndex indexed(indexPath);
    const auto scan = indexed.update({temp.path()});
    ok &= check(scan.scanned == 450, "explicit dependency check scans candidates");
    MibProfileDependencyCheck cached; cached.profileSignature = "signature";
    cached.indexGeneration = indexed.generation(); cached.checkedUtc = QDateTime::currentDateTimeUtc();
    indexed.setProfileCheck("profile", cached); indexed.save();
    MibDependencyIndex startup(indexPath); startup.load(); const auto unchanged = startup.inspect({temp.path()});
    ok &= check(unchanged.unchanged == 450 && unchanged.newOrChanged == 0 && semanticLoads == 0,
                "startup reuses unchanged persisted index without semantic loading");
    ok &= check(startup.profileCheckCurrent("profile", "signature"), "profile switching reuses cached graph");
    writeFile(dir.filePath("NEW-CANDIDATE.mib"), mib("NEW-CANDIDATE-MIB"));
    const auto changed = startup.inspect({temp.path()});
    ok &= check(changed.newOrChanged == 1 && changed.stale() && startup.generation() == indexed.generation(),
                "new candidate marks startup stale without rescanning or mutating index");
    ok &= check(startup.provider("VENDOR-1-MIB").status == MibProviderStatus::Found,
                "stale startup retains usable unchanged provider records");
    QTemporaryDir aliases; writeFile(aliases.filePath("synro.mib"), mib("SYNOPTICS-ROOT-MIB"));
    writeFile(aliases.filePath("bay.mib"), mib("BAY-STACK-TEST-MIB", "SYNOPTICS-ROOT-MIB"));
    MibDependencyIndex aliasIndex(aliases.filePath("index.json")); aliasIndex.update({aliases.path()});
    const auto provider = aliasIndex.provider("SYNOPTICS-ROOT-MIB");
    ok &= check(provider.status == MibProviderStatus::Found && provider.path.endsWith("synro.mib"),
                "startup requested identity uses cached mismatched provider filename");
    QStringList loaded; QStringList attemptedPaths;
    auto load = [&](const QString &path, const QString &identity) {
        ++semanticLoads; attemptedPaths.append(path); MibDependencyLoadAttempt result;
        if (identity == "BAY-STACK-TEST-MIB" && !loaded.contains("SYNOPTICS-ROOT-MIB")) return result;
        loaded.append(identity); result.success = true; result.loadedModuleNames = {identity}; return result;
    };
    const auto requested = MibBoundedDependencyLoader().load({"BAY-STACK-TEST-MIB"}, aliasIndex, load);
    ok &= check(requested.failures.isEmpty() && loaded.contains("SYNOPTICS-ROOT-MIB") &&
                loaded.contains("BAY-STACK-TEST-MIB") && attemptedPaths.contains(provider.path),
                "startup-style requested load resolves provider then consumer with bounded retry");
    const auto explicitFailure = MibBoundedDependencyLoader().load({"NOT-PRESENT"}, aliasIndex, load);
    ok &= check(explicitFailure.failures.size() == 1 &&
                explicitFailure.failures.first().kind == MibDependencyFailureKind::MissingProvider,
                "actual requested-module failure remains one concise classified diagnostic");
    return ok ? 0 : 1;
}
