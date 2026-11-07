#include "trapview.h"
#include "trapservice.h"
#include "trappresenter.h"
#include "appearancecolors.h"

#include <QAction>
#include <QApplication>
#include <QBoxLayout>
#include <QClipboard>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTextEdit>
#include <QTreeWidget>

TrapView::TrapView(TrapService *service, QTreeWidget *log, QTreeWidget *content,
                   QTextEdit *info, std::function<bool()> expandBindings, QObject *parent)
    : QObject(parent), service(service), log(log), content(content), info(info),
      expandBindings(expandBindings)
{
    content->setHeaderLabels({tr("Object.Instance"), tr("Value"), tr("Received type"),
        tr("Raw value"), tr("Units"), tr("Warnings")});
    content->header()->show();
    content->setSortingEnabled(false);
    content->setItemDelegate(new AppearanceColors::ItemDelegate(content));
    content->header()->setSectionResizeMode(QHeaderView::Interactive);
    content->header()->resizeSection(0, 210);
    content->header()->resizeSection(1, 180);
    content->header()->resizeSection(2, 125);
    content->header()->resizeSection(3, 180);
    content->header()->resizeSection(4, 80);
    content->header()->setStretchLastSection(true);
    log->setSortingEnabled(false);
    log->headerItem()->setText(7, tr("Source IP"));
    log->headerItem()->setText(8, tr("Source port"));
    connect(log, &QTreeWidget::currentItemChanged, this, [this] { showSelected(); });
    connect(service, &TrapService::recordAdded, this, &TrapView::appendRecord);
    connect(service, &TrapService::historyReset, this, [this] {
        if (this->service && this->service->history().count()) {
            // A history-limit change may have evicted the selected record.
            appendRecord();
        } else clearView();
    });

    copySource = new QAction(tr("Copy source IP"), log);
    copySource->setObjectName(QStringLiteral("CopyTrapSourceIP"));
    copySource->setEnabled(false);
    connect(copySource, &QAction::triggered, this, [this] {
        if (const auto *record = selectedRecord())
            QApplication::clipboard()->setText(TrapPresenter::sourceToCopy(*record));
    });
    log->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(log, &QWidget::customContextMenuRequested, this, [this](const QPoint &position) {
        if (!this->log || !this->log->itemAt(position)) return;
        this->log->setCurrentItem(this->log->itemAt(position));
        QMenu menu(this->log);
        menu.addAction(copySource);
        menu.exec(this->log->viewport()->mapToGlobal(position));
    });
    historyHint = new QLabel(tr("Trap history is empty. Listener status appears in the status bar; configure listening in Settings > Preferences > Traps."), log->parentWidget());
    historyHint->setObjectName("TrapHistoryHint"); historyHint->setWordWrap(true);
    if (auto *layout = qobject_cast<QBoxLayout *>(log->parentWidget()->layout()))
        layout->insertWidget(0, historyHint);
    clearButton = new QPushButton(tr("Clear History"), log->parentWidget());
    clearButton->setObjectName(QStringLiteral("ClearTrapHistory"));
    clearButton->setToolTip(tr("Remove all retained traps from the in-memory history"));
    clearButton->setEnabled(service->history().count() > 0);
    if (auto *layout = qobject_cast<QBoxLayout *>(log->parentWidget()->layout()))
        layout->insertWidget(1, clearButton, 0, Qt::AlignLeft);
    connect(clearButton, &QPushButton::clicked, service, &TrapService::clear);
    appendRecord();
}

const TrapRecord *TrapView::selectedRecord() const
{
    if (!service || !log || !log->currentItem()) return nullptr;
    const quint64 id = log->currentItem()->data(0, Qt::UserRole).toULongLong();
    for (const auto &record : service->history().records())
        if (record.recordId == id) return &record;
    return nullptr;
}

