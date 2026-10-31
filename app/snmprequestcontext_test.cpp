#include "snmprequestcontext.h"

#include <QCoreApplication>
#include <QTextStream>

namespace {
int failures = 0;

void check(bool condition, const char *description)
{
    if (!condition)
    {
        QTextStream(stderr) << "FAIL: " << description << Qt::endl;
        ++failures;
    }
}

SnmpRequestSettings profile(const QString &address, int maxRepetitions,
                           int nonRepeaters)
{
    SnmpRequestSettings record{};
    record.v1 = true;
    record.v2 = true;
    record.v3 = true;
    record.address = address;
    record.port = "1161";
    record.retries = 2;
    record.timeout = 10;
    record.readcomm = "public";
    record.writecomm = "private";
    record.maxrepetitions = maxRepetitions;
    record.nonrepeaters = nonRepeaters;
    record.secname = "user";
    record.seclevel = 2;
    record.contextname = "context";
    record.contextengineid = "engine";
    return record;
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    SnmpRequestSettings selected = profile("192.0.2.20", 25, 4);
    SnmpRequestConfig initialConfig;
    check(SnmpRequestConfig::FromProfile(selected, 1, &initialConfig),
          "initial SNMPv2c selection resolves");
    SnmpRequestContext bulk(initialConfig, SnmpRequestOperation::GetBulk);

    // Simulate changing the current UI/profile selection after dispatch.
    selected = profile("2001:db8::30", 99, 8);
    SnmpRequestConfig changedConfig;
    check(SnmpRequestConfig::FromProfile(selected, 2, &changedConfig),
          "replacement selection resolves");
    initialConfig = changedConfig;

    check(bulk.operation() == SnmpRequestOperation::GetBulk,
          "GET-BULK operation identity retained");
    check(bulk.config().version == SnmpRequestVersion::V2c,
          "GET-BULK protocol identity retained");
    check(bulk.config().endpoint() == "192.0.2.20/1161",
          "GET-BULK IPv4 endpoint retained");
    check(bulk.maxRepetitions() == 25 && bulk.nonRepeaters() == 4,
          "GET-BULK continuation parameters retained");

    SnmpRequestConfig walkConfig;
    SnmpRequestSettings walkProfile = profile("2001:db8::40", 40, 6);
    check(SnmpRequestConfig::FromProfile(walkProfile, 1, &walkConfig),
          "walk configuration resolves");
    SnmpRequestContext walk(walkConfig, SnmpRequestOperation::Walk);
    walkProfile.maxrepetitions = 1;
    walkProfile.nonrepeaters = 0;
    walkProfile.address = "192.0.2.99";
    check(walk.operation() == SnmpRequestOperation::Walk &&
          walk.maxRepetitions() == 40 && walk.nonRepeaters() == 6,
          "walk continuation retains original bulk parameters");
    check(walk.config().endpoint() == "2001:db8::40/1161",
          "walk continuation retains original IPv6 endpoint");

    for (int protocol = 0; protocol <= 2; ++protocol)
    {
        SnmpRequestConfig config;
        check(SnmpRequestConfig::FromProfile(selected, protocol, &config),
              "protocol configuration resolves");
        SnmpRequestContext context(config, SnmpRequestOperation::Get);
        const SnmpRequestVersion expected = protocol == 0
            ? SnmpRequestVersion::V1
            : (protocol == 1 ? SnmpRequestVersion::V2c
                             : SnmpRequestVersion::V3);
        check(context.config().version == expected,
              "captured protocol identity retained");
    }

    SnmpRequestConfig operationConfig;
    SnmpRequestConfig::FromProfile(selected, 2, &operationConfig);
    check(SnmpRequestContext(operationConfig, SnmpRequestOperation::GetNext)
                  .operation() == SnmpRequestOperation::GetNext &&
          SnmpRequestContext(operationConfig, SnmpRequestOperation::Set)
                  .operation() == SnmpRequestOperation::Set,
          "GET-NEXT and SET operation identities retained");

    if (failures == 0)
        QTextStream(stdout) << "All SNMP request context tests passed."
                            << Qt::endl;
    return failures == 0 ? 0 : 1;
}
