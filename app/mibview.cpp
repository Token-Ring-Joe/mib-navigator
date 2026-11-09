/*
    Copyright (C) 2004-2011 Martin Jolicoeur (snmpb1@gmail.com) 

    This file is part of the SnmpB project 
    (http://sourceforge.net/projects/snmpb)

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <qlabel.h>
#include <qmenu.h>
#include <qcursor.h>
#include <QContextMenuEvent>
#include <qtreewidget.h>
#include <QTreeWidgetItemIterator>
#include <functional>
#include <utility>

#include "mibnode.h"
#include "mibview.h"
#include "mibtreemodel.h"
#include "mibmodelview.h"

namespace {
QString accessText(MibEnvironmentAccess a){switch(a){case MibEnvironmentAccess::NotAccessible:return "not-accessible";case MibEnvironmentAccess::Notify:return "notify";case MibEnvironmentAccess::ReadOnly:return "read-only";case MibEnvironmentAccess::ReadWrite:return "read-write";case MibEnvironmentAccess::Install:return "install";case MibEnvironmentAccess::InstallNotify:return "install-notify";case MibEnvironmentAccess::ReportOnly:return "report-only";default:return {};}}
QString statusText(MibEnvironmentStatusCode s){switch(s){case MibEnvironmentStatusCode::Current:return "current";case MibEnvironmentStatusCode::Deprecated:return "deprecated";case MibEnvironmentStatusCode::Mandatory:return "mandatory";case MibEnvironmentStatusCode::Optional:return "optional";case MibEnvironmentStatusCode::Obsolete:return "obsolete";default:return {};}}
QString baseText(MibEnvironmentBaseType t){switch(t){case MibEnvironmentBaseType::Integer32:return "INTEGER";case MibEnvironmentBaseType::Unsigned32:return "UNSIGNED32";case MibEnvironmentBaseType::Integer64:return "INTEGER64";case MibEnvironmentBaseType::Unsigned64:return "UNSIGNED64";case MibEnvironmentBaseType::OctetString:return "OCTET STRING";case MibEnvironmentBaseType::ObjectIdentifier:return "OBJECT IDENTIFIER";case MibEnvironmentBaseType::Enumeration:return "ENUM";case MibEnvironmentBaseType::Bits:return "BITS";default:return {};}}
QString valueText(const MibEnvironmentValue&v){return v.isSigned?QString::number(v.signedValue):QString::number(v.unsignedValue);}
}

void MibModelView::RegisterToLoader(MibViewLoader *loader)
{
    setTreeModel(loader ? loader->TreeModel() : nullptr);
}

//
// BasicMibView class
//
//

BasicMibView::BasicMibView (QWidget * parent) : QTreeWidget(parent)
{
    // Set some properties for the TreeView
    header()->hide();
    setSortingEnabled( false );
    setHorizontalScrollBarPolicy ( Qt::ScrollBarAlwaysOn );
    header()->setSortIndicatorShown( false );
    setLineWidth( 2 );
    setAllColumnsShowFocus( false );
    setFrameShape(QFrame::WinPanel);
    setFrameShadow(QFrame::Plain);
    setRootIsDecorated( true );
    
    // Create context menu actions
    expandAct = new QAction(tr("Expand"), this);
    expandAct->setIcon(QIcon(":/icon/expand"));
    connect(expandAct, SIGNAL(triggered()), this, SLOT(ExpandFromNode()));
    collapseAct = new QAction(tr("Collapse"), this);
    collapseAct->setIcon(QIcon(":/icon/collapse"));
    connect(collapseAct, SIGNAL(triggered()), this, SLOT(CollapseFromNode()));
    findAct = new QAction(tr("Find"), this);
    connect(findAct, SIGNAL(triggered()), this, SLOT(FindFromNode()));
    
    // Connect some signals
    connect( this, SIGNAL( itemExpanded( QTreeWidgetItem * ) ),
             this, SLOT( ExpandNode( QTreeWidgetItem * ) ) );
    connect( this, SIGNAL( itemCollapsed( QTreeWidgetItem * ) ),
             this, SLOT( CollapseNode( QTreeWidgetItem * ) ) );
    connect( this, SIGNAL( currentItemChanged( QTreeWidgetItem *, QTreeWidgetItem * ) ),
             this, SLOT( SelectedNode( QTreeWidgetItem *, QTreeWidgetItem * ) ) );

    // Force initial refresh
    isdirty = 1;

    find_string = "";
    find_last = model()->index(0, 0, QModelIndex());
    find_back = false;
    find_cs = false;
    find_word = false;
}

void BasicMibView::SetDirty(void)
{
    isdirty = 1;
    find_last = QModelIndex();
}

void BasicMibView::RegisterToLoader(MibViewLoader *loader)
{
    MibLoader = loader;
    MibLoader->RegisterView(this);
}

void BasicMibView::Populate(void)
{
    if (isdirty)
    {
        isdirty = 0;
        // Create the root folder
        MibNode *root = new MibNode("MIB Tree", this);
        
        if (MibLoader) MibLoader->Populate(root);
    }
}

void BasicMibView::ExpandFromNode(void)
{
    QTreeWidgetItem *start = NULL, *end = NULL;
    
    // Could it be null ?
    if ((start = currentItem()) == NULL)
        return;

    // Go back in the tree till we find a sibling to mark the end
    // If end is NULL, we expanded from the root
    QTreeWidgetItem *ptr = start;
    while (ptr && ptr->parent() && 
           !(end = ptr->parent()->child(ptr->parent()->indexOfChild(ptr) + 1)))
        ptr = ptr->parent();
 
    // Now go thru all nodes till the end marker
    QTreeWidgetItemIterator it( start );
    while ( *it && (*it != end)) {
        QTreeWidgetItem *item = *it;
        item->setExpanded(true);
        ++it;
    }
}

void BasicMibView::CollapseFromNode(void)
{
    QTreeWidgetItem *start = NULL, *end = NULL;
    
    // Could it be null ?
    if ((start = currentItem()) == NULL)
        return;

    // Go back in the tree till we find a sibling to mark the end
    // If end is NULL, we collapsed from the root
    QTreeWidgetItem *ptr = start;
    while (ptr && ptr->parent() && 
           !(end = ptr->parent()->child(ptr->parent()->indexOfChild(ptr) + 1)))
        ptr = ptr->parent();
    
    // Now go thru all nodes till the end marker
    QTreeWidgetItemIterator it( start );
    while ( *it && (*it != end)) {
        QTreeWidgetItem *item = *it;
        item->setExpanded(false);
        ++it;
    }
}

void BasicMibView::FindFromNode(void)
{
    QDialog d(this);

    find_uid.setupUi(&d);
    connect( find_uid.buttonFindNext, SIGNAL( clicked() ), 
             this, SLOT( ExecuteFind() ));
    find_uid.comboFind->setFocus(Qt::TabFocusReason);

    find_uid.comboFind->addItems(find_strings);
    if (!find_string.isEmpty())
        find_uid.comboFind->setCurrentIndex(find_uid.comboFind->findText(find_string));
    find_last = model()->index(0, 0, QModelIndex());
    d.exec();
}

void BasicMibView::ExecuteFindNext(void)
{
    Find(false);
}

void BasicMibView::ExecuteFind(void)
{
    Find(true);
}

void BasicMibView::Find(bool reevaluate)
{
    if (reevaluate)
    {
        find_string = find_uid.comboFind->currentText();
        if (!find_strings.contains(find_string))
            find_strings.append(find_string);

        if (find_uid.checkWords->isChecked())
            find_word = true;
        else
            find_word = false;
        if (find_uid.checkCase->isChecked())
            find_cs = true;
        else
            find_cs = false;
        if (find_uid.checkBackward->isChecked())
            find_back = true;
        else
            find_back = false;
    }

    QTreeWidgetItem *start = NULL, *begin = NULL, *end = NULL, *cur = NULL;
  
    // Determine begin of tree 
    begin = itemFromIndex(model()->index(0, 0, QModelIndex()));
 
    // Determine end of tree
    if (find_back)
    {
        QTreeWidgetItemIterator it(begin);
        while ( *it ) end = *it++;
    }

    // Determine where we start the find 
    if ((start = itemFromIndex(find_last)) == NULL)
        start = begin;

    // Create iterator
    QTreeWidgetItemIterator it( start );

    goto start_find;

    // Loop thru tree items and break if item is found
    while ( *it && (*it != start))
    {
        cur = *it;

        if ((find_word && !cur->text(0).compare(find_string, 
                           find_cs?Qt::CaseSensitive:Qt::CaseInsensitive)) ||
            (!find_word && cur->text(0).contains(find_string, 
                           find_cs?Qt::CaseSensitive:Qt::CaseInsensitive)))
        {
            // Found item
            setCurrentItem(cur);
            find_last = indexFromItem(cur);
            break;
        }

start_find:
        // Move to next item, handle tree wrap-around
        if (find_back)
        {
            --it;
            if (!*it) it = QTreeWidgetItemIterator(end);
        }
        else
        {
            ++it;
            if (!*it) it = QTreeWidgetItemIterator(begin);
        }
    }
}

void BasicMibView::SelectFromOid(const QString& oid)
{
    QTreeWidgetItem *start = NULL, *cur = NULL;

    // Determine begin of tree 
    start = itemFromIndex(model()->index(0, 0, QModelIndex()));
 
    // Create iterator
    QTreeWidgetItemIterator it( start );

    // Loop thru tree items and break if item is found
    while ( (cur = *it++) != NULL)
    {
        MibNode* mn = (MibNode*)cur;
        if ((mn->GetOid() == oid) ||
            (oid.startsWith(mn->GetOid()) && 
             (mn->GetKind() == MibNode::MIBNODE_COLUMN)))
        {
            // Found item
            setCurrentItem(cur);
            break;
        }
    }
}

void BasicMibView::ExpandNode( QTreeWidgetItem * item)
{
    MibNode *node = (MibNode*)item;
    node->SetPixmap(MibNode::FoldState::EXPANDED);

    // speed up navigating deep hierarchies
    if (item->childCount() == 1) {
        item->child(0)->setExpanded(true);
    }
}

void BasicMibView::CollapseNode( QTreeWidgetItem * item)
{
    MibNode *node = (MibNode*)item;
    node->SetPixmap(MibNode::FoldState::COLLAPSED);

    // speed up navigating deep hierarchies
    if (item->childCount() == 1) {
        item->child(0)->setExpanded(false);
    }
}

void BasicMibView::SelectedNode( QTreeWidgetItem * item, QTreeWidgetItem *)
{
    MibNode *node = (MibNode*)item;
    
    if (node)
        emit SelectedOid(node->GetOid());
}

void BasicMibView::contextMenuEvent ( QContextMenuEvent *event)
{    
    QMenu menu(tr("Operations"), this);

    menu.addAction(expandAct);
    menu.addAction(collapseAct);
    menu.addSeparator();
    menu.addAction(findAct);

    menu.exec(event->globalPos());
}

//
// MibView class
//
//

MibView::MibView (QWidget * parent) : BasicMibView(parent)
{
    // Create context menu actions
    walkAct = new QAction(tr("Walk"), this);
    connect(walkAct, SIGNAL(triggered()), this, SLOT(WalkFromNode()));

    getAct = new QAction(tr("Get"), this);
    connect(getAct, SIGNAL(triggered()), this, SLOT(GetFromNode()));
    getPromptAct = new QAction(tr("Prompt for instance..."), this);
    connect(getPromptAct, SIGNAL(triggered()), this, SLOT(GetFromNodePromptInstance()));
    getSelectAct = new QAction(tr("Select Instance"), this);
    connect(getSelectAct, SIGNAL(triggered()), this, SLOT(GetFromNodeSelectInstance()));

    getnextAct = new QAction(tr("Get Next"), this);
    connect(getnextAct, SIGNAL(triggered()), this, SLOT(GetNextFromNode()));
    getnextPromptAct = new QAction(tr("Prompt for instance..."), this);
    connect(getnextPromptAct, SIGNAL(triggered()), this, SLOT(GetNextFromNodePromptInstance()));
    getnextSelectAct = new QAction(tr("Select Instance"), this);
    connect(getnextSelectAct, SIGNAL(triggered()), this, SLOT(GetNextFromNodeSelectInstance()));

    getbulkAct = new QAction(tr("Get Bulk"), this);
    connect(getbulkAct, SIGNAL(triggered()), this, SLOT(GetBulkFromNode()));
    getbulkPromptAct = new QAction(tr("Prompt for instance..."), this);
    connect(getbulkPromptAct, SIGNAL(triggered()), this, SLOT(GetBulkFromNodePromptInstance()));
    getbulkSelectAct = new QAction(tr("Select Instance"), this);
    connect(getbulkSelectAct, SIGNAL(triggered()), this, SLOT(GetBulkFromNodeSelectInstance()));

    setAct = new QAction(tr("Set..."), this);
    connect(setAct, SIGNAL(triggered()), this, SLOT(SetFromNode()));
    stopAct = new QAction(tr("Stop"), this);
    connect(stopAct, SIGNAL(triggered()), this, SLOT(StopNode()));
    tableviewAct = new QAction(tr("Table View"), this);
    connect(tableviewAct, SIGNAL(triggered()), this, SLOT(TableViewFromNode()));
    varbindsAct = new QAction(tr("Multiple Varbinds..."), this);
    connect(varbindsAct, SIGNAL(triggered()), this, SLOT(VarbindsFromNode()));

    walkinprogress = false;
    agentisv1 = true; 
}

void MibView::WalkFromNode(void)
{
    QTreeWidgetItem *start = NULL;
    
    // Could it be null ?
    if ((start = currentItem()) == NULL)
        return;

    emit WalkFromOid(((MibNode*)start)->GetOid());
}

void MibView::GetFromNode(void)
{
    QTreeWidgetItem *start = NULL;
    
    // Could it be null ?
    if ((start = currentItem()) == NULL)
        return;
    QString oid(((MibNode*)start)->GetOid());
    oid += ".0";
    emit GetFromOid(oid, 0);
}

void MibView::GetFromNodePromptInstance(void)
{
    QTreeWidgetItem *start = NULL;
    
    // Could it be null ?
    if ((start = currentItem()) == NULL)
        return;

    QString oid(((MibNode*)start)->GetOid());
    emit GetFromOidPromptInstance(oid, 0);
}

void MibView::GetFromNodeSelectInstance(void)
{
    QTreeWidgetItem *start = NULL;
    
    // Could it be null ?
    if ((start = currentItem()) == NULL)
        return;

    QString oid(((MibNode*)start)->GetOid());
    emit GetFromOidSelectInstance(oid, 0);
}

void MibView::GetNextFromNode(void)
{
    QTreeWidgetItem *start = NULL;
    
    // Could it be null ?
    if ((start = currentItem()) == NULL)
        return;
   
    QString oid(((MibNode*)start)->GetOid());
    oid += ".0";
    emit GetFromOid(oid, 1);
}

void MibView::GetNextFromNodePromptInstance(void)
{
    QTreeWidgetItem *start = NULL;
    
    // Could it be null ?
    if ((start = currentItem()) == NULL)
        return;
   
    QString oid(((MibNode*)start)->GetOid());
    emit GetFromOidPromptInstance(oid, 1);
}

void MibView::GetNextFromNodeSelectInstance(void)
{
    QTreeWidgetItem *start = NULL;
    
    // Could it be null ?
    if ((start = currentItem()) == NULL)
        return;
   
    QString oid(((MibNode*)start)->GetOid());
    emit GetFromOidSelectInstance(oid, 1);
}

void MibView::GetBulkFromNode(void)
{
    QTreeWidgetItem *start = NULL;
    
    // Could it be null ?
    if ((start = currentItem()) == NULL)
        return;
   
    QString oid(((MibNode*)start)->GetOid());
    oid += ".0";
    emit GetFromOid(oid, 2);
}

void MibView::GetBulkFromNodePromptInstance(void)
{
    QTreeWidgetItem *start = NULL;
    
    // Could it be null ?
    if ((start = currentItem()) == NULL)
        return;
   
    QString oid(((MibNode*)start)->GetOid());
    emit GetFromOidPromptInstance(oid, 2);
}

void MibView::GetBulkFromNodeSelectInstance(void)
{
    QTreeWidgetItem *start = NULL;
    
    // Could it be null ?
    if ((start = currentItem()) == NULL)
        return;
   
    QString oid(((MibNode*)start)->GetOid());
    emit GetFromOidSelectInstance(oid, 2);
}

void MibView::SetFromNode(void)
{
    QTreeWidgetItem *start = NULL;
    
    // Could it be null ?
    if ((start = currentItem()) == NULL)
        return;
    
    emit SetFromOid(((MibNode*)start)->GetOid());
}

void MibView::StopNode(void)
{
    QTreeWidgetItem *start = NULL;
    
    // Could it be null ?
    if ((start = currentItem()) == NULL)
        return;
    
    emit Stop();
}

void MibView::TableViewFromNode(void)
{
    QTreeWidgetItem *start = NULL;
    
    // Could it be null ?
    if ((start = currentItem()) == NULL)
        return;
    
    emit TableViewFromOid(((MibNode*)start)->GetOid());
}

void MibView::VarbindsFromNode(void)
{
    QTreeWidgetItem *start = NULL;
    
    // Could it be null ?
    if ((start = currentItem()) == NULL)
        return;
    
    QString oid(((MibNode*)start)->GetOid());
    emit VarbindsFromOid(oid);
}

void MibView::SelectedNode( QTreeWidgetItem * item, QTreeWidgetItem *)
{
    MibNode *node = (MibNode*)item;
    QString text;

    if (node)
    {
        node->PrintProperties(text);
        emit NodeProperties(text);
    }
}

void MibView::contextMenuEvent ( QContextMenuEvent *event)
{
    /*
       Node kinds:

       MIBNODE_NODE,
       MIBNODE_SCALAR,
       MIBNODE_TABLE,
       MIBNODE_ROW,
       MIBNODE_COLUMN,
       MIBNODE_NOTIFICATION,
       MIBNODE_GROUP,
       MIBNODE_COMPLIANCE,
    */
    enum MibNode::MibType kind = currentItem()?((MibNode*)currentItem())
                                 ->GetKind():MibNode::MIBNODE_NODE;
    QMenu menu(tr("Operations"), this);

    menu.addAction(expandAct);
    menu.addAction(collapseAct);
    menu.addSeparator();

    menu.addAction(walkAct);
    menu.addAction(stopAct);
    if (walkinprogress == true)
        stopAct->setEnabled(true);
    else
        stopAct->setEnabled(false);
    menu.addSeparator();

    if (kind == MibNode::MIBNODE_COLUMN)
    {
        QMenu *get_menu = menu.addMenu(tr("Get"));
        get_menu->addAction(getSelectAct);
        get_menu->addAction(getPromptAct);

        QMenu *getnext_menu = menu.addMenu(tr("Get Next"));
        getnextAct->setText(tr("No Instance"));
        getnext_menu->addAction(getnextAct);
        getnext_menu->addAction(getnextSelectAct);
        getnext_menu->addAction(getnextPromptAct);

        QMenu *getbulk_menu = menu.addMenu(tr("Get Bulk"));
        getbulkAct->setText(tr("No Instance"));
        getbulk_menu->addAction(getbulkAct);
        getbulk_menu->addAction(getbulkSelectAct);
        getbulk_menu->addAction(getbulkPromptAct);
        if (agentisv1)
            getbulk_menu->setEnabled(false);
        else
            getbulk_menu->setEnabled(true);
    }
    else
    {
        menu.addAction(getAct);
        if (kind == MibNode::MIBNODE_SCALAR)
            getAct->setEnabled(true);
        else
            getAct->setEnabled(false);

        getnextAct->setText(tr("Get Next"));
        menu.addAction(getnextAct);

        getbulkAct->setText(tr("Get Bulk"));
        if (agentisv1)
            getbulkAct->setEnabled(false);
        else
            getbulkAct->setEnabled(true);
        menu.addAction(getbulkAct);
    }

    menu.addAction(setAct);
    if ((kind == MibNode::MIBNODE_COLUMN) || (kind == MibNode::MIBNODE_SCALAR))
        setAct->setEnabled(true);
    else
        setAct->setEnabled(false);

    menu.addSeparator();

    menu.addAction(tableviewAct);
    if ((kind == MibNode::MIBNODE_TABLE) || (kind == MibNode::MIBNODE_ROW))
        tableviewAct->setEnabled(true);
    else
        tableviewAct->setEnabled(false);
    menu.addAction(varbindsAct);
    if ((kind == MibNode::MIBNODE_COLUMN) || (kind == MibNode::MIBNODE_SCALAR))
        varbindsAct->setEnabled(true);
    else
        varbindsAct->setEnabled(false);
    menu.addSeparator();

    menu.addAction(findAct);

    menu.exec(event->globalPos());
}

