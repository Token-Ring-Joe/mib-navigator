#include "miblegacymigration.h"
#include "mibdependencyindex.h"
#include "mibprofile.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <iostream>

namespace {
bool check(bool value,const char *message){if(!value)std::cerr<<"FAIL: "<<message<<'\n';return value;}
bool writeFile(const QString&p,const QByteArray&v){QFile f(p);return f.open(QIODevice::WriteOnly)&&f.write(v)==v.size();}
void preloads(QSettings&s,const QStringList&v){s.beginWriteArray("mibpreloads");for(int i=0;i<v.size();++i){s.setArrayIndex(i);s.setValue("mib",v[i]);}s.endArray();s.sync();}
}

int main(int argc,char**argv)
{
    QCoreApplication app(argc,argv);bool ok=true;QTemporaryDir temp;
    const QString mibs=temp.filePath("MIBs");QDir().mkpath(mibs);
    writeFile(QDir(mibs).filePath("a.mib"),"A-MIB DEFINITIONS ::= BEGIN\nEND\n");
    writeFile(QDir(mibs).filePath("synro.mib"),"SYNOPTICS-ROOT-MIB DEFINITIONS ::= BEGIN\nEND\n");
    MibDependencyIndex index(temp.filePath("index.json"));index.update({mibs});

    QSettings empty(temp.filePath("empty.ini"),QSettings::IniFormat);
    MibProfileService emptyProfiles{MibProfileRepository(temp.filePath("empty-profiles.json"))};
    auto emptyResult=MibLegacyMigration::migrate(empty,emptyProfiles,index);
    ok&=check(!emptyResult.needed&&!emptyResult.migrated&&!empty.contains(MibLegacyMigration::markerKey())&&emptyProfiles.profiles().size()==2,"empty/fresh state creates no artifact");

    QSettings settings(temp.filePath("legacy.ini"),QSettings::IniFormat);
    preloads(settings,{"A-MIB","A-MIB","synro.mib","STALE-MIB"});
    const QString profilesPath=temp.filePath("profiles.json");
    MibProfileService profiles{MibProfileRepository(profilesPath)};
    const QString userId=profiles.create("Existing User Profile");
    QElapsedTimer migrationTimer;migrationTimer.start();
    const auto first=MibLegacyMigration::migrate(settings,profiles,index);
    const qint64 migrationMsecs=migrationTimer.elapsed();
    const MibProfileRecord *imported=profiles.find(MibLegacyMigration::profileId());
    const int migratedIdentityCount=imported?imported->explicitModules.size()+imported->unresolvedLegacyModules.size():0;
    ok&=check(first.migrated&&first.inputCount==4&&first.duplicateCount==1&&first.unresolvedCount==1&&
        imported&&MibProfileIsManifest(*imported)&&imported->explicitModules==QStringList({"A-MIB","SYNOPTICS-ROOT-MIB"}),"identity normalization duplicate stale and filename mismatch migration");
    ok&=check(imported&&imported->roots.size()==2&&
        imported->unresolvedLegacyModules==QStringList{"STALE-MIB"},
        "legacy preload migration persists exact unique files and unresolved intent before marker");
    ok&=check(profiles.find(userId)&&profiles.find(userId)->name=="Existing User Profile","existing profile preserved");
    const auto second=MibLegacyMigration::migrate(settings,profiles,index);
    ok&=check(second.alreadyComplete&&!second.migrated&&profiles.profiles().size()==4,"repeated startup is idempotent");
    ok&=check(!profiles.requiresLegacyConversion()&&profiles.hasUnresolvedLegacyResidue(),
        "schema-5 unresolved residue is retryable but does not require startup conversion");
    preloads(settings,{"OTHER-MIB"});
    const auto behindBack=MibLegacyMigration::migrate(settings,profiles,index);
    ok&=check(behindBack.alreadyComplete&&profiles.find(MibLegacyMigration::profileId())->explicitModules==
        QStringList({"A-MIB","SYNOPTICS-ROOT-MIB"}),"legacy mutation is no longer live authority");
    const QList<MibProfileMember> exactBeforeMarkerRetry =
        profiles.find(MibLegacyMigration::profileId())->roots;
    settings.remove(MibLegacyMigration::markerKey());
    preloads(settings,{"A-MIB","synro.mib","STALE-MIB"});
    const auto markerRetry=MibLegacyMigration::migrate(settings,profiles,index);
    ok&=check(markerRetry.migrated&&
        profiles.find(MibLegacyMigration::profileId())->roots==exactBeforeMarkerRetry,
        "marker-write retry never downgrades already-persisted exact membership");
    profiles.remove(MibLegacyMigration::profileId());
    MibLegacyMigration::migrate(settings,profiles,index);
    ok&=check(!profiles.find(MibLegacyMigration::profileId()),"deleted migration profile is not resurrected");

    QSettings failed(temp.filePath("failed.ini"),QSettings::IniFormat);preloads(failed,{"A-MIB"});
    MibProfileService badProfiles{MibProfileRepository(temp.path())};
    const auto failure=MibLegacyMigration::migrate(failed,badProfiles,index);
    ok&=check(!failure.migrated&&!failure.error.isEmpty()&&!failed.contains(MibLegacyMigration::markerKey()),"profile persistence failure does not write marker");

    const QString pendingPath=temp.filePath("pending-profiles.json");
    writeFile(pendingPath,"{\"schemaVersion\":4,\"ordinaryProfilesMigrated\":true,\"profiles\":[{\"id\":\"old\",\"name\":\"Old\",\"type\":\"custom\",\"modules\":[\"A-MIB\"]}]}");
    MibProfileService pending{MibProfileRepository(pendingPath)};
    ok&=check(pending.requiresLegacyConversion(),"schema-1 through schema-4 authority still requests conversion");
    QString pendingError;
    ok&=check(pending.migrateLegacyProfiles(index,&pendingError)&&!pending.requiresLegacyConversion(),
        "explicit legacy conversion produces a schema-5 manifest");
    std::cout<<"migration_inputs="<<first.inputCount<<" identities="
             <<migratedIdentityCount<<" elapsed_ms="<<migrationMsecs<<'\n';
    return ok?0:1;
}
