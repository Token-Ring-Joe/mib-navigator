#include "mibenvironmentextractor.h"
#include "mibenvironmentregistry.h"
#include "mibnode.h"
#include "mibmodelview.h"
#include "mibtreemodel.h"
#include "mibview.h"
#include "tablenodevalidation.h"
#include "tabletraversal.h"
#include "trappresenter.h"
#include "mibvaluesemantics.h"
#include "snmprequestcontext.h"
#include "smi.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>

namespace {int failures=0;void check(bool c,const char*m){if(!c){QTextStream(stderr)<<"FAIL: "<<m<<Qt::endl;++failures;}}
MibEffectivePlan plan(){MibEffectivePlan p;p.sha256="phase3-consumer-test";for(SmiModule*m=smiGetFirstModule();m;m=smiGetNextModule(m)){MibEffectivePlanMember x;x.identity=QString::fromLatin1(m->name);x.provider.canonicalPath=QString::fromLocal8Bit(m->path);for(SmiImport*i=smiGetFirstImport(m);i;i=smiGetNextImport(i)){const QString n=QString::fromLatin1(i->module);if(!x.imports.contains(n))x.imports<<n;}p.members<<x;p.effectiveModules<<x.identity;}return p;}}
int liveCount(SmiNode*n){if(!n)return 0;int total=1;for(SmiNode*c=smiGetFirstChildNode(n);c;c=smiGetNextChildNode(c))total+=liveCount(c);return total;}int snapshotCount(const MibTreeNodeRecord&n){int total=1;for(const auto&c:n.children)total+=snapshotCount(c);return total;}
QModelIndex findOid(QAbstractItemModel*m,const QString&oid,const QModelIndex&p={}){for(int r=0;r<m->rowCount(p);++r){const QModelIndex i=m->index(r,0,p);if(i.data(MibTreeModel::OidRole).toString()==oid)return i;const QModelIndex n=findOid(m,oid,i);if(n.isValid())return n;}return {};}

int main(int argc,char**argv){QApplication app(argc,argv);const bool large=app.arguments().contains("--large");const QString root=SNMPB_SOURCE_DIR;const QStringList paths={root+"/libsmi/mibs/ietf",root+"/libsmi/mibs/iana",root+"/libsmi/mibs/irtf",root+"/libsmi/mibs/site",root+"/libsmi/mibs/tubs"};const QByteArray path=paths.join(QDir::listSeparator()).toLocal8Bit();smiInit("phase3-consumers");smiSetFlags((smiGetFlags()|SMI_FLAG_ERRORS)&~SMI_FLAG_NODESCR);smiSetPath(path.constData());for(const char*m:{"SNMPv2-MIB","IF-MIB","ACCOUNTING-CONTROL-MIB"})check(smiLoadModule(m)!=nullptr,m);if(large)for(const QString&directory:paths)for(const QString&file:QDir(directory).entryList(QDir::Files,QDir::Name))smiLoadModule(QDir::toNativeSeparators(QDir(directory).filePath(file)).toLocal8Bit().constData());QTemporaryDir valueFixtures;
check(valueFixtures.isValid(), "value-semantic fixture directory");
QFile valueFixture(valueFixtures.filePath("values.mib"));
const QByteArray valueMib = R"MIB(MAINTENANCE-VALUES-MIB DEFINITIONS ::= BEGIN
IMPORTS OBJECT-TYPE, Integer32, enterprises FROM SNMPv2-SMI
        DisplayString, DateAndTime, TEXTUAL-CONVENTION FROM SNMPv2-TC;
maintenanceValues OBJECT IDENTIFIER ::= { enterprises 64992 }
Hundredths ::= TEXTUAL-CONVENTION
    DISPLAY-HINT "d-2"
    STATUS current
    DESCRIPTION "Synthetic scaled measurement."
    SYNTAX Integer32 (-10000..10000)
scaledValue OBJECT-TYPE SYNTAX Hundredths MAX-ACCESS read-only STATUS current
    DESCRIPTION "Synthetic value." ::= { maintenanceValues 1 }
dateValue OBJECT-TYPE SYNTAX DateAndTime MAX-ACCESS read-only STATUS current
    DESCRIPTION "Synthetic date." ::= { maintenanceValues 2 }
textValue OBJECT-TYPE SYNTAX DisplayString MAX-ACCESS read-only STATUS current
    DESCRIPTION "Synthetic text." ::= { maintenanceValues 3 }
END
)MIB";
check(valueFixture.open(QIODevice::WriteOnly) && valueFixture.write(valueMib) == valueMib.size(), "write value-semantic fixture");
valueFixture.close();
check(smiLoadModule(QDir::toNativeSeparators(valueFixture.fileName()).toLocal8Bit().constData()) != nullptr, "load value-semantic fixture through real parser");
const int parserTreeCount=liveCount(smiGetNode(nullptr,"iso"));const MibEffectivePlan allPlan=plan();auto environment=MibEnvironmentExtractor().extract(allPlan);MibEffectivePlan partialPlan=allPlan;partialPlan.effectiveModules<<"NO-SUCH-MIB";auto partialEnvironment=MibEnvironmentExtractor().extract(partialPlan,{"NO-SUCH-MIB"});auto extremeEnvironment=std::make_shared<MibEnvironment>(*environment);auto extremeBEnvironment=std::make_shared<MibEnvironment>(*environment);MibEnvironmentRegistry::publish(environment);smiExit();
// Values retain their real parsed type ancestry and hints after the parser is gone.
Vb scaledResponse(Oid("1.3.6.1.4.1.64992.1.0")); scaledResponse.set_value(SnmpInt32(-123));
const auto scaledObject = ResolveMibObject(environment, scaledResponse.get_oid());
check(scaledObject.node && RenderMibValue(scaledObject, scaledResponse) == "-1.23", "parsed DISPLAY-HINT renders after smiExit");
Vb badDate(Oid("1.3.6.1.4.1.64992.2.0")); badDate.set_value(OctetStr::from_hex_string("07e7021d00000000"));
const auto dateCheck = CheckMibValue(ResolveMibObject(environment, badDate.get_oid()), badDate);
check(!dateCheck.warnings.isEmpty() && dateCheck.warnings.join(' ').contains("DateAndTime"), "parsed DateAndTime ancestry warns after smiExit");
TrapRecord typedTrap; typedTrap.notificationOid = "1.3.6.1.4.1.64992.0.1";
TrapVarbind textBinding;
textBinding.value.set_oid(Oid("1.3.6.1.4.1.64992.3.0"));
textBinding.value.set_value(OctetStr::from_hex_string("ff")); typedTrap.varbinds.append(textBinding);
const auto typedPresentation = TrapPresenter().present(typedTrap);
check(typedPresentation.bindings.size() == 1 && !typedPresentation.bindings.first().warnings.isEmpty() &&
      !typedPresentation.bindings.first().rawValue.isEmpty(), "trap workflow retains raw octets and shared textual-convention warnings after parser reset");
