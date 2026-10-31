#include "agentrequestselection.h"

#include <QCoreApplication>
#include <QTextStream>

namespace {
int failures = 0;

void check(bool condition, const char *description)
{
    if (!condition) { QTextStream(stderr) << "FAIL: " << description << Qt::endl; ++failures; }
}

SnmpRequestSettings record(const QString &address)
{
    SnmpRequestSettings r{};
    r.name = address; r.profileId = "request";
    r.address = address; r.port = "1161";
    r.v1 = r.v2 = r.v3 = true;
    r.retries = 2; r.timeout = 5;
    r.readcomm = "inline-read"; r.writecomm = "inline-write";
    r.maxrepetitions = 10; r.nonrepeaters = 0;
    r.secname = "inline-user"; r.seclevel = 1;
    return r;
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    // A selection carries the request's target, protocol version and credentials; requestConfig()
    // turns it into the settings the SNMP layer uses.
    AgentRequestSelection selection;
    selection.profile = record("192.0.2.1");
    SnmpRequestConfig config;
    const SnmpRequestVersion expected[] = {SnmpRequestVersion::V1, SnmpRequestVersion::V2c, SnmpRequestVersion::V3};
    for (int protocol = 0; protocol < 3; ++protocol) {
        selection.selectedProtocol = protocol;
        check(selection.requestConfig(&config) && config.version == expected[protocol] &&
              config.address == "192.0.2.1" && config.port == "1161" &&
              config.retries == 2 && config.timeout == 5 && config.readCommunity == "inline-read",
              "an unresolved selection falls back to the record's own values");
    }

    selection.hasResolvedCredentials = true;
    selection.credentials = CredentialResolver::inlineValues("saved-read", "saved-write", "saved-user", 2);
    selection.selectedProtocol = 1;
    check(selection.requestConfig(&config) && config.readCommunity == "saved-read" &&
          config.writeCommunity == "saved-write" && config.securityName == "saved-user" &&
          config.securityLevel == 2 && config.address == "192.0.2.1",
          "resolved credentials replace the record's inline values");

    selection.selectedProtocol = 3;
    check(!selection.requestConfig(&config), "an invalid protocol is rejected");
    check(!selection.requestConfig(nullptr), "a missing output is rejected");

    selection.profile = record("2001:db8::3");
    selection.selectedProtocol = 2;
    check(selection.requestConfig(&config) && config.endpoint() == "2001:db8::3/1161",
          "IPv6 targets keep their address and port");
    return failures ? 1 : 0;
}
