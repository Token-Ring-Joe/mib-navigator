#include "appearancecolors.h"
#include "queryresultspanel.h"
#include "csvformat.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QFontDatabase>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QStyle>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace {
// Compare decimal components without floating point (Counter64 can exceed 2^53).
int naturalCompare(const QString &a, const QString &b)
{
    const auto numeric = [](const QString &text) {
        if (text.isEmpty()) return false;
        const auto parts = (text.startsWith('-') ? text.mid(1) : text).split('.');
        for (const auto &part : parts) {
            if (part.isEmpty()) return false;
            for (const QChar ch : part) if (ch < QLatin1Char('0') || ch > QLatin1Char('9')) return false;
        }
        return !text.startsWith('-') || parts.size() == 1;
    };
    const bool numericA = numeric(a), numericB = numeric(b);
    if (numericA != numericB) return numericA ? -1 : 1;
    if (!numericA) return QString::localeAwareCompare(a, b);
    const bool negativeA = a.startsWith('-'), negativeB = b.startsWith('-');
    if (negativeA != negativeB) return negativeA ? -1 : 1;
    const auto partsA = (negativeA ? a.mid(1) : a).split('.');
    const auto partsB = (negativeB ? b.mid(1) : b).split('.');
    int comparison = 0;
    for (int i = 0; i < qMin(partsA.size(), partsB.size()); ++i) {
        auto left = partsA[i], right = partsB[i];
        while (left.size() > 1 && left.startsWith('0')) left.remove(0, 1);
        while (right.size() > 1 && right.startsWith('0')) right.remove(0, 1);
        comparison = left.size() < right.size() ? -1 : left.size() > right.size() ? 1 : QString::compare(left, right);
        if (comparison) break;
    }
    if (!comparison) comparison = partsA.size() < partsB.size() ? -1 : partsA.size() > partsB.size() ? 1 : 0;
    return negativeA ? -comparison : comparison;
}
class ResultItem : public QTableWidgetItem
{
public:
    using QTableWidgetItem::QTableWidgetItem;
    bool operator<(const QTableWidgetItem &other) const override
    {
        const QString left = data(QueryResultsPanel::SortRole).toString();
        const QString right = other.data(QueryResultsPanel::SortRole).toString();
        return naturalCompare(left.isEmpty() ? text() : left, right.isEmpty() ? other.text() : right) < 0;
    }
};
}

