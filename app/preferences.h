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
#ifndef PREFERENCES_H
#define PREFERENCES_H

#include "snmpb.h"
#include "ui_preferences.h"
#include "preferencesettings.h"
#include <qdialog.h>
#include <qtreewidget.h>
#include <qsettings.h>

class Preferences: public QObject
{
    Q_OBJECT
    
public:
    Preferences(Snmpb *snmpb);
    ~Preferences() override;
    void Init(void);
    void Execute(void);
    bool Save();
    int GetTrapPort4();
    int GetTrapPort6();
    bool GetEnableIPv4();
    bool GetEnableIPv6();
    bool ShouldListenStdTrapPort4();
    bool ShouldListenStdTrapPort6();
    bool GetExpandTrapBinding(void);
    bool GetShowAgentName(void);
    int GetTrapHistoryLimit(void) const;
    QStringList DefaultMibPaths() const;

signals:
    void trapHistoryLimitChanged(int maximumRecords);

public slots:
    void SetEnableIPv4(bool checked);
    void SetEnableIPv6(bool checked);

    static void RestoreWindowGeometry(QMainWindow&);
    static void SaveWindowGeometry(const QMainWindow&);

private:
    Snmpb *s;
    Ui_Preferences *p = nullptr;
    QDialog *pw = nullptr;

    void LoadControls();
    void ApplyLibraryRoot();
    class QLineEdit *libraryRoot = nullptr;
    PreferencesSettings ReadControls() const;
    void UpdateValidation();
    PreferencesSettings applied;
};

#endif /* PREFERENCES_H */
