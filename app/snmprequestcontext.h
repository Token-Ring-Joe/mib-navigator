#ifndef SNMPREQUESTCONTEXT_H
#define SNMPREQUESTCONTEXT_H

#include "snmprequestconfig.h"
#include "mibenvironment.h"

enum class SnmpRequestOperation
{
    Get,
    GetNext,
    GetBulk,
    Set,
    Walk
};

class SnmpRequestContext
{
public:
    SnmpRequestContext(const SnmpRequestConfig &config,
                       SnmpRequestOperation operation);
    SnmpRequestContext(const SnmpRequestConfig &config,
                       SnmpRequestOperation operation,
                       MibEnvironmentPtr environment);

    const SnmpRequestConfig &config() const;
    SnmpRequestOperation operation() const;
    int maxRepetitions() const;
    int nonRepeaters() const;
    const MibEnvironmentPtr &environment() const;

private:
    SnmpRequestConfig requestConfig;
    SnmpRequestOperation requestOperation;
    MibEnvironmentPtr requestEnvironment;
};

#endif
