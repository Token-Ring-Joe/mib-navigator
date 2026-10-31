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
#include <memory>
#include <QCoreApplication>
#include <QDir>
#include <qmessagebox.h> 
#include <qfileinfo.h>
#include <qtextstream.h>
#include <QFormLayout>
#include <QGroupBox>
#include <QSpinBox>
#include <QVBoxLayout>

#include "mibmodule.h"
#include "preferences.h"
#include "preferencesettings.h"
#include "productidentity.h"

const char default_mib_config[] = R"(
IF-MIB
RFC1213-MIB
SNMP-FRAMEWORK-MIB
SNMP-NOTIFICATION-MIB
SNMPv2-MIB
SNMPv2-TM
SNMP-VIEW-BASED-ACM-MIB
)";

#define STANDARD_TRAP_PORT       162


Preferences::Preferences(Snmpb *snmpb)
{
    s = snmpb;

    // setup defaults for the default constructor of QSettings
    QSettings::setDefaultFormat(QSettings::IniFormat);
    // These legacy identifiers intentionally remain unchanged so upgrades
    // retain every existing preference and application data file.
    QCoreApplication::setOrganizationDomain(ProductIdentity::LegacySettingsDomain);
    QCoreApplication::setApplicationName(ProductIdentity::LegacySettingsApplication);

    // read early settings necessary to decide when to drop root
    QSettings settings;
    const PreferencesSettings persisted = PreferencesSettings::load(settings);
    enableipv4 = persisted.enableIpv4;
    trapport4 = persisted.trapPort4;
    enableipv6 = persisted.enableIpv6;
    trapport6 = persisted.trapPort6;
}

