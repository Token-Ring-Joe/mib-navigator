#include "mibmodelview.h"

#include "mibtreemodel.h"
#include "tablenodevalidation.h"

#include <QContextMenuEvent>
#include <QInputDialog>
#include <QMenu>
#include <QTextDocument>

namespace { constexpr int NodeScalar=2,NodeTable=4,NodeRow=8,NodeColumn=16;
MibEnvironmentNodeKind environmentKind(int kind){switch(kind){case NodeScalar:return MibEnvironmentNodeKind::Scalar;case NodeTable:return MibEnvironmentNodeKind::Table;case NodeRow:return MibEnvironmentNodeKind::Row;case NodeColumn:return MibEnvironmentNodeKind::Column;default:return MibEnvironmentNodeKind::Unknown;}} }

MibModelView::MibModelView(QWidget *parent, Mode mode)
    : QTreeView(parent), filterModel(new MibTreeFilterModel(this)), interactionMode(mode)
{
    setHeaderHidden(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setUniformRowHeights(true);
}

void MibModelView::currentChanged(const QModelIndex &current, const QModelIndex &previous)
{
    QTreeView::currentChanged(current, previous);
    updateQueryTableAvailability();
    if (!current.isValid()) {
        if (interactionMode == Mode::Query) emit CurrentObjectChanged({});
        emit NodeProperties({}); return;
    }
    retainedOid = current.data(MibTreeModel::OidRole).toString();
    emit CurrentObjectChanged(retainedOid);
    emit NodeProperties(detailsFor(current));
}

void MibModelView::SetFilterText(const QString &text)
{
    searchText = text.trimmed();
    filterModel->setFilterFixedString(searchText);
    if (searchText.isEmpty()) expandToDepth(1); else expandAll();
}

void MibModelView::QueryCurrent(int operation)
{
    if (interactionMode == Mode::OidSelection) return;
    const QString oid = currentOid();
    if (!oid.isEmpty()) emit GetFromOid(oid + QStringLiteral(".0"), operation);
}

void MibModelView::WalkCurrent()
{
    if (interactionMode == Mode::OidSelection) return;
    if (!currentOid().isEmpty()) emit WalkFromOid(currentOid());
}

void MibModelView::SetCurrent()
{
    if (interactionMode == Mode::OidSelection) return;
    if (!currentOid().isEmpty()) emit SetFromOid(currentOid());
}

bool MibModelView::queryTableAvailable() const
{
    return interactionMode == Mode::Query && queryPrerequisitesAvailable && currentIndex().isValid() &&
           IsTableQueryCapableNodeKind(environmentKind(currentKind()));
}

void MibModelView::updateQueryTableAvailability()
{
    emit QueryTableAvailabilityChanged(queryTableAvailable());
}

void MibModelView::SetQueryPrerequisitesAvailable(bool available)
{
    if (queryPrerequisitesAvailable == available)
        return;
    queryPrerequisitesAvailable = available;
    updateQueryTableAvailability();
}

void MibModelView::QueryTableFromCurrent()
{
    if (queryTableAvailable())
        emit TableViewFromOid(currentOid());
}

void MibModelView::setTreeModel(MibTreeModel *source)
{
    filterModel->setSourceModel(source);
    setModel(filterModel);
    if (!source) return;
    connect(source, &QAbstractItemModel::modelAboutToBeReset, this, [this]() {
        if (currentIndex().isValid())
            retainedOid = currentOid();
        emit NodeProperties({});
        emit QueryTableAvailabilityChanged(false);
    });
    connect(source, &QAbstractItemModel::modelReset, this, &MibModelView::restoreSelection);
}

void MibModelView::setVisibleModules(const QStringList &modules)
{
    filterModel->setVisibleModules(modules);
    expandToDepth(1);
}

QStringList MibModelView::visibleModules() const
{
    return filterModel->visibleModules();
}

void MibModelView::showAllModules()
{
    filterModel->showAllModules();
    expandToDepth(1);
}

void MibModelView::Populate()
{
    if (model() && model()->rowCount()) {
        expandToDepth(1);
        restoreSelection();
    }
}

QModelIndex MibModelView::proxyIndexForOid(const QString &oid) const
{
    auto *source = qobject_cast<MibTreeModel *>(filterModel->sourceModel());
    if (!source) return {};
    QModelIndex sourceIndex = source->indexForOid(oid);
    if (!sourceIndex.isValid()) {
        QString candidate = oid;
        while (candidate.contains(QLatin1Char('.'))) {
            candidate.truncate(candidate.lastIndexOf(QLatin1Char('.')));
            sourceIndex = source->indexForOid(candidate);
            if (sourceIndex.isValid() &&
                sourceIndex.data(MibTreeModel::NodeKindRole).toInt() == NodeColumn)
                break;
            sourceIndex = {};
        }
    }
    return sourceIndex.isValid() ? filterModel->mapFromSource(sourceIndex) : QModelIndex();
}

void MibModelView::SelectFromOid(const QString &oid)
{
    LocateOid(oid);
}

bool MibModelView::LocateOid(const QString &oid)
{
    retainedOid = oid;
    const QModelIndex index = proxyIndexForOid(oid);
    if (!index.isValid()) return false;
    setCurrentIndex(index);
    scrollTo(index);
    expand(index.parent());
    retainedOid = index.data(MibTreeModel::OidRole).toString();
    return true;
}

void MibModelView::restoreSelection()
{
    if (!retainedOid.isEmpty()) SelectFromOid(retainedOid);
}

void MibModelView::FindFromNode()
{
    bool accepted = false;
    const QString text = QInputDialog::getText(this, tr("Find in MIB Tree"),
                                               tr("Name, OID, or module:"),
                                               QLineEdit::Normal, searchText, &accepted);
    if (!accepted) return;
    searchText = text;
    filterModel->setFilterFixedString(searchText);
    expandAll();
    setCurrentIndex(model()->index(0, 0));
}

void MibModelView::ExecuteFindNext()
{
    if (searchText.isEmpty() || !model()) return;
    QList<QModelIndex> matches;
    const auto collect = [&](const auto &self, const QModelIndex &parent) -> void {
        for (int row = 0; row < model()->rowCount(parent); ++row) {
            const QModelIndex index = model()->index(row, 0, parent);
            if (index.data(MibTreeModel::SearchTextRole).toString().contains(searchText, Qt::CaseInsensitive))
                matches.append(index);
            self(self, index);
        }
    };
    collect(collect, {});
    if (matches.isEmpty()) return;
    int next = 0;
    for (int i = 0; i < matches.size(); ++i)
        if (matches.at(i) == currentIndex()) { next = (i + 1) % matches.size(); break; }
    setCurrentIndex(matches.at(next));
    scrollTo(matches.at(next));
}

QString MibModelView::currentOid() const
{
    return currentIndex().data(MibTreeModel::OidRole).toString();
}

int MibModelView::currentKind() const
{
    return currentIndex().data(MibTreeModel::NodeKindRole).toInt();
}

QString MibModelView::detailsFor(const QModelIndex &index) const
{
    const auto value = [&index](int role) { return index.data(role).toString().toHtmlEscaped(); };
    const auto listValue = [&index](int role) {
        QStringList escaped;
        for (const QString &item : index.data(role).toStringList()) escaped.append(item.toHtmlEscaped());
        return escaped.join(QStringLiteral("<br>"));
    };
    QString text = QStringLiteral("<h3>%1</h3><p><b>%2</b><br><tt>%3</tt></p><table cellpadding=\"4\" cellspacing=\"0\" width=\"100%\">")
        .arg(index.data(Qt::DisplayRole).toString().toHtmlEscaped(), tr("OID"), value(MibTreeModel::OidRole));
    const auto row = [&text](const QString &label, const QString &content) {
        if (!content.isEmpty())
            text += QStringLiteral("<tr><td><b>%1:</b></td><td>%2</td></tr>").arg(label, content);
    };
    row(tr("Module"), value(MibTreeModel::ModuleRole));
    const int kind = index.data(MibTreeModel::NodeKindRole).toInt();
    QString structure;
    if (kind == NodeTable) structure = tr("Table");
    else if (kind == NodeRow) structure = tr("Table Entry");
    else if (kind == NodeColumn) structure = tr("Column");
    row(tr("Type"), structure);
    row(tr("Syntax"), value(MibTreeModel::TypeRole));
    row(tr("Base type"), value(MibTreeModel::BaseTypeRole));
    row(tr("Display hint"), value(MibTreeModel::DisplayHintRole));
    row(tr("Range / size"), listValue(MibTreeModel::RangesRole));
    row(tr("Values"), listValue(MibTreeModel::NamedValuesRole));
    row(tr("Access"), value(MibTreeModel::AccessRole));
    row(tr("Status"), value(MibTreeModel::StatusRole));
    row(tr("Units"), value(MibTreeModel::UnitsRole));
    text += QStringLiteral("</table>");
    for (const auto &section : {QPair<int, QString>{MibTreeModel::DescriptionRole, tr("Description")},
                               {MibTreeModel::ReferenceRole, tr("Reference")}}) {
        const QString content = index.data(section.first).toString();
        if (!content.isEmpty()) text += QStringLiteral("<h4>%1</h4>%2")
            .arg(section.second, Qt::convertFromPlainText(content));
    }
    return text;
}

void MibModelView::contextMenuEvent(QContextMenuEvent *event)
{
    if (event->reason() == QContextMenuEvent::Mouse) {
        const QModelIndex clicked = indexAt(event->pos());
        if (!clicked.isValid()) return;
        setCurrentIndex(clicked);
    }
    const QString oid = currentOid();
    QMenu menu(tr("Operations"), this);
    QAction *expandAction = menu.addAction(tr("Expand Branch"));
    QAction *collapseAction = menu.addAction(tr("Collapse Branch"));
    menu.addSeparator();
    if (interactionMode == Mode::OidSelection) {
        QAction *findAction = menu.addAction(tr("Find..."));
        QAction *chosen = menu.exec(event->globalPos());
        if (chosen == expandAction) expandRecursively(currentIndex());
        else if (chosen == collapseAction) collapse(currentIndex());
        else if (chosen == findAction) FindFromNode();
        return;
    }
    QAction *walkAction = menu.addAction(tr("Walk"));
    QAction *stopAction = menu.addAction(tr("Stop"));
    stopAction->setEnabled(walkInProgress);
    menu.addSeparator();
    QAction *getAction = nullptr, *getNextAction = nullptr, *getBulkAction = nullptr;
    QAction *getPromptAction = nullptr, *getSelectAction = nullptr;
    QAction *getNextPromptAction = nullptr, *getNextSelectAction = nullptr;
    QAction *getBulkPromptAction = nullptr, *getBulkSelectAction = nullptr;
    const int kind = currentKind();
    if (kind == NodeColumn) {
        QMenu *getMenu = menu.addMenu(tr("Get"));
        getSelectAction = getMenu->addAction(tr("Select Instance"));
        getPromptAction = getMenu->addAction(tr("Prompt for Instance..."));
        QMenu *nextMenu = menu.addMenu(tr("Get Next"));
        getNextAction = nextMenu->addAction(tr("No Instance"));
        getNextSelectAction = nextMenu->addAction(tr("Select Instance"));
        getNextPromptAction = nextMenu->addAction(tr("Prompt for Instance..."));
        QMenu *bulkMenu = menu.addMenu(tr("Get Bulk"));
        getBulkAction = bulkMenu->addAction(tr("No Instance"));
        getBulkSelectAction = bulkMenu->addAction(tr("Select Instance"));
        getBulkPromptAction = bulkMenu->addAction(tr("Prompt for Instance..."));
        bulkMenu->setEnabled(!agentIsV1);
    } else {
        getAction = menu.addAction(tr("Get"));
        getNextAction = menu.addAction(tr("Get Next"));
        getBulkAction = menu.addAction(tr("Get Bulk"));
    }
    QAction *watchAction = menu.addAction(tr("Watch this value..."));
    watchAction->setObjectName("QueryTreeWatch");
    QAction *setAction = menu.addAction(tr("Set..."));
    menu.addSeparator();
    QMenu *tableMenu = menu.addMenu(tr("Query Table"));
    tableMenu->setObjectName("QueryTableMenu");
    QAction *tableAction = tableMenu->addAction(tr("All Instances"));
    QAction *tableRowAction = tableMenu->addAction(tr("Select Instance..."));
    tableRowAction->setObjectName("QueryTableSelectInstance");
    QMenu *commandMenu = menu.addMenu(tr("Copy as Command"));
    commandMenu->setObjectName("QueryTreeCopyCommand");
    commandMenu->setToolTipsVisible(true);
    commandMenu->setToolTip(tr("Copies a Net-SNMP command for this object. Passphrases and community strings are never copied."));
    QAction *copyGet = commandMenu->addAction(tr("snmpget")); copyGet->setEnabled(kind == NodeScalar);
    QAction *copyNext = commandMenu->addAction(tr("snmpgetnext"));
    QAction *copyBulk = commandMenu->addAction(tr("snmpbulkget")); copyBulk->setEnabled(!agentIsV1);
    QAction *copyWalk = commandMenu->addAction(tr("snmpwalk"));
    QMenu *keyMenu = commandMenu->addMenu(tr("With keys (lab use)"));
    keyMenu->setObjectName("QueryTreeCopyCommandKeys");
    keyMenu->setToolTipsVisible(true);
    keyMenu->setToolTip(tr("SNMPv3 only. The command contains the passphrases' master keys, which log in as this user. Do not paste it into tickets or chats."));
    QAction *keyGet = keyMenu->addAction(tr("snmpget")); keyGet->setEnabled(kind == NodeScalar);
    QAction *keyNext = keyMenu->addAction(tr("snmpgetnext"));
    QAction *keyBulk = keyMenu->addAction(tr("snmpbulkget")); keyBulk->setEnabled(!agentIsV1);
    QAction *keyWalk = keyMenu->addAction(tr("snmpwalk"));
    QAction *macroAction = menu.addAction(tr("Add to Selected Macro"));
    macroAction->setObjectName("QueryTreeAddMacro");
    macroAction->setEnabled(kind == NodeScalar || kind == NodeColumn || kind == NodeTable || kind == NodeRow);
    menu.addSeparator();
    QAction *findAction = menu.addAction(tr("Find..."));

    const bool leaf = kind == NodeScalar || kind == NodeColumn;
    if (getAction) getAction->setEnabled(kind == NodeScalar);
    if (getBulkAction) getBulkAction->setEnabled(!agentIsV1);
    watchAction->setEnabled(leaf && queryPrerequisitesAvailable);
    setAction->setEnabled(leaf);
    tableMenu->setEnabled(queryTableAvailable() && !walkInProgress);

    QAction *chosen = menu.exec(event->globalPos());
    if (!chosen) return;
    if (chosen == expandAction) expandRecursively(currentIndex());
    else if (chosen == collapseAction) collapse(currentIndex());
    else if (chosen == walkAction) emit WalkFromOid(oid);
    else if (chosen == stopAction) emit Stop();
    else if (chosen == getAction) emit GetFromOid(oid + QStringLiteral(".0"), 0);
    else if (chosen == getSelectAction) emit GetFromOidSelectInstance(oid, 0);
    else if (chosen == getPromptAction) emit GetFromOidPromptInstance(oid, 0);
    else if (chosen == getNextAction) emit GetFromOid(oid + QStringLiteral(".0"), 1);
    else if (chosen == getNextSelectAction) emit GetFromOidSelectInstance(oid, 1);
    else if (chosen == getNextPromptAction) emit GetFromOidPromptInstance(oid, 1);
    else if (chosen == getBulkAction) emit GetFromOid(oid + QStringLiteral(".0"), 2);
    else if (chosen == getBulkSelectAction) emit GetFromOidSelectInstance(oid, 2);
    else if (chosen == getBulkPromptAction) emit GetFromOidPromptInstance(oid, 2);
    else if (chosen == watchAction) emit WatchFromOid(oid, kind == NodeColumn);
    else if (chosen == copyGet) emit CopyCommandFromOid(oid + QStringLiteral(".0"), 0, false);
    else if (chosen == copyNext) emit CopyCommandFromOid(kind == NodeScalar ? oid + QStringLiteral(".0") : oid, 1, false);
    else if (chosen == copyBulk) emit CopyCommandFromOid(kind == NodeScalar ? oid + QStringLiteral(".0") : oid, 2, false);
    else if (chosen == copyWalk) emit CopyCommandFromOid(oid, 3, false);
    else if (chosen == keyGet) emit CopyCommandFromOid(oid + QStringLiteral(".0"), 0, true);
    else if (chosen == keyNext) emit CopyCommandFromOid(kind == NodeScalar ? oid + QStringLiteral(".0") : oid, 1, true);
    else if (chosen == keyBulk) emit CopyCommandFromOid(kind == NodeScalar ? oid + QStringLiteral(".0") : oid, 2, true);
    else if (chosen == keyWalk) emit CopyCommandFromOid(oid, 3, true);
    else if (chosen == setAction) emit SetFromOid(oid);
    else if (chosen == tableAction) emit TableViewFromOid(oid);
    else if (chosen == tableRowAction) emit TableRowSelectFromOid(oid);
    else if (chosen == macroAction) emit MacroFromOid(oid);
    else if (chosen == findAction) {
        FindFromNode();
    }
}