QueryResultsPanel::QueryResultsPanel(QWidget *parent) : QWidget(parent)
{
    setObjectName("WorkbenchResultsPage");
    auto *layout = new QVBoxLayout(this); layout->setContentsMargins(10, 10, 10, 10);
    auto *tools = new QHBoxLayout; controls = tools;
    summary = new QLabel(tr("Right-click a MIB object to start a query."), this); summary->setObjectName("ResultsSummary");
    summary->setTextFormat(Qt::PlainText); summary->setWordWrap(true);
    summary->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred); layout->addWidget(summary);
    groupContext = new QLabel(this); groupContext->setObjectName("MacroRunContext");
    groupContext->setTextFormat(Qt::PlainText); groupContext->setWordWrap(true);
    groupContext->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred); groupContext->hide(); layout->addWidget(groupContext);
    groupSelector = new QComboBox(this); groupSelector->setObjectName("ResultsGroup");
    groupSelector->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    groupSelector->setMinimumContentsLength(16);
    groupSelector->setAccessibleName(tr("Result group")); groupSelector->hide(); layout->addWidget(groupSelector);
    groupStatus = new QLabel(this); groupStatus->setObjectName("ResultsGroupStatus");
    groupStatus->setTextFormat(Qt::PlainText); groupStatus->setWordWrap(true);
    groupStatus->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred); groupStatus->hide(); layout->addWidget(groupStatus);
    connect(groupSelector, &QComboBox::currentIndexChanged, this, [this] { showGroup(); });
    filter = new QLineEdit(this); filter->setObjectName("ResultsFilter");
    filter->setPlaceholderText(tr("Filter results…")); filter->setClearButtonEnabled(true); filter->setMinimumWidth(180);
    filter->setAccessibleName(tr("Filter results")); tools->addWidget(filter, 1);
    display = new QComboBox(this); display->setObjectName("ResultsDisplayMode");
    display->addItems({tr("Interpreted"), tr("Raw")}); display->setAccessibleName(tr("Value display"));
    tools->addWidget(display); layout->addLayout(tools);
    auto *status = new AppearanceColors::SemanticLabel({}, AppearanceColors::SemanticLabel::RequestStatus, this); status->setObjectName("StructuredQueryStatus");
    status->setTextFormat(Qt::PlainText); status->setWordWrap(true);
    status->hide(); layout->addWidget(status);
    results = new QTableWidget(this); results->setObjectName("StructuredQueryResults");
    results->setItemDelegate(new AppearanceColors::ItemDelegate(results));
    results->setEditTriggers(QAbstractItemView::NoEditTriggers);
    results->setSelectionBehavior(QAbstractItemView::SelectRows);
    results->setAlternatingRowColors(true); results->setWordWrap(false);
    results->verticalHeader()->hide(); results->setTextElideMode(Qt::ElideRight);
    results->setContextMenuPolicy(Qt::CustomContextMenu);
    results->horizontalHeader()->setContextMenuPolicy(Qt::CustomContextMenu);
    results->horizontalHeader()->setToolTip(tr("Right-click to choose which columns are shown."));
    layout->addWidget(results, 1);
    setProperty("workbenchFocusTarget", QStringLiteral("StructuredQueryResults"));
    connect(results, &QWidget::customContextMenuRequested, this, &QueryResultsPanel::showValueMenu);
    connect(results->horizontalHeader(), &QWidget::customContextMenuRequested, this, &QueryResultsPanel::showColumnMenu);
    auto *debounce = new QTimer(this); debounce->setSingleShot(true); debounce->setInterval(100);
    connect(filter, &QLineEdit::textChanged, debounce, [debounce]() { debounce->start(); });
    connect(debounce, &QTimer::timeout, this, &QueryResultsPanel::filterRows);
    connect(display, &QComboBox::currentIndexChanged, this, &QueryResultsPanel::updateDisplay);
    auto *progressTimer = new QTimer(this); progressTimer->setInterval(200);
    connect(progressTimer, &QTimer::timeout, this, [this]() {
        if (elapsed.isValid() && finishedMs < 0) updateSummary();
    });
    progressTimer->start(); configure(false);
}
void QueryResultsPanel::addControl(QWidget *widget) { controls->addWidget(widget); }

void QueryResultsPanel::configure(bool table, const QStringList &columns)
{
    ++layoutGeneration; tableMode = table;
    results->setSortingEnabled(false); results->clear(); results->setRowCount(0);
    // Keep the established hidden metadata columns for flat-query integrations.
    const QStringList names = table ? QStringList{tr("Index")} + columns + QStringList{tr("Instance")}
        : QStringList{tr("Object.Instance"), tr("Instance"), tr("OID"), tr("Syntax"), tr("Value"),
                      tr("Received"), tr("Warnings"), tr("Time"), tr("Type"), tr("Module")};
    results->setColumnCount(names.size()); results->setHorizontalHeaderLabels(names);
    const auto &visibility = table ? tableVisibility : flatVisibility;
    for (int column = 0; column < names.size(); ++column) {
        const bool defaultVisible = table ? column != names.size()-1 : column == 0 || column == 4 || column == 8 || column == 9;
        results->setColumnHidden(column, !visibility.value(names[column], defaultVisible));
        results->horizontalHeader()->setSectionResizeMode(column, QHeaderView::Interactive);
        results->setColumnWidth(column, table ? (column == 0 ? 280 : 190) : column == 0 ? 220 : column == 9 ? 150 : 190);
    }
    results->horizontalHeader()->setStretchLastSection(false);
    // Columns keep their logical numbers (other code reads them by number). Only the
    // display order changes: in flat results the Module column is shown first.
    auto *header = results->horizontalHeader();
    for (int visual = 0; visual < names.size(); ++visual)
        if (header->visualIndex(visual) != visual) header->moveSection(header->visualIndex(visual), visual);
    if (!table && header->visualIndex(9) != 0) header->moveSection(header->visualIndex(9), 0);
    // No automatic reordering: the engine supplies numeric instance/arrival order.
    results->horizontalHeader()->setSortIndicator(-1, Qt::AscendingOrder);
    results->setSortingEnabled(true);
}

void QueryResultsPanel::beginQuery(const QString &object, const QString &oid)
{
    groups.clear(); groupSelector->hide(); groupContext->hide(); groupStatus->hide();
    queryObject = object; queryOid = oid; finishedMs = -1; elapsed.start();
    configure(false); filter->clear(); warnings = 0; emit warningsChanged(0);
    if (auto *status = findChild<QLabel *>("StructuredQueryStatus")) { status->clear(); status->hide(); }
    updateSummary();
    if (!object.isEmpty()) emit queryStarted();
}

