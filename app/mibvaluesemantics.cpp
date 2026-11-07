#include "mibvaluesemantics.h"

#include <QObject>
#include <QSet>
#include <functional>
#include <cstdint>

namespace {
QString numericOid(const Oid &oid) { return QString::fromLatin1(oid.get_printable()); }
qint64 namedNumber(const MibEnvironmentNamedValue &v)
{ return v.value.isSigned ? v.value.signedValue : qint64(v.value.unsignedValue); }
QString typeName(const MibEnvironmentNodeRecord *n)
{
    if (!n) return {};
    return (QStringList{n->syntaxName, n->textualConventionId} +
            n->textualConventionAncestry).join('|');
}
bool integerConstraint(const MibEnvironmentConstraint &c, qint64 value)
{
    const qint64 lo=c.minimum.isSigned?c.minimum.signedValue:qint64(c.minimum.unsignedValue);
    const qint64 hi=c.maximum.isSigned?c.maximum.signedValue:qint64(c.maximum.unsignedValue);
    return value>=lo && value<=hi;
}
bool unsignedConstraint(const MibEnvironmentConstraint &c, quint64 value)
{
    if (c.minimum.isSigned && c.minimum.signedValue < 0) return false;
    const quint64 lo=c.minimum.isSigned?quint64(c.minimum.signedValue):c.minimum.unsignedValue;
    const quint64 hi=c.maximum.isSigned?quint64(c.maximum.signedValue):c.maximum.unsignedValue;
    return value>=lo && value<=hi;
}
}

MibResolvedObject ResolveMibObject(const MibEnvironmentPtr &environment,
                                   const Oid &oid)
{
    MibResolvedObject r; r.environment=environment;
    if (environment) r.node=environment->longestPrefixNode(numericOid(oid), &r.instanceSuffix);
    return r;
}

QString RenderMibOid(const MibEnvironmentPtr &environment, const Oid &oid)
{
    const auto r=ResolveMibObject(environment, oid);
    if (!r.node || r.node->name.isEmpty()) return numericOid(oid);
    return r.node->name+(r.instanceSuffix.isEmpty()?QString():QStringLiteral(".")+r.instanceSuffix.join('.'));
}

QString RenderMibValue(const MibResolvedObject &object, const Vb &source)
{
    Vb vb=source;
    const int syntax = int(vb.get_syntax());
    if (syntax == sNMP_SYNTAX_NULL || syntax == sNMP_SYNTAX_NOSUCHOBJECT ||
        syntax == sNMP_SYNTAX_NOSUCHINSTANCE || syntax == sNMP_SYNTAX_ENDOFMIBVIEW)
        return SnmpSyntaxName(syntax);
    const auto *node=object.node;
    if (!node) return QString::fromLatin1(vb.get_printable_value());
    if (node->baseType==MibEnvironmentBaseType::Enumeration) {
        long value=0; if (vb.get_value(value)==SNMP_CLASS_SUCCESS) {
            for (const auto &n:node->namedValues) if (namedNumber(n)==value)
                return QStringLiteral("%1(%2)").arg(n.name).arg(value);
            return QString::number(value);
        }
    }
    if (node->baseType==MibEnvironmentBaseType::ObjectIdentifier) {
        Oid value; if (vb.get_value(value)==SNMP_CLASS_SUCCESS)
            return RenderMibOid(object.environment, value);
    }
    if (node->baseType==MibEnvironmentBaseType::Bits) {
        unsigned char bytes[65536]; unsigned long length=0;
        if (vb.get_value(bytes,length,sizeof bytes)==SNMP_CLASS_SUCCESS) {
            QStringList values; QSet<int> modeled;
            for (const auto &n:node->namedValues) {
                const int bit=int(n.value.unsignedValue); modeled.insert(bit);
                if (bit>=0 && bit/8<int(length) && (bytes[bit/8]&(0x80>>(bit%8)))) values<<n.name;
            }
            for (int bit=0;bit<int(length)*8;++bit)
                if (!modeled.contains(bit) && (bytes[bit/8]&(0x80>>(bit%8)))) values<<QStringLiteral("bit%1").arg(bit);
            return values.join(' ');
        }
    }
    if (node->baseType==MibEnvironmentBaseType::OctetString &&
        node->displayHint==QStringLiteral("1x:")) {
        unsigned char bytes[65536]; unsigned long length=0;
        if (vb.get_value(bytes,length,sizeof bytes)==SNMP_CLASS_SUCCESS) {
            QStringList values;
            for (unsigned long i=0;i<length;++i)
                values<<QStringLiteral("%1").arg(bytes[i],2,16,QChar('0'));
            return values.join(':');
        }
    }
    if (node->baseType==MibEnvironmentBaseType::Unsigned64) {
        Counter64 value; if (vb.get_value(value)==SNMP_CLASS_SUCCESS)
            return QString::number(Counter64::c64_to_ll(value));
    }
    return QString::fromLatin1(vb.get_printable_value());
}

