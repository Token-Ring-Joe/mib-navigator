#ifndef AGENTREQUESTSELECTION_H
#define AGENTREQUESTSELECTION_H

#include <QString>

#include "snmprequestsettings.h"
#include "snmprequestconfig.h"

enum class AgentSelectionError
{
    None,
    ProfileNotFound,
    InvalidProtocol,
    UnsupportedProtocol
};

struct AgentRequestSelection
{
    SnmpRequestSettings profile;
    int selectedProtocol = 0;
    EffectiveCredentialValues credentials;
    bool hasResolvedCredentials = false;

    bool requestConfig(SnmpRequestConfig *config) const;
};

#endif
