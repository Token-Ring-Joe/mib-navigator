#include "mibcollectionservice.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QPointer>
#include <QRunnable>
#include <QSet>
#include <QThread>

MibCollectionService::MibCollectionService(FileSource files, QObject *parent)
    : QObject(parent), fileSource(std::move(files))
{
    pool.setMaxThreadCount(1);
    debounce.setSingleShot(true);
    debounce.setInterval(1500);
    connect(&debounce, &QTimer::timeout, this, &MibCollectionService::startBuild);
}

MibCollectionService::~MibCollectionService()
{
    debounce.stop();
    pool.waitForDone(30000);
}

void MibCollectionService::invalidate() { debounce.start(); }

void MibCollectionService::rebuildNow()
{
    debounce.stop();
    startBuild();
}

bool MibCollectionService::waitForBuild(int timeoutMs)
{
    QElapsedTimer waited; waited.start();
    while (building || debounce.isActive()) {
        if (waited.elapsed() > timeoutMs) return false;
        if (debounce.isActive() && !building) { debounce.stop(); startBuild(); }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
    return true;
}

MibCollectionSearchResult MibCollectionService::search(const QString &query, int limit) const
{
    return current ? current->search(query, limit) : MibCollectionSearchResult{};
}

QString MibCollectionService::statusText() const
{
    if (!current)
        return building ? tr("Indexing the MIB collection…") : tr("The MIB collection has not been indexed yet.");
    const QString base = tr("%n definition(s) in %1 MIB file(s)", nullptr, current->definitionCount()).arg(current->fileCount());
    const int unresolved = current->unresolvedCount();
    QString text = unresolved ? tr("%1; %2 without a numeric OID (a parent is missing)").arg(base).arg(unresolved) : base;
    return building ? tr("%1 (updating…)").arg(text) : text;
}

void MibCollectionService::startBuild()
{
    if (building) { rebuildAgain = true; return; }
    building = true;
    const QStringList files = fileSource ? fileSource() : QStringList{};
    emit indexChanged();
    class Task final : public QRunnable
    {
    public:
        Task(QPointer<MibCollectionService> service, QStringList files, QMutex *lock,
             QHash<QString, CachedFile> *cache)
            : service(std::move(service)), files(std::move(files)), lock(lock), cache(cache) {}
        void run() override
        {
            QElapsedTimer timer; timer.start();
            QList<MibCollectionIndex::ParsedFile> parsed;
            int read = 0, reused = 0;
            for (const QString &path : std::as_const(files)) {
                const QFileInfo info(path);
                // A file touched in the last few seconds is read again even if its size and time
                // match: some file systems stamp times coarsely, so an edit to the same size could
                // otherwise be missed.
                const bool recentlyModified =
                    info.lastModified().msecsTo(QDateTime::currentDateTime()) < 3000;
                {
                    QMutexLocker locker(lock);
                    const auto it = cache->constFind(path);
                    if (!recentlyModified && it != cache->cend() && it->size == info.size() &&
                        it->modified == info.lastModified().toMSecsSinceEpoch()) {
                        parsed.append(it->parsed); ++reused; continue;
                    }
                }
                QFile file(path);
                if (!file.open(QIODevice::ReadOnly)) continue;
                CachedFile entry;
                entry.size = info.size(); entry.modified = info.lastModified().toMSecsSinceEpoch();
                entry.parsed = MibCollectionIndex::parse(path, file.readAll());
                parsed.append(entry.parsed); ++read;
                QMutexLocker locker(lock);
                cache->insert(path, entry);
            }
            {   // forget files that are no longer in the collection
                QMutexLocker locker(lock);
                const QSet<QString> present(files.begin(), files.end());
                for (auto it = cache->begin(); it != cache->end();) it = present.contains(it.key()) ? std::next(it) : cache->erase(it);
            }
            auto index = std::make_shared<MibCollectionIndex>();
            index->setFiles(parsed);
            const qint64 elapsed = timer.elapsed();
            QPointer<MibCollectionService> target = service;
            if (!target) return;
            QMetaObject::invokeMethod(target.data(), [target, index, elapsed, read, reused]() {
                if (target) target->adopt(index, elapsed, read, reused);
            }, Qt::QueuedConnection);
        }
    private:
        QPointer<MibCollectionService> service;
        QStringList files;
        QMutex *lock;
        QHash<QString, CachedFile> *cache;
    };
    pool.start(new Task(QPointer<MibCollectionService>(this), files, &cacheLock, &cache));
}

void MibCollectionService::adopt(std::shared_ptr<const MibCollectionIndex> index, qint64 elapsedMs,
                                 int filesRead, int filesReused)
{
    current = std::move(index);
    building = false;
    ++builtGeneration;
    lastBuildMs = elapsedMs; lastRead = filesRead; lastReused = filesReused;
    emit indexChanged();
    if (rebuildAgain) { rebuildAgain = false; startBuild(); }
}
