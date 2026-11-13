#ifndef MIBENVIRONMENTDISKCACHE_H
#define MIBENVIRONMENTDISKCACHE_H

#include "mibeffectiveplan.h"
#include "mibenvironment.h"

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QThreadPool>
#include <functional>
#include <optional>

// Saved copies of built MIB Environments, so a profile that took a minute to parse starts in
// seconds the next time.
//
// An Environment is an immutable value: nothing outside the build reads live parser state
// from it, so a saved copy is as good as a fresh build provided it describes exactly the same
// inputs. The key therefore hashes content only: the plan, every provider file's SHA-256, the
// runtime configuration without its per-process Library generation, the Environment format
// versions, the parser identity and this exact application build. Any change to a MIB file, the
// profile, the Library location, the parser or the application produces a different key, so a
// stale copy cannot be found.
//
// A copy is never trusted blindly: it is checksummed, must carry the key it was found under,
// and is then vetted by the same authority check as a fresh build. Any failure deletes the copy
// and the caller builds normally. Copies live only under the application data folder, are
// written atomically, and the folder is pruned to a bounded size.
class MibEnvironmentDiskCache final
{
public:
    static constexpr int FormatVersion = 1;
    static constexpr quint64 DefaultByteBudget = 1024ULL * 1024ULL * 1024ULL;
    static constexpr int DefaultMaximumEntries = 16;

    struct Entry {
        MibEnvironmentPtr environment;
        QStringList loadedModules;
    };

    explicit MibEnvironmentDiskCache(const QString &directory,
                                     quint64 byteBudget = DefaultByteBudget,
                                     int maximumEntries = DefaultMaximumEntries);
    ~MibEnvironmentDiskCache();

    // The folder for saved Environments: inside the isolated state root of an automated run,
    // otherwise under the user's application data folder, beside the dependency indexes.
    static QString defaultDirectory();

    // Receives one line for each background save and each failed save. Called from the writer
    // thread, so it must be thread-safe (the diagnostic logger is).
    void setLogger(std::function<void(const QString &)> logger) { logLine = std::move(logger); }

    // Empty when this plan must not be cached (an old resolver policy whose hashes depend on
    // the per-process Library generation, or an incomplete plan).
    static QString keyFor(const MibEffectivePlan &plan, const QString &parserIdentity,
                          const QString &buildFingerprint);

    // The saved copy for `key`, re-stamped for `plan`'s Library generation, or nothing (with
    // `why` set when a copy existed but was rejected and removed).
    std::optional<Entry> load(const QString &key, const MibEffectivePlan &plan, QString *why = nullptr) const;
    // Writes synchronously; false with `error` set when it could not.
    bool store(const QString &key, const MibEnvironment &environment, const QStringList &loadedModules,
               QString *error = nullptr) const;
    // Writes on a background thread; the Environment is immutable, so sharing it is safe.
    void storeAsync(const QString &key, MibEnvironmentPtr environment, const QStringList &loadedModules);
    void remove(const QString &key) const;
    // Removes the oldest copies beyond the entry and size limits and stray temporary files.
    void prune() const;
    QString pathFor(const QString &key) const;
    void waitForPendingWrites();

    // Exposed for tests.
    static QByteArray serialize(const MibEnvironment &environment, const QStringList &loadedModules);
    static std::shared_ptr<MibEnvironment> deserialize(const QByteArray &payload,
                                                       QStringList *loadedModules, QString *error);

private:
    QString directoryPath;
    quint64 budget;
    int maximumEntryCount;
    std::function<void(const QString &)> logLine;
    QThreadPool writers;
};

#endif
