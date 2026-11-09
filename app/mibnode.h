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
#ifndef MIBNODE_H
#define MIBNODE_H

#include <qstring.h>
#include <qfile.h>
#include <qfileinfo.h>
#include <qtimer.h>
#include <qpixmap.h>
#include <qtreewidget.h>
#include <QCoreApplication>

#include "mibenvironment.h"

class MibNode : public QTreeWidgetItem
{
    Q_DECLARE_TR_FUNCTIONS(MibNode)

public:
    enum MibType 
    {
        MIBNODE_NODE,
        MIBNODE_SCALAR,
        MIBNODE_TABLE,
        MIBNODE_ROW,
        MIBNODE_COLUMN,
        MIBNODE_NOTIFICATION,
        MIBNODE_GROUP,
        MIBNODE_COMPLIANCE,
        MIBNODE_CAPABILITIES
    };

    enum class FoldState { COLLAPSED, EXPANDED };
    
    MibNode(enum MibType mibtype, const MibEnvironmentNodeRecord &node,
            MibEnvironmentPtr environment, MibNode* parent, MibNode * sibling = NULL);
    MibNode(QString label, QTreeWidget * parent);

    void SetPixmap(FoldState);
    void PrintProperties(QString& text);
    QString GetOid() const;
    enum MibNode::MibType GetKind(void) { return Type; }
    
protected:
    QString GetAccess(void) const;
    QString GetStatus(void) const;
    QString GetTypeName(void) const;
    QString GetBaseTypeName(void) const;
    QString GetKindName(void) const;
    QString GetSmiTypeName(void) const;
    QString GetRowIndex(const MibEnvironmentNodeRecord &node) const;
    QString GetSizeRange(void);
    QString GetValueList(void);
                    
private:
    const MibEnvironmentNodeRecord *ResolveNode() const;
    enum MibType Type;
    QString Oid;
    QString QualifiedName;
    MibEnvironmentPtr Environment;
};

#endif /* MIBNODE_H */
