#include "querymacroservice.h"
#include <QObject>

QueryMacroService::QueryMacroService(const QString &path, QObject *parent) : QObject(parent), repository(path) { reload(); }
std::optional<QueryMacroDefinition> QueryMacroService::definition(const QString &id) const {
    const int i = indexOf(id); if (i < 0) return {}; return working[i];
}
int QueryMacroService::indexOf(const QString &id) const { for (qsizetype i = 0; i < working.size(); ++i) if (working[i].id == id) return int(i); return -1; }
void QueryMacroService::edited() { lastError.clear(); emit changed(); }
QString QueryMacroService::create() {
    if (!loaded || working.size() >= QueryMacroMaximumDefinitions) return {};
    QueryMacroDefinition macro; macro.id = QueryMacroNewId();
    int number = 1;
    const auto nameExists = [this](const QString &name) { for (const auto &m : working) if (m.name == name) return true; return false; };
    do { macro.name = tr("Macro %1").arg(number++); } while (nameExists(macro.name));
    working.append(macro); edited(); return macro.id;
}
QString QueryMacroService::duplicate(const QString &id) {
    const auto original = definition(id); if (!loaded || !original || working.size() >= QueryMacroMaximumDefinitions) return {};
    auto copy = *original; copy.id = QueryMacroNewId(); copy.name = tr("%1 copy").arg(copy.name.left(240));
    for (auto &entry : copy.entries) entry.id = QueryMacroNewId();
    working.insert(indexOf(id) + 1, copy); edited(); return copy.id;
}
bool QueryMacroService::remove(const QString &id) {
    const int i = indexOf(id); if (!loaded || i < 0) return false;
    working.removeAt(i); edited(); return true;
}
bool QueryMacroService::move(const QString &id, int delta) {
    const int i = indexOf(id), next = i + delta;
    if (!loaded || i < 0 || (delta != -1 && delta != 1) || next < 0 || next >= working.size()) return false;
    working.move(i, next); edited(); return true;
}
bool QueryMacroService::rename(const QString &id, const QString &name) {
    const int i = indexOf(id); if (!loaded || i < 0 || name.size() > 256) return false;
    if (working[i].name == name) return true;
    working[i].name = name; edited(); return true; // Empty working names are diagnosed by Save, never written.
}
bool QueryMacroService::putEntry(const QString &macroId, const QueryMacroEntry &entry) {
    const int i = indexOf(macroId); if (!loaded || i < 0) return false;
    if (!ValidateQueryMacroEntry(entry, &lastError)) { emit changed(); return false; }
    for (auto &current : working[i].entries) if (current.id == entry.id) { current = entry; edited(); return true; }
    if (working[i].entries.size() >= QueryMacroMaximumEntries) { lastError = tr("This macro already has 1024 entries."); emit changed(); return false; }
    working[i].entries.append(entry); edited(); return true;
}
bool QueryMacroService::removeEntry(const QString &macroId, const QString &entryId) {
    const int i = indexOf(macroId); if (!loaded || i < 0) return false;
    auto &entries = working[i].entries;
    for (qsizetype n = 0; n < entries.size(); ++n) if (entries[n].id == entryId) { entries.removeAt(n); edited(); return true; }
    return false;
}
bool QueryMacroService::moveEntry(const QString &macroId, const QString &entryId, int delta) {
    const int i = indexOf(macroId); if (!loaded || i < 0 || (delta != -1 && delta != 1)) return false;
    auto &entries = working[i].entries;
    for (qsizetype n = 0; n < entries.size(); ++n) if (entries[n].id == entryId) {
        const qsizetype next = n + delta; if (next < 0 || next >= entries.size()) return false;
        entries.move(n, next); edited(); return true;
    }
    return false;
}
bool QueryMacroService::save() {
    if (!loaded) { emit changed(); return false; }
    if (!dirty()) return true;
    if (!repository.save(working, &snapshot, &lastError)) { emit changed(); return false; }
    committed = working; lastError.clear(); emit changed(); return true;
}
bool QueryMacroService::reload(bool discardWorking) {
    if (dirty() && !discardWorking) { lastError = tr("Save or discard working changes before reloading."); emit changed(); return false; }
    QList<QueryMacroDefinition> records; QueryMacroStoreSnapshot current;
    if (!repository.load(&records, &current, &lastError)) { loaded = false; emit changed(); return false; }
    snapshot = current; committed = records; working = records; loaded = true; lastError.clear(); emit changed(); return true;
}
void QueryMacroService::discard() { working = committed; if (loaded) lastError.clear(); emit changed(); }
