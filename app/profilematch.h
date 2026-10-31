#ifndef PROFILEMATCH_H
#define PROFILEMATCH_H

#include <QList>
#include <QString>
#include <QStringList>
#include <functional>

#include "mibcollectionindex.h"

// "Detect profile": given the sysObjectID a device reported, find which saved MIB Profile holds
// the MIBs for that product. The mapping comes entirely from the MIB collection (which module
// defines the device's registration OID) and from which profiles contain that module. Nothing
// here knows any vendor name or enterprise number.
struct ProfileMatchProfile
{
    QString id;
    QString name;
};

struct ProfileMatchCandidate
{
    QString profileId;
    QString profileName;
    QStringList viaModules;   // the modules that tie this profile to the device
};

struct ProfileMatchResult
{
    enum class Kind { NoMatch, NotEnterprise, Exact, Prefix, Enterprise };
    Kind kind = Kind::NoMatch;
    QString matchedOid;                        // the OID the match was made on
    QList<ProfileMatchCandidate> candidates;
    QStringList modulesWithoutProfile;         // MIBs for the device exist but no profile includes them
    QString explanation;                       // plain English, always filled in
};

// profilesContaining(module) must return only profiles that are meant to be detected
// (the caller leaves out the built-in Standards profile).
ProfileMatchResult MatchProfileForSysObjectId(
    const MibCollectionIndex &index, const QString &sysObjectId,
    const std::function<QList<ProfileMatchProfile>(const QString &module)> &profilesContaining);

#endif