QString SnmpSyntaxName(int syntax)
{
    switch (syntax) {
    case sNMP_SYNTAX_INT32: return QStringLiteral("Integer32");
    case sNMP_SYNTAX_OCTETS: return QStringLiteral("OCTET STRING");
    case sNMP_SYNTAX_BITS: return QStringLiteral("BITS");
    case sNMP_SYNTAX_NULL: return QStringLiteral("NULL");
    case sNMP_SYNTAX_OID: return QStringLiteral("OBJECT IDENTIFIER");
    case sNMP_SYNTAX_IPADDR: return QStringLiteral("IpAddress");
    case sNMP_SYNTAX_CNTR32: return QStringLiteral("Counter32");
    case sNMP_SYNTAX_GAUGE32: return QStringLiteral("Gauge32");
    case sNMP_SYNTAX_TIMETICKS: return QStringLiteral("TimeTicks");
    case sNMP_SYNTAX_OPAQUE: return QStringLiteral("Opaque");
    case sNMP_SYNTAX_CNTR64: return QStringLiteral("Counter64");
    case sNMP_SYNTAX_NOSUCHOBJECT: return QStringLiteral("noSuchObject");
    case sNMP_SYNTAX_NOSUCHINSTANCE: return QStringLiteral("noSuchInstance");
    case sNMP_SYNTAX_ENDOFMIBVIEW: return QStringLiteral("endOfMibView");
    default: return QStringLiteral("unknown type 0x%1").arg(syntax, 0, 16);
    }
}

namespace {
QString rangeText(const MibEnvironmentConstraint &c)
{
    const auto text = [](const MibEnvironmentValue &v) {
        return v.isSigned ? QString::number(v.signedValue) : QString::number(v.unsignedValue);
    };
    return c.minimum.isSigned == c.maximum.isSigned && text(c.minimum) == text(c.maximum)
        ? text(c.minimum) : QStringLiteral("%1..%2").arg(text(c.minimum), text(c.maximum));
}

// The MIB's ranges (or sizes) are alternatives: a value is fine if it fits any of them.
QString outsideRanges(const QList<MibEnvironmentConstraint> &constraints, bool sizes,
                      const std::function<bool(const MibEnvironmentConstraint &)> &fits)
{
    QStringList ranges; bool any = false;
    for (const MibEnvironmentConstraint &c : constraints) {
        if (c.isSizeConstraint != sizes) continue;
        any = true;
        if (fits(c)) return {};
        ranges << rangeText(c);
    }
    return any ? ranges.join(QStringLiteral(" | ")) : QString();
}

bool sameWireType(int expected, int received)
{
    if (expected == received) return true;
    if (expected == sNMP_SYNTAX_BITS && received == sNMP_SYNTAX_OCTETS) return true;  // BITS travel as OCTET STRING
    return false;
}
}

