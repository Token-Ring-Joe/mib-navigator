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
#ifndef MIBMODULE_H
#define MIBMODULE_H

#include "snmpb.h"
#include "mibview.h"
#include "mibrecords.h"
#include "miblibrary.h"
#include "mibdependencyindex.h"
#include "mibeffectiveplan.h"
#include "mibruntimeparser.h"
#include "mibenvironment.h"
#include "mibenvironmentmanager.h"

#define SMI_PATH_SEPARATOR ';'


class LoadedMibModule
{
public:
    explicit LoadedMibModule(MibModuleRecord moduleRecord);
    
    void PrintProperties(QString& text);   
    QString GetMibLanguage() const;

    QString name;
    QString path;
    MibModuleRecord record;
};

class MibModule: public QObject
{
    Q_OBJECT
    
public:

    MibModule(Snmpb *snmpb);
    ~MibModule() override;
    void SendLogError(const QString& text){ErrorWhileLoading=true; emit LogError(text);}

    void RegenerateSmiConf();
    void ReadMibPaths();
    QStringList AvailableModuleNames() const;
    QList<MibModuleRecord> AvailableModuleRecords() const { return AvailableRecords; }
    QStringList LoadedModuleNames() const;
    bool ApplyProfileRuntime(const MibEffectivePlan &plan, QString *error = nullptr);
    void RestoreRuntimeAfterEditorValidation();
    MibEffectivePlan BuildEffectivePlan(const MibProfileRecord &profile) const;
    MibEnvironmentPtr CurrentEnvironment() const { return currentEnvironment; }
    bool ValidateModuleFile(const QString &path, QString *error = nullptr,
                            MibValidationLevel level = MibValidationLevel::ErrorsAndWarnings);
    MibModuleRecord ModuleMetadata(const QString &moduleName, const QString &localPath = {});
    MibProfileDependencyCheck CheckProfileDependencies(const QString &profileId,
        const QStringList &explicitModules, bool includeStandardBase, QString *error = nullptr);
    MibProfileDependencyCheck CachedProfileDependencies(const QString &profileId,
        const QString &signature) const;
    MibDependencyIndex *DependencyIndex() { return &dependencyIndex; }
    bool DependencyIndexStale() const { return dependencyIndexStale; }
    MibDependencyScanResult RefreshDependencyIndex(QString *error = nullptr);

public slots:
    void Refresh();
    void RescanPath();

    void AddModule();
    void RemoveModule();
    void ShowModuleInfo();

signals:
    void ModuleProperties(const QString& text);
    void LogError(const QString& text);
    void StopAgentTimer();
    void inventoryChanged();
    void profileRuntimeBuildStarted(const QString &profileName);
    void profileRuntimeReady(const QString &profileId, const MibEffectivePlan &plan,
        MibEnvironmentPtr environment, QStringList loadedModules, bool cacheHit, bool partial);
    void profileRuntimeFailed(const QString &profileId, const QString &error);

private:
    MibEnvironmentBuildResult BuildEnvironment(const MibEffectivePlan &plan);
    QStringList LoadEffectivePlan(const MibEffectivePlan &plan,
                                  QList<MibExplicitRootLoadResult> *outcomes = nullptr);
    void InitLib(int restart);
    void RebuildTotalList();
    void RebuildCandidateList();
    void RebuildLoadedList();
    void RebuildUnloadedList();

private:
    Snmpb *s;

    QStringList Unloaded;
    QList<LoadedMibModule> Loaded;
    QList<QStringList> Total;
    QStringList KnownModuleNames;
    QList<MibModuleRecord> AvailableRecords;
    bool ErrorWhileLoading;
    MibDependencyIndex dependencyIndex;
    bool dependencyIndexStale = false;
    MibEffectivePlan activeProfilePlan;
    bool hasActiveProfilePlan = false;
    MibEnvironmentPtr currentEnvironment;
    std::unique_ptr<MibEnvironmentManager> environmentManager;
    QHash<QString, MibEffectivePlan> requestedPlans;
    MibEffectivePlan latestRequestedPlan;
};

#endif /* MIBMODULE_H */
