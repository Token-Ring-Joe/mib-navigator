#ifndef COMMUNITYCREDENTIALMANAGER_H
#define COMMUNITYCREDENTIALMANAGER_H

#include "credentialrecords.h"
#include <QDialog>

class CommunityCredentialService;
class QDialogButtonBox;
class QListWidget;
class QLineEdit;

class CommunityCredentialManager : public QDialog
{
    Q_OBJECT
public:
    CommunityCredentialManager(CommunityCredentialService *credentials,
                               QWidget *parent = nullptr);
    // Hosted by the Credentials screen, which owns OK and Cancel: reload() loads the saved
    // credentials and drops unsaved edits; applyChanges() saves the edits.
    void reload();
    void applyChanges();
    void setButtonsVisible(bool visible);
    void execute();
private slots:
    void selectRow(int row);
    void storeFields();
    void addCredential();
    void duplicateCredential();
    void deleteCredential();
private:
    void rebuildList(int selected = 0);
    CommunityCredentialService *service;
    QList<CommunityCredentialRecord> working;
    QDialogButtonBox *buttonBox = nullptr;
    QListWidget *list;
    QLineEdit *name;
    QLineEdit *readCommunity;
    QLineEdit *writeCommunity;
    int current = -1;
};

#endif
