#include "mibcollectionindex.h"

#include <QObject>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>

namespace {
enum class TokenKind { Ident, Number, Symbol, String, End };
struct Token { TokenKind kind = TokenKind::End; QString text; int line = 0; };

bool isIdentStart(QChar c) { return c.isLetter(); }
bool isIdentPart(QChar c) { return c.isLetterOrNumber() || c == QLatin1Char('-') || c == QLatin1Char('_'); }

// A tolerant tokenizer: comments are dropped and quoted strings become one token, so braces and
// "::=" inside a DESCRIPTION can never be mistaken for structure.
QList<Token> tokenize(const QString &text)
{
    QList<Token> tokens;
    const qsizetype n = text.size();
    qsizetype i = 0; int line = 1;
    while (i < n) {
        const QChar c = text.at(i);
        if (c == QLatin1Char('\n')) { ++line; ++i; continue; }
        if (c.isSpace()) { ++i; continue; }
        if (c == QLatin1Char('-') && i + 1 < n && text.at(i + 1) == QLatin1Char('-')) {
            i += 2;   // a comment ends at the next "--" or at the end of the line
            while (i < n && text.at(i) != QLatin1Char('\n') &&
                   !(text.at(i) == QLatin1Char('-') && i + 1 < n && text.at(i + 1) == QLatin1Char('-'))) ++i;
            if (i < n && text.at(i) == QLatin1Char('-')) i += 2;
            continue;
        }
        if (c == QLatin1Char('"')) {
            const int startLine = line; ++i;
            while (i < n) {
                if (text.at(i) == QLatin1Char('\n')) ++line;
                if (text.at(i) == QLatin1Char('"')) {
                    if (i + 1 < n && text.at(i + 1) == QLatin1Char('"')) { i += 2; continue; }
                    break;
                }
                ++i;
            }
            ++i;
            tokens.append({TokenKind::String, QString(), startLine});
            continue;
        }
        if (c == QLatin1Char('\'')) {   // '0f'H or '0101'B
            ++i; while (i < n && text.at(i) != QLatin1Char('\'') && text.at(i) != QLatin1Char('\n')) ++i;
            if (i < n && text.at(i) == QLatin1Char('\'')) ++i;
            if (i < n && (text.at(i) == QLatin1Char('H') || text.at(i) == QLatin1Char('B'))) ++i;
            tokens.append({TokenKind::String, QString(), line});
            continue;
        }
        if (c == QLatin1Char(':') && i + 2 < n && text.at(i + 1) == QLatin1Char(':') && text.at(i + 2) == QLatin1Char('=')) {
            tokens.append({TokenKind::Symbol, QStringLiteral("::="), line}); i += 3; continue;
        }
        if (c == QLatin1Char('.') && i + 1 < n && text.at(i + 1) == QLatin1Char('.')) {
            tokens.append({TokenKind::Symbol, QStringLiteral(".."), line}); i += 2; continue;
        }
        if (c.isDigit()) {
            const qsizetype start = i; while (i < n && text.at(i).isDigit()) ++i;
            tokens.append({TokenKind::Number, text.mid(start, i - start), line});
            continue;
        }
        if (isIdentStart(c)) {
            const qsizetype start = i; ++i;
            while (i < n && isIdentPart(text.at(i))) ++i;
            QString word = text.mid(start, i - start);
            while (word.endsWith(QLatin1Char('-'))) { word.chop(1); --i; }   // "foo--comment"
            tokens.append({TokenKind::Ident, word, line});
            continue;
        }
        tokens.append({TokenKind::Symbol, QString(c), line});
        ++i;
    }
    return tokens;
}

const QHash<QString, QString> &macroKinds()
{
    static const QHash<QString, QString> kinds{
        {QStringLiteral("OBJECT-TYPE"), QStringLiteral("Object")},
        {QStringLiteral("MODULE-IDENTITY"), QStringLiteral("Module identity")},
        {QStringLiteral("OBJECT-IDENTITY"), QStringLiteral("Object identity")},
        {QStringLiteral("NOTIFICATION-TYPE"), QStringLiteral("Notification")},
        {QStringLiteral("TRAP-TYPE"), QStringLiteral("Trap")},
        {QStringLiteral("OBJECT-GROUP"), QStringLiteral("Conformance group")},
        {QStringLiteral("NOTIFICATION-GROUP"), QStringLiteral("Conformance group")},
        {QStringLiteral("MODULE-COMPLIANCE"), QStringLiteral("Compliance")},
        {QStringLiteral("AGENT-CAPABILITIES"), QStringLiteral("Capabilities")},
    };
    return kinds;
}

bool isSym(const Token &t, const char *s) { return t.kind == TokenKind::Symbol && t.text == QLatin1String(s); }
bool isWord(const Token &t, const char *s) { return t.kind == TokenKind::Ident && t.text == QLatin1String(s); }

// Reads the contents of "{ ... }" starting at tokens[i] == "{". Leaves i after the "}".
QList<MibCollectionIndex::OidItem> readOidBraces(const QList<Token> &tokens, qsizetype &i)
{
    QList<MibCollectionIndex::OidItem> items;
    ++i;   // past "{"
    int depth = 1;
    while (i < tokens.size() && depth > 0) {
        const Token &t = tokens.at(i);
        if (isSym(t, "}")) { --depth; ++i; continue; }
        if (isSym(t, "{")) { ++depth; ++i; continue; }
        if (depth != 1) { ++i; continue; }
        if (t.kind == TokenKind::Number) {
            items.append({QString(), t.text.toInt(), QString()}); ++i;
        } else if (t.kind == TokenKind::Ident) {
            MibCollectionIndex::OidItem item; item.name = t.text; ++i;
            if (i + 1 < tokens.size() && isSym(tokens.at(i), ".") && tokens.at(i + 1).kind == TokenKind::Ident) {
                item.module = item.name; item.name = tokens.at(i + 1).text; i += 2;   // Module.name
            }
            if (i + 2 < tokens.size() && isSym(tokens.at(i), "(") && tokens.at(i + 1).kind == TokenKind::Number &&
                isSym(tokens.at(i + 2), ")")) {
                item.number = tokens.at(i + 1).text.toInt(); i += 3;
            }
            items.append(item);
        } else {
            ++i;
        }
    }
    return items;
}
}