//
// MibViewLoader class
//
//

MibViewLoader::MibViewLoader ()
{    
    ignoreconformance = 0;
    ignoreleafs = 0;
    treeModel = new MibTreeModel(this);
}

void MibViewLoader::SetEnvironment(MibEnvironmentPtr value, const QStringList &modules)
{
    environment = std::move(value); loadedModuleNames = modules;
    for (BasicMibView *view : std::as_const(views)) { view->SetDirty(); view->clear(); }
    treeSnapshot = {}; treeSnapshot.oid = QStringLiteral("1"); treeSnapshot.name = QStringLiteral("MIB Tree");
    std::function<MibTreeNodeRecord(const MibEnvironmentNodeRecord &)> project;
    project = [this, &project](const MibEnvironmentNodeRecord &node) {
        MibTreeNodeRecord record; record.oid=node.oid; record.name=node.name;
        record.moduleName=node.moduleIdentity;
        switch(node.kind){case MibEnvironmentNodeKind::Node:record.nodeKind=1;break;case MibEnvironmentNodeKind::Scalar:record.nodeKind=2;break;case MibEnvironmentNodeKind::Table:record.nodeKind=4;break;case MibEnvironmentNodeKind::Row:record.nodeKind=8;break;case MibEnvironmentNodeKind::Column:record.nodeKind=16;break;case MibEnvironmentNodeKind::Notification:record.nodeKind=32;break;case MibEnvironmentNodeKind::Group:record.nodeKind=64;break;case MibEnvironmentNodeKind::Compliance:record.nodeKind=128;break;case MibEnvironmentNodeKind::Capabilities:record.nodeKind=256;break;default:break;}
        record.typeName=node.syntaxName;
        if(record.typeName.isEmpty())if(const auto*t=environment->type(node.typeId))record.typeName=t->parentTypeId.section(QStringLiteral("::"),-1);
        record.displayHint=node.displayHint; record.units=node.units;
        record.access=accessText(node.access);record.status=statusText(node.status);record.baseType=baseText(node.baseType);
        record.description=node.description; record.reference=node.reference;
        for(const auto&range:node.constraints)record.ranges<<QString("%1 .. %2").arg(valueText(range.minimum),valueText(range.maximum));
        for(const auto&named:node.namedValues)record.namedValues<<QString("%1 (%2)").arg(named.name,named.value.canonicalText);
        for (const QString &childOid : node.childOids) {
            const auto *child=environment->nodeByOid(childOid);
            if (child && !PruneSubTree(*child)) record.children.append(project(*child));
        }
        return record;
    };
    if (environment) {
        const auto *iso=environment->nodeByOid(QStringLiteral("1"));
        if (iso) treeSnapshot.children.append(project(*iso));
    }
    treeModel->setSnapshot(treeSnapshot);
}