void Preferences::Init(void)
{
    p = new Ui_Preferences();
    pw = new QDialog(s->MainUI()->TabW);
    p->setupUi(pw);

    // Set some properties for the Preferences TreeView
    p->PreferencesTree->header()->hide();
    p->PreferencesTree->setSortingEnabled( false );
    p->PreferencesTree->header()->setSortIndicatorShown( false );
    p->PreferencesTree->setLineWidth( 2 );
    p->PreferencesTree->setAllColumnsShowFocus( false );
    p->PreferencesTree->setFrameShape(QFrame::WinPanel);
    p->PreferencesTree->setFrameShadow(QFrame::Plain);
    p->PreferencesTree->setRootIsDecorated( true );

    transport = new QTreeWidgetItem(p->PreferencesTree);
    transport->setText(0, tr("Transport"));
    mibtree = new QTreeWidgetItem(p->PreferencesTree);
    mibtree->setText(0, tr("MIB Tree"));
    modules = new QTreeWidgetItem(p->PreferencesTree);
    modules->setText(0, tr("Modules"));
    traps = new QTreeWidgetItem(p->PreferencesTree);
    traps->setText(0, tr("Traps"));
    requestDefaults = new QTreeWidgetItem(p->PreferencesTree);
    requestDefaults->setText(0, tr("SNMP Request Defaults"));
    auto *requestPage = new QWidget(p->PreferencesProps);
    auto *requestPageLayout = new QVBoxLayout(requestPage);
    auto *requestGroup = new QGroupBox(tr("SNMP Request Defaults"), requestPage);
    auto *requestForm = new QFormLayout(requestGroup);
    requestPortSpin = new QSpinBox(requestGroup);
    requestPortSpin->setRange(1, 65535);
    requestPortSpin->setToolTip(tr("Used when the Target has no :port"));
    requestTimeoutSpin = new QSpinBox(requestGroup);
    requestTimeoutSpin->setRange(1, 3600); requestTimeoutSpin->setSuffix(tr(" seconds"));
    requestRetriesSpin = new QSpinBox(requestGroup); requestRetriesSpin->setRange(0, 100);
    bulkNonRepeatersSpin = new QSpinBox(requestGroup); bulkNonRepeatersSpin->setRange(0, 65535);
    bulkMaxRepetitionsSpin = new QSpinBox(requestGroup); bulkMaxRepetitionsSpin->setRange(0, 65535);
    requestForm->addRow(tr("Default SNMP port"), requestPortSpin);
    requestForm->addRow(tr("Timeout"), requestTimeoutSpin);
    requestForm->addRow(tr("Retries"), requestRetriesSpin);
    requestForm->addRow(tr("GETBULK non-repeaters"), bulkNonRepeatersSpin);
    requestForm->addRow(tr("GETBULK max repetitions"), bulkMaxRepetitionsSpin);
    requestPageLayout->addWidget(requestGroup); requestPageLayout->addStretch();
    p->PreferencesProps->addWidget(requestPage);

    connect( p->PreferencesTree, 
             SIGNAL( currentItemChanged( QTreeWidgetItem *, QTreeWidgetItem * ) ),
             this, SLOT( SelectedPreferences( QTreeWidgetItem *, QTreeWidgetItem * ) ) );
    connect( p->HorizontalSplit, SIGNAL( toggled(bool) ),
             this, SLOT( SetHorizontalSplit(bool) ) );
    connect( p->TrapPort, SIGNAL( valueChanged( int ) ), 
             this, SLOT ( SetTrapPort() ) );
    connect( p->TrapPort6, SIGNAL( valueChanged( int ) ), 
             this, SLOT ( SetTrapPort6() ) );
    connect( p->EnableIPv4, SIGNAL( toggled(bool) ),
             this, SLOT( SetEnableIPv4(bool) ) );
    connect( p->EnableIPv6, SIGNAL( toggled(bool) ),
             this, SLOT( SetEnableIPv6(bool) ) );
    connect( p->ExpandTrapBinding, SIGNAL( toggled(bool) ),
             this, SLOT( SetExpandTrapBinding(bool) ) );
    connect( p->MibLoadingEnable, SIGNAL( toggled(bool) ),
             this, SLOT( SelectAutomaticLoading() ) );
    connect( p->MibLoadingEnablePrompt, SIGNAL( toggled(bool) ),
             this, SLOT( SelectAutomaticLoading() ) );
    connect( p->MibLoadingDisable, SIGNAL( toggled(bool) ),
             this, SLOT( SelectAutomaticLoading() ) );
    connect( p->ShowAgentName, SIGNAL( toggled(bool) ),
             this, SLOT( SetShowAgentName(bool) ) );
    connect( p->ModulePathsReset, 
             SIGNAL( clicked() ), this, SLOT( MibPathReset() ));
    connect( p->ModulePathsAdd, 
             SIGNAL( clicked() ), this, SLOT( MibPathAdd() ));
    connect( p->ModulePathsDelete, 
             SIGNAL( clicked() ), this, SLOT( MibPathDelete() ));

    // Load preferences from the user-scope INI file
    QSettings settings;
    const PreferencesSettings persisted = PreferencesSettings::load(settings);
    horizontalsplit = persisted.horizontalSplit;
    p->HorizontalSplit->setChecked(horizontalsplit);

    p->EnableIPv4->setChecked(enableipv4);
    p->EnableIPv6->setChecked(enableipv6);

    expandtrapbinding = persisted.expandTrapBinding;
    p->ExpandTrapBinding->setChecked(expandtrapbinding);

    showagentname = persisted.showAgentName;
    p->ShowAgentName->setChecked(showagentname);

    traphistorylimit = persisted.trapHistoryLimit;
    p->TrapHistoryLimit->setValue(traphistorylimit);
    p->TrapHistoryLimit->setSuffix(tr(" traps"));
    p->TrapHistoryLimit->setToolTip(tr("Maximum number of traps retained in memory"));
    p->MibLoadingEnable->setToolTip(tr("Automatically load a matching MIB during an OID walk"));
    p->MibLoadingEnablePrompt->setToolTip(tr("Ask before loading a matching MIB during an OID walk"));

    automaticloading = persisted.automaticLoading;
    if (automaticloading == 1) p->MibLoadingEnable->setChecked(true);
    else if (automaticloading == 2) p->MibLoadingEnablePrompt->setChecked(true);
    else if (automaticloading == 3) p->MibLoadingDisable->setChecked(true);

    curprofile = persisted.selectedProfile;
    curprofileid = persisted.selectedProfileId;
    curproto = persisted.selectedProtocol;
    requestPortSpin->setValue(persisted.requestPort);
    requestTimeoutSpin->setValue(persisted.requestTimeout);
    requestRetriesSpin->setValue(persisted.requestRetries);
    bulkNonRepeatersSpin->setValue(persisted.bulkNonRepeaters);
    bulkMaxRepetitionsSpin->setValue(persisted.bulkMaxRepetitions);

    // reset to default MIB paths if needed
    if (settings.value("mibpaths/size", 0) == 0) {
        MibPathReset();
    }

    MibPathRefresh();

    p->PreferencesTree->setCurrentItem(p->PreferencesTree->topLevelItem(0));
}

