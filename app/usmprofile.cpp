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
#include "usmprofile.h"
#include "agent.h"
#include "usmcredentialruntime.h"
#include "usmcredentialservice.h"
#include "usmcredentialcoordinator.h"
#include <QMessageBox>

USMProfileManager::USMProfileManager(Snmpb *snmpb)
{
    s = snmpb;

    up.setupUi(&upw);

    // Set some properties for the USM Profile TreeView
    up.ProfileTree->header()->hide();
    up.ProfileTree->setSortingEnabled( false );
    up.ProfileTree->header()->setSortIndicatorShown( false );
    up.ProfileTree->setLineWidth( 2 );
    up.ProfileTree->setAllColumnsShowFocus( false );
    up.ProfileTree->setFrameShape(QFrame::WinPanel);
    up.ProfileTree->setFrameShadow(QFrame::Plain);
    up.ProfileTree->setRootIsDecorated( true );

    // Create context menu actions
    up.ProfileTree->setContextMenuPolicy (Qt::CustomContextMenu);
    connect( up.ProfileTree, 
             SIGNAL( customContextMenuRequested ( const QPoint & ) ),
             this, SLOT( ContextMenu ( const QPoint & ) ) );
    addAct = new QAction(tr("&New USM Credential"), this);
    connect(addAct, SIGNAL(triggered()), this, SLOT(Add()));
    duplicateAct = new QAction(tr("D&uplicate USM Credential"), this);
    connect(duplicateAct, &QAction::triggered, this, &USMProfileManager::Duplicate);
    deleteAct = new QAction(tr("&Delete USM Credential"), this);
    connect(deleteAct, SIGNAL(triggered()), this, SLOT(Delete()));

    connect( up.ProfileTree, 
             SIGNAL( currentItemChanged( QTreeWidgetItem *, QTreeWidgetItem * ) ),
             this, SLOT( SelectedUSMProfile( QTreeWidgetItem *, QTreeWidgetItem * ) ) );
    connect( up.ProfileTree, 
             SIGNAL( itemChanged( QTreeWidgetItem *, int ) ),
             this, SLOT( USMSecNameChange( QTreeWidgetItem *, int ) ) );
    connect( up.SecName, SIGNAL( editingFinished() ), 
             this, SLOT ( SetSecName() ) );
    connect( up.AuthProtocol, SIGNAL( currentIndexChanged( int ) ), 
             this, SLOT ( SetAuthProto() ) );
    connect( up.AuthPass, SIGNAL( editingFinished() ), 
             this, SLOT ( SetAuthPass() ) );
    connect( up.PrivProtocol, SIGNAL( currentIndexChanged( int ) ), 
             this, SLOT ( SetPrivProto() ) );
    connect( up.PrivPass, SIGNAL( editingFinished() ), 
             this, SLOT ( SetPrivPass() ) );

    // Loop & load all stored USM profiles
    currentprofile = NULL;

    USM* usm = s->AgentObj()->GetUSMObj();
    const QList<UsmCredentialRecord> storedUsers =
        UsmCredentialRuntimeRepository::snapshot(usm);
    credentialService = new UsmCredentialService(
        storedUsers, UsmCredentialRepository(s->GetCredentialIdentitiesConfigFile()),
        this);
}

void USMProfileManager::Execute (void)
{
    ExecuteEditor(false);
}

void USMProfileManager::ExecuteNewCredential(void)
{
    ExecuteEditor(true);
}