QTableWidgetItem *QueryResultsPanel::valueItem(const QueryResultValue &value, const QString &text, bool warn)
{
    auto *item = new ResultItem(text);
    item->setData(OidRole, value.problem.isEmpty() ? value.oid : QString());
    item->setData(IdentityRole, value.id); item->setData(ProblemRole, value.problem);
    item->setData(InterpretedRole, value.interpreted);
    item->setData(RawRole, value.raw); item->setData(WarningRole, value.warnings);
    item->setData(SearchRole, QStringList{value.object, value.instance, value.oid, value.syntax, value.type,
                                         value.interpreted, value.raw, value.received, value.warnings, value.problem}.join(' '));
    item->setToolTip(tr("OID: %1\nSyntax: %2\nReceived type: %3\nRaw value: %4")
        .arg(value.oid, value.syntax, value.type, value.raw) +
        (value.warnings.isEmpty() ? QString() : QStringLiteral("\n") + value.warnings) +
        (value.problem.isEmpty() ? QString() : QStringLiteral("\n") + value.problem));
    if (!value.problem.isEmpty()) {
        item->setData(AppearanceColors::ToneRole, AppearanceColors::Error);
        if (warn) item->setIcon(style()->standardIcon(QStyle::SP_MessageBoxCritical));
    } else if (!value.warnings.isEmpty()) {
        item->setData(AppearanceColors::ToneRole, AppearanceColors::Warning);
        if (warn) item->setIcon(style()->standardIcon(QStyle::SP_MessageBoxWarning));
    }
    return item;
}

void QueryResultsPanel::appendValue(const QueryResultValue &value)
{
    if (tableMode) return;
    const bool sorting = results->isSortingEnabled(); results->setSortingEnabled(false);
    const int row = results->rowCount(); results->insertRow(row);
    // "IF-MIB::ifInOctets" is shown as Module "IF-MIB" and Object "ifInOctets.2". The full name stays
    // in the row's search text and tooltip, so filtering by either part still works.
    const qsizetype separator = value.object.indexOf(QStringLiteral("::"));
    const QString module = separator > 0 ? value.object.left(separator) : QString();
    const QString name = separator > 0 ? value.object.mid(separator + 2) : value.object;
    const QString object = name + (value.instance.isEmpty() ? QString() : QStringLiteral(".") + value.instance);
    const QStringList cells{object, value.instance, value.oid, value.syntax,
        !value.problem.isEmpty() ? value.problem : display->currentIndex() == 0 ? value.interpreted : value.raw,
        value.received, value.warnings, value.time, value.type, module};
    for (int column = 0; column < cells.size(); ++column) {
        auto *item = valueItem(value, cells[column], column == 0);
        if (column == 1 || column == 2 || column == 4 || column == 5)
            item->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        if (column == 4) {
            item->setData(SortRole, value.raw);
            if (value.problem.isEmpty()) item->setData(AppearanceColors::LinkInkRole, true);
        }
        results->setItem(row, column, item);
    }
    results->setRowHidden(row, !matchesFilter(row));
    if (sorting) results->setSortingEnabled(true);
    if (!value.warnings.isEmpty()) { ++warnings; emit warningsChanged(warnings); }
    // Coalesce summaries for streamed Walks; no row scan per received object.
    if (row == 0) updateSummary();
}

void QueryResultsPanel::showTable(const QStringList &columns, const QList<QueryResultTableRow> &rows)
{
    configure(true, columns); results->setSortingEnabled(false); warnings = 0;
    results->setRowCount(rows.size());
    for (int row = 0; row < rows.size(); ++row) {
        const auto &data = rows[row];
        auto *index = new ResultItem(data.index.isEmpty() ? data.instance : data.index);
        index->setData(SortRole, data.instance); index->setToolTip(tr("Instance: %1").arg(data.instance));
        results->setItem(row, 0, index);
        auto *instance = new ResultItem(data.instance); instance->setData(SortRole, data.instance);
        results->setItem(row, columns.size()+1, instance);
        for (int column = 0; column < columns.size(); ++column) {
            const QueryResultValue value = column < data.cells.size() ? data.cells[column] : QueryResultValue{};
            QTableWidgetItem *item;
            if (value.oid.isEmpty()) {
                item = new ResultItem(tr("—")); item->setData(MissingRole, true);
                item->setToolTip(tr("This instance has no value for this column."));
            } else {
                item = valueItem(value, display->currentIndex() == 0 ? value.interpreted : value.raw, true);
                item->setData(SortRole, value.raw);
                if (!value.warnings.isEmpty()) ++warnings;
            }
            results->setItem(row, column+1, item);
        }
    }
    results->setSortingEnabled(true); filterRows(); emit warningsChanged(warnings);
}