QElapsedTimer timer;timer.start();QStringList modules;for(const auto&m:environment->modules())modules<<m.identity;MibViewLoader loader;loader.SetEnvironment(environment,modules);check(!loader.TreeSnapshot().children.isEmpty(),"Tree projects after smiExit");check(snapshotCount(loader.TreeSnapshot())==parserTreeCount+1,"Tree structure parity");const qint64 treeMs=timer.elapsed();
// Exercise the chooser with a normal MIB-II scope even in the all-MIB stress run.
// A closed OID chooser must not remain a target of later profile publication.
const QStringList chooserModules = {"SNMPv2-MIB", "IF-MIB"};
loader.SetEnvironment(environment, chooserModules);
{
    QWidget dialog;
    auto *chooserTree = new BasicMibView(&dialog);
    chooserTree->RegisterToLoader(&loader);
    chooserTree->Populate();
    check(chooserTree->topLevelItemCount() > 0, "temporary OID chooser populated");
}
loader.SetEnvironment(environment, chooserModules);
BasicMibView survivingChooser;
survivingChooser.RegisterToLoader(&loader);
survivingChooser.Populate();
loader.SetEnvironment(environment, chooserModules);
check(survivingChooser.topLevelItemCount() == 0, "live OID chooser invalidated on publication");
survivingChooser.Populate();
check(survivingChooser.topLevelItemCount() > 0, "live OID chooser repopulates after publication");
survivingChooser.setCurrentItem(survivingChooser.topLevelItem(0));
QPointer<BasicMibView> closingChooser = new BasicMibView;
closingChooser->RegisterToLoader(&loader);
closingChooser->Populate();
const auto closeOnReset = QObject::connect(&survivingChooser, &QTreeWidget::currentItemChanged,
    &survivingChooser, [&] { delete closingChooser.data(); });
