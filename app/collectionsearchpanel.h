#ifndef COLLECTIONSEARCHPANEL_H
#define COLLECTIONSEARCHPANEL_H

#include <QSet>
#include <QWidget>
#include <functional>

class MibCollectionService;
class QLabel;
class QTableWidget;

// The Search tab: results of a search over every MIB file in the collection, with the profiles
// that contain each match.
class CollectionSearchPanel : public QWidget
{
    Q_OBJECT
public:
    struct Lookups
    {
        // Names of the profiles whose environment includes this module.
        std::function<QStringList(const QString &module)> profilesContaining;
        // Modules in the active environment (so the Tree can show them).
        std::function<QSet<QString>()> activeModules;
    };
    CollectionSearchPanel(MibCollectionService *service, Lookups lookups, QWidget *parent = nullptr);

    void search(const QString &query);
    int resultCount() const;

signals:
    void showInTreeRequested(const QString &oid);
    void openInEditorRequested(const QString &file, int line);

private:
    void activate(int row);

    MibCollectionService *service;
    Lookups lookups;
    QLabel *summary;
    QTableWidget *table;
    QString lastQuery;
};

#endif
