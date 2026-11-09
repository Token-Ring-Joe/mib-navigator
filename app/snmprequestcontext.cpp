#include "snmprequestcontext.h"

SnmpRequestContext::SnmpRequestContext(const SnmpRequestConfig &config,
                                       SnmpRequestOperation operation)
    : SnmpRequestContext(config, operation, {})
{
}

SnmpRequestContext::SnmpRequestContext(const SnmpRequestConfig &config,
                                       SnmpRequestOperation operation,
                                       MibEnvironmentPtr environment)
    : requestConfig(config), requestOperation(operation),
      requestEnvironment(std::move(environment)) {}

const SnmpRequestConfig &SnmpRequestContext::config() const
{
    return requestConfig;
}

SnmpRequestOperation SnmpRequestContext::operation() const
{
    return requestOperation;
}

int SnmpRequestContext::maxRepetitions() const
{
    return requestConfig.maxRepetitions;
}

int SnmpRequestContext::nonRepeaters() const
{
    return requestConfig.nonRepeaters;
}

const MibEnvironmentPtr &SnmpRequestContext::environment() const
{
    return requestEnvironment;
}
