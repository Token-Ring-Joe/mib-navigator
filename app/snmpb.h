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
#ifndef SNMPB_H
#define SNMPB_H

#include "ui_mainw.h"

#include "snmpb_version.h"

class MibModule;
class Trap;
class Agent;
class MibEditor;
class LogSnmpb;
class USMProfileManager;
class UsmCredentialService;
class CommunityCredentialService;
class Preferences;
class QDockWidget;
class MibLibraryWidget;
class WorkbenchShell;
class StartupCoordinator;

class Snmpb: public QObject
{
    Q_OBJECT
    
public:
    Snmpb(bool offline = false);
    ~Snmpb() override;
    void BindToGUI(QMainWindow *mw);
    void StartDeferredInitialization();
    Ui_MainW* MainUI(void);
    Agent* AgentObj(void);
    Trap* TrapObj(void);
    MibViewLoader* MibLoaderObj(void);
    MibModule* MibModuleObj(void);
    MibEditor* MibEditorObj(void);
    MibLibraryWidget* MibLibraryObj(void) { return mibLibrary; }
    USMProfileManager* UPManagerObj(void);
    UsmCredentialService* UsmCredentials(void);
    CommunityCredentialService* CommunityCredentials(void);
    Preferences* PreferencesObj(void);
    StartupCoordinator* StartupCoordinatorObj(void) { return startup; }
    void Shutdown();
    void ShowEditorDocument();

    void CheckForConfigFiles(void);
    QString GetBootCounterConfigFile(void);
    QString GetSmiConfigFile(void);
    QString GetUsmUsersConfigFile(void);
    QString GetLogConfigFile(void);
    QString GetCredentialIdentitiesConfigFile(void);
    QString GetCommunityCredentialsConfigFile(void);

public slots:
    void TabSelected(void);
    void ManagePreferences(bool);
    void AboutBox(bool);

private:
    void SetEditorMenus(bool value);

private:
    Ui_MainW w;
    USMProfileManager *upm;
    CommunityCredentialService *communityCredentialService;
    Preferences *prefs;

    MibModule *modules;
    MibViewLoader loader;
    Trap *trap;
    Agent *agent;
    MibEditor *editor;
    LogSnmpb *logsnmpb;
    MibLibraryWidget *mibLibrary;
    WorkbenchShell *workbench = nullptr;
    StartupCoordinator *startup = nullptr;
    QMainWindow *mainWindow = nullptr;
    class CredentialsDialog *credentialsDialog = nullptr;

    QString start_msg;
    bool start_issuccess;
    bool initializingMibProfile = true;
    bool startupCatalogRequested = false;
};

#endif /* SNMPB_H */

