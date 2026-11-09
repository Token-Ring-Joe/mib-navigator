#include "communitycredentialservice.h"
#include "snmprequestconfig.h"
#include "snmprequestcontext.h"
#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <iostream>

namespace {
bool check(bool value, const char *message)
{ if (!value) std::cerr << "FAIL: " << message << '\n'; return value; }
SnmpRequestSettings profile(const QString &id)
{
    SnmpRequestSettings p{}; p.profileId = id; p.name = id; p.v2 = true;
    p.address = "192.0.2.1"; p.port = "161"; p.retries = 2; p.timeout = 4;
    p.readcomm = "legacy-read"; p.writecomm = "legacy-write";
    return p;
}
CommunityCredentialRecord credential(const QString &name)
{
    CommunityCredentialRecord r; r.displayName = name;
    r.readCommunity = CredentialSecret("reusable-read");
    r.writeCommunity = CredentialSecret("reusable-write"); return r;
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv); QTemporaryDir dir;
    bool ok = check(dir.isValid(), "temporary directory");
    const QString credentialsFile = dir.filePath("community-credentials.conf");
    CommunityCredentialService service(credentialsFile);
    ok &= check(service.records().isEmpty() && !QFile::exists(credentialsFile),
                "missing store/read-only load");
    const QString first = service.create(credential("shared"));
    const QString second = service.create(credential("shared"));
    ok &= check(!first.isEmpty() && !second.isEmpty() && first != second &&
                service.records().size() == 2, "stable IDs/duplicate display names");
    CommunityCredentialRecord edited = *service.find(first);
    edited.displayName = "renamed";
    edited.readCommunity = CredentialSecret("updated-read");
    ok &= check(service.update(edited) && service.find(first)->displayName == "renamed",
                "update/rename preserves ID");
    const QString copy = service.duplicate(first);
    ok &= check(!copy.isEmpty() && copy != first, "duplicate gets new ID");

    CommunityCredentialService reloaded(credentialsFile);
    ok &= check(reloaded.records().size() == 3 &&
                reloaded.find(first) && reloaded.find(first)->displayName == "renamed",
                "credential repository round-trip");

    // A request captures the credential's values when it is built; later edits and deletes of the
    // saved credential never change a request that is already in flight.
    const SnmpRequestSettings request = profile("request");
    const auto values = CredentialResolver::communityValues(*service.find(first));
    ok &= check(values.readCommunity == "updated-read" && values.writeCommunity == "reusable-write",
                "saved credential supplies the read and write communities");
    SnmpRequestConfig config;
    SnmpRequestConfig::FromProfile(request, 1, values, &config);
    SnmpRequestContext captured(config, SnmpRequestOperation::Get);
    edited = *service.find(first); edited.readCommunity = CredentialSecret("next-read");
    ok &= check(service.update(edited) && captured.config().readCommunity == "updated-read" &&
                CredentialResolver::communityValues(*service.find(first)).readCommunity == "next-read",
                "credential updates affect the next request, not a captured context");
    ok &= check(service.remove(first) && !service.find(first) &&
                captured.config().readCommunity == "updated-read",
                "deleting a credential does not change a captured request");
    CommunityCredentialService afterDelete(credentialsFile);
    ok &= check(!afterDelete.find(first) && afterDelete.records().size() == 2,
                "delete is persisted");
    ok &= check(service.remove("no-such-id") == false, "removing an unknown credential fails cleanly");
    return ok ? 0 : 1;
}
