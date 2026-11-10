#ifndef CREDENTIALPROTECTIONDIALOG_H
#define CREDENTIALPROTECTIONDIALOG_H

#include <QDialog>
#include <functional>

class CredentialProtection;
class CommunityCredentialService;
class UsmCredentialService;
class QLabel;
class QPushButton;

// Asks for the master password until it is right or the user cancels. `attempt` performs the
// unlock (default: unlock the vault only). Returns true once unlocked.
bool PromptToUnlockCredentials(CredentialProtection *protection, QWidget *parent,
                               const std::function<bool(const QString &)> &attempt = {});

// Turn master-password protection on or off, change the password, or unlock after starting
// without it. Opened from the Credentials screen.
class CredentialProtectionDialog : public QDialog
{
    Q_OBJECT
public:
    CredentialProtectionDialog(CredentialProtection *protection,
                               CommunityCredentialService *communities, UsmCredentialService *users,
                               const QString &legacyUsmFile, QWidget *parent = nullptr);
private:
    void refresh();
    void turnOn();
    void turnOff();
    void changePassword();
    void unlock();
    bool askNewPassword(QString *password);
    CredentialProtection *protection;
    CommunityCredentialService *communities;
    UsmCredentialService *users;
    QString legacyUsmFile;
    QLabel *summary, *detail, *result;
    QPushButton *turnOnButton, *turnOffButton, *changeButton, *unlockButton;
};

#endif