void Preferences::Execute (void)
{
    if(pw->exec() != QDialog::Accepted)
    {
        MibPathRefresh();
        return;
    }

    QSettings settings;
    // Warn if trap port or transport changed ...
    PreferencesSettings edited;
    edited.trapPort4 = trapport4;
    edited.trapPort6 = trapport6;
    edited.enableIpv4 = enableipv4;
    edited.enableIpv6 = enableipv6;
    if (edited.networkRestartRequired(PreferencesSettings::load(settings)))
        QMessageBox::information(NULL,
                                 tr("Restart needed"),
                                 tr("MIB Navigator transport protocol or trap port has changed.\n"
                                    "Please restart MIB Navigator for the changes to take effect."),
                                 QMessageBox::Ok);

    Save();
}

void Preferences::Save()
{
    QSettings settings;

    PreferencesSettings values = PreferencesSettings::load(settings);
    values.trapPort4 = trapport4;
    values.trapPort6 = trapport6;
    values.enableIpv4 = enableipv4;
    values.enableIpv6 = enableipv6;
    values.horizontalSplit = horizontalsplit;
    values.expandTrapBinding = expandtrapbinding;
    values.showAgentName = showagentname;
    traphistorylimit = p->TrapHistoryLimit->value();
    values.trapHistoryLimit = traphistorylimit;
    values.automaticLoading = automaticloading;
    values.selectedProfile = curprofile;
    values.selectedProfileId = curprofileid;
    values.selectedProtocol = curproto;
    values.requestPort = requestPortSpin->value();
    values.requestTimeout = requestTimeoutSpin->value();
    values.requestRetries = requestRetriesSpin->value();
    values.bulkNonRepeaters = bulkNonRepeatersSpin->value();
    values.bulkMaxRepetitions = bulkMaxRepetitionsSpin->value();

    QList<QListWidgetItem *> l = p->ModulePaths->findItems("*", Qt::MatchWildcard);
    for (int i = 0; i < l.size(); i++)
        values.mibPaths << l[i]->text();

    values.save(settings, true);

    // Refresh the MIB lists
    s->MibModuleObj()->Refresh();

    // Refresh UI
    s->TabSelected();
}

void Preferences::SaveWindowGeometry(const QMainWindow& mw)
{
    QSettings settings;
    settings.beginGroup("mainwindow");
    settings.setValue("size", mw.size());
    settings.setValue("pos", mw.pos());
}

void Preferences::RestoreWindowGeometry(QMainWindow & mw)
{
    QSettings settings;
    settings.beginGroup("mainwindow");
    if (settings.contains("size"))
        mw.resize(settings.value("size").toSize());
    if (settings.contains("pos"))
        mw.move(settings.value("pos").toPoint());
}

void Preferences::MibPathAdd()
{
    QListWidgetItem *item = new QListWidgetItem(tr("type new path here"), p->ModulePaths);
    item->setFlags(item->flags() | Qt::ItemIsEditable);

    p->ModulePaths->addItem(item);
    p->ModulePaths->editItem(item);
}