void MibViewLoader::RegisterView(BasicMibView* view)
{
    views.append(view);
}

int MibViewLoader::IsPartOfLoadedModules(const MibEnvironmentNodeRecord &node)
{
    return loadedModuleNames.contains(node.moduleIdentity);
}

/*
 * The following function pruneSubTree() is tricky. There are some
 * interactions between the supported options. See the detailed
 * comments below. Good examples to test the implemented behaviour
 * are:
 *
 * smidump -u -f tree --tree-no-leaf IF-MIB ETHER-CHIPSET-MIB
 *
 * (And the example above does _not_ work in combination with
 * --tree-no-conformance so the code below is still broken.)
 */

int MibViewLoader::PruneSubTree(const MibEnvironmentNodeRecord &node)
{
    const bool conformance=node.kind==MibEnvironmentNodeKind::Group||node.kind==MibEnvironmentNodeKind::Compliance;
    const bool leaf=conformance||node.kind==MibEnvironmentNodeKind::Column||node.kind==MibEnvironmentNodeKind::Scalar||node.kind==MibEnvironmentNodeKind::Row||node.kind==MibEnvironmentNodeKind::Notification;
    
    /*
     * First, prune all nodes which the user has told us to ignore.
     * In the case of ignoreleafs, we have to special case nodes with
     * an unknown status (which actually represent OBJECT-IDENTITY
     * definitions). More special case code is needed to exclude
     * module identity nodes.
     */
    
    if (ignoreconformance && conformance) return 1;
    
    if (ignoreleafs) {
        if (leaf) return 1;
        if (node.kind==MibEnvironmentNodeKind::Node && node.status!=MibEnvironmentStatusCode::Unknown) return 1;
    }
    
    /*
      * Next, generally do not prune nodes that belong to the set of
      * modules we are looking at.
      */
    
    if (IsPartOfLoadedModules(node) && (!ignoreconformance || node.childOids.isEmpty())) return 0;
    
    /*
     * Finally, prune all nodes where all child nodes are pruned.
     */
    
    if (!environment) return 1;
    for (const QString &childOid : node.childOids) {
        const auto *childNode=environment->nodeByOid(childOid); if(!childNode) continue;
        
        /*
         * In the case of ignoreleafs, we have to peek at the child
         * nodes. Otherwise, we would prune too much. we still want to
         * see the path to the leafs we have pruned away. This also
         * interact with the semantics of ignoreconformance since we
         * still want in combination with ignoreleafs to see the path
         * to the pruned conformance leafs.
         */
        
        const bool childConf=childNode->kind==MibEnvironmentNodeKind::Group||childNode->kind==MibEnvironmentNodeKind::Compliance;
        const bool childLeaf=childConf||childNode->kind==MibEnvironmentNodeKind::Column||childNode->kind==MibEnvironmentNodeKind::Scalar||childNode->kind==MibEnvironmentNodeKind::Row||childNode->kind==MibEnvironmentNodeKind::Notification;
        if (ignoreleafs && childLeaf) {
            if (IsPartOfLoadedModules(*childNode)) {
                if (ignoreconformance && childConf) return 1;
                return 0;
            }
        }
        if (!PruneSubTree(*childNode)) return 0;
    }
    
    return 1;
}

