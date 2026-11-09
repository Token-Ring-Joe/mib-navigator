#ifndef CREDENTIALSDIALOG_H
#define CREDENTIALSDIALOG_H

#include <QDialog>

class CommunityCredentialManager;
class CommunityCredentialService;
class QTabWidget;
class USMProfileManager;

// One screen for every saved credential: SNMPv1/v2c communities and SNMPv3 users on two tabs
// with a single OK and Cancel. OK saves both tabs; Cancel discards the edits on both.
class CredentialsDialog : public QDialog
{
    Q_OBJECT
public:
    enum Tab { Communities = 0, Users = 1 };

    CredentialsDialog(CommunityCredentialService *communities, USMProfileManager *users,
                      QWidget *parent = nullptr);
    // Loads the saved credentials (dropping unsaved edits) and shows the requested tab.
    void prepare(Tab tab);
    // prepare(), then runs the dialog.
    void execute(Tab tab);
    // Saves both tabs. Returns false (and keeps the screen open) when the SNMPv3 users are
    // invalid or could not be written. The communities are saved only after the users succeed.
    bool applyChanges();
    void accept() override;
    QTabWidget *tabs() const { return tabWidget; }

private:
    CommunityCredentialManager *communityEditor;
    USMProfileManager *userEditor;
    QTabWidget *tabWidget;
};

#endif
