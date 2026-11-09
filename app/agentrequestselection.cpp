#include "agentrequestselection.h"

bool AgentRequestSelection::requestConfig(SnmpRequestConfig *config) const
{
    return hasResolvedCredentials
        ? SnmpRequestConfig::FromProfile(profile, selectedProtocol, credentials, config)
        : SnmpRequestConfig::FromProfile(profile, selectedProtocol, config);
}