void QueryResultsPanel::finishQuery()
{
    finishedMs = elapsed.isValid() ? elapsed.elapsed() : 0;
    filterRows();
}

void QueryResultsPanel::beginGroups(const QString &name, const QString &captured)
{
    beginQuery(name, {});
    { const QSignalBlocker blocker(groupSelector); groupSelector->clear(); }
    groupContext->setText(captured); groupContext->setToolTip(captured); groupContext->show();
}
void QueryResultsPanel::updateGroups(const QList<QueryResultGroup> &incoming, const QString &status, bool failed)
{
    const QString selected = groupSelector->currentData().toString();
    groups = incoming;
    bool same = groupSelector->count() == groups.size();
    for (int i = 0; same && i < groups.size(); ++i)
        same = groupSelector->itemData(i).toString() == groups[i].id && groupSelector->itemText(i) == groups[i].name;
    if (!same) {
        const QSignalBlocker blocker(groupSelector);
        groupSelector->clear();
        for (const auto &group : groups) {
            groupSelector->addItem(group.name, group.id);
            groupSelector->setItemData(groupSelector->count() - 1, group.name, Qt::ToolTipRole);
        }
        const int index = groupSelector->findData(selected);
        groupSelector->setCurrentIndex(index >= 0 ? index : 0);
    }
    groupSelector->setVisible(!groups.isEmpty());
    if (auto *label = findChild<QLabel *>("StructuredQueryStatus")) {
        label->setProperty("failed", failed); label->setText(status); label->show();
    }
    showGroup();
}
void QueryResultsPanel::showGroup()
{
    const int group = groupSelector->currentIndex();
    if (group < 0 || group >= groups.size()) return;
    const QString rowId = results->currentItem() ? results->currentItem()->data(IdentityRole).toString() : QString();
    const int column = results->currentColumn();
    const int scroll = results->verticalScrollBar()->value();
    const int sort = results->horizontalHeader()->sortIndicatorSection();
    const auto direction = results->horizontalHeader()->sortIndicatorOrder();
    const bool sorting = results->isSortingEnabled();
    {
        const QSignalBlocker blocker(this);
        ++layoutGeneration; results->setSortingEnabled(false); results->setRowCount(0); warnings = 0;
        // Switching between a table group and a flat group changes the column layout.
        if (groups[group].isTable) showTable(groups[group].columns, groups[group].rows);
        else {
            if (tableMode) configure(false);
            for (const auto &value : groups[group].values) appendValue(value);
        }
        results->setSortingEnabled(sorting);
        if (sort >= 0) results->sortItems(sort, direction);
        for (int row = 0; row < results->rowCount(); ++row)
            if (!rowId.isEmpty() && results->item(row, 0)->data(IdentityRole).toString() == rowId)
                results->setCurrentCell(row, qMax(0, column));
        results->verticalScrollBar()->setValue(scroll);
        warnings = 0;
        for (const auto &item : groups) {
            for (const auto &value : item.values) warnings += !value.warnings.isEmpty();
            for (const auto &row : item.rows) for (const auto &cell : row.cells) warnings += !cell.warnings.isEmpty();
        }
    }
    groupStatus->setText(groups[group].status); groupStatus->setVisible(!groups[group].status.isEmpty());
    filterRows(); emit warningsChanged(warnings);
}

void QueryResultsPanel::updateDisplay()
{
    const bool sorting = results->isSortingEnabled(); results->setSortingEnabled(false);
    for (int row = 0; row < results->rowCount(); ++row)
        for (int column = tableMode ? 1 : 4; column < (tableMode ? results->columnCount()-1 : 5); ++column)
            if (auto *item = results->item(row, column); item && !item->data(MissingRole).toBool() && item->data(ProblemRole).toString().isEmpty())
                item->setText(item->data(display->currentIndex() == 0 ? InterpretedRole : RawRole).toString());
    results->setSortingEnabled(sorting); filterRows();
}

