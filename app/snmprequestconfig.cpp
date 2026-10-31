#include "snmprequestconfig.h"

QString SnmpRequestConfig::endpoint() const
{
    return address + QLatin1Char('/') + port;
}

bool SnmpRequestConfig::FromProfile(const SnmpRequestSettings &profile,
                                    int selectedProtocol,
                                    SnmpRequestConfig *config)
{
    return FromProfile(profile, selectedProtocol,
                       CredentialResolver::inlineValues(
                           profile.readcomm, profile.writecomm,
                           profile.secname, profile.seclevel), config);
}

bool SnmpRequestConfig::FromProfile(const SnmpRequestSettings &profile,
                                    int selectedProtocol,
                                    const EffectiveCredentialValues &credentials,
                                    SnmpRequestConfig *config)
{
    if (!config)
        return false;

    SnmpRequestVersion version;
    if (selectedProtocol == 0)
        version = SnmpRequestVersion::V1;
    else if (selectedProtocol == 1)
        version = SnmpRequestVersion::V2c;
    else if (selectedProtocol == 2)
        version = SnmpRequestVersion::V3;
    else
        return false;

    config->version = version;
    config->address = profile.address;
    config->port = profile.port;
    config->retries = profile.retries;
    config->timeout = profile.timeout;
    config->readCommunity = credentials.readCommunity;
    config->writeCommunity = credentials.writeCommunity;
    config->securityName = credentials.securityName;
    config->securityLevel = credentials.securityLevel;
    config->contextName = profile.contextname;
    config->contextEngineId = profile.contextengineid;
    config->maxRepetitions = profile.maxrepetitions;
    config->nonRepeaters = profile.nonrepeaters;
    return true;
}
