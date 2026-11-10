#include "credentialprotectiondialog.h"
#include "credentialprotection.h"
#include "communitycredentialservice.h"
#include "usmcredentialservice.h"
#include "dialoglayout.h"
#include <QDialogButtonBox>
#include <QFile>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

bool PromptToUnlockCredentials(CredentialProtection *protection, QWidget *parent,
                               const std::function<bool(const QString &)> &attempt)
{
    QString hint;
    for (;;) {
        bool accepted = false;
        const QString password = QInputDialog::getText(parent, QObject::tr("Unlock credentials"),
            hint + QObject::tr("Enter the master password to unlock the saved credentials."),
            QLineEdit::Password, QString(), &accepted);
        if (!accepted) return false;
        if (attempt ? attempt(password) : protection->unlockVault(password)) return true;
        hint = protection->error() + QStringLiteral("\n\n");
    }
}

CredentialProtectionDialog::CredentialProtectionDialog(CredentialProtection *p,
    CommunityCredentialService *c, UsmCredentialService *u, const QString &legacyFile, QWidget *parent)
    : QDialog(parent), protection(p), communities(c), users(u), legacyUsmFile(legacyFile)
{
    setObjectName("CredentialProtectionDialog"); setWindowTitle(tr("Password protection"));
    auto *layout = new QVBoxLayout(this);
    summary = new QLabel(this); summary->setObjectName("CredentialProtectionSummary");
    summary->setWordWrap(true); summary->setProperty("heading", true);
    detail = new QLabel(this); detail->setObjectName("CredentialProtectionDetail"); detail->setWordWrap(true);
    result = new QLabel(this); result->setObjectName("CredentialProtectionResult"); result->setWordWrap(true);
    layout->addWidget(summary); layout->addWidget(detail); layout->addWidget(result); layout->addStretch();
    auto *actions = new QHBoxLayout;
    turnOnButton = new QPushButton(tr("Turn on…"), this); turnOnButton->setObjectName("CredentialProtectionOn");
    turnOffButton = new QPushButton(tr("Turn off…"), this); turnOffButton->setObjectName("CredentialProtectionOff");
    changeButton = new QPushButton(tr("Change password…"), this); changeButton->setObjectName("CredentialProtectionChange");
    unlockButton = new QPushButton(tr("Unlock…"), this); unlockButton->setObjectName("CredentialProtectionUnlock");
    for (auto *b : {turnOnButton, unlockButton, changeButton, turnOffButton}) { b->setAutoDefault(false); actions->addWidget(b); }
    actions->addStretch(); layout->addLayout(actions);
    auto *close = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(close, &QDialogButtonBox::rejected, this, &QDialog::accept);
    layout->addWidget(close);
    connect(turnOnButton, &QPushButton::clicked, this, &CredentialProtectionDialog::turnOn);
    connect(turnOffButton, &QPushButton::clicked, this, &CredentialProtectionDialog::turnOff);
    connect(changeButton, &QPushButton::clicked, this, &CredentialProtectionDialog::changePassword);
    connect(unlockButton, &QPushButton::clicked, this, &CredentialProtectionDialog::unlock);
    DialogLayout::initialize(*this, QSize(520, 280));
    refresh();
}

