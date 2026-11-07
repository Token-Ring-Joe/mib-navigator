#include "trappresenter.h"
#include "mibenvironmentregistry.h"
#include "mibvaluesemantics.h"
#include <QCoreApplication>

namespace {
QString versionText(TrapSnmpVersion version)
{
    switch (version) {
    case TrapSnmpVersion::V1: return QStringLiteral("SNMPv1");
    case TrapSnmpVersion::V2c: return QStringLiteral("SNMPv2c");
    case TrapSnmpVersion::V3: return QStringLiteral("SNMPv3");
    default: return QStringLiteral("Unknown");
    }
}
QStringList summaryFor(const TrapRecord &record, const MibEnvironmentPtr &environment)
{
    return {QString("%1").arg(record.recordId, 4, 10, QChar('0')),
        record.receivedTimestamp.date().toString(Qt::ISODate),
        record.receivedTimestamp.time().toString(Qt::ISODate),
        QString::number(record.notificationTicks),
        RenderMibOid(environment, Oid(record.notificationOid.toLatin1().constData())),
        record.messageType, versionText(record.snmpVersion), record.sourceAddress,
        QString::number(record.sourcePort)};
}
}

QString TrapPresenter::symbolicOid(const QString &oid)
{
    return RenderMibOid(MibEnvironmentRegistry::active(), Oid(oid.toLatin1().constData()));
}

QString TrapPresenter::formattedValue(const TrapVarbind &binding)
{
    Oid oid;
    binding.value.get_oid(oid);
    return RenderMibValue(ResolveMibObject(MibEnvironmentRegistry::active(), oid), binding.value);
}

QString TrapPresenter::sourceToCopy(const TrapRecord &record)
{
    const QString agent = record.agentAddress.trimmed();
    if (record.snmpVersion == TrapSnmpVersion::V1 && !agent.isEmpty() &&
        agent != QLatin1String("0.0.0.0") && agent != QLatin1String("::") &&
        agent.compare(record.sourceAddress.trimmed(), Qt::CaseInsensitive) != 0)
        return agent;
    return record.sourceAddress.trimmed();
}

QStringList TrapPresenter::summary(const TrapRecord &record) const
{
    return summaryFor(record, MibEnvironmentRegistry::active());
}

TrapPresentation TrapPresenter::present(const TrapRecord &record) const
{
    // One immutable environment for the entire presentation, including OID values.
    const auto environment = MibEnvironmentRegistry::active();
    TrapPresentation result;
    result.summaryColumns = summaryFor(record, environment);
    result.communityText = record.snmpVersion == TrapSnmpVersion::V3
        ? QCoreApplication::translate("Trap", "USM user: %1 · %2").arg(record.securityName, record.securityLevel)
        : QCoreApplication::translate("Trap", "Community: %1").arg(record.community);
    for (const auto &binding : record.varbinds) {
        TrapBindingPresentation value;
        if (!binding.valid) {
            value.warnings << QCoreApplication::translate("Trap", "The received binding could not be decoded.");
            result.bindings << value;
            continue;
        }
        Oid oid;
        binding.value.get_oid(oid);
        const auto object = ResolveMibObject(environment, oid);
        const auto check = CheckMibValue(object, binding.value);
        value.oid = QString::fromLatin1(oid.get_printable());
        value.object = RenderMibOid(environment, oid);
        value.receivedType = check.receivedType;
        value.rawValue = check.rawValue;
        value.interpretedValue = RenderMibValue(object, binding.value);
        value.warnings = check.warnings;
        if (object.node) value.units = object.node->units;
        result.bindings << value;
    }
    const auto object = ResolveMibObject(environment, Oid(record.notificationOid.toLatin1().constData()));
    const auto *node = object.node;
    if (!node || node->kind != MibEnvironmentNodeKind::Notification || !object.instanceSuffix.isEmpty())
        return result;
    // Escape every MIB-supplied field and let the current palette supply colours.
    result.notificationHtml = QStringLiteral(
        "<table><tr><th align='left'>Name:</th><td>%1</td></tr>"
        "<tr><th align='left'>OID:</th><td>%2</td></tr>"
        "<tr><th align='left'>Units:</th><td>%3</td></tr>"
        "<tr><th align='left'>Module:</th><td>%4</td></tr>"
        "<tr><th align='left'>Reference:</th><td>%5</td></tr>"
        "<tr><th align='left'>Description:</th><td>%6</td></tr></table>")
        .arg(node->name.toHtmlEscaped(), node->oid.toHtmlEscaped(), node->units.toHtmlEscaped(),
             node->moduleIdentity.toHtmlEscaped(), node->reference.toHtmlEscaped(),
             node->description.toHtmlEscaped());
    return result;
}
