#include "mibnode.h"
#include <QTextDocument>

namespace {
QString baseName(MibEnvironmentBaseType t) { switch(t) {
case MibEnvironmentBaseType::Integer32:return "INTEGER"; case MibEnvironmentBaseType::OctetString:return "OCTET STRING";
case MibEnvironmentBaseType::ObjectIdentifier:return "OBJECT IDENTIFIER"; case MibEnvironmentBaseType::Unsigned32:return "UNSIGNED32";
case MibEnvironmentBaseType::Integer64:return "INTEGER64"; case MibEnvironmentBaseType::Unsigned64:return "UNSIGNED64";
case MibEnvironmentBaseType::Float32:return "FLOAT32"; case MibEnvironmentBaseType::Float64:return "FLOAT64";
case MibEnvironmentBaseType::Float128:return "FLOAT128"; case MibEnvironmentBaseType::Enumeration:return "ENUM";
case MibEnvironmentBaseType::Bits:return "BITS"; case MibEnvironmentBaseType::Pointer:return "POINTER"; default:return {}; } }
QString number(const MibEnvironmentValue &v) { return v.isSigned?QString::number(v.signedValue):QString::number(v.unsignedValue); }
}

MibNode::MibNode(MibType type,const MibEnvironmentNodeRecord &node,MibEnvironmentPtr env,MibNode *parent,MibNode *sibling)
 : QTreeWidgetItem(parent,sibling),Type(type),Oid(node.oid),QualifiedName(node.qualifiedName),Environment(std::move(env))
{ setText(0,node.name); SetPixmap(FoldState::COLLAPSED); }
MibNode::MibNode(QString label,QTreeWidget *parent):QTreeWidgetItem(parent),Type(MIBNODE_NODE),Oid("1")
{ setText(0,label); SetPixmap(FoldState::COLLAPSED); }
void MibNode::SetPixmap(FoldState f) { const bool o=f==FoldState::EXPANDED; switch(Type) {
case MIBNODE_SCALAR:case MIBNODE_COLUMN:setIcon(0,QIcon(":/icon/scalar"));break;
case MIBNODE_ROW:setIcon(0,o?QIcon(":/icon/table-entry-open"):QIcon(":/icon/table-entry"));break;
case MIBNODE_TABLE:setIcon(0,o?QIcon(":/icon/table-open"):QIcon(":/icon/table"));break;
case MIBNODE_NOTIFICATION:setIcon(0,QIcon(":/icon/alert"));break; case MIBNODE_GROUP:setIcon(0,QIcon(":/icon/group"));break;
case MIBNODE_COMPLIANCE:setIcon(0,QIcon(":/icon/conformance"));break; case MIBNODE_CAPABILITIES:setIcon(0,QIcon(":/icon/capabilities"));break;
default:setIcon(0,o?QIcon(":/icon/folder-open"):QIcon(":/icon/folder"));break;} }
const MibEnvironmentNodeRecord *MibNode::ResolveNode() const { if(!Environment)return nullptr; return QualifiedName.isEmpty()?Environment->nodeByOid(Oid):Environment->nodeByQualifiedName(QualifiedName); }
QString MibNode::GetAccess() const { const auto *n=ResolveNode(); if(!n)return {}; switch(n->access) {
case MibEnvironmentAccess::NotAccessible:return "not-accessible";case MibEnvironmentAccess::Notify:return "notify";
case MibEnvironmentAccess::ReadOnly:return "read-only";case MibEnvironmentAccess::ReadWrite:{if(n->kind==MibEnvironmentNodeKind::Column){const auto*r=Environment->nodeByOid(n->rowOid);if(r&&r->creatable)return "read-create";}return "read-write";}
case MibEnvironmentAccess::Install:return "install";case MibEnvironmentAccess::InstallNotify:return "install-notify";case MibEnvironmentAccess::ReportOnly:return "report-only";default:return {};} }
QString MibNode::GetStatus() const { const auto*n=ResolveNode();if(!n)return {};switch(n->status){case MibEnvironmentStatusCode::Current:return "current";case MibEnvironmentStatusCode::Deprecated:return "<font color=red>deprecated</font>";case MibEnvironmentStatusCode::Mandatory:return "mandatory";case MibEnvironmentStatusCode::Optional:return "optional";case MibEnvironmentStatusCode::Obsolete:return "<font color=red>obsolete</font>";default:return {};} }
QString MibNode::GetKindName() const { switch(Type){case MIBNODE_NODE:return "Node";case MIBNODE_SCALAR:return "Scalar";case MIBNODE_TABLE:return "Table";case MIBNODE_ROW:return "Row";case MIBNODE_COLUMN:return "Column";case MIBNODE_NOTIFICATION:return "Notification";case MIBNODE_GROUP:return "Group";case MIBNODE_COMPLIANCE:return "Compliance";case MIBNODE_CAPABILITIES:return "Capabilities";}return {}; }
QString MibNode::GetSmiTypeName() const { const auto*n=ResolveNode();if(!n)return {};switch(n->declaration){case 5:return "OBJECT-IDENTIFIER";case 6:return "OBJECT-TYPE";case 7:return "OBJECT-IDENTITY";case 8:return "MODULE-IDENTITY";case 9:return "NOTIFICATION-TYPE";case 10:return "TRAP-TYPE";case 11:return "OBJECT-GROUP";case 12:return "NOTIFICATION-GROUP";case 13:return "MODULE-COMPLIANCE";case 14:return "AGENT-CAPABILITIES";case 33:return "module";case 36:return "node";case 37:return "scalar";case 38:return "table";case 39:return "row";case 40:return "column";case 41:return "notification";case 42:return "group";case 43:return "compliance";default:return {};} }
QString MibNode::GetTypeName() const {const auto*n=ResolveNode();if(!n||n->kind==MibEnvironmentNodeKind::Table)return {};if(!n->syntaxName.isEmpty())return n->syntaxName;const auto*t=Environment->type(n->typeId);return t?t->parentTypeId.section("::",-1):QString();}
QString MibNode::GetBaseTypeName() const {const auto*n=ResolveNode();return !n||n->kind==MibEnvironmentNodeKind::Table?QString():baseName(n->baseType);}
QString MibNode::GetRowIndex(const MibEnvironmentNodeRecord &n) const {QString r;if(n.indexKind==MibEnvironmentIndexKind::Index){r=tr("<tr><td><b>Index(es):</b></td><td>");for(qsizetype i=0;i<n.indexObjects.size();++i){r+=n.indexObjects[i].qualifiedName.section("::",-1);if(n.implied)r+=" (Implied)";if(i+1<n.indexObjects.size())r+="<br>";}return r+tr("</td></tr>");}if(!n.augmentsRowOid.isEmpty()){const auto*other=Environment->nodeByOid(n.augmentsRowOid);const QString label=other?other->name:n.augmentsRowOid;const QString relation=n.indexKind==MibEnvironmentIndexKind::Sparse?tr("Sparse"):n.indexKind==MibEnvironmentIndexKind::Expand?tr("Expands"):tr("Augments");return tr("<tr><td><b>%1:</b></td><td>%2</td></tr>").arg(relation,label);}return {};}
QString MibNode::GetSizeRange(){const auto*n=ResolveNode();if(!n||n->constraints.isEmpty())return {};QStringList values;for(const auto&c:n->constraints)values<<QString("%1 .. %2").arg(number(c.minimum),number(c.maximum));return tr("<tr><td><b>Size</b></td><td>%1</td></tr>").arg(values.join("<br>"));}
QString MibNode::GetValueList(){const auto*n=ResolveNode();if(!n||n->namedValues.isEmpty())return {};QStringList values;for(const auto&v:n->namedValues)values<<QString("%1 (%2)").arg(v.name,v.value.canonicalText);return tr("<tr><td><b>Value List</b></td><td><font color=green>%1</font></td></tr>").arg(values.join("<br>"));}
QString MibNode::GetOid() const{return Oid;}
void MibNode::PrintProperties(QString &out){const auto*n=ResolveNode();if(!n)return;out=tr("<table border=\"1\" cellpadding=\"0\" cellspacing=\"0\" align=\"left\">");out+=tr("<tr><td><b>Name:</b></td><td><font color=#009000><b>%1</b></font></td>").arg(n->name);out+=tr("<tr><td><b>Oid:</b></td><td>%1</td></tr>").arg(n->oid);out+=tr("<tr><td><b>Composed Type:</b></td><td>%1</td></tr>").arg(GetTypeName());out+=tr("<tr><td><b>Base Type:</b></td><td>%1</td></tr>").arg(GetBaseTypeName());out+=tr("<tr><td><b>Status:</b></td><td>%1</td></tr>").arg(GetStatus());out+=tr("<tr><td><b>Access:</b></td><td>%1</td></tr>").arg(GetAccess());out+=tr("<tr><td><b>Kind:</b></td><td>%1</td></tr>").arg(GetKindName());if(n->kind==MibEnvironmentNodeKind::Row)out+=GetRowIndex(*n);out+=tr("<tr><td><b>SMI Type:</b></td><td>%1</td></tr>").arg(GetSmiTypeName());out+=GetSizeRange();if(!n->units.isEmpty())out+=tr("<tr><td><b>Units:</b></td><td>%1</td></tr>").arg(n->units);out+=GetValueList();out+=tr("<tr><td><b>Module:</b></td><td>%1</td></tr>").arg(n->moduleIdentity);if(!n->reference.isEmpty())out+=tr("<tr><td><b>Reference:</b></td><td><font face=fixed color=blue>%1</font></td></tr>").arg(Qt::convertFromPlainText(n->reference));out+=tr("<tr><td><b>Description:</b></td><td><font face=fixed color=blue>%1</font></td></tr>").arg(Qt::convertFromPlainText(n->description));out+=tr("</table>");}