void USMProfileManager::ExecuteEditor(bool createCredential)
{
    RebuildEditor();
    if (createCredential)
        Add();
    if(upw.exec() == QDialog::Accepted)
    {
        const QList<UsmCredentialRecord> before = credentialService->records();
        const QList<UsmCredentialRecord> after = EditorRecords();
        if (!credentialService->validateWorkingCopy(after))
        {
            QMessageBox::warning(&upw, tr("USM Profiles"),
                                 tr("Security names must be non-empty and unique. "
                                    "Selected authentication and privacy protocols "
                                    "also require their corresponding secrets."));
            return;
        }
        USM* usm = s->AgentObj()->GetUSMObj();
        UsmCredentialRepository identities(s->GetCredentialIdentitiesConfigFile());
        auto runtimeWriter = [usm, this](const QList<UsmCredentialRecord> &records) {
            return UsmCredentialRuntimeRepository::replaceAndSave(
                       usm, records, s->GetUsmUsersConfigFile()) == SNMPv3_USM_OK;
        };
        auto identityWriter = [&identities](const QList<UsmCredentialRecord> &records) {
            QList<UsmCredentialIdentityRecord> mappings;
            for (const auto &record : records)
                mappings.append({record.identity.credentialId, record.securityName});
            return identities.save(mappings);
        };
        const UsmCommitResult result = UsmCredentialCoordinator::apply(
            before, after, runtimeWriter, identityWriter);
        if (result.status != UsmCommitStatus::Success)
        {
            QMessageBox::critical(&upw, tr("USM Profiles"),
                                  tr("The credential changes could not be saved. "
                                     "The previous configuration was restored where possible."));
            return;
        }
        credentialService->applyCommitted(after);
        emit CredentialsChanged();
    }
}

void USMProfileManager::RebuildEditor()
{
    currentprofile = NULL;
    qDeleteAll(users);
    users.clear();
    for (const UsmCredentialRecord &stored : credentialService->records())
    {
        UsmCredentialRecord record = stored;
        record.authProtocol = LibAuthToUiAuth(stored.authProtocol);
        record.privacyProtocol = LibPrivToUiPriv(stored.privacyProtocol);
        users.append(new USMProfile(&up, record));
    }
    if (!users.isEmpty()) up.ProfileTree->setCurrentItem(users.first()->GetUserWidgetItem());
}

QList<UsmCredentialRecord> USMProfileManager::EditorRecords()
{
    QList<UsmCredentialRecord> result;
    for (USMProfile *profile : users)
    {
        UsmCredentialRecord record = profile->GetRecord();
        record.authProtocol = UiAuthToLibAuth(record.authProtocol);
        record.privacyProtocol = UiPrivToLibPriv(record.privacyProtocol);
        result.append(record);
    }
    return result;
}

void USMProfileManager::SetSecName(void)
{
    if (currentprofile)
        currentprofile->SetSecName();
}

void USMProfileManager::SetAuthProto(void)
{
    if (currentprofile)
        currentprofile->SetAuthProto();
}

void USMProfileManager::SetAuthPass(void)
{
    if (currentprofile)
        currentprofile->SetAuthPass();
}

void USMProfileManager::SetPrivProto(void)
{
    if (currentprofile)
        currentprofile->SetPrivProto();
}

void USMProfileManager::SetPrivPass(void)
{
    if (currentprofile)
        currentprofile->SetPrivPass();
}

void USMProfileManager::ContextMenu ( const QPoint &pos )
{    
    QMenu menu(tr("Actions"), up.ProfileTree);

    menu.addAction(addAct);
    menu.addAction(duplicateAct);
    duplicateAct->setEnabled(currentprofile != nullptr);
    menu.addAction(deleteAct);
    deleteAct->setEnabled(currentprofile != nullptr);

    menu.exec(up.ProfileTree->mapToGlobal(pos));
}

void USMProfileManager::Add(void)
{
    UsmCredentialRecord record = credentialService->createWorkingRecord("newuser");
    USMProfile * newuser = new USMProfile(&up, record);
 
    users.append(newuser);

    // Select the new item and change the focus to change its name ...
    up.ProfileTree->setCurrentItem(newuser->GetUserWidgetItem());
    up.SecName->setFocus(Qt::OtherFocusReason);  
    up.SecName->selectAll();  
}

