#include "credentialsdialog.h"

#include "communitycredentialmanager.h"
#include "usmprofile.h"

#include <QDialogButtonBox>
#include <QTabWidget>
#include <QVBoxLayout>

CredentialsDialog::CredentialsDialog(CommunityCredentialService *communities,
                                     USMProfileManager *users, QWidget *parent)
    : QDialog(parent), userEditor(users)
{
    setObjectName(QStringLiteral("CredentialsDialog"));
    setWindowTitle(tr("Credentials"));
    resize(760, 520);
    auto *layout = new QVBoxLayout(this);
    tabWidget = new QTabWidget(this);
    tabWidget->setObjectName(QStringLiteral("CredentialsTabs"));

    communityEditor = new CommunityCredentialManager(communities, tabWidget);
    communityEditor->setWindowFlags(Qt::Widget);   // embedded, not a window of its own
    communityEditor->setButtonsVisible(false);
    tabWidget->addTab(communityEditor, tr("SNMPv1 / v2c communities"));

    QWidget *usersPage = userEditor->EditorWidget();
    usersPage->setParent(tabWidget);
    usersPage->setWindowFlags(Qt::Widget);
    tabWidget->addTab(usersPage, tr("SNMPv3 users"));
    layout->addWidget(tabWidget, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &CredentialsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

void CredentialsDialog::prepare(Tab tab)
{
    communityEditor->reload();
    userEditor->PrepareEditor();
    tabWidget->setCurrentIndex(tab);
}

void CredentialsDialog::execute(Tab tab)
{
    prepare(tab);
    exec();
}

bool CredentialsDialog::applyChanges()
{
    if (!userEditor->ApplyEditor()) return false;
    communityEditor->applyChanges();
    return true;
}

void CredentialsDialog::accept()
{
    if (applyChanges()) QDialog::accept();
}
