#include "credentialprotection.h"
#include "communitycredentialservice.h"
#include "usmcredentialservice.h"

#include <QCoreApplication>
#include "tomcrypt.h"
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <cstdio>

namespace {
int failures = 0;
void check(bool ok, const char *why) { if (!ok) { std::fprintf(stderr, "FAIL: %s\n", why); ++failures; } }
QByteArray slurp(const QString &path) { QFile f(path); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray(); }
bool mentions(const QByteArray &file, const QByteArray &secret)
{ return file.contains(secret) || file.contains(secret.toBase64()) || file.contains(secret.toHex()); }
constexpr int Rounds = 2000;

struct Fixture {
    QString communityFile, usmFile, vaultFile;
    explicit Fixture(const QTemporaryDir &dir)
        : communityFile(dir.filePath("community.conf")), usmFile(dir.filePath("identities.conf")),
          vaultFile(dir.filePath("credentials.vault")) {}
};

// One "application run": protection object, then the two services reading the same files.
struct Session {
    CredentialProtection protection;
    CommunityCredentialService communities;
    UsmCredentialService users;
    explicit Session(const Fixture &f)
        : protection(f.vaultFile, Rounds),
          communities(f.communityFile, protection.activeSecretStore()),
          users({}, makeRepository(f, protection)) {}
    static UsmCredentialRepository makeRepository(const Fixture &f, CredentialProtection &p)
    { UsmCredentialRepository r(f.usmFile); r.setSecretStore(p.activeSecretStore()); return r; }
};

UsmCredentialRecord usmRecord(const QString &id, const QString &name, const QByteArray &auth, const QByteArray &priv)
{
    UsmCredentialRecord r; r.identity = {id, CredentialKind::Usm}; r.displayName = name; r.securityName = "operator";
    r.authProtocol = 2; r.authSecret = CredentialSecret(auth); r.privacyProtocol = 4; r.privacySecret = CredentialSecret(priv);
    return r;
}

class FakeStore : public CredentialSecretStore {
public:
    bool isUnlocked() const override { return open; }
    const QHash<QString, QByteArray> &secrets() const override { return held; }
    bool store(const QHash<QString, QByteArray> &all) override { ++stores; held = all; return true; }
    bool open = true; int stores = 0; QHash<QString, QByteArray> held;
};
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    // SNMP++ registers libtomcrypt's "rijndael" cipher before any credential is touched; the
    // vault must work in that state, which is the state of the real application.
    register_cipher(&rijndael_desc);
    const QByteArray readSecret = "public-read-9731", writeSecret = "private-write-4482",
                     authSecret = "auth-pass-5519", privSecret = "priv-pass-2076", otherAuth = "other-auth-8840";

