#include "appearancecolors.h"
#include "collectionsearchpanel.h"

#include "mibcollectionservice.h"

#include <QApplication>
#include <QBrush>
#include <QClipboard>
#include <QKeyEvent>
#include <QColor>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QTableWidget>
#include <QVBoxLayout>

namespace { enum Column { Name, Kind, Module, Oid, Profiles, File, ColumnCount }; }

CollectionSearchPanel::CollectionSearchPanel(MibCollectionService *collection, Lookups lookupFunctions,
                                             QWidget *parent)
    : QWidget(parent), service(collection), lookups(std::move(lookupFunctions))
{
    setObjectName(QStringLiteral("WorkbenchSearchPage"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    summary = new QLabel(tr("Type a name, a Module::name, or a numeric OID in \"Search all MIBs\"."), this);
    summary->setObjectName(QStringLiteral("SearchSummary"));
    summary->setWordWrap(true);
    layout->addWidget(summary);

    table = new QTableWidget(0, ColumnCount, this);
    table->setObjectName(QStringLiteral("SearchResults"));
    table->setItemDelegate(new AppearanceColors::ItemDelegate(table));
    table->setHorizontalHeaderLabels({tr("Name"), tr("Kind"), tr("Module"), tr("OID"), tr("In profiles"), tr("File")});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setAlternatingRowColors(true);
    table->setWordWrap(false);
    table->verticalHeader()->setVisible(false);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(Profiles, QHeaderView::Stretch);
    table->setContextMenuPolicy(Qt::CustomContextMenu);
    layout->addWidget(table, 1);
    setProperty("workbenchFocusTarget", QStringLiteral("SearchResults"));

    connect(table, &QTableWidget::cellActivated, this, [this](int row, int) { activate(row); });
    // Qt item views do not emit activated() for Return on macOS, so handle it here on every platform.
    table->installEventFilter(this);
    connect(service, &MibCollectionService::indexChanged, this, [this]() {
        if (!lastQuery.isEmpty() && !service->isBuilding()) search(lastQuery);   // refresh with the new index
        else if (lastQuery.isEmpty()) search({});
    });
    connect(table, &QWidget::customContextMenuRequested, this, [this](const QPoint &position) {
        QTableWidgetItem *item = table->itemAt(position);
        if (!item) return;
        const int row = item->row();
        const QString oid = table->item(row, Oid)->text();
        const QString module = table->item(row, Module)->text();
        const bool inTree = !oid.isEmpty() && table->item(row, Module)->data(Qt::UserRole).toBool();
        // A background index publication may rebuild the table while the menu runs.
        const QString file = table->item(row, File)->data(Qt::UserRole).toString();
        const int line = table->item(row, File)->data(Qt::UserRole + 1).toInt();
        QMenu menu(table);
        QAction *tree = menu.addAction(tr("Show in the MIB Tree"));
        tree->setEnabled(inTree);
        tree->setToolTip(tr("Available when the module is in the active MIB Profile"));
        QAction *editor = menu.addAction(tr("Open %1 in the editor at this definition").arg(module));
        QAction *copy = menu.addAction(tr("Copy OID"));
        copy->setEnabled(!oid.isEmpty());
        QAction *chosen = menu.exec(table->viewport()->mapToGlobal(position));
        if (chosen == tree) emit showInTreeRequested(oid);
        else if (chosen == editor)
            emit openInEditorRequested(file, line);
        else if (chosen == copy) QApplication::clipboard()->setText(oid);
    });
}

int CollectionSearchPanel::resultCount() const { return table->rowCount(); }

void CollectionSearchPanel::search(const QString &query)
{
    lastQuery = query.trimmed();
    table->setRowCount(0);
    if (lastQuery.isEmpty()) {
        summary->setText(tr("Type a name, a Module::name, or a numeric OID in \"Search all MIBs\".")
            + QStringLiteral(" ") + service->statusText());
        return;
    }
    if (!service->hasIndex()) {
        summary->setText(service->statusText() + (service->isBuilding()
            ? tr(" Your search will run when indexing finishes. You can keep browsing.")
            : tr(" No search index is available. Refresh the Library and try again.")));
        return;
    }
    const MibCollectionSearchResult result = service->search(lastQuery);
    const QSet<QString> active = lookups.activeModules ? lookups.activeModules() : QSet<QString>();
    table->setSortingEnabled(false);
    for (const MibCollectionEntry &entry : result.entries) {
        const int row = table->rowCount();
        table->insertRow(row);
        const QStringList profiles = lookups.profilesContaining ? lookups.profilesContaining(entry.module) : QStringList{};
        const bool inActive = active.contains(entry.module);
        const QString oidText = entry.oid.isEmpty() ? QString() : entry.oid;
        const QStringList cells{entry.name, entry.kind, entry.module,
                                oidText.isEmpty() && entry.kind != QLatin1String("Type") ? tr("(unresolved)") : oidText,
                                profiles.isEmpty() ? tr("none: add this module to a profile to use it")
                                                   : profiles.join(QStringLiteral(", ")),
                                QFileInfo(entry.file).fileName()};
        for (int column = 0; column < ColumnCount; ++column) {
            auto *item = new QTableWidgetItem(cells.at(column));
            if (column == Oid) item->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
            if (column == Oid && oidText.isEmpty()) item->setText(entry.kind == QLatin1String("Type") ? QString() : tr("(unresolved)"));
            if (column == Oid && !entry.unresolved.isEmpty()) item->setToolTip(entry.unresolved);
            if (column == File) item->setToolTip(entry.file);
            if (inActive) item->setData(AppearanceColors::ToneRole, AppearanceColors::Success);
            table->setItem(row, column, item);
        }
        table->item(row, Oid)->setText(oidText.isEmpty() && entry.kind != QLatin1String("Type") ? tr("(unresolved)") : oidText);
        table->item(row, Module)->setData(Qt::UserRole, inActive);
        table->item(row, File)->setData(Qt::UserRole, entry.file);
        table->item(row, File)->setData(Qt::UserRole + 1, entry.line);
        table->item(row, Name)->setData(Qt::UserRole, entry.oid);   // the real OID, never the placeholder
    }
    table->setSortingEnabled(true);

    QString text = result.entries.isEmpty()
        ? tr("No MIB in the collection defines \"%1\".").arg(lastQuery)
        : tr("%n match(es) for \"%1\" across the whole collection.", nullptr, result.entries.size()).arg(lastQuery);
    if (result.truncated) text += tr(" Showing the first %1; make the search more specific.").arg(result.entries.size());
    if (!result.note.isEmpty() && result.note != QLatin1String("exact"))
        text += tr(" No definition has exactly that OID; this is the closest one above it (%1).").arg(result.note);
    text += tr(" Green rows are in the active MIB Profile. Double-click a row to open it.");
    if (service->isBuilding()) text += tr(" (The index is updating.)");
    summary->setText(text);
}

bool CollectionSearchPanel::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == table && event->type() == QEvent::KeyPress) {
        const int key = static_cast<QKeyEvent *>(event)->key();
        if (key == Qt::Key_Return || key == Qt::Key_Enter) {
            activate(table->currentRow());
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void CollectionSearchPanel::activate(int row)
{
    if (row < 0 || row >= table->rowCount()) return;
    const QString oid = table->item(row, Name)->data(Qt::UserRole).toString();
    const bool inTree = !oid.isEmpty() && table->item(row, Module)->data(Qt::UserRole).toBool();
    if (inTree) emit showInTreeRequested(oid);
    else emit openInEditorRequested(table->item(row, File)->data(Qt::UserRole).toString(),
                                    table->item(row, File)->data(Qt::UserRole + 1).toInt());
}