enum MibNode::MibType MibViewLoader::EnvironmentKindToMibNodeType(MibEnvironmentNodeKind kind)
{
    switch(kind)
    {
    case MibEnvironmentNodeKind::Node:
        return (MibNode::MIBNODE_NODE);
    case MibEnvironmentNodeKind::Scalar:
        return (MibNode::MIBNODE_SCALAR);
    case MibEnvironmentNodeKind::Table:
        return (MibNode::MIBNODE_TABLE);
    case MibEnvironmentNodeKind::Row:
        return (MibNode::MIBNODE_ROW);
    case MibEnvironmentNodeKind::Column:
        return (MibNode::MIBNODE_COLUMN);
    case MibEnvironmentNodeKind::Notification:
        return (MibNode::MIBNODE_NOTIFICATION);
    case MibEnvironmentNodeKind::Group:
        return (MibNode::MIBNODE_GROUP);
    case MibEnvironmentNodeKind::Compliance:
        return (MibNode::MIBNODE_COMPLIANCE);
    case MibEnvironmentNodeKind::Capabilities:
        return (MibNode::MIBNODE_CAPABILITIES);
    case MibEnvironmentNodeKind::Unknown:
    default:
        break;
    }
    
    return (MibNode::MIBNODE_NODE);
}

void MibViewLoader::Populate(MibNode *root)
{
    if (!environment || !root) return;
    const auto *iso=environment->nodeByOid(QStringLiteral("1"));
    if (iso) PopulateSubTree(*iso,root,nullptr);
}

MibNode *MibViewLoader::PopulateSubTree(const MibEnvironmentNodeRecord &node,MibNode *parent,MibNode *sibling)
{
    MibNode *current=new MibNode(EnvironmentKindToMibNodeType(node.kind),node,environment,parent,sibling),*prev=nullptr;
    for(const QString &oid:node.childOids){const auto*child=environment->nodeByOid(oid);if(!child||PruneSubTree(*child))continue;prev=PopulateSubTree(*child,current,prev);}
    return current;
}