    // ---- Full user workflow: protect, edit, restart, lock, unlock, change password, unprotect.
    {
        QTemporaryDir dir; const Fixture f(dir);
        QList<CommunityCredentialRecord> originalCommunities; QList<UsmCredentialRecord> originalUsers;
        {
            Session s(f);
            check(!s.protection.isEnabled(), "protection is off by default");
            CommunityCredentialRecord lab; lab.displayName = "Lab"; lab.protocol = 1;
            lab.readCommunity = CredentialSecret(readSecret); lab.writeCommunity = CredentialSecret(writeSecret);
            CommunityCredentialRecord readOnly; readOnly.displayName = "Read only"; readOnly.protocol = 0;
            readOnly.readCommunity = CredentialSecret("ro-4410");   // empty write community
            check(!s.communities.create(lab).isEmpty() && !s.communities.create(readOnly).isEmpty(), "communities are saved");
            const QList<UsmCredentialRecord> usm{usmRecord("u-1", "Operator A", authSecret, privSecret),
                                                 usmRecord("u-2", "Operator B", otherAuth, privSecret)};
            check(s.users.saveWorkingCopy(usm), "SNMPv3 credentials are saved"); s.users.applyCommitted(usm);
            check(mentions(slurp(f.communityFile), readSecret) && mentions(slurp(f.usmFile + ".v2.json"), authSecret),
                  "before protection the secrets are in the files, as always");
            originalCommunities = s.communities.records(); originalUsers = s.users.records();

            check(!s.protection.enable("", &s.communities, &s.users) && !s.protection.isEnabled(), "an empty master password is refused");
            check(s.protection.enable("hunter2", &s.communities, &s.users) && s.protection.isEnabled() && s.protection.isUnlocked(),
                  "protection turns on");
            for (const auto &file : {slurp(f.communityFile), slurp(f.usmFile + ".v2.json"), slurp(f.vaultFile)})
                for (const auto &secret : {readSecret, writeSecret, authSecret, privSecret, otherAuth, QByteArray("ro-4410")})
                    check(!mentions(file, secret), "no secret remains readable in any file after protecting");
            check(slurp(f.communityFile).contains("Lab") && slurp(f.usmFile + ".v2.json").contains("operator"),
                  "names and users stay readable so the credentials can be listed");
            check(s.communities.records() == originalCommunities && s.users.records() == originalUsers,
                  "protecting changes nothing the application sees");
            check(!s.protection.enable("again", &s.communities, &s.users), "protecting twice is refused");

            // Editing while protected keeps secrets out of the files and in the vault.
            auto edited = s.communities.records().first(); edited.readCommunity = CredentialSecret("rotated-6603");
            check(s.communities.update(edited), "an edit saves while protected");
            check(!mentions(slurp(f.communityFile), "rotated-6603"), "an edited secret does not reach the file");
            originalCommunities = s.communities.records();
            auto usm2 = s.users.records(); usm2[0].authSecret = CredentialSecret("rotated-auth-1");
            check(s.users.saveWorkingCopy(usm2), "an SNMPv3 edit saves while protected"); s.users.applyCommitted(usm2);
            check(!mentions(slurp(f.usmFile + ".v2.json"), "rotated-auth-1"), "an edited SNMPv3 secret does not reach the file");
            originalUsers = s.users.records();
            check(s.communities.remove(s.communities.records().last().identity.credentialId), "a delete saves while protected");
            originalCommunities = s.communities.records();
        }
        {   // Restart without the password: names only, no secrets, no blank sends, no saves.
            Session s(f);
            check(s.protection.isEnabled() && !s.protection.isUnlocked(), "after a restart the vault is locked");
            check(s.communities.isLocked() && s.users.isLocked(), "both services report locked");
            check(s.communities.records().isEmpty() && s.users.records().isEmpty(), "locked services expose no credentials");
            check(s.communities.storageError() == CommunityCredentialRepository::lockedMessage(), "locked state is explained");
            CommunityCredentialRecord draft; draft.displayName = "New"; draft.readCommunity = CredentialSecret("x");
            check(s.communities.create(draft).isEmpty(), "saving while locked is refused, never written blank");
            check(!s.users.saveWorkingCopy({usmRecord("u-9", "N", "a", "b")}), "an SNMPv3 save while locked is refused");
            const QByteArray before = slurp(f.communityFile), beforeUsm = slurp(f.usmFile + ".v2.json");
            int changedCount = 0, usersChangedCount = 0;
            QObject::connect(&s.communities, &CommunityCredentialService::credentialsChanged, [&]() { ++changedCount; });
            QObject::connect(&s.users, &UsmCredentialService::credentialsChanged, [&]() { ++usersChangedCount; });
            check(!s.protection.unlock("wrong", &s.communities, &s.users) && s.communities.isLocked(), "a wrong password does not unlock");
            check(slurp(f.communityFile) == before && slurp(f.usmFile + ".v2.json") == beforeUsm, "a failed unlock leaves every file untouched");
            check(s.protection.unlock("hunter2", &s.communities, &s.users) && !s.communities.isLocked() && !s.users.isLocked(),
                  "the right password unlocks both services");
            check(s.communities.records() == originalCommunities && s.users.records() == originalUsers,
                  "every secret is restored exactly after the restart");
            check(changedCount >= 1 && usersChangedCount >= 1, "unlocking announces the credentials so the UI and engine refresh");
            check(s.communities.storageError().isEmpty(), "no stale error after unlocking");

            check(!s.protection.changePassword("nope", "second"), "changing the password needs the current one");
            check(s.protection.changePassword("hunter2", "second pass"), "the password changes");
        }
        {
            Session s(f);
            check(!s.protection.unlockVault("hunter2"), "the old password stops working");
            check(s.protection.unlock("second pass", &s.communities, &s.users) && s.communities.records() == originalCommunities,
                  "the new password unlocks everything");
            const QByteArray protectedFile = slurp(f.communityFile);
            check(!s.protection.disable("", &s.communities, &s.users) && !s.protection.disable("wrong", &s.communities, &s.users) &&
                  s.protection.isEnabled() && slurp(f.communityFile) == protectedFile,
                  "turning protection off needs the master password, even in an unlocked session");
            check(s.protection.disable("second pass", &s.communities, &s.users) && !s.protection.isEnabled() && !QFile::exists(f.vaultFile),
                  "protection turns off and the vault file is removed");
            check(mentions(slurp(f.communityFile), "rotated-6603") && mentions(slurp(f.usmFile + ".v2.json"), "rotated-auth-1"),
                  "turning protection off writes the secrets back to the files");
            check(!slurp(f.communityFile).contains("protected"), "the files no longer claim to be protected");
        }
        {
            Session s(f);
            check(!s.protection.isEnabled() && !s.communities.isLocked() && s.communities.records() == originalCommunities &&
                  s.users.records() == originalUsers, "after turning off, a restart reads everything with no password");
            check(s.protection.enable("again", &s.communities, &s.users) && s.protection.isEnabled(), "protection can be turned on again");
        }
    }

