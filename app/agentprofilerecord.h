#ifndef AGENTPROFILERECORD_H
#define AGENTPROFILERECORD_H

#include <qstring.h>

// The settings one SNMP request needs: where it goes, which protocol version, and how it
// authenticates. The connection bar builds one of these for every request
// (see connectionmodel.cpp). The name is historical: saved Agent Profiles (agents.conf) no
// longer exist, and that file is left on disk untouched and unread.
struct AgentProfileRecord
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

#endif /* AGENTPROFILERECORD_H */
