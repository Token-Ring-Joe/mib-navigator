#include "credentialrecords.h"
#include "snmprequestconfig.h"
#include "usmcredentialservice.h"
#include "usmcredentialcoordinator.h"

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <iostream>

namespace {
bool check(bool value, const char *message)
{
    if (!value) std::cerr << "FAIL: " << message << '\n';
    return value;
}

UsmCredentialRecord usm(const QString &name, int auth = 1, int privacy = 1)
{
    UsmCredentialRecord record;
    record.securityName = name;
    record.authProtocol = auth;
    record.privacyProtocol = privacy;
    record.authSecret = CredentialSecret("auth-value");
    record.privacySecret = CredentialSecret("privacy-value");
    return record;
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir dir;
    bool ok = check(dir.isValid(), "temporary directory");
    UsmCredentialRecord noAuth = usm("no-auth", 1, 1);
    noAuth.authSecret = {}; noAuth.privacySecret = {};
    UsmCredentialRecord authNoPriv = usm("auth-only", 4, 1);
    authNoPriv.authSecret = {}; authNoPriv.privacySecret = {};
    UsmCredentialRecord authPriv = usm("auth-priv", 4, 3);
    authPriv.authSecret = {}; authPriv.privacySecret = {};
    ok &= check(UsmCredentialService::requirementsSatisfied(noAuth),
                "noAuth/noPriv rejected a valid security name");
    ok &= check(!UsmCredentialService::requirementsSatisfied(authNoPriv),
                "AuthNoPriv accepted a missing authentication secret");
    ok &= check(!UsmCredentialService::requirementsSatisfied(authPriv),
                "AuthPriv accepted missing authentication/privacy secrets");
    authNoPriv.authSecret = CredentialSecret("auth-secret");
    authPriv.authSecret = CredentialSecret("auth-secret");
    authPriv.privacySecret = CredentialSecret("privacy-secret");
    ok &= check(UsmCredentialService::requirementsSatisfied(authNoPriv),
                "AuthNoPriv rejected complete requirements");
    ok &= check(UsmCredentialService::requirementsSatisfied(authPriv),
                "AuthPriv rejected complete requirements");
    ok &= check(UsmCredentialService::securityLevel(noAuth) == 0 &&
                UsmCredentialService::securityLevel(authNoPriv) == 1 &&
                UsmCredentialService::securityLevel(authPriv) == 2,
                "SNMP++ protocol IDs map to the three security levels");
    auto invalidProtocol = noAuth; invalidProtocol.authProtocol = 0;
    ok &= check(!UsmCredentialService::requirementsSatisfied(invalidProtocol),
                "zero is not a SNMP++ protocol ID");
    auto privacyWithoutAuth = authPriv; privacyWithoutAuth.authProtocol = UsmProtocolNone;
    ok &= check(!UsmCredentialService::requirementsSatisfied(privacyWithoutAuth),
                "privacy requires an authentication protocol even with stored secrets");
    const QString identityFile = dir.filePath("credential-identities.conf");
    UsmCredentialRepository repository(identityFile);

    UsmCredentialService first({usm("operator")}, repository);
    ok &= check(!QFile::exists(identityFile), "read-only construction persisted data");
    const QString stableId = first.records().first().identity.credentialId;
    ok &= check(!stableId.isEmpty() && first.saveIdentities(), "identity save");
    QFile identityContents(identityFile);
    const bool identityOpened = identityContents.open(QIODevice::ReadOnly);
    const QByteArray identityBytes = identityOpened ? identityContents.readAll()
                                                    : QByteArray();
    ok &= check(identityOpened && !identityBytes.contains("auth-value") &&
                !identityBytes.contains("privacy-value"),
                "identity sidecar contains no secrets");
    identityContents.close();

    UsmCredentialService reloaded({usm("operator")}, repository);
    ok &= check(reloaded.records().first().identity.credentialId == stableId,
                "stable identity round-trip");

    const QByteArray authBefore = reloaded.records().first().authSecret.bytes();
    ok &= check(reloaded.rename(stableId, "renamed") &&
                reloaded.records().first().identity.credentialId == stableId &&
                reloaded.records().first().authSecret.bytes() == authBefore,
                "rename preserves identity and secret value");
    ok &= check(!reloaded.isSecurityNameUnambiguous("operator") &&
                reloaded.isSecurityNameUnambiguous("renamed"),
                "rename changes the name the service answers to");
    ok &= check(reloaded.saveIdentities(), "renamed identity save");
    UsmCredentialService renamedReload({usm("renamed")}, repository);
    ok &= check(renamedReload.records().first().identity.credentialId == stableId,
                "rename remains stable across save/load");

    QList<UsmCredentialRecord> working = renamedReload.records();
    working.first().authProtocol = 2;
    working.first().authSecret = CredentialSecret("changed-working-secret");
    const UsmCredentialRecord added = renamedReload.createWorkingRecord("new-user");
    working.append(added);
    ok &= check(renamedReload.records().size() == 1 &&
                renamedReload.records().first().authSecret.bytes() !=
                    working.first().authSecret.bytes() &&
                !added.identity.credentialId.isEmpty() &&
                added.identity.credentialId != stableId,
                "cancelled existing/new working records do not mutate service");

    QList<UsmCredentialRecord> runtimeState = renamedReload.records();
    QList<UsmCredentialRecord> identityState = renamedReload.records();
    auto runtimeWriter = [&runtimeState](const QList<UsmCredentialRecord> &records) {
        runtimeState = records; return true;
    };
    auto identityWriter = [&identityState](const QList<UsmCredentialRecord> &records) {
        identityState = records; return true;
    };
    ok &= check(UsmCredentialCoordinator::apply(
                    renamedReload.records(), working, runtimeWriter,
                    identityWriter).status == UsmCommitStatus::Success &&
                runtimeState.size() == 2 && identityState.size() == 2,
                "coordinated save");
    runtimeState = renamedReload.records(); identityState = renamedReload.records();
    bool failRuntimeOnce = true;
    auto failingRuntime = [&runtimeState, &failRuntimeOnce](
                              const QList<UsmCredentialRecord> &records) {
        if (failRuntimeOnce) { failRuntimeOnce = false; return false; }
        runtimeState = records; return true;
    };
    ok &= check(UsmCredentialCoordinator::apply(
                    renamedReload.records(), working, failingRuntime,
                    identityWriter).status ==
                    UsmCommitStatus::RuntimePersistenceFailed &&
                runtimeState.size() == 1,
                "runtime failure rollback");
    runtimeState = renamedReload.records(); identityState = renamedReload.records();
    bool failIdentityOnce = true;
    auto failingIdentity = [&identityState, &failIdentityOnce](
                               const QList<UsmCredentialRecord> &records) {
        if (failIdentityOnce) { failIdentityOnce = false; return false; }
        identityState = records; return true;
    };
    ok &= check(UsmCredentialCoordinator::apply(
                    renamedReload.records(), working, runtimeWriter,
                    failingIdentity).status ==
                    UsmCommitStatus::IdentityPersistenceFailed &&
                runtimeState.size() == 1 && identityState.size() == 1,
                "identity failure rolls back both boundaries");
    QList<UsmCredentialRecord> failedRename = renamedReload.records();
    failedRename.first().securityName = "must-not-commit";
    runtimeState = renamedReload.records(); identityState = renamedReload.records();
    failIdentityOnce = true;
    ok &= check(UsmCredentialCoordinator::apply(
                    renamedReload.records(), failedRename, runtimeWriter,
                    failingIdentity).status ==
                    UsmCommitStatus::IdentityPersistenceFailed &&
                runtimeState.first().securityName == "renamed" &&
                identityState.first().securityName == "renamed" &&
                renamedReload.records().first().securityName == "renamed",
                "rename failure rollback");

    int createdSignals = 0, updatedSignals = 0, renamedSignals = 0,
        deletedSignals = 0;
    QObject::connect(&renamedReload, &UsmCredentialService::credentialCreated,
                     [&createdSignals](const QString &) { ++createdSignals; });
    QObject::connect(&renamedReload, &UsmCredentialService::credentialUpdated,
                     [&updatedSignals](const QString &) { ++updatedSignals; });
    QObject::connect(&renamedReload, &UsmCredentialService::credentialRenamed,
                     [&renamedSignals](const QString &, const QString &,
                                       const QString &) { ++renamedSignals; });
    QObject::connect(&renamedReload, &UsmCredentialService::credentialDeleted,
                     [&deletedSignals](const QString &) { ++deletedSignals; });
    working.first().securityName = "renamed-again";
    renamedReload.applyCommitted(working);
    ok &= check(createdSignals == 1 && updatedSignals == 1 &&
                renamedSignals == 1 &&
                renamedReload.records().first().identity.credentialId == stableId,
                "identity-based create/update/rename notifications");

    QList<UsmCredentialRecord> afterDelete = working;
    afterDelete.removeFirst();
    renamedReload.applyCommitted(afterDelete);
    ok &= check(deletedSignals == 1 && renamedReload.records().size() == 1,
                "delete notification and removal");

    UsmCredentialService ambiguous({usm("duplicate"), usm("duplicate")}, repository);
    ok &= check(ambiguous.records()[0].identity.credentialId !=
                    ambiguous.records()[1].identity.credentialId,
                "ambiguous names are not merged");
    ok &= check(!ambiguous.isSecurityNameUnambiguous("duplicate"),
                "two users with one name are reported as ambiguous");
    UsmCredentialService incompatible({usm("noauth", 1, 1)}, repository);
    ok &= check(UsmCredentialService::securityLevel(incompatible.records().first()) == 0,
                "a user with no authentication is noAuthNoPriv");
    ok &= check(reloaded.remove(stableId) && !reloaded.find(stableId),
                "delete removes the credential");

    CommunityCredentialRecord community;
    community.identity = {"community-id", CredentialKind::Community};
    community.displayName = "read/write";
    community.readCommunity = CredentialSecret("inline-read");
    community.writeCommunity = CredentialSecret("inline-write");
    SnmpRequestSettings legacy{};
    legacy.address = "192.0.2.1"; legacy.port = "161";
    legacy.retries = 2; legacy.timeout = 5; legacy.maxrepetitions = 20;
    legacy.nonrepeaters = 1; legacy.readcomm = "inline-read";
    legacy.writecomm = "inline-write"; legacy.secname = "operator";
    legacy.seclevel = 1; legacy.contextname = "ctx";
    legacy.contextengineid = "engine";
    SnmpRequestConfig inlineConfig, reusableConfig;
    ok &= check(SnmpRequestConfig::FromProfile(legacy, 1, &inlineConfig) &&
                SnmpRequestConfig::FromProfile(
                    legacy, 1, CredentialResolver::communityValues(community),
                    &reusableConfig) &&
                inlineConfig.readCommunity == reusableConfig.readCommunity &&
                inlineConfig.writeCommunity == reusableConfig.writeCommunity &&
                inlineConfig.address == reusableConfig.address &&
                inlineConfig.retries == reusableConfig.retries &&
                inlineConfig.timeout == reusableConfig.timeout &&
                inlineConfig.maxRepetitions == reusableConfig.maxRepetitions,
                "community abstraction preserves effective request configuration");
    const EffectiveCredentialValues legacyValues = CredentialResolver::inlineValues(
        legacy.readcomm, legacy.writecomm, legacy.secname, legacy.seclevel);
    for (int protocol = 0; protocol < 3; ++protocol)
    {
        SnmpRequestConfig original, resolved;
        ok &= check(SnmpRequestConfig::FromProfile(legacy, protocol, &original) &&
                    SnmpRequestConfig::FromProfile(legacy, protocol, legacyValues,
                                                   &resolved) &&
                    original.version == resolved.version &&
                    original.readCommunity == resolved.readCommunity &&
                    original.writeCommunity == resolved.writeCommunity &&
                    original.securityName == resolved.securityName &&
                    original.securityLevel == resolved.securityLevel &&
                    original.contextName == resolved.contextName &&
                    original.contextEngineId == resolved.contextEngineId &&
                    original.retries == resolved.retries &&
                    original.timeout == resolved.timeout &&
                    original.maxRepetitions == resolved.maxRepetitions &&
                    original.nonRepeaters == resolved.nonRepeaters,
                    "v1/v2c/v3 legacy credential resolution equivalence");
    }

    // New storage is independent of the wire username. Legacy mappings remain untouched.
    {
        UsmCredentialRepository variantRepository(dir.filePath("variants.ini"));
        UsmCredentialService variants({usm("shared-user")}, variantRepository);
        auto records = variants.records();
        records[0].displayName = "Switch Engine";
        auto alternate = variants.createWorkingRecord("shared-user");
        alternate.displayName = "Fabric Engine"; alternate.authProtocol = 2;
        alternate.authSecret = CredentialSecret("different-auth");
        records.append(alternate);
        ok &= check(variants.validateWorkingCopy(records), "same username with different named variants is valid");
        ok &= check(variants.saveWorkingCopy(records), "variant store saves atomically");
        UsmCredentialService restart({usm("irrelevant-legacy-user")}, variantRepository);
        ok &= check(restart.records() == records && restart.storageError().isEmpty(),
                    "restart preserves IDs, names, protocols and secrets for same-user variants");
        ok &= check(!QFile::exists(variantRepository.path()), "new store does not rewrite legacy sidecar");
        QFile file(variantRepository.recordPath());
        ok &= check(file.open(QIODevice::WriteOnly), "corruption fixture opens");
        file.write("{broken"); file.close();
        UsmCredentialService corrupt({usm("must-not-fallback")}, variantRepository);
        ok &= check(!corrupt.storageError().isEmpty() && corrupt.records().isEmpty() &&
                    !corrupt.saveWorkingCopy(records), "corrupt store is reported and cannot be overwritten");
        UsmCredentialRepository missingParent(dir.filePath("missing/path/identities.ini"));
        ok &= check(!missingParent.saveRecords(records), "persistence failure is surfaced");
    }

    return ok ? 0 : 1;
}