MibCollectionIndex::ParsedFile MibCollectionIndex::parse(const QString &path, const QByteArray &bytes)
{
    ParsedFile file; file.path = path;
    const QList<Token> tokens = tokenize(QString::fromUtf8(bytes));
    const qsizetype n = tokens.size();
    ParsedModule *module = nullptr;

    for (qsizetype i = 0; i < n; ++i) {
        const Token &t = tokens.at(i);

        // "NAME DEFINITIONS ::= BEGIN" opens a module (a file may hold several).
        if (t.kind == TokenKind::Ident && i + 3 < n && isWord(tokens.at(i + 1), "DEFINITIONS")) {
            file.modules.append(ParsedModule{t.text, {}, {}});
            module = &file.modules.last();
            continue;
        }
        if (!module) continue;

        if (isWord(t, "IMPORTS")) {
            QStringList pending;
            ++i;
            while (i < n && !isSym(tokens.at(i), ";")) {
                const Token &u = tokens.at(i);
                if (isWord(u, "FROM") && i + 1 < n && tokens.at(i + 1).kind == TokenKind::Ident) {
                    const QString from = tokens.at(i + 1).text;
                    for (const QString &symbol : std::as_const(pending)) module->imports.insert(symbol, from);
                    pending.clear(); i += 2;
                    if (i < n && isSym(tokens.at(i), "{")) readOidBraces(tokens, i);   // a few old MIBs
                    continue;
                }
                if (u.kind == TokenKind::Ident) pending.append(u.text);
                ++i;
            }
            continue;
        }
        if (isWord(t, "MACRO")) {   // the SMI macro definitions themselves
            while (i < n && !isWord(tokens.at(i), "END")) ++i;
            continue;
        }
        if (t.kind != TokenKind::Ident || i + 1 >= n) continue;
        const Token &next = tokens.at(i + 1);

        // name OBJECT IDENTIFIER ::= { ... }
        if (isWord(next, "OBJECT") && i + 4 < n && isWord(tokens.at(i + 2), "IDENTIFIER") && isSym(tokens.at(i + 3), "::=") &&
            isSym(tokens.at(i + 4), "{")) {
            Definition def; def.name = t.text; def.kind = QStringLiteral("Node"); def.line = t.line;
            qsizetype j = i + 4; def.oid = readOidBraces(tokens, j);
            module->definitions.append(def); i = j - 1;
            continue;
        }
        // name <macro> ... ::= { ... }
        if (next.kind == TokenKind::Ident && macroKinds().contains(next.text) && t.text.at(0).isLower()) {
            Definition def; def.name = t.text; def.kind = macroKinds().value(next.text); def.line = t.line;
            qsizetype j = i + 2; int depth = 0;
            for (; j < n; ++j) {
                const Token &u = tokens.at(j);
                if (isSym(u, "{")) ++depth;
                else if (isSym(u, "}")) --depth;
                else if (depth == 0 && isWord(u, "ENTERPRISE") && j + 1 < n && tokens.at(j + 1).kind == TokenKind::Ident)
                    def.enterprise = tokens.at(j + 1).text;
                else if (depth == 0 && isSym(u, "::=")) break;
                else if (depth == 0 && u.kind == TokenKind::Ident && j + 1 < n && j > i + 2 &&
                         tokens.at(j + 1).kind == TokenKind::Ident && macroKinds().contains(tokens.at(j + 1).text) &&
                         u.text.at(0).isLower()) { j = n; break; }   // the next definition began: this one was cut short
            }
            if (j < n && isSym(tokens.at(j), "::=")) {
                if (j + 1 < n && isSym(tokens.at(j + 1), "{")) { qsizetype k = j + 1; def.oid = readOidBraces(tokens, k); j = k - 1; }
                else if (j + 1 < n && tokens.at(j + 1).kind == TokenKind::Number) def.trapNumber = tokens.at(j + 1).text.toInt();
                module->definitions.append(def);
                i = j;
            } else {
                module->definitions.append(def);   // found by name even though its OID could not be read
            }
            continue;
        }
        // Name ::= TEXTUAL-CONVENTION
        if (isSym(next, "::=") && i + 2 < n && isWord(tokens.at(i + 2), "TEXTUAL-CONVENTION")) {
            Definition def; def.name = t.text; def.kind = QStringLiteral("Type"); def.line = t.line;
            module->definitions.append(def);
        }
    }
    return file;
}

