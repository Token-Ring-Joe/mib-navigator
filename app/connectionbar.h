#ifndef CONNECTIONBAR_H
#define CONNECTIONBAR_H

#include <QWidget>

#include "connectionmodel.h"

class QComboBox;
class QLabel;
class CommunityCredentialService;
class UsmCredentialService;

// The three independent session controls at the top of the main window:
// Target (typed, with recent history), Credential (saved, reusable), and the MIB
// Profile selector, which is the existing profile combo moved here so there is
// exactly one selector for it.
class ConnectionBar : public QWidget
{
    Q_OBJECT
public:
    ConnectionBar(CommunityCredentialService *community, UsmCredentialService *usm,
                  QComboBox *mibProfileCombo, QWidget *parent = nullptr);

    QString target() const;
    QString credentialKey() const;
    int credentialProtocol() const;   // 0, 1, 2 as SnmpRequestVersion; -1 when none
    // Remember the current Target (and the Credential used with it) at the top of the history.
    void rememberCurrentTarget();
    void refreshCredentials();

signals:
    void selectionChanged();
    // protocol is the chosen credential's SNMP version (0, 1, 2), or -1 when none is chosen.
    void manageCredentialsRequested(int protocol);

private:
    void applyHistoryCredential();
    void reloadHistory();

    CommunityCredentialService *community;
    UsmCredentialService *usm;
    QComboBox *targetCombo;
    QComboBox *credentialCombo;
    QLabel *hint;
    QList<ConnectionCredentialChoice> choices;
    bool updating = false;
};

#endif
