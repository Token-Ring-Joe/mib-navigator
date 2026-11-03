#include "credentialsdialog.h"
#include "communitycredentialservice.h"
#include "usmcredentialservice.h"
#include <QApplication>
#include <QAction>
#include <QComboBox>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QFile>
#include <QLabel>
#include <cstdio>

namespace {
int failures = 0;
void check(bool ok, const char *why) { if (!ok) { ++failures; std::fprintf(stderr, "FAIL: %s\n", why); } }
void field(CredentialsDialog &d, const char *name, const QString &value) { d.findChild<QLineEdit *>(name)->setText(value); }
void add(CredentialsDialog &d, int version) { d.findChild<QAction *>(QStringLiteral("CredentialNewV%1").arg(version))->trigger(); }
QByteArray bytes(const QString &path) { QFile f(path); if (!f.open(QIODevice::ReadOnly)) return {}; return f.readAll(); }
}
int main(int argc, char **argv)
{
    QApplication app(argc, argv); QTemporaryDir dir;
    const QString communityFile = dir.filePath("communities.ini"), usmFile = dir.filePath("identities.ini");
    CommunityCredentialService communities(communityFile);
    CommunityCredentialRecord legacy; legacy.displayName = "Legacy shared"; legacy.readCommunity = CredentialSecret("legacy-ro");
    const QString legacyId = communities.create(legacy);
    UsmCredentialService users({}, UsmCredentialRepository(usmFile));
    CredentialsDialog dialog(&communities, &users); dialog.prepare();
    check(dialog.credentialList()->count() == 1, "legacy community appears once in unified list");
    const auto original = bytes(communityFile);
    check(dialog.applyChanges() && bytes(communityFile) == original && !QFile::exists(usmFile + ".v2.json"),
          "no-op save does not rewrite legacy files or trigger migration");
    add(dialog, 2); field(dialog, "CredentialDisplayName", "First product"); field(dialog, "CredentialUsmUser", "same-user");
    auto *auth = dialog.findChild<QComboBox *>("CredentialAuthProtocol");
    check(auth->currentData().toInt() == UsmProtocolNone &&
          dialog.findChild<QComboBox *>("CredentialPrivacyProtocol")->currentData().toInt() == UsmProtocolNone &&
          !dialog.findChild<QLineEdit *>("CredentialAuthSecret")->isEnabled(),
          "new v3 credential defaults to noAuthNoPriv without unsupported protocol indexes");
    auth->setCurrentIndex(2); field(dialog, "CredentialAuthSecret", "first-auth-secret");
    const QString first = dialog.credentialList()->currentItem()->data(Qt::UserRole).toString();
    dialog.findChild<QPushButton *>("CredentialDuplicate")->click();
    field(dialog, "CredentialDisplayName", "Second product");
    check(dialog.findChild<QLineEdit *>("CredentialUsmUser")->text() == "same-user", "Duplicate keeps wire username");
    auth->setCurrentIndex(4); field(dialog, "CredentialAuthSecret", "second-auth-secret");
    dialog.findChild<QComboBox *>("CredentialPrivacyProtocol")->setCurrentIndex(4);
    field(dialog, "CredentialPrivacySecret", "second-privacy-secret");
    const QString second = dialog.credentialList()->currentItem()->data(Qt::UserRole).toString();
    for (int version : {0, 1}) {
        add(dialog, version); field(dialog, "CredentialDisplayName", version ? "Version two" : "Version one");
        field(dialog, "CredentialReadCommunity", "read-secret");
    }
    check(dialog.applyChanges() && users.records().size() == 2 && communities.records().size() == 3,
          "one save commits both kinds and all three versions");
    check(users.find(first) && users.find(second) && users.find(first)->securityName == users.find(second)->securityName &&
          users.find(first)->displayName != users.find(second)->displayName &&
          users.find(first)->authProtocol != users.find(second)->authProtocol &&
          users.find(first)->authSecret.bytes() != users.find(second)->authSecret.bytes(),
          "same-user variants retain distinct names, protocols and secrets");
    CommunityCredentialService communityRestart(communityFile);
    UsmCredentialService userRestart({}, UsmCredentialRepository(usmFile));
    check(communityRestart.records() == communities.records() && userRestart.records() == users.records(),
          "restart preserves all credential IDs and values");
    check(communityRestart.find(legacyId) && communityRestart.find(legacyId)->protocol == -1,
          "legacy v1/v2c shared credential and ID are preserved");
    dialog.prepare(); add(dialog, 2); field(dialog, "CredentialDisplayName", "Discarded"); dialog.reject(); dialog.prepare();
    check(dialog.credentialList()->count() == 5 && users.records().size() == 2, "Cancel discards new rows across versions");
    for (int row = 0; row < dialog.credentialList()->count(); ++row)
        if (dialog.credentialList()->item(row)->data(Qt::UserRole).toString() == first) { dialog.credentialList()->setCurrentRow(row); break; }
    field(dialog, "CredentialDisplayName", "Renamed product");
    check(dialog.applyChanges() && users.find(first)->displayName == "Renamed product" &&
          users.find(first)->securityName == "same-user", "display rename preserves username and stable ID");
    dialog.findChild<QPushButton *>("CredentialDelete")->click();
    check(dialog.applyChanges() && !users.find(first) && users.find(second), "Delete removes the selected ID only");

    // The second atomic writer fails: restore the first file, publish neither service, keep edits open.
    CommunityCredentialService failingCommunity(dir.filePath("missing/communities.ini"));
    UsmCredentialService rollbackUsers({}, UsmCredentialRepository(dir.filePath("rollback.ini")));
    CredentialsDialog failed(&failingCommunity, &rollbackUsers); failed.prepare();
    int notifications = 0;
    QObject::connect(&rollbackUsers, &UsmCredentialService::credentialsChanged, [&]() { ++notifications; });
    add(failed, 2); field(failed, "CredentialDisplayName", "Must roll back"); field(failed, "CredentialUsmUser", "rollback-user");
    add(failed, 0); field(failed, "CredentialDisplayName", "Cannot save"); field(failed, "CredentialReadCommunity", "ro");
    check(!failed.applyChanges() && rollbackUsers.records().isEmpty() && failingCommunity.records().isEmpty() && notifications == 0,
          "save failure publishes neither working copy");
    UsmCredentialService rollbackRestart({}, UsmCredentialRepository(dir.filePath("rollback.ini")));
    check(rollbackRestart.records().isEmpty() && rollbackRestart.storageError().isEmpty(), "failed mixed save restores durable v3 state");
    check(!failed.findChild<QLabel *>("CredentialStatus")->text().isEmpty(), "save failure is explained without closing the editor");
    return failures ? 1 : 0;
}