loader.SetEnvironment(environment, chooserModules);
check(closingChooser.isNull(), "publication tolerates another chooser closing during selection reset");
QObject::disconnect(closeOnReset);
loader.SetEnvironment(environment, modules);
// Use the complete stress snapshot here: opening the chooser must avoid eager
// QTreeWidgetItem/icon construction even for a large, already-built environment.
QElapsedTimer chooserTimer;
chooserTimer.start();
MibTreeModel frozenChooserModel;
frozenChooserModel.setSnapshot(loader.TreeSnapshot());
MibModelView chooserView(nullptr, MibModelView::Mode::OidSelection);
chooserView.setTreeModel(&frozenChooserModel);
chooserView.Populate();
chooserView.SelectFromOid("1.3.6.1.2.1.2.2.1.2.27");
const qint64 chooserMs = chooserTimer.elapsed();
check(chooserMs < 2000, "complete-library chooser construction and OID selection stay below two seconds");
check(chooserView.selectedOid() == "1.3.6.1.2.1.2.2.1.2", "chooser selects a column from its instance OID");
QPersistentModelIndex frozenSelection(chooserView.currentIndex());
loader.SetEnvironment({}, {});
check(frozenSelection.isValid() && chooserView.selectedOid() == "1.3.6.1.2.1.2.2.1.2",
      "profile changes cannot retarget an open chooser's frozen tree");
