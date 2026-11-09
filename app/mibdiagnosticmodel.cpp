#include "mibdiagnosticmodel.h"


MibDiagnosticModel::MibDiagnosticModel(QObject *parent) : QAbstractTableModel(parent) {}

void MibDiagnosticModel::setDiagnostics(const QList<MibDiagnosticRecord> &diagnostics)
{
    beginResetModel(); records = diagnostics; endResetModel();
}

int MibDiagnosticModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : records.size();
}

int MibDiagnosticModel::columnCount(const QModelIndex &) const { return ColumnCount; }

QVariant MibDiagnosticModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= records.size()) return {};
    const auto &record = records.at(index.row());
    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case SeverityColumn:
            if (record.severity <= 3) return tr("Error (%1)").arg(record.severity);
            if (record.severity <= 5) return tr("Warning (%1)").arg(record.severity);
            return tr("Info (%1)").arg(record.severity);
        case SourceColumn: return record.sourcePath;
        case LineColumn: return record.line;
        case MessageColumn: return record.message;
        default: return {};
        }
    }
    switch (role) {
    case SeverityRole: return record.severity;
    case TagRole: return record.tag;
    case ModuleRole: return record.module;
    case SourcePathRole: return record.sourcePath;
    case LineRole: return record.line;
    case RawTextRole: return record.rawText;
    case OperationRole: return QVariant::fromValue(record.operationId);
    default: return {};
    }
}

QVariant MibDiagnosticModel::headerData(int section, Qt::Orientation orientation,
                                        int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
    switch (section) {
    case SeverityColumn: return tr("Severity");
    case SourceColumn: return tr("Module / file");
    case LineColumn: return tr("Line");
    case MessageColumn: return tr("Diagnostic");
    default: return {};
    }
}