int MibCollectionIndex::moduleCount() const { return byModule.size(); }
int MibCollectionIndex::unresolvedCount() const
{
    int count = 0;
    for (const MibCollectionEntry &e : entries) if (e.oid.isEmpty() && e.kind != QLatin1String("Type")) ++count;
    return count;
}

void MibCollectionIndex::setFiles(const QList<ParsedFile> &files)
{
    entries.clear(); byLowerName.clear(); byOid.clear(); byOidAll.clear(); byModule.clear();
    filesIndexed = files.size();
    resolveAll(files);
    for (int i = 0; i < entries.size(); ++i) {
        byLowerName[entries.at(i).name.toLower()].append(i);
        byModule[entries.at(i).module].append(i);
        if (!entries.at(i).oid.isEmpty() && !byOid.contains(entries.at(i).oid)) byOid.insert(entries.at(i).oid, i);
        if (!entries.at(i).oid.isEmpty()) byOidAll.insert(entries.at(i).oid, i);
    }
}

namespace {
struct Slot { const MibCollectionIndex::Definition *def = nullptr; const MibCollectionIndex::ParsedModule *module = nullptr; const QString *file = nullptr; };

struct Resolver
{
    QHash<QString, QHash<QString, Slot>> byModule;      // module -> name -> definition
    QHash<QString, QList<Slot>> byName;                  // name -> every definition with that name
    QHash<QString, QString> cache;                       // "module|name" -> oid ("" = failed)
    QHash<QString, QString> failure;                     // "module|name" -> why
    QSet<QString> visiting;

    static QString key(const QString &module, const QString &name) { return module + QLatin1Char('|') + name; }

    QString resolveName(const QString &module, const QString &name, const QString &qualifier, QString *why)
    {
        static const QHash<QString, QString> roots{{"iso", "1"}, {"ccitt", "0"}, {"joint-iso-ccitt", "2"}};
        if (roots.contains(name)) return roots.value(name);
        QString owner;
        if (!qualifier.isEmpty() && byModule.contains(qualifier) && byModule[qualifier].contains(name)) owner = qualifier;
        else if (byModule.contains(module) && byModule[module].contains(name)) owner = module;
        else {
            // imported: find the module named in the importing module's IMPORTS
            const MibCollectionIndex::ParsedModule *current = moduleRecord(module);
            const QString imported = current ? current->imports.value(name) : QString();
            if (!imported.isEmpty() && byModule.contains(imported) && byModule[imported].contains(name)) owner = imported;
            else if (!byName.value(name).isEmpty()) {
                // not imported (sloppy MIB) or the provider is absent: any other module defining it,
                // chosen deterministically, as the original SnmpB tolerance would
                QStringList candidates;
                for (const Slot &slot : byName.value(name)) candidates << slot.module->name;
                std::sort(candidates.begin(), candidates.end());
                owner = candidates.first();
            }
        }
        if (owner.isEmpty()) { if (why) *why = QObject::tr("%1 is not defined in the collection").arg(name); return {}; }
        return resolveDefinition(owner, name, why);
    }

