#include "mibcollectionservice.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <iostream>

namespace {
bool check(bool value, const char *message)
{ if (!value) std::cerr << "FAIL: " << message << '\n'; return value; }

void write(const QString &path, const QByteArray &text)
{ QFile f(path); f.open(QIODevice::WriteOnly | QIODevice::Truncate); f.write(text); }

QByteArray mib(const char *module, const char *name, int number)
{
    return QByteArray(module) + " DEFINITIONS ::= BEGIN\nIMPORTS enterprises FROM SNMPv2-SMI;\n" + name +
           " OBJECT IDENTIFIER ::= { enterprises " + QByteArray::number(number) + " }\nEND\n";
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    bool ok = true;
    QTemporaryDir dir; ok &= check(dir.isValid(), "temporary directory");
    write(dir.filePath("SNMPv2-SMI"), "SNMPv2-SMI DEFINITIONS ::= BEGIN\ninternet OBJECT IDENTIFIER ::= { iso org(3) dod(6) 1 }\n"
                                       "private OBJECT IDENTIFIER ::= { internet 4 }\nenterprises OBJECT IDENTIFIER ::= { private 1 }\nEND\n");
    write(dir.filePath("A-MIB"), mib("A-MIB", "alpha", 100));
    write(dir.filePath("B-MIB"), mib("B-MIB", "bravo", 200));

    QStringList files{dir.filePath("SNMPv2-SMI"), dir.filePath("A-MIB"), dir.filePath("B-MIB")};
    MibCollectionService service([&files] { return files; });
    ok &= check(!service.hasIndex() && service.search("alpha").entries.isEmpty() &&
                service.statusText().contains("not been indexed"), "before the first build there is nothing to search");

    int changes = 0;
    QObject::connect(&service, &MibCollectionService::indexChanged, [&changes] { ++changes; });
    service.rebuildNow();
    ok &= check(service.isBuilding() && service.statusText().contains("Indexing"), "a build runs in the background");
    ok &= check(service.waitForBuild(10000) && service.hasIndex() && !service.isBuilding(), "the build finishes");
    ok &= check(service.search("alpha").entries.size() == 1 && service.search("alpha").entries.first().oid == "1.3.6.1.4.1.100" &&
                service.search("bravo").entries.first().oid == "1.3.6.1.4.1.200", "both MIBs are searchable");
    ok &= check(service.statusText().contains("3 MIB file") && changes >= 2, "the status reports what was indexed; start and finish are announced");

    // A new file appears and one changes: after invalidate() the next index has them.
    write(dir.filePath("C-MIB"), mib("C-MIB", "charlie", 300)); files << dir.filePath("C-MIB");
    write(dir.filePath("A-MIB"), mib("A-MIB", "alpha", 111));
    const quint64 before = service.generation();
    service.invalidate();
    ok &= check(service.waitForBuild(10000) && service.generation() == before + 1, "changes trigger a rebuild");
    ok &= check(service.search("charlie").entries.size() == 1 && service.search("alpha").entries.first().oid == "1.3.6.1.4.1.111",
                "new and edited files are picked up");

    // Same size, edited within the same second: still picked up (the cache must not trust it).
    write(dir.filePath("A-MIB"), mib("A-MIB", "alpha", 122));
    service.rebuildNow();
    ok &= check(service.waitForBuild(10000) && service.search("alpha").entries.first().oid == "1.3.6.1.4.1.122",
                "an edit to the same size in the same second is not missed");

    // A file that disappears is forgotten.
    files.removeAll(dir.filePath("B-MIB")); QFile::remove(dir.filePath("B-MIB"));
    service.rebuildNow();
    ok &= check(service.waitForBuild(10000) && service.search("bravo").entries.isEmpty(), "a removed file is no longer searchable");

    // Searching while a rebuild runs still answers from the previous index.
    service.rebuildNow();
    ok &= check(service.search("alpha").entries.size() == 1, "search keeps working during a rebuild");
    service.waitForBuild(10000);

    // Several invalidations close together share one rebuild.
    const quint64 generation = service.generation();
    for (int i = 0; i < 5; ++i) service.invalidate();
    ok &= check(service.waitForBuild(10000) && service.generation() == generation + 1, "changes close together share one rebuild");

    // An unreadable path is skipped, not fatal.
    files << dir.filePath("does-not-exist");
    service.rebuildNow();
    ok &= check(service.waitForBuild(10000) && service.search("charlie").entries.size() == 1, "a missing file does not break the index");
    return ok ? 0 : 1;
}
