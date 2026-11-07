#include "queryresultspanel.h"
#include "csvformat.h"
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QEventLoop>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QTableWidget>
#include <QTimer>
#include <cstdio>
#include <functional>

namespace {
int failures = 0;
void check(bool ok, const char *why) { if (!ok) { ++failures; std::fprintf(stderr, "FAIL: %s\n", why); } }
void settle() { QEventLoop loop; QTimer::singleShot(120, &loop, &QEventLoop::quit); loop.exec(); }
QueryResultValue value(QString oid, QString raw, QString interpreted = {})
{
    QueryResultValue v; v.oid = oid; v.object = "IF-MIB::ifInOctets"; v.instance = oid.section('.', -1);
    v.raw = raw; v.interpreted = interpreted.isEmpty() ? raw : interpreted;
    v.syntax = "Counter32"; v.type = "Counter32"; v.received = "Counter32: " + raw; v.time = "12:34:56.789";
    return v;
}
// Exercise real popup actions through keyboard activation, using the offscreen Qt platform.
void chooseMenu(const std::function<void()> &open, const QString &name, bool enabled = true)
{
    QTimer::singleShot(0, [name, enabled]() {
        auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
        check(menu, "popup opens"); if (!menu) return;
        QAction *chosen = nullptr;
        for (auto *action : menu->actions()) if (action->text() == name) { chosen = action; break; }
        check(chosen && chosen->isEnabled() == enabled, "requested popup action has the expected state");
        if (!chosen || !enabled) { menu->close(); return; }
        menu->setActiveAction(chosen); QKeyEvent key(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
        QApplication::sendEvent(menu, &key);
    });
    open();
}
}
int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QueryResultsPanel panel; panel.resize(1200, 600); panel.show();
    auto *table = panel.table(); auto *filter = panel.findChild<QLineEdit *>("ResultsFilter");
    auto *display = panel.findChild<QComboBox *>("ResultsDisplayMode");
    auto *summary = panel.findChild<QLabel *>("ResultsSummary");
    check(panel.csvRecords().isEmpty(), "empty results have no export");
    int starts = 0;
    QObject::connect(&panel, &QueryResultsPanel::queryStarted, &panel, [&starts]() { ++starts; });
    panel.beginQuery({}, {});
    check(starts == 0, "clearing presentation does not request navigation");
    panel.beginQuery("Get ifInOctets", "1.3.6.1.2.1.2.2.1.10");
    check(summary->text().contains("Waiting for the first response"), "busy empty Results does not claim completion");
    auto warned = value("1.3.6.1.2.1.2.2.1.10.2", "4294967295", "4,294,967,295 octets");
    warned.warnings = "Unexpected received type";
    panel.appendValue(warned); panel.finishQuery();
    check(starts == 1, "only a named query start requests navigation, never value delivery or completion");
    check(!table->isColumnHidden(0) && !table->isColumnHidden(4) && !table->isColumnHidden(8) &&
          table->isColumnHidden(2) && table->isColumnHidden(5), "flat queries default to Object.Instance, Value, Type");
    check(table->item(0, 0)->text() == "IF-MIB::ifInOctets.2" && table->item(0, 4)->text() == warned.interpreted &&
          table->item(0, 8)->text() == "Counter32", "flat row contains instance, interpreted value and received type");
    check(panel.warningCount() == 1 && !table->item(0, 0)->icon().isNull() &&
          table->item(0, 4)->toolTip().contains(warned.raw) &&
          table->item(0, 4)->toolTip().contains(warned.warnings), "warnings have an icon and raw-value explanation");
    display->setCurrentIndex(1);
    check(table->item(0, 4)->text() == warned.raw && panel.oidAt(0, 4) == warned.oid, "Raw changes presentation without changing the watched OID");
    chooseMenu([&]() { emit table->horizontalHeader()->customContextMenuRequested(QPoint(10, 10)); }, "OID");
    check(!table->isColumnHidden(2), "header menu exposes optional metadata");
    check(summary->text().contains("1 row(s)") && summary->text().contains("1.3.6.1.2.1.2.2.1.10") &&
          !summary->text().contains("Running"), "summary identifies the query, count and elapsed time");
    // Snapshot ownership: changing a source value cannot mutate displayed data.
    warned.raw = "changed source";
    check(table->item(0, 4)->text() == "4294967295", "result metadata is copied, independent of its source");