    QHash<QString, const MibCollectionIndex::ParsedModule *> moduleByName;
    const MibCollectionIndex::ParsedModule *moduleRecord(const QString &name) const { return moduleByName.value(name); }

    QString resolveDefinition(const QString &module, const QString &name, QString *why)
    {
        const QString k = key(module, name);
        if (cache.contains(k)) { if (cache[k].isEmpty() && why) *why = failure.value(k); return cache[k]; }
        if (visiting.contains(k)) { if (why) *why = QObject::tr("%1 is defined in terms of itself").arg(name); return {}; }
        const Slot slot = byModule[module].value(name);
        if (!slot.def) return {};
        visiting.insert(k);
        QString result, reason;
        const auto &def = *slot.def;
        if (def.trapNumber >= 0 && !def.enterprise.isEmpty()) {
            const QString base = resolveName(module, def.enterprise, {}, &reason);
            if (!base.isEmpty()) result = base + QStringLiteral(".0.") + QString::number(def.trapNumber);
        } else if (!def.oid.isEmpty()) {
            QString path;
            const auto &first = def.oid.first();
            if (first.name.isEmpty()) path = QString::number(first.number);
            else if (first.number >= 0 && def.oid.size() > 1) path = QString::number(first.number);   // iso(1) org(3) ...
            else path = resolveName(module, first.name, first.module, &reason);
            if (!path.isEmpty()) {
                bool ok = true;
                for (int i = 1; i < def.oid.size() && ok; ++i) {
                    if (def.oid.at(i).number < 0) { ok = false; reason = QObject::tr("a component of %1 has no number").arg(name); }
                    else path += QLatin1Char('.') + QString::number(def.oid.at(i).number);
                }
                if (ok) result = path;
            }
        } else if (def.kind != QLatin1String("Type")) {
            reason = QObject::tr("the definition of %1 has no readable OID").arg(name);
        }
        visiting.remove(k);
        cache.insert(k, result);
        if (result.isEmpty()) failure.insert(k, reason);
        if (result.isEmpty() && why) *why = reason;
        return result;
    }
};
}

void MibCollectionIndex::resolveAll(const QList<ParsedFile> &files)
{
    Resolver resolver;
    // The first file to define a module wins; a second copy of the same module is the Library's
    // duplicate-provider problem to report, not a second set of search hits.
    for (const ParsedFile &file : files)
        for (const ParsedModule &module : file.modules) {
            if (resolver.moduleByName.contains(module.name)) continue;
            resolver.moduleByName.insert(module.name, &module);
            for (const Definition &def : module.definitions) {
                Slot slot{&def, &module, &file.path};
                if (!resolver.byModule[module.name].contains(def.name)) resolver.byModule[module.name].insert(def.name, slot);
                resolver.byName[def.name].append(slot);
            }
        }
    for (const ParsedFile &file : files)
        for (const ParsedModule &module : file.modules) {
            if (resolver.moduleByName.value(module.name) != &module) continue;
            for (const Definition &def : module.definitions) {
                MibCollectionEntry entry;
                entry.name = def.name; entry.module = module.name; entry.kind = def.kind;
                entry.file = file.path; entry.line = def.line;
                if (def.kind != QLatin1String("Type")) {
                    QString why;
                    entry.oid = resolver.resolveDefinition(module.name, def.name, &why);
                    if (entry.oid.isEmpty()) entry.unresolved = why.isEmpty() ? QObject::tr("the OID could not be worked out") : why;
                }
                entries.append(entry);
            }
        }
}

QStringList MibCollectionIndex::modulesDefining(const QString &name) const
{
    QStringList modules;
    for (int i : byLowerName.value(name.toLower()))
        if (entries.at(i).name == name && !modules.contains(entries.at(i).module)) modules << entries.at(i).module;
    return modules;
}

