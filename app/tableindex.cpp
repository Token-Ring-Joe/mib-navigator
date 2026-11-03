#include "tableindex.h"

#include <QSet>
#include <QStringList>

namespace
{
QString dotted(const QVector<quint32> &values, qsizetype from, qsizetype count)
{
    QStringList parts;
    for (qsizetype i = 0; i < count; ++i)
        parts.append(QString::number(values.at(from + i)));
    return parts.join(QLatin1Char('.'));
}

QString octetText(const QVector<quint32> &values, qsizetype from, qsizetype count,
                  bool allowText)
{
    QByteArray bytes;
    bool printable = allowText;
    for (qsizetype i = 0; i < count; ++i) {
        const char byte = static_cast<char>(values.at(from + i));
        bytes.append(byte);
        if (byte < 0x20 || byte > 0x7e) printable = false;
    }
    if (printable) return QStringLiteral("\"%1\"").arg(QString::fromLatin1(bytes));
    QStringList hex;
    for (const char byte : bytes)
        hex.append(QStringLiteral("%1").arg(static_cast<uchar>(byte), 2, 16, QLatin1Char('0')));
    return hex.join(QLatin1Char(':'));
}
}

bool DecodeTableInstance(const QVector<TableIndexSpec> &specs,
                         const QVector<quint32> &suffix, QString *text)
{
    if (!text || specs.isEmpty() || suffix.isEmpty()) return false;
    QStringList parts;
    qsizetype pos = 0;
    const qsizetype total = suffix.size();
    for (qsizetype i = 0; i < specs.size(); ++i) {
        const TableIndexSpec &spec = specs.at(i);
        const bool last = i + 1 == specs.size();
        QString value;
        switch (spec.baseType) {
        case MibEnvironmentBaseType::Integer32:
        case MibEnvironmentBaseType::Enumeration:
            if (pos >= total) return false;
            value = QString::number(static_cast<qint32>(suffix.at(pos++)));
            break;
        case MibEnvironmentBaseType::Unsigned32:
            if (pos >= total) return false;
            value = QString::number(suffix.at(pos++));
            break;
        case MibEnvironmentBaseType::OctetString:
        case MibEnvironmentBaseType::ObjectIdentifier: {
            const bool isOid = spec.baseType == MibEnvironmentBaseType::ObjectIdentifier;
            qsizetype count = 0;
            if (!isOid && spec.ipAddress) count = 4;
            else if (!isOid && spec.fixedSize >= 0) count = spec.fixedSize;
            else if (spec.implied && last) count = total - pos;
            else {
                if (pos >= total) return false;
                count = suffix.at(pos++);
            }
            if (count < 0 || pos + count > total) return false;
            if (isOid) {
                value = count ? dotted(suffix, pos, count) : QStringLiteral("0.0");
            } else {
                for (qsizetype k = 0; k < count; ++k)
                    if (suffix.at(pos + k) > 255) return false;
                value = spec.ipAddress ? dotted(suffix, pos, count)
                                       : octetText(suffix, pos, count, spec.fixedSize < 0);
            }
            pos += count;
            break;
        }
        default:
            return false;
        }
        parts.append(spec.name.isEmpty() ? value : QStringLiteral("%1=%2").arg(spec.name, value));
    }
    if (pos != total) return false;
    *text = parts.join(QStringLiteral(", "));
    return true;
}

namespace
{
bool typeHasName(const MibEnvironment &environment, const QString &typeId, const QString &name)
{
    QSet<QString> seen;
    for (QString id = typeId; !id.isEmpty() && !seen.contains(id);) {
        seen.insert(id);
        const MibEnvironmentTypeRecord *type = environment.type(id);
        if (!type) return false;
        if (type->name == name) return true;
        id = type->parentTypeId;
    }
    return false;
}

// A single SIZE (n) constraint on the type or the nearest ancestor that has one.
int fixedSizeOf(const MibEnvironment &environment, const QString &typeId)
{
    QSet<QString> seen;
    for (QString id = typeId; !id.isEmpty() && !seen.contains(id);) {
        seen.insert(id);
        const MibEnvironmentTypeRecord *type = environment.type(id);
        if (!type) return -1;
        QList<MibEnvironmentConstraint> sizes;
        for (const MibEnvironmentConstraint &constraint : type->constraints)
            if (constraint.isSizeConstraint) sizes.append(constraint);
        if (!sizes.isEmpty()) {
            if (sizes.size() == 1 &&
                sizes.first().minimum.unsignedValue == sizes.first().maximum.unsignedValue)
                return static_cast<int>(sizes.first().minimum.unsignedValue);
            return -1;
        }
        id = type->parentTypeId;
    }
    return -1;
}
}

QVector<TableIndexSpec> TableIndexSpecsForRow(const MibEnvironment &environment,
                                              const MibEnvironmentNodeRecord &row)
{
    const MibEnvironmentNodeRecord *declaring = &row;
    QSet<QString> seen;
    while (declaring->indexObjects.isEmpty() && !declaring->augmentsRowOid.isEmpty() &&
           !seen.contains(declaring->oid)) {
        seen.insert(declaring->oid);
        const MibEnvironmentNodeRecord *base = environment.nodeByOid(declaring->augmentsRowOid);
        if (!base) return {};
        declaring = base;
    }
    QVector<TableIndexSpec> specs;
    for (qsizetype i = 0; i < declaring->indexObjects.size(); ++i) {
        const MibEnvironmentIndexObject &object = declaring->indexObjects.at(i);
        TableIndexSpec spec;
        spec.name = object.qualifiedName.section(QStringLiteral("::"), -1);
        spec.baseType = object.baseType;
        spec.implied = declaring->implied && i + 1 == declaring->indexObjects.size();
        if (object.baseType == MibEnvironmentBaseType::OctetString) {
            spec.ipAddress = typeHasName(environment, object.typeId, QStringLiteral("IpAddress"));
            spec.fixedSize = spec.ipAddress ? 4 : fixedSizeOf(environment, object.typeId);
        }
        specs.append(spec);
    }
    return specs;
}