MibValueCheck CheckMibValue(const MibResolvedObject &object, const Vb &source)
{
    Vb vb = source;
    MibValueCheck result;
    const int received = int(vb.get_syntax());
    result.receivedType = SnmpSyntaxName(received);
    result.rawValue = QString::fromLatin1(vb.get_printable_value());
    if (received == sNMP_SYNTAX_NOSUCHOBJECT || received == sNMP_SYNTAX_NOSUCHINSTANCE ||
        received == sNMP_SYNTAX_ENDOFMIBVIEW || received == sNMP_SYNTAX_NULL) {
        result.rawValue = result.receivedType;
        return result;
    }
    const MibEnvironmentNodeRecord *node = object.node;
    if (!node) return result;                       // not in the active MIB Profile: nothing to compare with

    const bool knownBase = node->baseType == MibEnvironmentBaseType::Integer32 ||
        node->baseType == MibEnvironmentBaseType::Enumeration ||
        node->baseType == MibEnvironmentBaseType::Unsigned32 ||
        node->baseType == MibEnvironmentBaseType::Unsigned64 ||
        node->baseType == MibEnvironmentBaseType::OctetString ||
        node->baseType == MibEnvironmentBaseType::ObjectIdentifier ||
        node->baseType == MibEnvironmentBaseType::Bits;
    if (!knownBase) return result;

    const int expected = SnmpSyntaxForMibNode(node);
    if (!sameWireType(expected, received))
        result.warnings << QObject::tr("Wrong type: the device sent %1 but the MIB defines %2 as %3.")
            .arg(result.receivedType, node->name, SnmpSyntaxName(expected));

    if (received == sNMP_SYNTAX_INT32) {
        long value = 0;
        if (vb.get_value(value) == SNMP_CLASS_SUCCESS) {
            if (node->baseType == MibEnvironmentBaseType::Enumeration && !node->namedValues.isEmpty()) {
                bool defined = false; QStringList names;
                for (const auto &n : node->namedValues) {
                    defined = defined || namedNumber(n) == value;
                    names << QStringLiteral("%1(%2)").arg(n.name).arg(namedNumber(n));
                }
                if (!defined)
                    result.warnings << QObject::tr("Undefined value: %1 is not one of the values the MIB defines (%2).")
                        .arg(value).arg(names.join(QStringLiteral(", ")));
            }
            const QString ranges = outsideRanges(node->constraints, false,
                [value](const MibEnvironmentConstraint &c) { return integerConstraint(c, value); });
            if (!ranges.isEmpty())
                result.warnings << QObject::tr("Out of range: %1 is outside the range the MIB allows (%2).")
                    .arg(value).arg(ranges);
        }
    } else if (received == sNMP_SYNTAX_GAUGE32 || received == sNMP_SYNTAX_CNTR32 ||
               received == sNMP_SYNTAX_TIMETICKS) {
        unsigned long value = 0;
        if (vb.get_value(value) == SNMP_CLASS_SUCCESS) {
            const QString ranges = outsideRanges(node->constraints, false,
                [value](const MibEnvironmentConstraint &c) { return unsignedConstraint(c, quint64(value)); });
            if (!ranges.isEmpty())
                result.warnings << QObject::tr("Out of range: %1 is outside the range the MIB allows (%2).")
                    .arg(quint64(value)).arg(ranges);
        }
    } else if (received == sNMP_SYNTAX_OCTETS || received == sNMP_SYNTAX_OPAQUE) {
        unsigned char bytes[65536]; unsigned long length = 0;
        if (vb.get_value(bytes, length, sizeof bytes) == SNMP_CLASS_SUCCESS) {
            const QString sizes = outsideRanges(node->constraints, true,
                [length](const MibEnvironmentConstraint &c) { return unsignedConstraint(c, quint64(length)); });
            if (!sizes.isEmpty())
                result.warnings << QObject::tr("Wrong size: the value is %n byte(s) long, outside the size the MIB allows (%1).", nullptr, int(length))
                    .arg(sizes);
        }
    }
    return result;
}

