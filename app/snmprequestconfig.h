#ifndef SNMPREQUESTCONFIG_H
#define SNMPREQUESTCONFIG_H

#include <QString>

#include "snmprequestsettings.h"
#include "credentialrecords.h"

enum class SnmpRequestVersion
{
    V1,
    V2c,
    V3
};

struct SnmpRequestConfig
{
    SnmpRequestVersion version = SnmpRequestVersion::V1;
    QString address;
    QString port;
    int retries = 0;
    int timeout = 0;
    QString readCommunity;
    QString writeCommunity;
    QString securityName;
    int securityLevel = 0;
    std::optional<UsmCredentialRecord> usmCredential;
    QString contextName;
    QString contextEngineId;
    int maxRepetitions = 0;
    int nonRepeaters = 0;

    QString endpoint() const;

    // selectedProtocol uses the existing UI/persistence convention:
    // 0 = SNMPv1, 1 = SNMPv2c, 2 = SNMPv3. Profile support flags are used
    // while resolving the UI selection and do not override that selection.
    static bool FromProfile(const SnmpRequestSettings &profile,
                            int selectedProtocol,
                            SnmpRequestConfig *config);
    static bool FromProfile(const SnmpRequestSettings &profile,
                            int selectedProtocol,
                            const EffectiveCredentialValues &credentials,
                            SnmpRequestConfig *config);
};

#endif
