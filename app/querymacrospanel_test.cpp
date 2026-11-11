#include "querymacrospanel.h"
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QFile>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLockFile>
#include <QListWidget>
#include <QMainWindow>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <cstdio>
namespace {
int failures = 0;
void check(bool ok, const char *why) { if (!ok) { std::fprintf(stderr, "FAIL: %s\n", why); ++failures; } }
void click(QWidget &w, const char *name) { auto *b = w.findChild<QPushButton *>(name); check(b && b->isEnabled(), name); if (b) b->click(); }
void text(QLineEdit *field, const QString &value) { field->selectAll(); QKeyEvent key(QEvent::KeyPress, 0, Qt::NoModifier, value); QApplication::sendEvent(field, &key); }
QDialog *dialog() { return qobject_cast<QDialog *>(QApplication::activeModalWidget()); }
void answer(QMessageBox::StandardButton value) { auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); check(box != nullptr, "close/reload confirmation exists"); if (box) box->button(value)->click(); }
}
int main(int argc, char **argv)
{
    QApplication app(argc, argv); QTemporaryDir dir; const QString path = dir.filePath("query-macros-v1.json");
    auto service = std::make_unique<QueryMacroService>(path);
    std::optional<QueryMacroEntry> choice; int pickerCalls = 0; std::function<void()> duringPick;
    QMainWindow window;
    auto *panel = new QueryMacrosPanel(service.get(), [&](QWidget *, QString *) { ++pickerCalls; if (duringPick) duringPick(); return choice; }, {}, &window);
    window.setCentralWidget(panel); window.resize(1000, 650); window.show(); QApplication::processEvents();
    check(!panel->findChild<QPushButton *>("QueryMacroSave")->isEnabled(), "empty editor has no pending save");
    click(*panel, "QueryMacroNew"); const QString first = panel->selectedMacroId();
    text(panel->findChild<QLineEdit *>("QueryMacroName"), "Offline essentials");
    check(service->definition(first)->name == "Offline essentials" && !QFile::exists(path), "real name edit stays in working service until Save");
    click(*panel, "QueryMacroAddObject"); check(pickerCalls == 1 && service->definition(first)->entries.isEmpty(), "canceled object chooser changes no definitions");
    QueryMacroEntry scalar; scalar.id = QueryMacroNewId(); scalar.kind = QueryMacroKind::Scalar; scalar.oid = "1.3.6.1.2.1.1.1"; scalar.label = "SNMPv2-MIB::sysDescr"; choice = scalar;
    click(*panel, "QueryMacroAddObject");
    QTimer::singleShot(0, &window, [&] {
        auto *d = dialog(); check(d != nullptr, "exact-instance dialog opens"); if (!d) return;
        d->findChild<QLineEdit *>("QueryMacroEntryOid")->setText("1.3.bad"); click(*d, "QueryMacroEntryApply");
        check(!d->findChild<QLabel *>("QueryMacroEntryError")->text().isEmpty(), "invalid exact OID is explained inside editor");
        d->findChild<QLineEdit *>("QueryMacroEntryOid")->setText(".1.3.6.1.4.1.64994.1.0");
        d->findChild<QLineEdit *>("QueryMacroEntryLabel")->setText("Unknown numeric instance"); click(*d, "QueryMacroEntryApply");
    });
    click(*panel, "QueryMacroAddExact");
    int runs = 0; QueryMacroDefinition requested;
    QObject::connect(panel, &QueryMacrosPanel::runRequested, [&](const QueryMacroDefinition &definition) { ++runs; requested = definition; });
    click(*panel, "QueryMacroRun");
    check(runs == 1 && requested.id == first && requested.entries.size() == 2 && service->dirty(),
        "explicit Run submits a value copy of unsaved working edits without forcing a save");
    panel->setQueryBusy(true);
    check(!panel->findChild<QPushButton *>("QueryMacroRun")->isEnabled() && panel->findChild<QPushButton *>("QueryMacroAddObject")->isEnabled(),
        "another foreground query disables Run while offline editing remains available");
    panel->setQueryBusy(false);
    check(service->definition(first)->entries.size() == 2 && service->definition(first)->entries[1].oid == "1.3.6.1.4.1.64994.1.0", "exact instance created and canonicalized through real editor");
    auto *rows = panel->findChild<QTableWidget *>("QueryMacroEntries"); rows->selectRow(1); click(*panel, "QueryMacroEntryUp");
    check(service->definition(first)->entries.first().kind == QueryMacroKind::ExactInstance && rows->currentRow() == 0, "entry order and ID selection survive row rebuild");
    click(*panel, "QueryMacroDuplicateEntry"); check(service->definition(first)->entries.size() == 3, "entry duplicate creates another stable ID");
    click(*panel, "QueryMacroDuplicate"); const QString copied = panel->selectedMacroId(); click(*panel, "QueryMacroUp");
    check(copied != first && service->definitions().first().id == copied, "macro duplication and reordering through real controls");
    panel->selectMacro(first);
    auto table = scalar; table.id = QueryMacroNewId(); table.kind = QueryMacroKind::Table; table.oid = "1.3.6.1.2.1.2.2.1"; table.columns = {"1.3.6.1.2.1.2.2.1.2"}; choice = table;
    duringPick = [&] { panel->selectMacro(copied); };
    click(*panel, "QueryMacroAddObject");
    check(panel->selectedMacroId() == first, "object picker retains its originating macro across selection changes"); duringPick = {}; choice->columns = {"1.3.6.1.2.1.2.2.1.8"}; click(*panel, "QueryMacroAddObject");
    check(service->definition(first)->entries.size() == 4 && service->definition(first)->entries.last().columns.size() == 2, "same-row picks merge columns without duplicate table groups");
    check(panel->findChild<QLabel *>("QueryMacroStatus")->text().contains("Added 1 column"), "merging a new column says so");
    // Adding a table the macro already holds, with the same columns, must say so instead of looking like a failure.
    click(*panel, "QueryMacroAddObject");
    check(service->definition(first)->entries.size() == 4 && service->definition(first)->entries.last().columns.size() == 2 &&
          panel->findChild<QLabel *>("QueryMacroStatus")->text().contains("already in") &&
          panel->findChild<QLabel *>("QueryMacroStatus")->text().contains("Nothing was added"),
          "adding a table that is already in the macro explains that nothing changed");
    // A different table is a new entry and the panel says so.
    {
        auto other = table; other.id = QueryMacroNewId(); other.oid = "1.3.6.1.2.1.4.20.1"; other.columns = {"1.3.6.1.2.1.4.20.1.2"}; other.label = "IP-MIB::ipAddrEntry";
        const auto savedChoice = choice; choice = other; click(*panel, "QueryMacroAddObject");
        check(service->definition(first)->entries.size() == 5 &&
              panel->findChild<QLabel *>("QueryMacroStatus")->text().contains("as a new entry"),
              "a second, different table becomes its own entry and the panel says so");
        service->removeEntry(first, other.id); choice = savedChoice;
    }
    rows->selectRow(3);
    QTimer::singleShot(0, &window, [&] {
        auto *d = dialog(); check(d != nullptr, "table editor opens"); if (!d) return;
        d->findChild<QComboBox *>("QueryMacroInstancePolicy")->setCurrentIndex(int(QueryMacroInstancePolicy::Fixed));
        d->findChild<QPlainTextEdit *>("QueryMacroFixedInstances")->setPlainText("1\n4.101.116.104.49"); click(*d, "QueryMacroEntryApply");
    });
    click(*panel, "QueryMacroEditEntry");
    check(service->definition(first)->entries.last().fixedInstances == QStringList{"1", "4.101.116.104.49"}, "fixed compound suffix policy edits offline");
    const auto tableBeforeConflict = service->definition(first)->entries.last();
    QTimer::singleShot(0, &window, [&] {
        service->removeEntry(first, tableBeforeConflict.id);
        if (auto *d = dialog()) click(*d, "QueryMacroEntryApply");
    });
    click(*panel, "QueryMacroEditEntry");
    check(service->definition(first)->entries.size() == 3 &&
          panel->findChild<QLabel *>("QueryMacroStatus")->text().contains("removed"),
          "accepting a stale modal editor cannot resurrect a removed entry");
    service->putEntry(first, tableBeforeConflict);
    click(*panel, "QueryMacroSave"); check(!service->dirty(), "Save commits working collection");
    QueryMacroService restart(path); check(restart.definitions() == service->definitions(), "real UI workflow restarts with exact definitions");
    click(*panel, "QueryMacroDelete"); check(service->dirty(), "Delete remains undoable until Save");
    QTimer::singleShot(0, &window, [] { answer(QMessageBox::Cancel); }); click(*panel, "QueryMacroReload"); check(service->dirty(), "Reload cancel keeps working deletion");
    QTimer::singleShot(0, &window, [] { answer(QMessageBox::Discard); }); click(*panel, "QueryMacroReload"); check(!service->dirty() && service->definition(first), "explicit reload discards only unsaved edits");
    text(panel->findChild<QLineEdit *>("QueryMacroName"), "Pending close");
    QTimer::singleShot(0, &window, [] { answer(QMessageBox::Cancel); }); check(!window.close() && service->dirty(), "window close cancel preserves working edits");
    {
        QLockFile lock(path + ".lock"); check(lock.tryLock(), "hold lock for close-save failure");
        QTimer::singleShot(0, &window, [] { answer(QMessageBox::Save); });
        check(!window.close() && service->dirty() && !panel->findChild<QLabel *>("QueryMacroStatus")->text().isEmpty(),
              "failed close-save keeps the window, edits and error available");
    }
    QTimer::singleShot(0, &window, [] { answer(QMessageBox::Save); }); check(window.close() && !service->dirty(), "window close Save persists before closing");
    QueryMacroService afterClose(path); check(afterClose.definitions() == service->definitions(), "close/save survives restart");
    {
        // A maximum-size macro must stay responsive while its name is typed.
        QueryMacroService largeService(dir.filePath("large-query-macros-v1.json"));
        for (int m = 0; m < 20; ++m) largeService.create();
        const QString large = largeService.definitions().first().id;
        for (int i = 0; i < QueryMacroMaximumEntries; ++i) {
            QueryMacroEntry e; e.id = QueryMacroNewId(); e.oid = QString("1.3.6.1.4.1.64993.1.%1.0").arg(i); e.label = QString("Entry %1").arg(i);
            largeService.putEntry(large, e);
        }
        check(largeService.save(), "maximum-size macro saves");
        QueryMacroService largeRestart(largeService.path()); // the editor opens on a restarted store
        QMainWindow largeWindow;
        auto *largePanel = new QueryMacrosPanel(&largeRestart, {}, {}, &largeWindow);
        largeWindow.setCentralWidget(largePanel); largeWindow.resize(1000, 650); largeWindow.show();
        largePanel->selectMacro(large); QApplication::processEvents();
        auto *largeRows = largePanel->findChild<QTableWidget *>("QueryMacroEntries"); largeRows->selectRow(700);
        auto *largeName = largePanel->findChild<QLineEdit *>("QueryMacroName"); largeName->setFocus(); largeName->end(false);
        QElapsedTimer typing; typing.start();
        for (const QChar c : QString("Typed name")) {
            QKeyEvent key(QEvent::KeyPress, 0, Qt::NoModifier, QString(c)); QApplication::sendEvent(largeName, &key);
        }
        const qint64 elapsed = typing.elapsed();
        std::fprintf(stderr, "INFO: 10 keystrokes on a %d-entry macro took %lld ms\n", QueryMacroMaximumEntries, static_cast<long long>(elapsed));
        check(largeRestart.definition(large)->name.endsWith("Typed name") && largeName->text().endsWith("Typed name"), "typing a large macro name keeps every keystroke");
        check(largeRows->rowCount() == QueryMacroMaximumEntries && largeRows->currentRow() == 700, "typing a name keeps the selected entry");
        check(largePanel->selectedMacroId() == large && largePanel->findChild<QListWidget *>("QueryMacroList")->count() == 20, "typing a name keeps macro selection and list");
        check(elapsed < 5000, "renaming a maximum-size macro stays responsive");
    }
    {
        // A preserved (unreadable/newer) store explains why a tree "Add" did nothing.
        const QString corrupt = dir.filePath("corrupt-query-macros-v1.json"); const QByteArray bytes("{\"version\": 99, \"macros\": []}");
        { QFile f(corrupt); check(f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size(), "write newer-version fixture"); }
        QueryMacroService preserved(corrupt); QMainWindow preservedWindow;
        auto *preservedPanel = new QueryMacrosPanel(&preserved, {}, {}, &preservedWindow);
        QueryMacroEntry e; e.id = QueryMacroNewId(); e.oid = "1.3.6.1.2.1.1.5.0";
        check(!preservedPanel->addObject(e) && preservedPanel->findChild<QLabel *>("QueryMacroStatus")->text().contains("not added") &&
              !preservedPanel->findChild<QPushButton *>("QueryMacroNew")->isEnabled(), "read-only store refuses additions with an explanation");
        QFile f(corrupt); check(f.open(QIODevice::ReadOnly) && f.readAll() == bytes, "refused addition leaves the preserved file untouched");
    }
    service.reset(); check(!panel->findChild<QPushButton *>("QueryMacroNew")->isEnabled(), "destroyed service safely disables remaining widget");
    return failures ? 1 : 0;
}
