#include "dialoglayout.h"
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
#include <QCoreApplication>
#include <QDir>
#include <QMessageBox>
#include <QPushButton>
#include "preferences.h"
#include "productidentity.h"

Preferences::Preferences(Snmpb *snmpb) : QObject(snmpb), s(snmpb)
{
    // Keep historical settings identity so existing installations retain their data.
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QCoreApplication::setOrganizationDomain(ProductIdentity::LegacySettingsDomain);
    QCoreApplication::setApplicationName(ProductIdentity::LegacySettingsApplication);
    QSettings settings;
    applied = PreferencesSettings::load(settings);
}

Preferences::~Preferences()
{
    delete p; // Ui_Preferences holds pointers only; widgets belong to the dialog.
}

void Preferences::Init()
{
    p = new Ui_Preferences();
    pw = new QDialog(s->MainUI()->TabW);
    p->setupUi(pw);
    DialogLayout::initialize(*pw, QSize(760, 560));
    for (const QString &section : {tr("General"), tr("SNMP"), tr("Traps")})
        new QTreeWidgetItem(p->PreferencesTree, QStringList{section});
    connect(p->PreferencesTree, &QTreeWidget::currentItemChanged, this,
        [this](QTreeWidgetItem *item) {
            if (item) p->PreferencesProps->setCurrentIndex(p->PreferencesTree->indexOfTopLevelItem(item));
        });
    connect(p->EnableIPv4, &QCheckBox::toggled, this, [this]() { UpdateValidation(); });
    connect(p->EnableIPv6, &QCheckBox::toggled, this, [this]() { UpdateValidation(); });
    LoadControls();
    p->PreferencesTree->setCurrentItem(p->PreferencesTree->topLevelItem(0));
}

void Preferences::LoadControls()
{
    p->EnableIPv4->setChecked(applied.enableIpv4);
    p->EnableIPv6->setChecked(applied.enableIpv6);
    p->TrapPort->setValue(applied.trapPort4);
    p->TrapPort6->setValue(applied.trapPort6);
    p->ExpandTrapBinding->setChecked(applied.expandTrapBinding);
    p->ShowAgentName->setChecked(applied.showAgentName);
    p->TrapHistoryLimit->setValue(applied.trapHistoryLimit);
    p->RequestPort->setValue(applied.requestPort);
    p->RequestTimeout->setValue(applied.requestTimeout);
    p->RequestRetries->setValue(applied.requestRetries);
    p->BulkNonRepeaters->setValue(applied.bulkNonRepeaters);
    p->BulkMaxRepetitions->setValue(applied.bulkMaxRepetitions);
    UpdateValidation();
}

PreferencesSettings Preferences::ReadControls() const
{
    // Retain retired preferences and other settings that this dialog does not edit.
    QSettings settings;
    PreferencesSettings values = PreferencesSettings::load(settings);
    values.enableIpv4 = p->EnableIPv4->isChecked();
    values.enableIpv6 = p->EnableIPv6->isChecked();
    values.trapPort4 = p->TrapPort->value();
    values.trapPort6 = p->TrapPort6->value();
    values.expandTrapBinding = p->ExpandTrapBinding->isChecked();
    values.showAgentName = p->ShowAgentName->isChecked();
    values.trapHistoryLimit = p->TrapHistoryLimit->value();
    values.requestPort = p->RequestPort->value();
    values.requestTimeout = p->RequestTimeout->value();
    values.requestRetries = p->RequestRetries->value();
    values.bulkNonRepeaters = p->BulkNonRepeaters->value();
    values.bulkMaxRepetitions = p->BulkMaxRepetitions->value();
    return values;
}

void Preferences::UpdateValidation()
{
    const bool transport = p->EnableIPv4->isChecked() || p->EnableIPv6->isChecked();
    p->TransportValidation->setVisible(!transport);
    p->OKCancelBox->button(QDialogButtonBox::Ok)->setEnabled(transport);
}

void Preferences::Execute()
{
    LoadControls();
    if (pw->exec() != QDialog::Accepted) {
        LoadControls();
        return;
    }
    const bool restart = ReadControls().networkRestartRequired(applied);
    if (!Save()) return;
    if (restart)
        QMessageBox::information(pw, tr("Restart needed"),
            tr("Transport protocol or trap listen port changed. Restart MIB Navigator for these changes to take effect."));
}

bool Preferences::Save()
{
    const PreferencesSettings values = ReadControls();
    if (!values.enableIpv4 && !values.enableIpv6)
        return false;
    QSettings settings;
    values.save(settings, true, true);
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        QMessageBox::warning(pw, tr("Preferences could not be saved"),
            tr("Check that your settings location is writable. The running preferences have not changed."));
        return false;
    }
    applied = values;
    emit trapHistoryLimitChanged(applied.trapHistoryLimit);
    s->TabSelected();
    return true;
}

void Preferences::SetEnableIPv4(bool checked) { applied.enableIpv4 = checked; }
void Preferences::SetEnableIPv6(bool checked) { applied.enableIpv6 = checked; }
bool Preferences::GetEnableIPv4() { return applied.enableIpv4; }
bool Preferences::GetEnableIPv6() { return applied.enableIpv6; }
int Preferences::GetTrapPort4() { return applied.trapPort4; }
int Preferences::GetTrapPort6() { return applied.trapPort6; }
bool Preferences::GetExpandTrapBinding() { return applied.expandTrapBinding; }
bool Preferences::GetShowAgentName() { return applied.showAgentName; }
int Preferences::GetTrapHistoryLimit() const { return applied.trapHistoryLimit; }
bool Preferences::ShouldListenStdTrapPort4() { return applied.enableIpv4 && applied.trapPort4 == 162; }
bool Preferences::ShouldListenStdTrapPort6() { return applied.enableIpv6 && applied.trapPort6 == 162; }

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

