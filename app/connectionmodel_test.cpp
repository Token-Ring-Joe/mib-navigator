#include "communitycredentialservice.h"
#include "connectionmodel.h"
#include "usmcredentialservice.h"

#include <QCoreApplication>
#include <QSettings>
#include <QTemporaryDir>
#include <iostream>

namespace {
bool check(bool value, const char *message)
{ if (!value) std::cerr << "FAIL: " << message << '\n'; return value; }

CommunityCredentialRecord community(const QString &name, const QByteArray &read)
{
    CommunityCredentialRecord r; r.displayName = name;
    r.readCommunity = CredentialSecret(read); r.writeCommunity = CredentialSecret("write-" + read);
    return r;
}
UsmCredentialRecord usm(const QString &name, int auth, int priv)
{
    UsmCredentialRecord r; r.securityName = name; r.authProtocol = auth;
    r.privacyProtocol = priv;
    if (auth) r.authSecret = CredentialSecret("auth-pass");
    if (priv) r.privacySecret = CredentialSecret("priv-pass");
    return r;
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName("connection-model-test");
    QCoreApplication::setApplicationName("connection-model-test");
    QTemporaryDir dir;
    bool ok = check(dir.isValid(), "temporary directory");

    // ---- target parsing -------------------------------------------------------------------
    const ConnectionTarget plain = ConnectionTarget::parse("  10.1.1.20 ");
    ok &= check(plain.valid && plain.host == "10.1.1.20" && plain.port == "161", "plain IPv4 target");
    const ConnectionTarget withPort = ConnectionTarget::parse("lab-switch.example.net:1161");
    ok &= check(withPort.valid && withPort.host == "lab-switch.example.net" && withPort.port == "1161",
                "host name with port");
    ok &= check(ConnectionTarget::parse("sw1", 1161).port == "1161" && ConnectionTarget::parse("sw1:162", 1161).port == "162",
                "a typed port overrides the default port");
    const ConnectionTarget v6 = ConnectionTarget::parse("fe80::1");
    ok &= check(v6.valid && v6.host == "fe80::1" && v6.port == "161", "bare IPv6 address");
    const ConnectionTarget v6port = ConnectionTarget::parse("[2001:db8::5]:162");
    ok &= check(v6port.valid && v6port.host == "2001:db8::5" && v6port.port == "162", "bracketed IPv6 with port");
    for (const char *bad : {"", "   ", "host name", "10.1.1.20:0", "10.1.1.20:70000", "10.1.1.20:abc",
                            ":161", "[::1", "[::1]x", "a/b"})
        ok &= check(!ConnectionTarget::parse(bad).valid, bad);

    // ---- history --------------------------------------------------------------------------
    QList<ConnectionHistoryEntry> history;
    history = ConnectionHistory::remembered(history, "10.0.0.1", "community:a:v2c");
    history = ConnectionHistory::remembered(history, "10.0.0.2", "usm:u");
    history = ConnectionHistory::remembered(history, "10.0.0.1", "community:b:v1");
    ok &= check(history.size() == 2 && history.first().target == "10.0.0.1" &&
                history.first().credentialKey == "community:b:v1",
                "re-used target moves to the front with its latest credential, no duplicate");
    history = ConnectionHistory::remembered(history, "10.0.0.2", "usm:u");
    history = ConnectionHistory::remembered(history, "  ", "x");
    ok &= check(history.size() == 2, "blank target is not remembered");
    for (int i = 0; i < 60; ++i)
        history = ConnectionHistory::remembered(history, QString("192.0.2.%1").arg(i), "k");
    ok &= check(history.size() == ConnectionHistory::kMaxEntries && history.first().target == "192.0.2.59",
                "history is limited, newest first");
    history = ConnectionHistory::removed(history, "192.0.2.59");
    ok &= check(history.size() == ConnectionHistory::kMaxEntries - 1 && history.first().target == "192.0.2.58",
                "entries can be deleted");
    {
        QSettings settings(dir.filePath("history.ini"), QSettings::IniFormat);
        ConnectionHistory::save(settings, history);
        settings.sync();
        QSettings reread(dir.filePath("history.ini"), QSettings::IniFormat);
        const QList<ConnectionHistoryEntry> loaded = ConnectionHistory::load(reread);
        ok &= check(loaded.size() == history.size() && loaded.first().target == history.first().target &&
                    loaded.first().credentialKey == "k", "history round-trips through settings");
        QSettings garbage(dir.filePath("garbage.ini"), QSettings::IniFormat);
        garbage.setValue("connection/target-history", "not json at all");
        ok &= check(ConnectionHistory::load(garbage).isEmpty(), "corrupt history is ignored, not fatal");
    }

    // ---- credentials and request building ------------------------------------------------
    CommunityCredentialService communityService(dir.filePath("c.conf"));
    const QString lab = communityService.create(community("Lab", "labro"));
    const QString empty = communityService.create(community("Empty", ""));
    UsmCredentialRepository repository(dir.filePath("u.conf"));
    UsmCredentialService usmService({usm("operator", 2, 2), usm("incomplete", 2, 1)}, repository);
    UsmCredentialRecord broken = usm("broken", 2, 1); broken.authSecret = CredentialSecret();
    const QString operatorId = usmService.records().first().identity.credentialId;
    const QString incompleteId = usmService.records().at(1).identity.credentialId;

    const QList<ConnectionCredentialChoice> choices =
        ConnectionModel::credentialChoices(&communityService, &usmService);
    ok &= check(choices.size() == 2 * 2 + 2, "each community offers v2c and v1; each v3 user once");
    ok &= check(choices.first().key == ConnectionModel::communityKey(lab, 1) && choices.first().protocol == 1 &&
                choices.first().label.contains("Lab") && choices.first().label.contains("v2c"),
                "community choice is labelled with name and version");
    bool sawV3 = false;
    for (const auto &c : choices) sawV3 = sawV3 || (c.protocol == 2 && c.label.contains("operator") && c.label.contains("authPriv"));
    ok &= check(sawV3, "v3 choice shows the user and security level");
    ok &= check(ConnectionModel::credentialChoices(nullptr, nullptr).isEmpty(), "no services, no choices");

    PreferencesSettings prefs;
    prefs.requestTimeout = 7; prefs.requestRetries = 4; prefs.bulkMaxRepetitions = 25; prefs.bulkNonRepeaters = 1;

    prefs.contextName = QString::fromUtf8("Lab / réseau");
    AgentRequestSelection selection;
    ok &= check(ConnectionModel::build("10.1.1.20:1161", ConnectionModel::communityKey(lab, 1),
                    &communityService, &usmService, prefs, &selection) == ConnectionError::None,
                "v2c request builds");
    SnmpRequestConfig config;
    ok &= check(selection.requestConfig(&config) && config.version == SnmpRequestVersion::V2c &&
                config.address == "10.1.1.20" && config.port == "1161" && config.readCommunity == "labro" &&
                config.timeout == 7 && config.retries == 4 && config.maxRepetitions == 25 && config.nonRepeaters == 1,
                "v2c request carries target, community and the GLOBAL timeout/retries/bulk settings");
    ok &= check(config.contextName.isEmpty(), "SNMPv3 context does not leak into v1/v2c requests");
    prefs.requestPort = 1161;
    ok &= check(ConnectionModel::build("sw1", ConnectionModel::communityKey(lab, 0),
                    &communityService, &usmService, prefs, &selection) == ConnectionError::None &&
                selection.requestConfig(&config) && config.version == SnmpRequestVersion::V1 && config.port == "1161",
                "v1 request uses the global default port when the Target has none");
    prefs.requestPort = 161;
    ok &= check(ConnectionModel::build("sw1", ConnectionModel::usmKey(operatorId),
                    &communityService, &usmService, prefs, &selection) == ConnectionError::None &&
                selection.requestConfig(&config) && config.version == SnmpRequestVersion::V3 &&
                config.securityName == "operator" && config.securityLevel == 2,
                "v3 request carries the security name and level");
    ok &= check(config.contextName == prefs.contextName, "global context reaches the resolved SNMPv3 request");
    const SnmpRequestConfig capturedContext = config;
    prefs.contextName = "Another context";
    ok &= check(capturedContext.contextName == QString::fromUtf8("Lab / réseau"),
                "a running request keeps its captured context after preferences change");
    ok &= check(ConnectionModel::build("sw1", ConnectionModel::usmKey(incompleteId),
                    &communityService, &usmService, prefs, &selection) == ConnectionError::None &&
                selection.requestConfig(&config) && config.securityLevel == 1,
                "authNoPriv user maps to security level 1");

    ok &= check(ConnectionModel::build("", ConnectionModel::communityKey(lab, 1), &communityService,
                    &usmService, prefs, &selection) == ConnectionError::NoTarget, "empty target is reported");
    ok &= check(ConnectionModel::build("bad target", ConnectionModel::communityKey(lab, 1), &communityService,
                    &usmService, prefs, &selection) == ConnectionError::BadTarget, "malformed target is reported");
    ok &= check(ConnectionModel::build("sw1", "", &communityService, &usmService, prefs, &selection) ==
                ConnectionError::NoCredential, "no credential is reported");
    ok &= check(ConnectionModel::build("sw1", "community:deleted:v2c", &communityService, &usmService, prefs,
                    &selection) == ConnectionError::CredentialMissing, "deleted community credential is reported");
    ok &= check(ConnectionModel::build("sw1", "usm:deleted", &communityService, &usmService, prefs, &selection) ==
                ConnectionError::CredentialMissing, "deleted v3 credential is reported");
    ok &= check(ConnectionModel::build("sw1", "garbage", &communityService, &usmService, prefs, &selection) ==
                ConnectionError::CredentialMissing, "unrecognised credential key is reported");
    ok &= check(ConnectionModel::build("sw1", ConnectionModel::communityKey(empty, 1), &communityService,
                    &usmService, prefs, &selection) == ConnectionError::CredentialIncomplete,
                "community credential with no read community is reported as incomplete");
    {
        UsmCredentialService twins({usm("twin", 2, 2), usm("twin", 2, 2)}, repository);
        ok &= check(ConnectionModel::build("sw1", ConnectionModel::usmKey(twins.records().first().identity.credentialId),
                        &communityService, &twins, prefs, &selection) == ConnectionError::None &&
                    selection.credentials.usmCredential &&
                    selection.credentials.usmCredential->identity.credentialId == twins.records().first().identity.credentialId,
                    "same-name SNMPv3 credentials resolve by their saved ID");
    }
    for (int version : {0, 1}) {
        auto record = community(version ? "Only v2c" : "Only v1", "explicit-ro");
        record.protocol = version;
        const auto id = communityService.create(record);
        int matchingChoices = 0;
        for (const auto &choice : ConnectionModel::credentialChoices(&communityService, &usmService))
            if (choice.key == ConnectionModel::communityKey(id, version)) ++matchingChoices;
        ok &= check(matchingChoices == 1 &&
                    ConnectionModel::build("sw1", ConnectionModel::communityKey(id, version),
                        &communityService, &usmService, prefs, &selection) == ConnectionError::None &&
                    ConnectionModel::build("sw1", ConnectionModel::communityKey(id, 1-version),
                        &communityService, &usmService, prefs, &selection) == ConnectionError::CredentialIncomplete,
                    "new community credentials use only their chosen SNMP version, including stale history references");
    }
    for (ConnectionError e : {ConnectionError::NoTarget, ConnectionError::BadTarget, ConnectionError::NoCredential,
                              ConnectionError::CredentialMissing, ConnectionError::CredentialIncomplete,
                              ConnectionError::CredentialAmbiguous})
        ok &= check(!ConnectionModel::errorText(e).isEmpty(), "every error has a plain-language message");
    return ok ? 0 : 1;
}
