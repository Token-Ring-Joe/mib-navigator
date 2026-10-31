#ifndef MIBCOLLECTIONSERVICE_H
#define MIBCOLLECTIONSERVICE_H

#include <QHash>
#include <QMutex>
#include <QObject>
#include <QThreadPool>
#include <QTimer>
#include <functional>
#include <memory>

#include "mibcollectionindex.h"

// Keeps the collection-wide search index up to date. The index is rebuilt in the background
// whenever the collection changes; searching never waits for a rebuild (it uses the previous
// index until the new one is ready) and never touches the disk.
class MibCollectionService : public QObject
{
    Q_OBJECT
public:
    using FileSource = std::function<QStringList()>;

    explicit MibCollectionService(FileSource files, QObject *parent = nullptr);
    ~MibCollectionService() override;

    // The collection changed: rebuild soon (changes arriving close together share one rebuild).
    void invalidate();
    void rebuildNow();
    // Blocks until no build is running (tests, shutdown).
    bool waitForBuild(int timeoutMs);

    bool hasIndex() const { return static_cast<bool>(current); }
    bool isBuilding() const { return building; }
    MibCollectionSearchResult search(const QString &query, int limit = 500) const;
    // The current index, for callers that need more than search() (Detect profile).
    std::shared_ptr<const MibCollectionIndex> snapshot() const { return current; }
    QString statusText() const;
    quint64 generation() const { return builtGeneration; }

signals:
    void indexChanged();    // a build started or finished

private:
    struct CachedFile { qint64 size = -1; qint64 modified = -1; MibCollectionIndex::ParsedFile parsed; };
    void startBuild();
    void adopt(std::shared_ptr<const MibCollectionIndex> index, qint64 elapsedMs, int filesRead, int filesReused);

    FileSource fileSource;
    QThreadPool pool;
    QTimer debounce;
    std::shared_ptr<const MibCollectionIndex> current;
    bool building = false;
    bool rebuildAgain = false;
    quint64 builtGeneration = 0;
    qint64 lastBuildMs = 0;
    int lastRead = 0, lastReused = 0;
    QMutex cacheLock;
    QHash<QString, CachedFile> cache;   // per file, so an unchanged file is not re-read
};

#endif