void USMProfileManager::Delete(void)
{
    QTreeWidgetItem *p = NULL;

    for (int i = 0; i < users.count(); i++) 
    {
        if (currentprofile && (users[i] == currentprofile))
        {
            // Delete the profile (removes from the list)
            delete users.takeAt(i);
            currentprofile = NULL;
            // Readjust the currentprofile pointer with the new current widget item
            if ((p = up.ProfileTree->currentItem()) != NULL)
            {
                for (int i = 0; i < users.count(); i++) 
                {
                    if (users[i]->IsPartOfUSMProfile(p))
                    {
                        currentprofile = users[i];
                        break;
                    }
                }
            }
            break;
        }
    }
}

void USMProfileManager::SelectedUSMProfile(QTreeWidgetItem * item, QTreeWidgetItem *)
{
    for (int i = 0; i < users.count(); i++) 
    {
        if (users[i]->IsPartOfUSMProfile(item))
        {
            currentprofile = users[i];
            users[i]->SelectUSMProfile(item);
            return;
        }
    }
}

void USMProfileManager::USMSecNameChange(QTreeWidgetItem * item, int column)
{
    if (column != 0)
        return;

    for (int i = 0; i < users.count(); i++) 
    {
        if (users[i]->IsPartOfUSMProfile(item))
        {
            users[i]->SetName(item->text(0));
            return;
        }
    }
}

int USMProfileManager::UiAuthToLibAuth(int prot)
{
    switch(prot)
    {
        case 0: return SNMP_AUTHPROTOCOL_NONE;
        case 1: return SNMP_AUTHPROTOCOL_HMACMD5;
        case 2: return SNMP_AUTHPROTOCOL_HMACSHA;
        case 3: return SNMP_AUTHPROTOCOL_HMAC128SHA224;
        case 4: return SNMP_AUTHPROTOCOL_HMAC192SHA256;
        case 5: return SNMP_AUTHPROTOCOL_HMAC256SHA384;
        case 6: return SNMP_AUTHPROTOCOL_HMAC384SHA512;
        default:
            break;
    }

    return SNMP_AUTHPROTOCOL_NONE;
}

int USMProfileManager::LibAuthToUiAuth(int prot)
{
    switch(prot)
    {
        case SNMP_AUTHPROTOCOL_NONE: return 0;
        case SNMP_AUTHPROTOCOL_HMACMD5: return 1;
        case SNMP_AUTHPROTOCOL_HMACSHA: return 2;
        case SNMP_AUTHPROTOCOL_HMAC128SHA224: return 3;
        case SNMP_AUTHPROTOCOL_HMAC192SHA256: return 4;
        case SNMP_AUTHPROTOCOL_HMAC256SHA384: return 5;
        case SNMP_AUTHPROTOCOL_HMAC384SHA512: return 6;
        default:
            break;
    }

    return 0;
}

int USMProfileManager::UiPrivToLibPriv(int prot)
{
    switch(prot)
    {
        case 0: return SNMP_PRIVPROTOCOL_NONE;
        case 1: return SNMP_PRIVPROTOCOL_DES;
        case 2: return SNMP_PRIVPROTOCOL_3DESEDE;
        case 3: return SNMP_PRIVPROTOCOL_IDEA;
        case 4: return SNMP_PRIVPROTOCOL_AES128;
        case 5: return SNMP_PRIVPROTOCOL_AES192;
        case 6: return SNMP_PRIVPROTOCOL_AES256;
        default:
            break;
    }

    return SNMP_PRIVPROTOCOL_NONE;
}

int USMProfileManager::LibPrivToUiPriv(int prot)
{
    switch(prot)
    {
        case SNMP_PRIVPROTOCOL_NONE: return 0;
        case SNMP_PRIVPROTOCOL_DES: return 1;
        case SNMP_PRIVPROTOCOL_3DESEDE: return 2;
        case SNMP_PRIVPROTOCOL_IDEA: return 3;
        case SNMP_PRIVPROTOCOL_AES128: return 4;
        case SNMP_PRIVPROTOCOL_AES192: return 5;
        case SNMP_PRIVPROTOCOL_AES256: return 6;
        default:
            break;
    }

    return 0;
}