    panel.beginQuery("Table ifTable", "1.3.6.1.2.1.2.2");
    auto high = value("1.3.6.1.2.1.2.2.1.10.10", "18446744073709551615"); high.type = "Counter64";
    auto lower = value("1.3.6.1.2.1.2.2.1.10.2", "18446744073709551614"); lower.type = "Counter64";
    auto text = value("1.3.6.1.2.1.2.2.1.2.10", "link,\"up\"\nsecond line"); text.type = "OctetString";
    auto mismatch = value("1.3.6.1.2.1.2.2.1.2.3", "3", "up(3)"); mismatch.warnings = "Outside declared range";
    panel.showTable({"ifInOctets", "ifDescr"}, {{"10", "ifIndex=10", {high, text}},
        {"2", "ifIndex=2", {lower, {}}}, {"3", "ifIndex=3", {{}, mismatch}}});
    panel.finishQuery();
    check(panel.isTableResult() && table->rowCount() == 3 && table->columnCount() == 4 &&
          table->horizontalHeaderItem(0)->text() == "Index" && table->isColumnHidden(3), "table results form a grid with one row per instance");
    check(panel.oidAt(0, 1) == high.oid && panel.oidAt(0, 0).isEmpty() && panel.oidAt(1, 2).isEmpty(),
          "only actual grid values carry watchable OIDs");
    check(panel.warningCount() == 1 && table->item(2, 1)->icon().isNull() && !table->item(2, 2)->icon().isNull(),
          "table warnings identify the affected cell");
    table->sortItems(0, Qt::AscendingOrder);
    check(table->item(0, 0)->text() == "ifIndex=2" && table->item(2, 0)->text() == "ifIndex=10", "Index sorts by numeric instance");
    table->sortItems(1, Qt::AscendingOrder);
    check(table->item(0, 1)->text() == lower.raw && table->item(1, 1)->text() == high.raw,
          "Counter64 sorting preserves values above floating-point precision");
    filter->setText(high.oid); settle();
    check(!table->isRowHidden(1) && table->isRowHidden(0) && panel.csvRecords().size() == 2 &&
          summary->text().contains("1 of 3"), "filter searches OID metadata and updates the visible count and CSV");
    table->sortItems(1, Qt::DescendingOrder); settle();
    check(panel.csvRecords().size() == 2 && panel.csvRecords()[1][0] == "ifIndex=10",
          "sorting filtered rows keeps the matching instance visible");
    table->sortItems(1, Qt::AscendingOrder); settle();
    filter->clear(); settle();
    const QString csv = FormatCsv(panel.csvRecords());
    check(csv.startsWith("Index,ifInOctets,ifDescr\r\n") && csv.contains("\"link,\"\"up\"\"\nsecond line\""),
          "table CSV exports visible columns in sorted order with quoted raw values");
    check(panel.csvRecords().last().at(1).isEmpty(), "unavailable cells export as empty values");
    int watches = 0; QString watched;
    QObject::connect(&panel, &QueryResultsPanel::watchRequested, [&](const QString &oid) { ++watches; watched = oid; });
    auto menuAt = [&](int row, int column) { const auto point = table->visualItemRect(table->item(row, column)).center();
        emit table->customContextMenuRequested(point); };
    chooseMenu([&]() { menuAt(1, 1); }, "Watch this value");
    check(watches == 1 && watched == high.oid, "grid Watch action captures the selected cell's full instance OID");
    chooseMenu([&]() { menuAt(2, 1); }, "Watch this value", false);
    chooseMenu([&]() { menuAt(0, 0); }, "Watch this value", false);
    chooseMenu([&]() { menuAt(1, 1); }, "Copy table as CSV");
    check(QApplication::clipboard()->text() == csv, "grid context menu copies the displayed table");
    display->setCurrentIndex(0);
    check(table->item(2, 2)->text() == mismatch.interpreted, "table display toggle restores interpreted values");
    panel.beginQuery("Walk", "1.3.6.1");
    check(!panel.isTableResult() && !table->isColumnHidden(2) && table->rowCount() == 0 && panel.warningCount() == 0,
          "table-to-flat transition resets data and restores flat column choices");
    for (const auto &raw : {QString("10"), QString("2"), QString("11x"), QString("-2"), QString("-10")})
        panel.appendValue(value("1.2.3.0", raw));
    table->sortItems(4, Qt::AscendingOrder);
    check(table->item(0, 4)->text() == "-10" && table->item(1, 4)->text() == "-2" &&
          table->item(2, 4)->text() == "2" && table->item(3, 4)->text() == "10" && table->item(4, 4)->text() == "11x",
          "mixed numeric and textual values have consistent sorting");
    filter->setText("no such value"); settle();
    check(panel.csvRecords().size() == 1 && summary->text().contains("0 of 5"), "empty filter results retain a useful header and count");
    panel.beginQuery("Get", "1.3.6.1"); panel.appendValue(value("1.3.6.1.1.0", "7")); panel.finishQuery();
    filter->setText("nothing matches this filter"); settle();
    check(summary->text().contains("No rows match the filter") && table->isRowHidden(0),
          "filtered-empty results explain how to restore the retained data");
    filter->clear(); settle();
    check(!table->isRowHidden(0) && !summary->text().contains("No rows match"),
          "clearing the filter restores results and removes empty guidance");
    return failures ? 1 : 0;
}
