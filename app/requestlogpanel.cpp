#include "appearancecolors.h"
#include "requestlogpanel.h"

#include "requestlog.h"

#include <QApplication>
#include <QBrush>
#include <QClipboard>
#include <QColor>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace { enum Column { Time, Operation, Target, Identity, Object, Result, Objects, Took, Detail, ColumnCount }; }

RequestLogPanel::RequestLogPanel(RequestLog *requestLog, QWidget *parent)
    : QWidget(parent), log(requestLog)
{
    setObjectName(QStringLiteral("WorkbenchLogPage"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    auto *bar = new QHBoxLayout;
    auto *hint = new QLabel(tr("The request log is empty. Start a query from the MIB tree. Passwords and communities are never logged."), this);
    hint->setObjectName("RequestLogHint"); hint->setWordWrap(true);
    bar->addWidget(hint, 1);
    bar->addStretch();
    copyButton = new QPushButton(tr("Copy all"), this);
    clearButton = new QPushButton(tr("Clear"), this);
    copyButton->setObjectName(QStringLiteral("LogCopy"));
    clearButton->setObjectName(QStringLiteral("RequestLogClear"));
    copyButton->setToolTip(tr("Copy the log as tab-separated text, for a ticket or a bug report"));
    bar->addWidget(copyButton); bar->addWidget(clearButton);
    layout->addLayout(bar);

    table = new QTableWidget(0, ColumnCount, this);
    table->setObjectName(QStringLiteral("RequestLogTable"));
    table->setItemDelegate(new AppearanceColors::ItemDelegate(table));
    table->setHorizontalHeaderLabels({tr("Time"), tr("Operation"), tr("Target"), tr("Identity"), tr("Object"),
                                      tr("Result"), tr("Objects"), tr("Took"), tr("Detail")});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setAlternatingRowColors(true);
    table->setWordWrap(false);
    table->verticalHeader()->setVisible(false);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(Detail, QHeaderView::Stretch);
    layout->addWidget(table, 1);
    setProperty("workbenchFocusTarget", QStringLiteral("RequestLogTable"));
    copyButton->setEnabled(false); clearButton->setEnabled(false);

    connect(log, &RequestLog::entryAdded, this, [this, hint](quint64 id) {
        hint->setText(tr("Requests this session and how they ended. Passwords and communities are never logged."));
        copyButton->setEnabled(true); clearButton->setEnabled(true);
        const int row = table->rowCount();
        table->insertRow(row);
        for (int column = 0; column < ColumnCount; ++column) table->setItem(row, column, new QTableWidgetItem);
        table->item(row, Time)->setData(Qt::UserRole, QVariant::fromValue(id));
        fill(row, id);
        table->scrollToBottom();
        // The log keeps a bounded number of entries; keep the table in step.
        while (table->rowCount() > log->entries().size()) table->removeRow(0);
    });
    connect(log, &RequestLog::entryChanged, this, [this](quint64 id) {
        const int row = rowOf(id);
        if (row >= 0) fill(row, id);
    });
    connect(log, &RequestLog::cleared, this, [this, hint]() {
        table->setRowCount(0); copyButton->setEnabled(false); clearButton->setEnabled(false);
        hint->setText(tr("The request log is empty. Start a query from the MIB tree. Passwords and communities are never logged."));
    });
    connect(clearButton, &QPushButton::clicked, log, &RequestLog::clear);
    connect(copyButton, &QPushButton::clicked, this, [this]() {
        QApplication::clipboard()->setText(RequestLogAsText(log->entries()));
    });
}

int RequestLogPanel::rowOf(quint64 id) const
{
    for (int row = table->rowCount() - 1; row >= 0; --row)
        if (table->item(row, Time)->data(Qt::UserRole).toULongLong() == id) return row;
    return -1;
}

void RequestLogPanel::fill(int row, quint64 id)
{
    const RequestLogEntry *e = log->entry(id);
    if (!e) return;
    const bool running = e->outcome == RequestOutcome::Running;
    const QStringList cells{e->started.toString(QStringLiteral("HH:mm:ss.zzz")), e->operation, e->target, e->identity,
                            e->oid, RequestOutcomeText(e->outcome), running ? QString() : QString::number(e->objects),
                            running ? QString() : tr("%1 ms").arg(e->durationMs), e->detail};
    auto tone = AppearanceColors::None;
    if (e->outcome == RequestOutcome::Failed || e->outcome == RequestOutcome::TimedOut ||
        e->outcome == RequestOutcome::Refused) tone = AppearanceColors::Error;
    else if (e->outcome == RequestOutcome::Stopped) tone = AppearanceColors::Warning;
    for (int column = 0; column < ColumnCount; ++column) {
        QTableWidgetItem *item = table->item(row, column);
        item->setText(cells.at(column));
        item->setData(AppearanceColors::ToneRole, tone);
        if (column == Object || column == Time) item->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        if (column == Detail) item->setToolTip(cells.at(column));
    }
}
