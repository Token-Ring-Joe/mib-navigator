#ifndef MIBDIAGNOSTICMODEL_H
#define MIBDIAGNOSTICMODEL_H

#include "mibrecords.h"

#include <QAbstractTableModel>

class MibDiagnosticModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    enum Column { SeverityColumn, SourceColumn, LineColumn, MessageColumn, ColumnCount };
    enum Role { SeverityRole = Qt::UserRole + 1, TagRole, ModuleRole,
                SourcePathRole, LineRole, RawTextRole, OperationRole };
    explicit MibDiagnosticModel(QObject *parent = nullptr);
    void setDiagnostics(const QList<MibDiagnosticRecord> &diagnostics);
    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
private:
    QList<MibDiagnosticRecord> records;
};

#endif
