#ifndef COMMUNITYCREDENTIALMANAGER_H
#define COMMUNITYCREDENTIALMANAGER_H

#include "credentialrecords.h"
#include <QDialog>

class CommunityCredentialService;
class QListWidget;
class QLineEdit;

class CommunityCredentialManager : public QDialog
{
    Q_OBJECT
public:
    CommunityCredentialManager(CommunityCredentialService *credentials,
                               QWidget *parent = nullptr);
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
    QListWidget *list;
    QLineEdit *name;
    QLineEdit *readCommunity;
    QLineEdit *writeCommunity;
    int current = -1;
};

#endif
