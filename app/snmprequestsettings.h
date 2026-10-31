#ifndef SNMPREQUESTSETTINGS_H
#define SNMPREQUESTSETTINGS_H

#include <qstring.h>

// The settings one SNMP request needs: where it goes, which protocol version, and how it
// authenticates. The connection bar builds one of these for every request
// (see connectionmodel.cpp). Saved Agent Profiles (agents.conf) no longer exist; that file
// is left on disk untouched and unread. The member names (profileId, name, readcomm, ...)
// are the old field names, kept so request code did not have to change.
struct SnmpRequestSettings
{
    QString profileId;
    QString name;
    bool v1;
    bool v2;
    bool v3;
    QString address;
    QString port;
    int retries;
    int timeout;
    QString readcomm;
    QString writecomm;
    int maxrepetitions;
    int nonrepeaters;
    QString secname;
    int seclevel;
    QString contextname;
    QString contextengineid;
};

#endif /* SNMPREQUESTSETTINGS_H */
