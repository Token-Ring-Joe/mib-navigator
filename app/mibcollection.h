#ifndef MIBCOLLECTION_H
#define MIBCOLLECTION_H

#include <QString>
#include <QStringList>

class QSettings;

struct MibCollectionResult
{
    bool success = false;
    int standardsCopied = 0;
    int importedCopied = 0;
    int identicalSkipped = 0;
    QStringList conflicts;
    QString error;
};

class MibCollection
{
public:
    explicit MibCollection(QString root = {});
    static QString defaultRoot();
    static QString legacyManagedRoot();
    static QString internalStateRoot();
    static QString configuredRoot(QSettings &settings);
    static bool setConfiguredRoot(QSettings &settings, const QString &root,
                                  const QStringList &bundledPaths,
                                  MibCollectionResult *result = nullptr);
    QString rootPath() const { return root; }
    QString standardsPath() const;
    QString unassignedPath() const;
    QString importedPath() const { return unassignedPath(); }
    QString prototypeProfilesPath() const;
    QStringList runtimeSearchPaths() const;
    MibCollectionResult initialize(const QStringList &bundledPaths,
                                   const QString &legacyRoot = legacyManagedRoot()) const;
    MibCollectionResult importFiles(const QStringList &sourceFiles) const;
private:
    QString root;
};

#endif