bool QueryResultsPanel::matchesFilter(int row) const
{
    const auto text = filter->text().trimmed();
    if (text.isEmpty()) return true;
    for (int column = 0; column < results->columnCount(); ++column)
        if (auto *item = results->item(row, column); item &&
            (item->text().contains(text, Qt::CaseInsensitive) || item->data(SearchRole).toString().contains(text, Qt::CaseInsensitive)))
            return true;
    return false;
}
void QueryResultsPanel::filterRows()
{
    for (int row = 0; row < results->rowCount(); ++row) results->setRowHidden(row, !matchesFilter(row));
    updateSummary();
}
void QueryResultsPanel::updateSummary()
{
    if (!elapsed.isValid()) { summary->setText(tr("Right-click a MIB object to start a query.")); return; }
    int visible = results->rowCount();
    if (!filter->text().trimmed().isEmpty()) {
        visible = 0; for (int row = 0; row < results->rowCount(); ++row) visible += !results->isRowHidden(row);
    }
    const QString rows = visible == results->rowCount() ? tr("%1 row(s)").arg(visible)
        : tr("%1 of %2 row(s)").arg(visible).arg(results->rowCount());
    QStringList parts;
    if (!queryObject.isEmpty()) parts << queryObject;
    if (!queryOid.isEmpty()) parts << queryOid;
    parts << rows << (finishedMs < 0 ? tr("Running…") : tr("%1 s").arg(finishedMs/1000.0, 0, 'f', 2));
    if (visible == 0 && results->rowCount() > 0)
        parts << tr("No rows match the filter. Clear the filter to see all results.");
    else if (results->rowCount() == 0 && finishedMs < 0)
        parts << tr("Waiting for the first response…");
    summary->setText(parts.join(QStringLiteral("  |  ")));
}
QString QueryResultsPanel::oidAt(int row, int column) const
{
    auto *item = results->item(row, column); return item ? item->data(OidRole).toString() : QString();
}
QList<QStringList> QueryResultsPanel::csvRecords() const
{
    QList<QStringList> records; if (!results->rowCount()) return records;
    // Export the columns in the order they are displayed.
    QList<int> shown;
    for (int visual = 0; visual < results->columnCount(); ++visual) {
        const int column = results->horizontalHeader()->logicalIndex(visual);
        if (column >= 0 && !results->isColumnHidden(column)) shown << column;
    }
    QStringList header;
    for (const int column : shown) header << results->horizontalHeaderItem(column)->text();
    records << header;
    for (int row = 0; row < results->rowCount(); ++row) {
        if (results->isRowHidden(row)) continue;
        QStringList record;
        for (const int column : shown) {
            const auto *item = results->item(row, column);
            record << (item && !item->data(MissingRole).toBool() ? item->text() : QString());
        }
        records << record;
    }
    return records;
}
void QueryResultsPanel::showColumnMenu(const QPoint &position)
{
    QMenu menu(results); const int generation = layoutGeneration;
    for (int column = 0; column < results->columnCount(); ++column) {
        auto *action = menu.addAction(results->horizontalHeaderItem(column)->text());
        action->setCheckable(true); action->setChecked(!results->isColumnHidden(column)); action->setData(column);
    }
    if (auto *chosen = menu.exec(results->horizontalHeader()->mapToGlobal(position)); chosen && generation == layoutGeneration) {
        const int column = chosen->data().toInt();
        int visible = 0; for (int i = 0; i < results->columnCount(); ++i) visible += !results->isColumnHidden(i);
        if (!chosen->isChecked() && visible == 1) return;
        results->setColumnHidden(column, !chosen->isChecked());
        (tableMode ? tableVisibility : flatVisibility).insert(chosen->text(), chosen->isChecked());
    }
}
void QueryResultsPanel::showValueMenu(const QPoint &position)
{
    const int generation = layoutGeneration;
    const auto *item = results->itemAt(position);
    const QString oid = item ? oidAt(item->row(), item->column()) : QString();
    QMenu menu(results);
    auto *watch = menu.addAction(tr("Watch this value")); watch->setEnabled(!oid.isEmpty());
    menu.addSeparator();
    auto *copy = menu.addAction(tableMode ? tr("Copy table as CSV") : tr("Copy results as CSV"));
    copy->setEnabled(csvRecords().size() > 1);
    auto *chosen = menu.exec(results->viewport()->mapToGlobal(position));
    if (generation != layoutGeneration) return;
    if (chosen == watch) emit watchRequested(oid);
    else if (chosen == copy) QApplication::clipboard()->setText(FormatCsv(csvRecords()));
}
