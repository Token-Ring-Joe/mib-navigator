#ifndef MIBMODELVIEW_H
#define MIBMODELVIEW_H

#include <QTreeView>

class MibTreeFilterModel;
class MibTreeModel;
class MibViewLoader;

class MibModelView : public QTreeView
{
    Q_OBJECT
public:
    enum class Mode { Query, OidSelection };
    explicit MibModelView(QWidget *parent = nullptr, Mode mode = Mode::Query);
    void RegisterToLoader(MibViewLoader *loader);
    void setTreeModel(MibTreeModel *model);
    void Populate();
    void SelectFromOid(const QString &oid);
    // Like SelectFromOid, but says whether the active tree has that object.
    bool LocateOid(const QString &oid);
    void SetCurrentAgentIsV1(bool value) { agentIsV1 = value; }
    QString selectedOid() const { return currentOid(); }
    void setVisibleModules(const QStringList &modules);
    void showAllModules();
    QStringList visibleModules() const;
    bool queryTableAvailable() const;
    QString currentObjectOid() const { return currentOid(); }

signals:
    void NodeProperties(const QString &text);
    void WalkFromOid(const QString &oid);
    void GetFromOid(const QString &oid, int operation);
    void GetFromOidPromptInstance(const QString &oid, int operation);
    void GetFromOidSelectInstance(const QString &oid, int operation);
    void SetFromOid(const QString &oid);
    void Stop();
    void WatchFromOid(const QString &oid, bool column);
    void TableViewFromOid(const QString &oid);
    void TableRowSelectFromOid(const QString &oid);
    void GetTableInstancesFromOid(const QString &oid);
    void MacroFromOid(const QString &oid);
    void QueryTableAvailabilityChanged(bool available);
    void CurrentObjectChanged(const QString &oid);

public slots:
    void SetWalkInProgress(bool value) { walkInProgress = value; }
    void FindFromNode();
    void ExecuteFindNext();
    void QueryTableFromCurrent();
    void SetQueryPrerequisitesAvailable(bool available);
    void SetFilterText(const QString &text);
    void QueryCurrent(int operation);
    void WalkCurrent();
    void SetCurrent();

protected:
    void contextMenuEvent(QContextMenuEvent *event) override;
    void currentChanged(const QModelIndex &current, const QModelIndex &previous) override;

private:
    QString currentOid() const;
    int currentKind() const;
    QString detailsFor(const QModelIndex &proxyIndex) const;
    void updateQueryTableAvailability();
    QModelIndex proxyIndexForOid(const QString &oid) const;
    void restoreSelection();
    MibTreeFilterModel *filterModel;
    Mode interactionMode;
    QString retainedOid;
    bool walkInProgress = false;
    bool agentIsV1 = true;
    QString searchText;
    bool queryPrerequisitesAvailable = false;
};

#endif
