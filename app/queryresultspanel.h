#ifndef QUERYRESULTSPANEL_H
#define QUERYRESULTSPANEL_H

#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QStringList>
#include <QWidget>

class QComboBox;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QTableWidget;
class QTableWidgetItem;

// Copied presentation values: no parser pointers or model indexes survive a profile reset.
struct QueryResultValue
{
    QString object, instance, oid, syntax, type, interpreted, raw, received, warnings, time;
};
struct QueryResultTableRow
{
    QString instance, index;
    QList<QueryResultValue> cells; // an empty OID denotes an unavailable cell
};

class QueryResultsPanel : public QWidget
{
    Q_OBJECT
public:
    enum Role { OidRole = Qt::UserRole + 1, InterpretedRole, RawRole, WarningRole,
                SearchRole, MissingRole, SortRole };
    explicit QueryResultsPanel(QWidget *parent = nullptr);
    void addControl(QWidget *widget);
    void beginQuery(const QString &object, const QString &oid);
    void appendValue(const QueryResultValue &value);
    void showTable(const QStringList &columns, const QList<QueryResultTableRow> &rows);
    void finishQuery();
    QTableWidget *table() const { return results; }
    bool isTableResult() const { return tableMode; }
    QString oidAt(int row, int column) const;
    QList<QStringList> csvRecords() const;
    int warningCount() const { return warnings; }
signals:
    void queryStarted();
    void watchRequested(const QString &oid);
    void warningsChanged(int count);
private:
    void configure(bool table, const QStringList &columns = {});
    QTableWidgetItem *valueItem(const QueryResultValue &value, const QString &text, bool warn);
    void updateDisplay();
    void filterRows();
    bool matchesFilter(int row) const;
    void updateSummary();
    void showColumnMenu(const QPoint &position);
    void showValueMenu(const QPoint &position);
    QHBoxLayout *controls;
    QTableWidget *results;
    QLabel *summary;
    QLineEdit *filter;
    QComboBox *display;
    QElapsedTimer elapsed;
    QString queryObject, queryOid;
    qint64 finishedMs = -1;
    bool tableMode = false;
    int warnings = 0, layoutGeneration = 0;
    QHash<QString, bool> flatVisibility, tableVisibility;
};
#endif