    // ---- Failure handling.
    {   // A community file that cannot be written: enabling must not lose or expose anything.
        QTemporaryDir dir; const Fixture f(dir);
        Session s(f);
        CommunityCredentialRecord r; r.displayName = "Lab"; r.readCommunity = CredentialSecret(readSecret);
        check(!s.communities.create(r).isEmpty(), "fixture credential saved");
        const QByteArray usmBefore = slurp(f.usmFile + ".v2.json");
        QFile::remove(f.communityFile); QDir().mkpath(f.communityFile);   // a folder where the file belongs
        check(!s.protection.enable("pw", &s.communities, &s.users), "enabling fails when the credential file cannot be written");
        check(!s.protection.error().isEmpty(), "the failure is explained");
        check(s.protection.isEnabled() && QFile::exists(f.vaultFile), "when the plain files cannot be restored either, the vault is kept so no secret is lost");
        check(s.communities.records().first().readCommunity.bytes() == readSecret, "the secrets in memory are unharmed");
        check(!mentions(slurp(f.usmFile + ".v2.json"), readSecret) && !mentions(slurp(f.vaultFile), readSecret),
              "no secret was exposed by the failed attempt");
        (void)usmBefore;
    }
    {   // Repository-level atomicity: if the file write fails the secret store is put back.
        QTemporaryDir dir;
        const QString file = dir.filePath("c.conf");
        CommunityCredentialRepository repo(file);
        auto fake = std::make_shared<FakeStore>(); fake->held = {{"usm/keep/auth", "kept"}};
        repo.setSecretStore(fake);
        CommunityCredentialRecord r; r.identity = {"id1", CredentialKind::Community}; r.displayName = "A"; r.readCommunity = CredentialSecret("s1");
        check(repo.save({r}) && fake->held.value("community/id1/read") == "s1" && fake->held.value("usm/keep/auth") == "kept",
              "a protected save writes its secrets and leaves the other repository's alone");
        auto before = fake->held;
        QFile::remove(file); QDir().mkpath(file);
        r.readCommunity = CredentialSecret("s2");
        check(!repo.save({r}) && fake->held == before, "a failed file write restores the secret store");
        fake->open = false;
        check(!repo.save({r}), "a locked store refuses the save");
        QString error;
        QDir().rmdir(file);
        fake->open = true; repo.save({r}); fake->open = false;
        check(repo.load(&error).isEmpty() && error == CommunityCredentialRepository::lockedMessage(), "a protected file read while locked is refused");
        fake->open = true; fake->held.remove("community/id1/write");
        check(repo.load(&error).isEmpty() && !error.isEmpty(), "a protected file whose secrets are missing is an error, not a blank credential");
    }

    if (failures) { std::fprintf(stderr, "%d check(s) failed\n", failures); return 1; }
    std::puts("credential protection checks passed");
    return 0;
}
