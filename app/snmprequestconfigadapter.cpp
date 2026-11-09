#include "snmprequestconfigadapter.h"

snmp_version SnmpVersion(const SnmpRequestConfig &config)
{
    if (config.version == SnmpRequestVersion::V3)
        return version3;
    if (config.version == SnmpRequestVersion::V2c)
        return version2c;
    return version1;
}

void ApplySnmpRequestConfig(const SnmpRequestConfig &config,
                            SnmpTarget &target)
{
    if (config.version == SnmpRequestVersion::V3)
    {
        UTarget &userTarget = static_cast<UTarget &>(target);
        userTarget.set_security_model(SNMP_SECURITY_MODEL_USM);
        userTarget.set_security_name(config.securityName.toLatin1().data());
    }
    else
    {
        CTarget &communityTarget = static_cast<CTarget &>(target);
        communityTarget.set_readcommunity(config.readCommunity.toLatin1().data());
        communityTarget.set_writecommunity(config.writeCommunity.toLatin1().data());
    }

    target.set_version(SnmpVersion(config));
    target.set_retry(config.retries);
    target.set_timeout(100 * config.timeout);
}

void ApplySnmpV3PduConfig(const SnmpRequestConfig &config, Pdu &pdu)
{
    if (config.version != SnmpRequestVersion::V3)
        return;

    if (config.securityLevel == 0)
        pdu.set_security_level(SNMP_SECURITY_LEVEL_NOAUTH_NOPRIV);
    else if (config.securityLevel == 1)
        pdu.set_security_level(SNMP_SECURITY_LEVEL_AUTH_NOPRIV);
    else
        pdu.set_security_level(SNMP_SECURITY_LEVEL_AUTH_PRIV);

    pdu.set_context_name(config.contextName.toLatin1().data());
    pdu.set_context_engine_id(config.contextEngineId.toLatin1().data());
}
