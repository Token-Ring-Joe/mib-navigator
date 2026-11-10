#ifndef CREDENTIALSDIALOG_H
#define CREDENTIALSDIALOG_H

#include <QDialog>
#include <functional>
#include "credentialrecords.h"

class CommunityCredentialService;
class UsmCredentialService;
class QListWidget;
class QLineEdit;
class QComboBox;
class QLabel;
class QStackedWidget;
class QPushButton;
class QDialogButtonBox;

class CredentialsDialog : public QDialog
{
    Q_OBJECT
public:
    enum Tab { Communities = 0, Users = 1 }; // preferred initial selection, not separate tabs
    CredentialsDialog(CommunityCredentialService *communities, UsmCredentialService *users,
                      QWidget *parent = nullptr);
    void prepare(Tab preferred = Communities);
    void execute(Tab preferred = Communities);
    bool applyChanges();
    void accept() override;
    QListWidget *credentialList() const { return list; }
    // Adds a "Password protection…" button that runs `handler`. If the credentials were locked
    // and the handler unlocked them, the list is read again.
    void setProtectionHandler(std::function<void()> handler);
private:
    void storeFields();
    void selectRow();
    void rebuildList(const QString &selectedId = QString(), CredentialKind kind = CredentialKind::Community);
    void addCredential(int version);
    void duplicateCredential();
    void deleteCredential();
    void updateSecurityFields();
    QString uniqueName(const QString &base) const;
    CommunityCredentialService *communityService;
    UsmCredentialService *userService;
    QList<CommunityCredentialRecord> communities;
    QList<UsmCredentialRecord> users;
    QString currentId;
    CredentialKind currentKind = CredentialKind::Community;
    QListWidget *list;
    QWidget *editor;
    QLineEdit *displayName, *readCommunity, *writeCommunity, *securityName, *authSecret, *privacySecret;
    QComboBox *authProtocol, *privacyProtocol;
    QLabel *versionLabel, *status;
    QStackedWidget *pages;
    QPushButton *removeButton, *duplicateButton;
    QDialogButtonBox *buttons;
    QPushButton *protectionButton = nullptr;
};
#endif
