// Compares the collection index (a text scan) with the real MIB parser, module by module.
// The index may fail to resolve an OID; it must never resolve one to the wrong value.
#include "mibcollectionindex.h"
#include "smi.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QSet>
#include <iostream>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QStringList directories;
    const QString root = QStringLiteral(SNMPB_SOURCE_DIR) + "/libsmi/mibs/";
    for (const char *d : {"ietf", "iana", "irtf", "tubs"}) directories << root + d;
    const QByteArray extra = qgetenv("SNMPB_ACCURACY_EXTRA_DIRS");   // optional: a real vendor collection
    for (const QString &d : QString::fromLocal8Bit(extra).split(QLatin1Char(':'), Qt::SkipEmptyParts)) directories << d;

    QList<MibCollectionIndex::ParsedFile> parsed;
    QStringList files;
    QElapsedTimer indexTimer;
    for (const QString &directory : std::as_const(directories)) {
        QDirIterator it(directory, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) files << it.next();
    }
    files.sort();
    indexTimer.start();
    for (const QString &path : std::as_const(files)) {
        QFile f(path);
        if (f.open(QIODevice::ReadOnly)) parsed << MibCollectionIndex::parse(path, f.readAll());
    }
    MibCollectionIndex index;
    index.setFiles(parsed);
    const qint64 indexMs = indexTimer.elapsed();

    QString searchPath;
    for (const QString &d : std::as_const(directories)) {
        QDirIterator it(d, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        searchPath += d + QDir::listSeparator();
        while (it.hasNext()) searchPath += it.next() + QDir::listSeparator();
    }
    smiInit("accuracy");
    smiSetErrorLevel(-1);
    smiSetFlags(SMI_FLAG_ERRORS & 0);
    smiSetPath(searchPath.toLocal8Bit().constData());

    int compared = 0, matched = 0, wrong = 0, unresolvedHere = 0, parserHasNoNode = 0, parserGaveUp = 0;
    QSet<QString> modules;
    for (const auto &file : parsed) for (const auto &m : file.modules) modules.insert(m.name);
    QStringList sortedModules(modules.begin(), modules.end());
    sortedModules.sort();
    QStringList examples;
    // Load by module name first (the standard MIBs), then every file by its path, because vendor
    // files rarely carry their module's name.
    for (const QString &module : std::as_const(sortedModules)) smiLoadModule(module.toLatin1().constData());
    for (const QString &path : std::as_const(files)) smiLoadModule(path.toLocal8Bit().constData());
    // Walk the index through the public API: every defined name, looked up per module.
    for (const QString &module : std::as_const(sortedModules)) {
        SmiModule *smiModule = smiGetModule(module.toLatin1().constData());
        if (!smiModule) continue;
        for (SmiNode *node = smiGetFirstNode(smiModule, SMI_NODEKIND_ANY); node; node = smiGetNextNode(node, SMI_NODEKIND_ANY)) {
            if (!node->name || !node->oidlen) continue;
            const auto hits = index.search(module + "::" + QString::fromLatin1(node->name));
            const MibCollectionEntry *entry = nullptr;
            for (const auto &h : hits.entries) if (h.module == module && h.name == QString::fromLatin1(node->name)) { entry = &h; break; }
            if (!entry) { ++parserHasNoNode; continue; }
            QString expected;
            for (unsigned i = 0; i < node->oidlen; ++i) expected += (i ? "." : "") + QString::number(node->oid[i]);
            ++compared;
            if (entry->oid.isEmpty()) { ++unresolvedHere; if (examples.size() < 8) examples << module + "::" + entry->name + " -- " + entry->unresolved; }
            else if (entry->oid == expected) ++matched;
            // libsmi answers with a relative fragment starting at 0 when it could not find a parent;
            // that is the parser giving up, not a disagreement.
            else if (expected.startsWith(QLatin1String("0.")) && !entry->oid.startsWith(QLatin1String("0."))) ++parserGaveUp;
            else { ++wrong; std::cerr << "WRONG OID " << module.toStdString() << "::" << entry->name.toStdString()
                                      << " index=" << entry->oid.toStdString() << " parser=" << expected.toStdString() << '\n'; }
        }
    }
    std::cout << "files=" << files.size() << " modules=" << modules.size() << " compared=" << compared << " matched=" << matched
              << " wrong=" << wrong << " parserGaveUp=" << parserGaveUp << " unresolved=" << unresolvedHere
              << " notIndexed=" << parserHasNoNode << " indexBuildMs=" << indexMs << " definitions=" << index.definitionCount() << '\n';
    for (const QString &e : std::as_const(examples)) std::cout << "  unresolved: " << e.toStdString() << '\n';
    if (wrong != 0) return 1;
    if (compared < 1000) { std::cerr << "FAIL: too few nodes compared\n"; return 1; }
    if ((matched + parserGaveUp) * 100 < compared * 95) { std::cerr << "FAIL: fewer than 95% of nodes resolved\n"; return 1; }
    smiExit();
    return 0;
}