void TrapView::appendRecord()
{
    if (!service || !log) return;
    const auto &records = service->history().records();
    if (records.isEmpty()) { clearView(); return; }
    const bool initiallyEmpty = log->topLevelItemCount() == 0;
    const quint64 selected = log->currentItem()
        ? log->currentItem()->data(0, Qt::UserRole).toULongLong() : 0;
    const QSignalBlocker blocker(log);
    const quint64 first = records.first().recordId;
    while (log->topLevelItemCount() &&
           log->topLevelItem(0)->data(0, Qt::UserRole).toULongLong() < first)
        delete log->takeTopLevelItem(0);
    const quint64 last = log->topLevelItemCount()
        ? log->topLevelItem(log->topLevelItemCount()-1)->data(0, Qt::UserRole).toULongLong() : 0;
    for (const auto &record : records) {
        if (record.recordId <= last) continue;
        auto *item = new QTreeWidgetItem(log, TrapPresenter().summary(record));
        item->setData(0, Qt::UserRole, QVariant::fromValue(record.recordId));
    }
    if (clearButton) clearButton->setEnabled(true);
    if (historyHint) historyHint->hide();
    QTreeWidgetItem *restore = nullptr;
    for (int i = 0; i < log->topLevelItemCount(); ++i)
        if (log->topLevelItem(i)->data(0, Qt::UserRole).toULongLong() == selected)
            restore = log->topLevelItem(i);
    if (initiallyEmpty) restore = log->topLevelItem(0);
    log->setCurrentItem(restore);
    // Eviction clears details, but never silently selects a different trap.
    if (initiallyEmpty || !restore) showSelected();
}

void TrapView::refreshEnvironment()
{
    if (!service || !log) return;
    QHash<quint64, QStringList> summaries;
    for (const auto &record : service->history().records())
        summaries.insert(record.recordId, TrapPresenter().summary(record));
    for (int i = 0; i < log->topLevelItemCount(); ++i) {
        auto *item = log->topLevelItem(i);
        const auto columns = summaries.value(item->data(0, Qt::UserRole).toULongLong());
        for (int c = 0; c < columns.size(); ++c) item->setText(c, columns[c]);
    }
    showSelected();
}

void TrapView::clearView()
{
    if (log) log->clear();
    if (content) content->clear();
    if (info) info->clear();
    if (copySource) copySource->setEnabled(false);
    if (clearButton) clearButton->setEnabled(false);
    if (historyHint) historyHint->show();
}

void TrapView::showSelected()
{
    if (!content || !info) return;
    const auto *record = selectedRecord();
    content->clear();
    info->clear();
    if (copySource) copySource->setEnabled(record && !TrapPresenter::sourceToCopy(*record).isEmpty());
    if (!record) return;
    const auto view = TrapPresenter().present(*record);
    auto *identity = new QTreeWidgetItem(content, {view.communityText});
    identity->setFirstColumnSpanned(true);
    auto *bindings = new QTreeWidgetItem(content, {tr("Bindings (%1)").arg(view.bindings.size())});
    bindings->setFirstColumnSpanned(true);
    bindings->setExpanded(expandBindings && expandBindings());
    for (const auto &value : view.bindings) {
        auto *item = new QTreeWidgetItem(bindings, {value.object, value.interpretedValue,
            value.receivedType, value.rawValue, value.units, value.warnings.join(' ')});
        const QString tooltip = tr("OID: %1\nReceived %2: %3").arg(value.oid, value.receivedType, value.rawValue)
            + (value.warnings.isEmpty() ? QString() : "\n" + value.warnings.join('\n'));
        for (int c = 0; c < content->columnCount(); ++c) {
            item->setToolTip(c, tooltip);
            if (!value.warnings.isEmpty()) item->setData(c, AppearanceColors::ToneRole, AppearanceColors::Warning);
        }
        if (!value.warnings.isEmpty())
            item->setIcon(0, content->style()->standardIcon(QStyle::SP_MessageBoxWarning));
    }
    info->setHtml(view.notificationHtml);
}