int SnmpSyntaxForMibNode(const MibEnvironmentNodeRecord *node)
{
    if (!node) return sNMP_SYNTAX_OCTETS;
    const QString name=typeName(node);
    switch(node->baseType) {
    case MibEnvironmentBaseType::Integer32:
    case MibEnvironmentBaseType::Enumeration:return sNMP_SYNTAX_INT32;
    case MibEnvironmentBaseType::Unsigned32:
        if (name.contains("TimeTicks",Qt::CaseInsensitive)) return sNMP_SYNTAX_TIMETICKS;
        if (name.contains("Counter32",Qt::CaseInsensitive)||name.contains("COUNTER")) return sNMP_SYNTAX_CNTR32;
        if (name.contains("Gauge32",Qt::CaseInsensitive)||name.contains("GAUGE")) return sNMP_SYNTAX_GAUGE32;
        return sNMP_SYNTAX_UINT32;
    case MibEnvironmentBaseType::OctetString:
        if (name.contains("IpAddress",Qt::CaseInsensitive)) return sNMP_SYNTAX_IPADDR;
        if (name.contains("Opaque",Qt::CaseInsensitive)) return sNMP_SYNTAX_OPAQUE;
        return sNMP_SYNTAX_OCTETS;
    case MibEnvironmentBaseType::Bits:return sNMP_SYNTAX_BITS;
    case MibEnvironmentBaseType::ObjectIdentifier:return sNMP_SYNTAX_OID;
    case MibEnvironmentBaseType::Unsigned64:return sNMP_SYNTAX_CNTR64;
    default:return sNMP_SYNTAX_OCTETS;
    }
}

bool IsWritableMibNode(const MibEnvironmentNodeRecord *node)
{ return node && node->access==MibEnvironmentAccess::ReadWrite; }

bool ValidateMibSetValue(const MibEnvironmentNodeRecord *node, int syntax,
                         const QString &value, QString *error)
{
    auto fail=[&](const QString &s){if(error)*error=s;return false;};
    if (!IsWritableMibNode(node)) return fail(QStringLiteral("Object is not writable"));
    bool ok=false; qint64 signedValue=0; quint64 unsignedValue=0;
    if (syntax==sNMP_SYNTAX_INT32) {
        signedValue=value.toLongLong(&ok); if(!ok||signedValue<INT32_MIN||signedValue>INT32_MAX)return fail(QStringLiteral("Invalid integer value"));
        if(node->baseType==MibEnvironmentBaseType::Enumeration&&!node->namedValues.isEmpty()){
            bool found=false;for(const auto&n:node->namedValues)found|=namedNumber(n)==signedValue;
            if(!found)return fail(QStringLiteral("Invalid enumeration value"));}
    } else if (syntax==sNMP_SYNTAX_CNTR32||syntax==sNMP_SYNTAX_GAUGE32||syntax==sNMP_SYNTAX_UINT32||syntax==sNMP_SYNTAX_TIMETICKS) {
        unsignedValue=value.toULongLong(&ok);if(!ok||unsignedValue>UINT32_MAX)return fail(QStringLiteral("Invalid unsigned value"));
    } else if (syntax==sNMP_SYNTAX_CNTR64) { unsignedValue=value.toULongLong(&ok);if(!ok)return fail(QStringLiteral("Invalid Counter64 value")); }
    else if (syntax==sNMP_SYNTAX_OID) { Oid oid(value.toLatin1().constData());if(!value.isEmpty()&&!oid.valid())return fail(QStringLiteral("Invalid Object Identifier value")); }
    else if (syntax==sNMP_SYNTAX_IPADDR) { IpAddress ip(value.toLatin1().constData());if(!value.isEmpty()&&!ip.valid())return fail(QStringLiteral("Invalid IP address value")); }
    for(const auto&c:node->constraints){
        if(c.isSizeConstraint){const quint64 size=quint64(value.toLatin1().size());if(!unsignedConstraint(c,size))return fail(QStringLiteral("Value violates SIZE constraint"));}
        else if(syntax==sNMP_SYNTAX_INT32){if(!integerConstraint(c,signedValue))return fail(QStringLiteral("Value violates range constraint"));}
        else if(ok&&!unsignedConstraint(c,unsignedValue))return fail(QStringLiteral("Value violates range constraint"));
    }
    return true;
}