void Preferences::MibPathDelete()
{
    QList<QListWidgetItem *> todel  = p->ModulePaths->selectedItems();
    QList<QListWidgetItem *> total = p->ModulePaths->findItems("*", Qt::MatchWildcard);

    // Protection
    if (todel.size() >= total.size())
    {
        QMessageBox::warning(NULL, tr("MIB Navigator warning"),
                             tr("Must have at least one defined path. Delete failed."),
                             QMessageBox::Ok);
        return;
    }

    for (int i = 0; i < todel.size(); i++)
        delete p->ModulePaths->takeItem(p->ModulePaths->row(todel[i]));
}

void Preferences::MibPathRefresh()
{
    // discard widget contents and refill it with mib preloads from settings
    p->ModulePaths->clear();
    QSettings settings;
    int size = settings.beginReadArray("mibpaths");
    for (int i = 0; i < size; ++i) {
        settings.setArrayIndex(i);
        p->ModulePaths->addItem(settings.value("dir").toString());
    }

    // set all editable
    QList<QListWidgetItem *> l = p->ModulePaths->findItems("*", Qt::MatchWildcard);
    for (int i = 0; i < l.size(); i++)
        l[i]->setFlags(l[i]->flags() | Qt::ItemIsEditable);
}

void Preferences::MibPathReset()
{
    // "Reset to default" for MIB paths
    QStringList defaultpaths = DefaultMibPaths();

    QSettings settings;
    settings.beginWriteArray("mibpaths");
    for (int i = 0; i < defaultpaths.size(); ++i) {
        settings.setArrayIndex(i);
        settings.setValue("dir", defaultpaths[i]);
    }
    settings.endArray();

    s->MibModuleObj()->RescanPath();
}

QStringList Preferences::DefaultMibPaths() const
{
    const QDir application_dir(QCoreApplication::applicationDirPath());
    QStringList data_roots;

#ifdef SNMPB_INSTALL_DATA_RELATIVE_PATH
    data_roots << QDir::cleanPath(
        application_dir.filePath(SNMPB_INSTALL_DATA_RELATIVE_PATH));
#endif

#ifdef Q_OS_MACOS
    data_roots << QDir::cleanPath(application_dir.filePath("../Resources"));
#endif

    data_roots << application_dir.absolutePath();

    QStringList paths;
    for (const QString &data_root : data_roots)
    {
        const QDir root(data_root);
        const QString mib_path = root.absoluteFilePath("mibs");
        const QString pib_path = root.absoluteFilePath("pibs");

        if (QDir(mib_path).exists() && !paths.contains(mib_path))
            paths << mib_path;
        if (QDir(pib_path).exists() && !paths.contains(pib_path))
            paths << pib_path;
    }

    // Keep a deterministic executable-relative default even when a damaged or
    // incomplete installation is missing its data directories.
    if (paths.isEmpty())
    {
        paths << application_dir.absoluteFilePath("mibs")
              << application_dir.absoluteFilePath("pibs");
    }

    return paths;
}

void Preferences::SetHorizontalSplit(bool checked)
{
    horizontalsplit = checked;
    s->MainUI()->QuerySplitter->setOrientation(checked ? Qt::Vertical : Qt::Horizontal);
}

void Preferences::SetTrapPort()
{
    trapport4 = p->TrapPort->value();
}

void Preferences::SetTrapPort6()
{
    trapport6 = p->TrapPort6->value();
}

void Preferences::SetEnableIPv4(bool checked)
{
    if (!checked && !enableipv6)
    {
        QMessageBox::critical(NULL, tr("MIB Navigator error"),
                              tr("Must enable at least one transport protocol."),
                              QMessageBox::Ok);
        p->EnableIPv4->setCheckState(Qt::Checked);
        return;
    }

    enableipv4 = checked;
}

void Preferences::SetEnableIPv6(bool checked)
{
    if (!checked && !enableipv4)
    {
        QMessageBox::critical(NULL, tr("MIB Navigator error"),
                              tr("Must enable at least one transport protocol."),
                              QMessageBox::Ok);
        p->EnableIPv6->setCheckState(Qt::Checked);
        return;
    }

    enableipv6 = checked;
}