void CredentialProtectionDialog::refresh()
{
    const bool on = protection->isEnabled(), open = protection->isUnlocked();
    const bool unreadable = !on && (communities->isLocked() || users->isLocked());
    turnOnButton->setVisible(!on && !unreadable);
    unlockButton->setVisible(on && !open);
    changeButton->setVisible(on && open);
    turnOffButton->setVisible(on && open);
    if (unreadable) {
        summary->setText(tr("The protected credentials cannot be opened."));
        detail->setText(tr("The credential files say their secrets are password protected, but the vault file is missing:\n%1\n\n"
                           "Restore the vault file from a backup. Nothing was deleted; without it the secrets must be entered again.")
                            .arg(protection->vaultPath()));
    } else if (!on) {
        summary->setText(tr("Saved credentials are not password protected."));
        detail->setText(tr("Communities and SNMPv3 passwords are kept in this computer's settings folder as readable text, "
                           "so anyone who can read your files can read them. A master password encrypts them. "
                           "If you forget it, the saved secrets cannot be recovered and must be entered again."));
    } else if (!open) {
        summary->setText(tr("Saved credentials are locked."));
        detail->setText(tr("Enter the master password to use them. Until then the credential names are listed, "
                           "but no secret is loaded or sent."));
    } else {
        summary->setText(tr("Saved credentials are password protected."));
        detail->setText(tr("Communities and SNMPv3 passwords are stored encrypted. The master password is asked "
                           "for when MIB Navigator starts. It is never saved."));
    }
}

bool CredentialProtectionDialog::askNewPassword(QString *password)
{
    bool ok = false;
    const QString first = QInputDialog::getText(this, tr("Master password"),
        tr("Choose a master password."), QLineEdit::Password, QString(), &ok);
    if (!ok) return false;
    if (first.isEmpty()) { result->setText(tr("The master password cannot be empty.")); return false; }
    const QString second = QInputDialog::getText(this, tr("Master password"),
        tr("Enter the master password again."), QLineEdit::Password, QString(), &ok);
    if (!ok) return false;
    if (first != second) { result->setText(tr("The two passwords did not match. Nothing was changed.")); return false; }
    *password = first;
    return true;
}

void CredentialProtectionDialog::turnOn()
{
    result->clear();
    QString password;
    if (!askNewPassword(&password)) return;
    if (!protection->enable(password, communities, users)) {
        result->setText(protection->error()); refresh();
        QMessageBox::warning(this, tr("Password protection"), protection->error());
        return;
    }
    QString message = tr("Password protection is on. Saved secrets are now encrypted.");
    if (!legacyUsmFile.isEmpty() && QFile::exists(legacyUsmFile))
        message += tr("\n\nAn older MIB Navigator or SnmpB file, %1, may still contain SNMPv3 passwords as readable text. "
                      "It is no longer used and was not changed or deleted; remove it yourself if you do not need it.")
                       .arg(legacyUsmFile);
    result->setText(message);
    refresh();
    QMessageBox::information(this, tr("Password protection"), message);
}

void CredentialProtectionDialog::turnOff()
{
    result->clear();
    bool accepted = false;
    const QString password = QInputDialog::getText(this, tr("Turn off password protection"),
        tr("Saved communities and SNMPv3 passwords will be stored as readable text again.\n\nEnter the master password to confirm."),
        QLineEdit::Password, QString(), &accepted);
    if (!accepted) return;
    if (!protection->disable(password, communities, users)) {
        result->setText(protection->error()); refresh();
        QMessageBox::warning(this, tr("Password protection"), protection->error());
        return;
    }
    result->setText(tr("Password protection is off."));
    refresh();
    QMessageBox::information(this, tr("Password protection"), tr("Password protection is off."));
}

void CredentialProtectionDialog::changePassword()
{
    result->clear();
    bool ok = false;
    const QString current = QInputDialog::getText(this, tr("Change master password"),
        tr("Enter the current master password."), QLineEdit::Password, QString(), &ok);
    if (!ok) return;
    QString replacement;
    if (!askNewPassword(&replacement)) return;
    if (!protection->changePassword(current, replacement)) {
        result->setText(protection->error());
        QMessageBox::warning(this, tr("Password protection"), protection->error());
        return;
    }
    result->setText(tr("The master password was changed."));
    QMessageBox::information(this, tr("Password protection"), tr("The master password was changed."));
}

void CredentialProtectionDialog::unlock()
{
    result->clear();
    if (PromptToUnlockCredentials(protection, this, [this](const QString &password) {
            return protection->unlock(password, communities, users); }))
        result->setText(tr("Credentials unlocked."));
    refresh();
}
