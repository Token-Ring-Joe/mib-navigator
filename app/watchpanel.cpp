#include "appearancecolors.h"
#include "watchpanel.h"

#include <QAction>
#include <QBrush>
#include <QColor>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QInputDialog>
#include <QMenu>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {
enum Column { Target, Credential, Object, Value, Change, Rate, Received, Notes, Every, Status, Updated, ColumnCount };
}

WatchPanel::WatchPanel(WatchService *watchService,
                       std::function<QString(const QString &)> label, QWidget *parent)
    : QWidget(parent), service(watchService), credentialLabel(std::move(label))
{
    setObjectName(QStringLiteral("WorkbenchWatchPage"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);

    auto *hint = new QLabel(tr("No watches yet. Right-click an object in the Query tree or a value in Results and choose Watch."), this);
    hint->setObjectName("WatchHint");
    hint->setWordWrap(true);
    layout->addWidget(hint);
    auto *saveStatus = new QLabel(this);
    saveStatus->setObjectName("WatchPersistenceStatus");
    saveStatus->setWordWrap(true);
    saveStatus->setTextFormat(Qt::PlainText);
    saveStatus->setText(service->persistenceError());
    saveStatus->setVisible(!service->persistenceError().isEmpty());
    layout->addWidget(saveStatus);
    connect(service, &WatchService::persistenceErrorChanged, saveStatus, [saveStatus](const QString &error) {
        saveStatus->setText(error); saveStatus->setVisible(!error.isEmpty());
    });
    auto *bar = new QHBoxLayout;
    bar->addWidget(new QLabel(tr("New watches repeat every"), this));
    interval = new QSpinBox(this);
    interval->setObjectName(QStringLiteral("WatchInterval"));
    interval->setRange(1, 3600); interval->setValue(5); interval->setSuffix(tr(" s"));
    bar->addWidget(interval);
    bar->addStretch();
    editIntervalButton = new QPushButton(tr("Interval…"), this);
    editIntervalButton->setObjectName("WatchEditInterval");
    editIntervalButton->setAccessibleName(tr("Change selected watch interval"));
    editIntervalButton->setEnabled(false);
    pauseButton = new QPushButton(tr("Pause"), this);
    removeButton = new QPushButton(tr("Remove"), this);
    clearButton = new QPushButton(tr("Remove all"), this);
    pauseButton->setObjectName(QStringLiteral("WatchPause"));
    removeButton->setObjectName(QStringLiteral("WatchRemove"));
    clearButton->setObjectName(QStringLiteral("WatchClear"));
    for (QPushButton *button : {pauseButton, removeButton}) button->setEnabled(false);
    clearButton->setEnabled(false);
    layout->addLayout(bar);
    auto *actions = new QHBoxLayout;
    actions->addStretch();
    actions->addWidget(editIntervalButton); actions->addWidget(pauseButton);
    actions->addWidget(removeButton); actions->addWidget(clearButton);
    layout->addLayout(actions);

    table = new QTableWidget(0, ColumnCount, this);
    table->setObjectName(QStringLiteral("WatchTable"));
    table->setItemDelegate(new AppearanceColors::ItemDelegate(table));
    table->setHorizontalHeaderLabels({tr("Target"), tr("Credential"), tr("Object"), tr("Value"), tr("Change"),
                                      tr("Rate"), tr("Received"), tr("Notes"), tr("Every"), tr("Status"), tr("Updated")});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setAlternatingRowColors(true);
    table->setWordWrap(false);
    table->verticalHeader()->setVisible(false);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(Notes, QHeaderView::Stretch);
    table->setContextMenuPolicy(Qt::CustomContextMenu);
    layout->addWidget(table, 1);
    setProperty("workbenchFocusTarget", QStringLiteral("WatchTable"));

    const auto addRow = [this, hint](quint64 id) {
        const WatchRow *data = service->row(id);
        if (!data) return;
        const int row = table->rowCount();
        table->insertRow(row);
        for (int column = 0; column < ColumnCount; ++column) table->setItem(row, column, new QTableWidgetItem);
        table->item(row, Target)->setData(Qt::UserRole, QVariant::fromValue(id));
        fill(row, *data);
        clearButton->setEnabled(true);
        hint->setText(tr("Watches are saved automatically and return paused after restart. Each row keeps its target, credential and context; select Resume to poll."));
    };
    connect(service, &WatchService::rowAdded, this, addRow);
    for (quint64 id : service->ids()) addRow(id);
    connect(editIntervalButton, &QPushButton::clicked, this, [this]() { editInterval(selectedId()); });
    connect(table, &QTableWidget::cellDoubleClicked, this, [this](int row, int column) {
        if (column == Every) editInterval(table->item(row, Target)->data(Qt::UserRole).toULongLong());
    });
    connect(service, &WatchService::rowChanged, this, [this](quint64 id) {
        const int row = rowOf(id);
        if (const WatchRow *data = service->row(id); data && row >= 0) fill(row, *data);
        selectionChanged();
    });
    connect(service, &WatchService::rowRemoved, this, [this, hint](quint64 id) {
        const int row = rowOf(id);
        if (row >= 0) table->removeRow(row);
        clearButton->setEnabled(table->rowCount() > 0);
        if (!table->rowCount()) hint->setText(tr("No watches yet. Right-click an object in the Query tree or a value in Results and choose Watch."));
        selectionChanged();
    });
    connect(table, &QTableWidget::itemSelectionChanged, this, &WatchPanel::selectionChanged);
    connect(pauseButton, &QPushButton::clicked, this, [this]() {
        const quint64 id = selectedId();
        if (const WatchRow *data = service->row(id)) service->setPaused(id, !data->paused);
    });
    connect(removeButton, &QPushButton::clicked, this, [this]() { service->remove(selectedId()); });
    connect(clearButton, &QPushButton::clicked, service, &WatchService::clear);
    connect(table, &QWidget::customContextMenuRequested, this, [this](const QPoint &position) {
        QTableWidgetItem *item = table->itemAt(position);
        if (!item) return;
        table->selectRow(item->row());
        const WatchRow *data = service->row(selectedId());
        if (!data) return;
        const quint64 id = data->id;
        const bool wasPaused = data->paused;
        QMenu menu(table);
        QAction *pause = menu.addAction(data->paused ? tr("Resume") : tr("Pause"));
        QAction *poll = menu.addAction(tr("Poll now"));
        QAction *changeInterval = menu.addAction(tr("Change interval…"));
        QAction *remove = menu.addAction(tr("Remove"));
        QAction *chosen = menu.exec(table->viewport()->mapToGlobal(position));
        if (chosen == pause) service->setPaused(id, !wasPaused);
        else if (chosen == poll) service->pollNow(id);
        else if (chosen == changeInterval) editInterval(id);
        else if (chosen == remove) service->remove(id);
    });
}

int WatchPanel::intervalSeconds() const { return interval->value(); }

quint64 WatchPanel::selectedId() const
{
    const auto selected = table->selectedItems();
    return selected.isEmpty() ? 0 : table->item(selected.first()->row(), Target)->data(Qt::UserRole).toULongLong();
}

int WatchPanel::rowOf(quint64 id) const
{
    for (int row = 0; row < table->rowCount(); ++row)
        if (table->item(row, Target)->data(Qt::UserRole).toULongLong() == id) return row;
    return -1;
}

void WatchPanel::selectionChanged()
{
    const WatchRow *data = service->row(selectedId());
    pauseButton->setEnabled(data != nullptr);
    removeButton->setEnabled(data != nullptr);
    editIntervalButton->setEnabled(data != nullptr);
    pauseButton->setText(data && data->paused ? tr("Resume") : tr("Pause"));
}

void WatchPanel::fill(int row, const WatchRow &data)
{
    const QString notes = QStringList{data.change.note, data.warnings}.join(QStringLiteral("  ")).trimmed();
    const bool failed = data.status != tr("OK") && data.status != tr("Paused") &&
                        data.status != tr("Waiting for the first answer") && data.status != tr("Resuming");
    const QStringList cells{data.spec.target, credentialLabel ? credentialLabel(data.spec.credentialKey) : data.spec.credentialKey,
                            data.objectLabel, data.value, data.change.change, data.change.rate,
                            data.received, notes, tr("%1 s").arg(data.spec.intervalSeconds), data.status,
                            data.updated.isValid() ? data.updated.toString(QStringLiteral("HH:mm:ss")) : QString()};
    for (int column = 0; column < ColumnCount; ++column) {
        QTableWidgetItem *item = table->item(row, column);
        item->setText(cells.at(column));
        item->setToolTip(column == Status || column == Notes ? cells.at(column) : QString());
        if (column == Value || column == Change || column == Rate || column == Received)
            item->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        item->setData(AppearanceColors::ToneRole, failed ? AppearanceColors::Error
            : !data.warnings.isEmpty() ? AppearanceColors::Warning : AppearanceColors::None);
        item->setData(AppearanceColors::MutedRole, data.paused);
    }
}


void WatchPanel::editInterval(quint64 id)
{
    const WatchRow *data = service->row(id);
    if (!data) return;
    const int seconds = data->spec.intervalSeconds; // value copy before the modal event loop
    QInputDialog dialog(this);
    dialog.setObjectName("WatchIntervalDialog");
    dialog.setWindowTitle(tr("Watch interval"));
    dialog.setLabelText(tr("Repeat this watch every (seconds):"));
    dialog.setInputMode(QInputDialog::IntInput);
    dialog.setIntRange(1, 3600);
    dialog.setIntValue(seconds);
    if (dialog.exec() == QDialog::Accepted) service->setInterval(id, dialog.intValue());
}