int networkActions = 0;
QObject::connect(&chooserView, &MibModelView::WalkFromOid, [&] { ++networkActions; });
QObject::connect(&chooserView, &MibModelView::GetFromOid, [&] { ++networkActions; });
QObject::connect(&chooserView, &MibModelView::SetFromOid, [&] { ++networkActions; });
chooserView.SetQueryPrerequisitesAvailable(true);
chooserView.WalkCurrent(); chooserView.QueryCurrent(0); chooserView.SetCurrent(); chooserView.QueryTableFromCurrent();
check(!chooserView.queryTableAvailable(), "OID chooser cannot enable table queries");
check(networkActions == 0, "OID-only chooser exposes no network commands");
loader.SetEnvironment(environment, modules);
QTextStream(stdout) << "Chooser model build/select ms=" << chooserMs << " nodes=" << environment->nodes().size() << Qt::endl;
MibModelView modelView;modelView.RegisterToLoader(&loader);QString details;QObject::connect(&modelView,&MibModelView::NodeProperties,[&details](const QString&value){details=value;});modelView.Populate();const QModelIndex selected=findOid(modelView.model(),"1.3.6.1.2.1.2.2.1.2");check(selected.isValid(),"transition fixture selection exists");modelView.setCurrentIndex(selected);modelView.expand(selected.parent());QPersistentModelIndex staleSelection(selected);const QString selectedOid=modelView.selectedOid();
check(details.contains("IF-MIB")&&details.contains("Display hint")&&details.contains("Range / size")&&details.contains("Access")&&details.contains("Description"),"OID inspector projects rich immutable Environment metadata");modelView.SetFilterText("ifDescr");check(findOid(modelView.model(),"1.3.6.1.2.1.2.2.1.2").isValid(),"Tree filter retains matching node and hierarchy");modelView.SetFilterText(QString());
const QStringList extremeModules={"SNMPv2-MIB","IF-MIB"};const QStringList extremeBModules={"SNMPv2-MIB","ACCOUNTING-CONTROL-MIB"};
loader.SetEnvironment(extremeEnvironment,extremeModules);modelView.setVisibleModules(extremeModules);modelView.Populate();check(!staleSelection.isValid()&&modelView.selectedOid()==selectedOid,"All MIBs to Extreme invalidates QModelIndex and restores numeric OID");
QPersistentModelIndex extremeSelection(modelView.currentIndex());loader.SetEnvironment(environment,modules);modelView.showAllModules();modelView.Populate();check(!extremeSelection.isValid()&&modelView.selectedOid()==selectedOid,"Extreme to All MIBs resets safely by OID");
for(int cycle=0;cycle<20;++cycle){QPersistentModelIndex before(modelView.currentIndex());loader.SetEnvironment(extremeEnvironment,extremeModules);modelView.setVisibleModules(extremeModules);modelView.Populate();check(!before.isValid(),"stress switch invalidates old index");before=modelView.currentIndex();loader.SetEnvironment(environment,modules);modelView.showAllModules();modelView.Populate();check(!before.isValid(),"stress return invalidates old index");}
loader.SetEnvironment(extremeEnvironment,extremeModules);modelView.setVisibleModules(extremeModules);loader.SetEnvironment(extremeBEnvironment,extremeBModules);modelView.setVisibleModules(extremeBModules);loader.SetEnvironment(environment,modules);modelView.showAllModules();modelView.Populate();check(modelView.model()->rowCount()>0,"Extreme A to Extreme B to All MIBs");
loader.SetEnvironment(partialEnvironment,modules);check(MibEnvironmentRegistry::publishMaterialization(partialEnvironment),"partial Environment publishes before All MIBs");loader.SetEnvironment(environment,modules);MibEnvironmentRegistry::publishMaterialization(environment);check(MibEnvironmentRegistry::active()==environment,"partial Environment to All MIBs");
loader.SetEnvironment({},{});MibEnvironmentRegistry::publish({});modelView.Populate();check(!modelView.currentIndex().isValid()&&modelView.selectedOid().isEmpty()&&details.isEmpty()&&modelView.model()->rowCount()==0,"no Environment clears Tree selection and OID Info");loader.SetEnvironment(environment,modules);MibEnvironmentRegistry::publishMaterialization(environment);modelView.showAllModules();modelView.Populate();
const auto*ifEntry=environment->nodeByQualifiedName("IF-MIB::ifEntry");const auto*ifTable=environment->nodeByQualifiedName("IF-MIB::ifTable");const MibEnvironmentNodeRecord*row=nullptr;check(ResolveTableRowNode(ifTable,ifEntry,&row)==TableNodeValidation::Valid&&row==ifEntry,"table planning after smiExit");Oid oid;check(RenderEnvironmentNodeOid(environment->nodeByQualifiedName("IF-MIB::ifDescr"),&oid),"column OID after smiExit");
QTreeWidget widget;MibNode rootNode("MIB Tree",&widget);MibNode info(MibNode::MIBNODE_ROW,*ifEntry,environment,&rootNode);QString html;info.PrintProperties(html);check(html.contains("ifEntry")&&html.contains("ifIndex")&&html.contains("IF-MIB"),"OID Info after smiExit");
TrapVarbind vb;vb.value.set_oid(Oid("1.3.6.1.2.1.2.2.1.8.7"));vb.value.set_value(SnmpInt32(1));check(TrapPresenter::symbolicOid("1.3.6.1.2.1.2.2.1.8.7")=="ifOperStatus.7"&&TrapPresenter::formattedValue(vb)=="up(1)","trap semantics after smiExit");
Vb response(Oid("1.3.6.1.2.1.2.2.1.8.7"));response.set_value(SnmpInt32(1));const auto resolved=ResolveMibObject(environment,response.get_oid());check(RenderMibOid(environment,response.get_oid())=="ifOperStatus.7"&&RenderMibValue(resolved,response)=="up(1)","Agent value semantics use Environment enum and instance suffix");
std::weak_ptr<const MibEnvironment> dispatchedWeak;{auto dispatched=std::make_shared<MibEnvironment>(*environment);dispatchedWeak=dispatched;SnmpRequestContext request({},SnmpRequestOperation::Walk,dispatched);dispatched.reset();MibEnvironmentRegistry::publish(extremeEnvironment);MibEnvironmentRegistry::publish(extremeBEnvironment);check(!dispatchedWeak.expired()&&request.environment()!=MibEnvironmentRegistry::active(),"A request retains dispatch Environment across B and C publication");check(RenderMibValue(ResolveMibObject(request.environment(),response.get_oid()),response)=="up(1)","response renders through dispatch Environment");}check(dispatchedWeak.expired(),"dispatch Environment releases after request completion");MibEnvironmentRegistry::publish(environment);
Vb unknown(Oid("9.9.9"));unknown.set_value(Counter64::ll_to_c64(0xffffffffffffffffULL));check(RenderMibOid(environment,unknown.get_oid())=="9.9.9"&&!RenderMibValue(ResolveMibObject(environment,unknown.get_oid()),unknown).isEmpty(),"unknown OID retains numeric and raw value");
const auto *ifAdmin=environment->nodeByQualifiedName("IF-MIB::ifAdminStatus");check(ifAdmin&&IsWritableMibNode(ifAdmin)&&SnmpSyntaxForMibNode(ifAdmin)==sNMP_SYNTAX_INT32,"writable enum syntax comes from Environment");QString validation;check(ValidateMibSetValue(ifAdmin,sNMP_SYNTAX_INT32,"1",&validation)&&!ValidateMibSetValue(ifAdmin,sNMP_SYNTAX_INT32,"99",&validation),"SET enum validation uses typed mappings");const auto *ifOper=environment->nodeByQualifiedName("IF-MIB::ifOperStatus");check(ifOper&&!ValidateMibSetValue(ifOper,sNMP_SYNTAX_INT32,"1",&validation),"SET rejects non-writable object");
MibEnvironmentNodeRecord constrained;constrained.access=MibEnvironmentAccess::ReadWrite;constrained.baseType=MibEnvironmentBaseType::Integer32;MibEnvironmentConstraint range;range.minimum.isSigned=true;range.minimum.signedValue=INT32_MIN;range.maximum.isSigned=true;range.maximum.signedValue=INT32_MAX;constrained.constraints<<range;check(ValidateMibSetValue(&constrained,sNMP_SYNTAX_INT32,QString::number(INT32_MIN),&validation)&&ValidateMibSetValue(&constrained,sNMP_SYNTAX_INT32,QString::number(INT32_MAX),&validation)&&!ValidateMibSetValue(&constrained,sNMP_SYNTAX_INT32,"2147483648",&validation),"signed Integer32 boundaries retain width");constrained.baseType=MibEnvironmentBaseType::Unsigned32;constrained.constraints.clear();check(ValidateMibSetValue(&constrained,sNMP_SYNTAX_GAUGE32,"4294967295",&validation)&&!ValidateMibSetValue(&constrained,sNMP_SYNTAX_GAUGE32,"4294967296",&validation)&&ValidateMibSetValue(&constrained,sNMP_SYNTAX_CNTR64,"18446744073709551615",&validation),"unsigned32 and Counter64 boundaries do not truncate");range.isSizeConstraint=true;range.minimum.isSigned=false;range.minimum.unsignedValue=1;range.maximum.isSigned=false;range.maximum.unsignedValue=3;constrained.baseType=MibEnvironmentBaseType::OctetString;constrained.constraints={range};check(ValidateMibSetValue(&constrained,sNMP_SYNTAX_OCTETS,"abc",&validation)&&!ValidateMibSetValue(&constrained,sNMP_SYNTAX_OCTETS,"abcd",&validation),"SET SIZE boundaries use Environment constraints");
MibEnvironmentNodeRecord bits;bits.baseType=MibEnvironmentBaseType::Bits;for(const auto &entry:QList<QPair<QString,quint64>>{{"first",0},{"third",2}}){MibEnvironmentNamedValue named;named.name=entry.first;named.value.unsignedValue=entry.second;bits.namedValues<<named;}unsigned char bitBytes[]={0xa1};OctetStr bitOctets(bitBytes,1);Vb bitVb(Oid("1.3.6"));bitVb.set_value(bitOctets);MibResolvedObject bitObject{environment,&bits,{}};check(RenderMibValue(bitObject,bitVb)=="first third bit7","BITS preserves modeled order and unknown set bits");OctetStr noBitOctets(reinterpret_cast<const unsigned char *>(""),0);bitVb.set_value(noBitOctets);check(RenderMibValue(bitObject,bitVb).isEmpty(),"BITS no-bit value renders deterministically");
MibEnvironmentRegistry::publish({});check(TrapPresenter::symbolicOid("1.3.6.1")=="1.3.6.1","null Environment numeric fallback");
const QStringList guarded={"mibview.cpp","mibview.h","mibnode.cpp","mibnode.h","mibmodelview.cpp","trappresenter.cpp","tablenodevalidation.cpp","tabletraversal.cpp","agent.cpp","agent.h","mibselection.cpp","mibselection.h","mibvaluesemantics.cpp","mibvaluesemantics.h"};for(const QString&name:guarded){QFile f(root+"/app/"+name);check(f.open(QIODevice::ReadOnly),"guard source readable");const QByteArray s=f.readAll();check(!s.contains("#include \"smi.h\"")&&!s.contains("smiGet")&&!s.contains("smiRender")&&!s.contains("SmiNode *")&&!s.contains("SmiType *")&&!s.contains("SmiModule *"),"migrated source has no direct libsmi");}
QFile agentSource(root+"/app/agent.cpp");check(agentSource.open(QIODevice::ReadOnly),"Agent dispatch guard source readable");QByteArray compactAgent=agentSource.readAll();for(const char whitespace:{' ','\t','\r','\n'})compactAgent.replace(whitespace,QByteArray());check(!compactAgent.contains("SnmpRequestContext(config,SnmpRequestOperation::Walk,MibEnvironmentRegistry::active())"),"table and instance dispatch reuse the Environment captured for validation");
if(!failures)QTextStream(stdout)<<"Phase3 "<<(large?"large ":"")<<"tree-projection-ms="<<treeMs<<" nodes="<<environment->nodes().size()<<Qt::endl;return failures?1:0;}
