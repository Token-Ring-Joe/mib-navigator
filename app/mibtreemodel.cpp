#include "mibtreemodel.h"

MibTreeModel::MibTreeModel(QObject *parent) : QAbstractItemModel(parent)
{
    setSnapshot(MibTreeNodeRecord{});
}

std::unique_ptr<MibTreeModel::Item> MibTreeModel::makeItem(
    const MibTreeNodeRecord &record, Item *parent)
{
    auto item = std::make_unique<Item>();
    item->value = record;
    item->parent = parent;
    for (const MibTreeNodeRecord &child : record.children)
        item->children.push_back(makeItem(child, item.get()));
    item->value.children.clear();
    return item;
}

void MibTreeModel::setSnapshot(const MibTreeNodeRecord &root)
{
    beginResetModel();
    rootItem = makeItem(root, nullptr);
    endResetModel();
}

QModelIndex MibTreeModel::index(int row, int column, const QModelIndex &parentIndex) const
{
    if (!rootItem || row < 0 || column != 0)
        return {};
    Item *parent = parentIndex.isValid() ? static_cast<Item *>(parentIndex.internalPointer())
                                         : rootItem.get();
    return row < int(parent->children.size())
        ? createIndex(row, column, parent->children.at(row).get()) : QModelIndex();
}

QModelIndex MibTreeModel::parent(const QModelIndex &child) const
{
    if (!child.isValid())
        return {};
    Item *item = static_cast<Item *>(child.internalPointer());
    Item *parentItem = item->parent;
    if (!parentItem || parentItem == rootItem.get())
        return {};
    Item *grandparent = parentItem->parent;
    const int row = grandparent ? [&] {
        for (int i = 0; i < int(grandparent->children.size()); ++i)
            if (grandparent->children.at(i).get() == parentItem) return i;
        return -1;
    }() : -1;
    return row >= 0 ? createIndex(row, 0, parentItem) : QModelIndex();
}

int MibTreeModel::rowCount(const QModelIndex &parentIndex) const
{
    if (parentIndex.column() > 0 || !rootItem)
        return 0;
    Item *item = parentIndex.isValid() ? static_cast<Item *>(parentIndex.internalPointer())
                                       : rootItem.get();
    return int(item->children.size());
}

int MibTreeModel::columnCount(const QModelIndex &) const { return 1; }

QVariant MibTreeModel::data(const QModelIndex &modelIndex, int role) const
{
    if (!modelIndex.isValid())
        return {};
    const MibTreeNodeRecord &value = static_cast<Item *>(modelIndex.internalPointer())->value;
    switch (role) {
    case Qt::DisplayRole: return value.name;
    case OidRole: return value.oid;
    case ModuleRole: return value.moduleName;
    case NodeKindRole: return value.nodeKind;
    case AccessRole: return value.access;
    case StatusRole: return value.status;
    case TypeRole: return value.typeName;
    case BaseTypeRole: return value.baseType;
    case DisplayHintRole: return value.displayHint;
    case RangesRole: return value.ranges;
    case NamedValuesRole: return value.namedValues;
    case UnitsRole: return value.units;
    case DescriptionRole: return value.description;
    case ReferenceRole: return value.reference;
    case SearchTextRole: return value.name + QLatin1Char(' ') + value.oid +
                                QLatin1Char(' ') + value.moduleName;
    default: return {};
    }
}

QHash<int, QByteArray> MibTreeModel::roleNames() const
{
    return {{OidRole, "oid"}, {ModuleRole, "module"}, {NodeKindRole, "nodeKind"},
            {AccessRole, "access"}, {StatusRole, "status"}, {TypeRole, "type"},
            {BaseTypeRole, "baseType"}, {UnitsRole, "units"},
            {DisplayHintRole, "displayHint"}, {RangesRole, "ranges"},
            {NamedValuesRole, "namedValues"},
            {DescriptionRole, "description"}, {ReferenceRole, "reference"},
            {SearchTextRole, "searchText"}};
}

QString MibTreeModel::oidForIndex(const QModelIndex &index) const
{
    return data(index, OidRole).toString();
}

QModelIndex MibTreeModel::findOid(Item *item, const QString &oid) const
{
    for (int row = 0; row < int(item->children.size()); ++row) {
        Item *child = item->children.at(row).get();
        if (child->value.oid == oid)
            return createIndex(row, 0, child);
        const QModelIndex nested = findOid(child, oid);
        if (nested.isValid())
            return nested;
    }
    return {};
}

QModelIndex MibTreeModel::indexForOid(const QString &oid) const
{
    return rootItem ? findOid(rootItem.get(), oid) : QModelIndex();
}

MibTreeFilterModel::MibTreeFilterModel(QObject *parent) : QSortFilterProxyModel(parent)
{
    setFilterRole(MibTreeModel::SearchTextRole);
    setRecursiveFilteringEnabled(true);
    setFilterCaseSensitivity(Qt::CaseInsensitive);
}

void MibTreeFilterModel::setVisibleModules(const QStringList &modules)
{
    moduleFilterEnabled = true;
    allowedModules = QSet<QString>(modules.cbegin(), modules.cend());
    invalidateFilter();
}

void MibTreeFilterModel::showAllModules()
{
    moduleFilterEnabled = false;
    allowedModules.clear();
    invalidateFilter();
}

bool MibTreeFilterModel::acceptsModuleOrDescendant(const QModelIndex &sourceIndex) const
{
    if (!moduleFilterEnabled ||
        allowedModules.contains(sourceIndex.data(MibTreeModel::ModuleRole).toString())) return true;
    for (int row = 0; row < sourceModel()->rowCount(sourceIndex); ++row)
        if (acceptsModuleOrDescendant(sourceModel()->index(row, 0, sourceIndex))) return true;
    return false;
}

bool MibTreeFilterModel::filterAcceptsRow(int sourceRow,
                                         const QModelIndex &sourceParent) const
{
    const QModelIndex index = sourceModel()->index(sourceRow, 0, sourceParent);
    return acceptsModuleOrDescendant(index) &&
           QSortFilterProxyModel::filterAcceptsRow(sourceRow, sourceParent);
}
