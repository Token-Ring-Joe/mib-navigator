#include "trappresenter.h"
#include "mibenvironmentregistry.h"
#include "snmp_pp/smi.h"
#include <QCoreApplication>

namespace {
QString versionText(TrapSnmpVersion v){switch(v){case TrapSnmpVersion::V1:return "SNMPv1";case TrapSnmpVersion::V2c:return "SNMPv2c";case TrapSnmpVersion::V3:return "SNMPv3";default:return "Unknown";}}
QByteArray octets(const QString&p){QByteArray b;for(const QString&line:p.split('\n')){const int c=line.indexOf("   ",2);const QString h=c>=0?line.left(c):line;for(const QString&t:h.simplified().split(' ',Qt::SkipEmptyParts)){bool ok=false;const uint v=t.size()==2?t.toUInt(&ok,16):0;if(!ok)break;b.append(char(v));}}return b;}
const MibEnvironmentNodeRecord *nodeFor(const MibEnvironmentPtr&e,const QString&oid,QStringList*suffix=nullptr){if(!e)return nullptr;const auto exact=e->nodeByQualifiedName(oid);if(exact)return exact;const auto names=e->nodesByName(oid);if(names.size()==1)return names.first();return e->longestPrefixNode(oid,suffix);}
}
QString TrapPresenter::symbolicOid(const QString &oid){const auto e=MibEnvironmentRegistry::active();QStringList suffix;const auto*n=nodeFor(e,oid,&suffix);if(!n||n->name.isEmpty())return oid;return n->name+(suffix.isEmpty()?QString():QStringLiteral(".")+suffix.join('.'));}
QString TrapPresenter::formattedValue(const TrapVarbind &v){const auto e=MibEnvironmentRegistry::active();const auto*n=nodeFor(e,v.oid);if(!n)return v.printableValue;
 if(n->baseType==MibEnvironmentBaseType::Enumeration){bool ok=false;const qint64 x=v.printableValue.toLongLong(&ok);if(ok)for(const auto&i:n->namedValues)if((i.value.isSigned?i.value.signedValue:qint64(i.value.unsignedValue))==x)return QString("%1(%2)").arg(i.name).arg(x);}
 if(n->baseType==MibEnvironmentBaseType::ObjectIdentifier){const QString label=symbolicOid(v.printableValue);return label;}
 if(n->baseType==MibEnvironmentBaseType::Bits){const QByteArray b=octets(v.printableValue);QStringList names;for(const auto&i:n->namedValues){const int bit=int(i.value.unsignedValue);if(bit/8<b.size()&&(quint8(b[bit/8])&(0x80>>(bit%8))))names<<i.name;}if(!names.isEmpty())return names.join(" ");}
 if(n->baseType==MibEnvironmentBaseType::OctetString&&n->displayHint==QStringLiteral("1x:")){const QByteArray b=octets(v.printableValue);if(!b.isEmpty()){QStringList values;for(unsigned char c:b)values<<QString("%1").arg(c,2,16,QChar('0'));return values.join(':');}}
 return v.printableValue;}
QString TrapPresenter::sourceToCopy(const TrapRecord &record)
{
    const QString agent = record.agentAddress.trimmed();
    if (record.snmpVersion == TrapSnmpVersion::V1 && !agent.isEmpty() &&
        agent != QLatin1String("0.0.0.0") && agent != QLatin1String("::") &&
        agent.compare(record.sourceAddress.trimmed(), Qt::CaseInsensitive) != 0)
        return agent;
    return record.sourceAddress.trimmed();
}

TrapPresentation TrapPresenter::present(const TrapRecord&r,bool)const{TrapPresentation o;const QString notification=symbolicOid(r.notificationOid);o.summaryColumns<<QString("%1").arg(r.recordId,4,10,QChar('0'))<<r.receivedTimestamp.date().toString(Qt::ISODate)<<r.receivedTimestamp.time().toString(Qt::ISODate)<<QString::number(r.notificationTicks)<<notification<<r.messageType<<versionText(r.snmpVersion)<<r.sourceAddress<<QString::number(r.sourcePort);o.communityText=QCoreApplication::translate("TrapItem","Community: %1").arg(r.community);for(int i=0;i<r.varbinds.size();++i){const auto&v=r.varbinds[i];o.varbindLines<<QString("#%1 %2: %3").arg(i).arg(symbolicOid(v.oid),formattedValue(v));}
 const auto e=MibEnvironmentRegistry::active();const auto*n=nodeFor(e,r.notificationOid);if(!n||n->kind!=MibEnvironmentNodeKind::Notification)return o;o.notificationHtml=QString("<table border=\"1\" cellpadding=\"0\" cellspacing=\"0\" align=\"left\"><tr><td><b>Name:</b></td><td><font color=#009000><b>%1</b></font></td><tr><td><b>Oid:</b></td><td>%2</td></tr><tr><td><b>Units:</b></td><td>%3</td></tr><tr><td><b>Module:</b></td><td>%4</td></tr><tr><td><b>Reference:</b></td><td><font face=fixed color=blue>%5</font></td></tr><tr><td><b>Description:</b></td><td><font face=fixed color=blue>%6</font></td></tr></table>").arg(n->name,n->oid,n->units,n->moduleIdentity,n->reference.toHtmlEscaped(),n->description.toHtmlEscaped());return o;}