QStringList USMProfileManager::GetUsersList(void)
{
    QStringList sl;

    for (const UsmCredentialRecord &record : credentialService->records())
        sl << record.securityName;

    return sl;
}

void USMProfileManager::Duplicate(void)
{
    if (!currentprofile) return;
    UsmCredentialRecord copy = currentprofile->GetRecord();
    copy.identity = credentialService->createWorkingRecord(copy.securityName + tr(" copy")).identity;
    copy.securityName += tr(" copy");
    auto *duplicate = new USMProfile(&up, copy);
    users.append(duplicate);
    up.ProfileTree->setCurrentItem(duplicate->GetUserWidgetItem());
    up.SecName->setFocus(Qt::OtherFocusReason);
    up.SecName->selectAll();
}

UsmCredentialService *USMProfileManager::Credentials() const
{
    return credentialService;
}

USMProfile::USMProfile(Ui_USMProfile *uiup, const UsmCredentialRecord &source)
    : record(source)
{
    up = uiup;

    user = new QTreeWidgetItem(up->ProfileTree);

    user->setText(0, record.securityName);
}

USMProfile::~USMProfile()
{
    delete user;
}

int USMProfile::SelectUSMProfile(QTreeWidgetItem * item)
{
    if (item == user)
    {
        up->SecName->setText(record.securityName);
        up->AuthProtocol->setCurrentIndex(record.authProtocol);
        up->AuthPass->setText(QString::fromLatin1(record.authSecret.bytes()));
        up->PrivProtocol->setCurrentIndex(record.privacyProtocol);
        up->PrivPass->setText(QString::fromLatin1(record.privacySecret.bytes()));

        return 1;
    }

    return 0;
}

int USMProfile::IsPartOfUSMProfile(QTreeWidgetItem * item)
{
    if (item == user)
        return 1;
    else
        return 0;
}

QTreeWidgetItem *USMProfile::GetUserWidgetItem(void)
{
    return user;
}

void USMProfile::SetName(QString n)
{
    record.securityName = n;
    record.displayName = n;
    up->SecName->setText(n);
}

QString USMProfile::GetName(void)
{
    return record.securityName;
}

void USMProfile::SetSecName(void)
{
    record.securityName = up->SecName->text();
    record.displayName = record.securityName;
    user->setText(0, record.securityName);
}

void USMProfile::SetAuthProto(void)
{
    record.authProtocol = up->AuthProtocol->currentIndex();
}

int USMProfile::GetAuthProto(void)
{
    return record.authProtocol;
}

void USMProfile::SetAuthPass(void)
{
    record.authSecret = CredentialSecret(up->AuthPass->text().toLatin1());
}

QString USMProfile::GetAuthPass(void)
{
    return QString::fromLatin1(record.authSecret.bytes());
}

void USMProfile::SetPrivProto(void)
{
    record.privacyProtocol = up->PrivProtocol->currentIndex();
}

int USMProfile::GetPrivProto(void)
{
    return record.privacyProtocol;
}

void USMProfile::SetPrivPass(void)
{
    record.privacySecret = CredentialSecret(up->PrivPass->text().toLatin1());
}

QString USMProfile::GetPrivPass(void)
{
    return QString::fromLatin1(record.privacySecret.bytes());
}

void USMProfile::SetSecurity(int aprot, QString apass, int pprot, QString ppass)
{
    record.authProtocol = aprot;
    record.authSecret = CredentialSecret(apass.toLatin1());
    record.privacyProtocol = pprot;
    record.privacySecret = CredentialSecret(ppass.toLatin1());
}