QStringList MibCollectionIndex::modulesDefiningOid(const QString &oid) const
{
    QStringList modules;
    for (int i : byOidAll.values(oid)) {
        if (entries.at(i).kind == QLatin1String("Type")) continue;
        if (!modules.contains(entries.at(i).module)) modules << entries.at(i).module;
    }
    modules.sort();
    return modules;
}

QStringList MibCollectionIndex::modulesUnderOid(const QString &oid) const
{
    QStringList modules;
    const QString below = oid + QLatin1Char('.');
    for (const MibCollectionEntry &e : entries) {
        if (e.oid.isEmpty() || e.kind == QLatin1String("Type")) continue;
        if ((e.oid == oid || e.oid.startsWith(below)) && !modules.contains(e.module)) modules << e.module;
    }
    modules.sort();
    return modules;
}

MibCollectionSearchResult MibCollectionIndex::search(const QString &rawQuery, int limit) const
{
    MibCollectionSearchResult result;
    const QString query = rawQuery.trimmed();
    if (query.isEmpty()) return result;
    const auto finish = [&](QList<int> hits) {
        if (hits.size() > limit) { result.truncated = true; hits = hits.mid(0, limit); }
        for (int i : hits) result.entries.append(entries.at(i));
    };

    static const QRegularExpression numeric(QStringLiteral("^\\.?[0-9]+(\\.[0-9]+)*$"));
    if (numeric.match(query).hasMatch()) {
        const QString oid = query.startsWith(QLatin1Char('.')) ? query.mid(1) : query;
        QList<int> exact;
        for (int i = 0; i < entries.size(); ++i) if (entries.at(i).oid == oid) exact.append(i);
        if (!exact.isEmpty()) { result.note = QStringLiteral("exact"); finish(exact); return result; }
        // An instance such as ...10.1: the closest definition above it.
        QString prefix = oid;
        while (prefix.contains(QLatin1Char('.'))) {
            prefix = prefix.left(prefix.lastIndexOf(QLatin1Char('.')));
            QList<int> hits;
            for (int i = 0; i < entries.size(); ++i) if (entries.at(i).oid == prefix) hits.append(i);
            if (!hits.isEmpty()) { result.note = prefix; finish(hits); return result; }
        }
        return result;
    }

    const int sep = query.indexOf(QStringLiteral("::")) >= 0 ? query.indexOf(QStringLiteral("::")) : -1;
    QString modulePart, namePart = query;
    if (sep > 0) { modulePart = query.left(sep); namePart = query.mid(sep + 2); }
    else if (query.contains(QLatin1Char('.')) && !query.startsWith(QLatin1Char('.'))) {
        const int dot = query.indexOf(QLatin1Char('.')); modulePart = query.left(dot); namePart = query.mid(dot + 1);
    }
    if (!modulePart.isEmpty()) {
        QList<int> hits;
        for (int i : byModule.value(modulePart)) if (entries.at(i).name.compare(namePart, Qt::CaseInsensitive) == 0) hits.append(i);
        if (hits.isEmpty())   // module-qualified prefix search, e.g. "IF-MIB::ifIn"
            for (int i : byModule.value(modulePart)) if (entries.at(i).name.startsWith(namePart, Qt::CaseInsensitive)) hits.append(i);
        if (!hits.isEmpty()) { finish(hits); return result; }
        namePart = query;   // it was not "Module.name" after all
    }

    // Plain name: exact, then prefix, then anywhere in the name.
    const QString lower = namePart.toLower();
    QList<int> exact, prefix, inside;
    for (auto it = byLowerName.cbegin(); it != byLowerName.cend(); ++it) {
        if (!it.key().contains(lower)) continue;
        QList<int> &bucket = it.key() == lower ? exact : it.key().startsWith(lower) ? prefix : inside;
        bucket += it.value();
    }
    const auto order = [this](int a, int b) {
        const auto &x = entries.at(a), &y = entries.at(b);
        const int byName = x.name.compare(y.name, Qt::CaseInsensitive);
        return byName != 0 ? byName < 0 : x.module < y.module;
    };
    std::sort(exact.begin(), exact.end(), order); std::sort(prefix.begin(), prefix.end(), order); std::sort(inside.begin(), inside.end(), order);
    finish(exact + prefix + inside);
    return result;
}
