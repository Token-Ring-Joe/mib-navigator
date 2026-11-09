#ifndef SNMPREQUESTCONFIGADAPTER_H
#define SNMPREQUESTCONFIGADAPTER_H

#include "snmprequestconfig.h"
#include "snmp_pp/snmp_pp.h"

snmp_version SnmpVersion(const SnmpRequestConfig &config);
void ApplySnmpRequestConfig(const SnmpRequestConfig &config,
                            SnmpTarget &target);
void ApplySnmpV3PduConfig(const SnmpRequestConfig &config, Pdu &pdu);

#endif
