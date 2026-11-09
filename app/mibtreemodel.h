#ifndef MIBTREEMODEL_H
#define MIBTREEMODEL_H

#include "mibrecords.h"

#include <QAbstractItemModel>
#include <QSortFilterProxyModel>
#include <QSet>
#include <memory>
#include <vector>

class MibTreeModel : public QAbstractItemModel
{
    Q_OBJECT
public:
    enum Role { OidRole = Qt::UserRole + 1, ModuleRole, NodeKindRole,
                AccessRole, StatusRole, TypeRole, BaseTypeRole, DisplayHintRole,
                RangesRole, NamedValuesRole, UnitsRole, DescriptionRole,
                ReferenceRole, SearchTextRole };
    explicit MibTreeModel(QObject *parent = nullptr);
    void setSnapshot(const MibTreeNodeRecord &root);
    QString oidForIndex(const QModelIndex &index) const;
    QModelIndex indexForOid(const QString &oid) const;
    QModelIndex index(int row, int column, const QModelIndex &parent = {}) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

private:
    struct Item {
        MibTreeNodeRecord value;
        Item *parent = nullptr;
        std::vector<std::unique_ptr<Item>> children;
    };
    static std::unique_ptr<Item> makeItem(const MibTreeNodeRecord &record, Item *parent);
    QModelIndex findOid(Item *item, const QString &oid) const;
    std::unique_ptr<Item> rootItem;
};

class MibTreeFilterModel : public QSortFilterProxyModel
{
    Q_OBJECT
public:
    explicit MibTreeFilterModel(QObject *parent = nullptr);
    void setVisibleModules(const QStringList &modules);
    void showAllModules();
    QStringList visibleModules() const { return allowedModules.values(); }
protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;
private:
    bool acceptsModuleOrDescendant(const QModelIndex &sourceIndex) const;
    QSet<QString> allowedModules;
    bool moduleFilterEnabled = false;
};

#endif
