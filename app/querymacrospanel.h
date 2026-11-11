#ifndef QUERYMACROSPANEL_H
#define QUERYMACROSPANEL_H
#include "querymacroservice.h"
#include <QPointer>
#include <QWidget>
#include <functional>
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QTableWidget;

class QueryMacrosPanel final : public QWidget {
    Q_OBJECT
public:
    using ObjectPicker = std::function<std::optional<QueryMacroEntry>(QWidget *, QString *)>;
    using ObjectLabel = std::function<QString(const QString &)>;
    QueryMacrosPanel(QueryMacroService *service, ObjectPicker picker, ObjectLabel label,
                     QWidget *parent = nullptr);
    QString selectedMacroId() const;
    void selectMacro(const QString &id);
    bool addObject(QueryMacroEntry entry);
    void showError(const QString &message);
    void showNote(const QString &message);
    void setQueryBusy(bool busy);
signals:
    void runRequested(const QueryMacroDefinition &definition);
protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
private:
    void refresh(bool rebuildList = true);
    void updateEntryButtons();
    QString selectedEntryId() const;
    void editEntry(bool createExact);
    void pickObject();
    bool confirmDiscard();
    QPointer<QueryMacroService> service;
    QPointer<QWidget> closeWindow;
    ObjectPicker picker;
    ObjectLabel objectLabel;
    QListWidget *macros;
    QLineEdit *name;
    QTableWidget *entries;
    QLabel *status;
    QPushButton *saveButton, *newButton, *duplicateButton, *deleteButton, *macroUp, *macroDown;
    QPushButton *addButton, *exactButton, *editButton, *duplicateEntryButton, *removeButton, *entryUp, *entryDown;
    QString localError;
    QString localNote;   // what the last add did; shown until the definitions change again
    QPushButton *runButton;
    bool queryBusy = false;
};
#endif
