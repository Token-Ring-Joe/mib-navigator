#ifndef QUERYMACROSERVICE_H
#define QUERYMACROSERVICE_H
#include "querymacrorepository.h"
#include <QObject>

// Sole owner of working definitions and their durable baseline. No transport dependency.
class QueryMacroService final : public QObject {
    Q_OBJECT
public:
    explicit QueryMacroService(const QString &path, QObject *parent = nullptr);
    const QList<QueryMacroDefinition> &definitions() const { return working; }
    std::optional<QueryMacroDefinition> definition(const QString &id) const;
    bool editable() const { return loaded; }
    bool dirty() const { return !(working == committed); }
    QString error() const { return lastError; }
    QString path() const { return repository.path(); }
    QString create();
    QString duplicate(const QString &id);
    bool remove(const QString &id);
    bool move(const QString &id, int delta);
    bool rename(const QString &id, const QString &name);
    bool putEntry(const QString &macroId, const QueryMacroEntry &entry);
    bool removeEntry(const QString &macroId, const QString &entryId);
    bool moveEntry(const QString &macroId, const QString &entryId, int delta);
    bool save();
    bool reload(bool discardWorking = false);
    void discard();
signals:
    void changed();
private:
    int indexOf(const QString &id) const;
    void edited();
    QueryMacroRepository repository;
    QueryMacroStoreSnapshot snapshot;
    QList<QueryMacroDefinition> working, committed;
    QString lastError;
    bool loaded = false;
};
#endif