void Preferences::SetExpandTrapBinding(bool checked)
{
    expandtrapbinding = checked;
}

void Preferences::SetShowAgentName(bool checked)
{
    showagentname = checked;
}

void Preferences::SelectAutomaticLoading()
{
    if (p->MibLoadingEnable->isChecked()) automaticloading = 1;
    else if (p->MibLoadingEnablePrompt->isChecked()) automaticloading = 2;
    else if (p->MibLoadingDisable->isChecked()) automaticloading = 3;
}

bool Preferences::GetExpandTrapBinding(void)
{
    return expandtrapbinding;
}

bool Preferences::GetShowAgentName(void)
{
    return showagentname;
}

bool Preferences::GetEnableIPv4()
{
    return enableipv4;
}

bool Preferences::GetEnableIPv6()
{
    return enableipv6;
}

int Preferences::GetAutomaticLoading(void)
{
    return automaticloading;
}

int Preferences::GetTrapPort4(void)
{
    return trapport4;
}

int Preferences::GetTrapPort6(void)
{
    return trapport6;
}

bool Preferences::ShouldListenStdTrapPort4()
{
    return enableipv4 && trapport4 == STANDARD_TRAP_PORT;
}

bool Preferences::ShouldListenStdTrapPort6()
{
    return enableipv6 && trapport6 == STANDARD_TRAP_PORT;
}

void Preferences::SaveCurrentProfile(QString &name, int proto)
{
    SaveCurrentProfile(name, QString(), proto);
}

int Preferences::GetTrapHistoryLimit(void) const
{
    return traphistorylimit;
}

void Preferences::SaveCurrentProfile(const QString &name,
                                     const QString &profileId, int proto)
{
    curprofile = name;
    curprofileid = profileId;
    curproto = proto;
    QSettings settings;
    settings.setValue("ui/selectedprofile", curprofile);
    if (!curprofileid.isEmpty())
        settings.setValue("ui/selectedprofileid", curprofileid);
    else
        settings.remove("ui/selectedprofileid");
    settings.setValue("ui/selectedproto", curproto);
}

QString Preferences::GetCurrentProfileId(void) const
{
    return curprofileid;
}

int Preferences::GetCurrentProfile(QString &name)
{
    name = curprofile;
    return curproto;
}

void Preferences::SelectedPreferences(QTreeWidgetItem * item, QTreeWidgetItem *)
{
    if (item == transport)
    {
        p->PreferencesProps->setCurrentIndex(0);

        p->EnableIPv4->setCheckState(enableipv4==true?Qt::Checked:Qt::Unchecked);
        p->EnableIPv6->setCheckState(enableipv6==true?Qt::Checked:Qt::Unchecked);
    }
    else
    if (item == mibtree)
    {
        p->PreferencesProps->setCurrentIndex(1);

        p->HorizontalSplit->setCheckState(horizontalsplit==true?Qt::Checked:Qt::Unchecked);
        if (automaticloading == 1) p->MibLoadingEnable->setChecked(true);
        else if (automaticloading == 2) p->MibLoadingEnablePrompt->setChecked(true);
        else if (automaticloading == 3) p->MibLoadingDisable->setChecked(true);
    }
    else
    if (item == modules)
    {
        p->PreferencesProps->setCurrentIndex(2);

        MibPathRefresh();
    }
    else
    if (item == traps)
    {
        p->PreferencesProps->setCurrentIndex(3);

        p->TrapPort->setValue(trapport4);
        p->TrapPort6->setValue(trapport6);
        p->ExpandTrapBinding->setCheckState(expandtrapbinding==true?Qt::Checked:Qt::Unchecked);
        p->ShowAgentName->setCheckState(showagentname==true?Qt::Checked:Qt::Unchecked);
    }
    else if (item == requestDefaults)
        p->PreferencesProps->setCurrentIndex(4);
}

