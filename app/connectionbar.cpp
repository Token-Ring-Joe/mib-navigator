#include "connectionbar.h"

#include "communitycredentialservice.h"
#include "usmcredentialservice.h"

#include <QAbstractItemView>
#include <QAction>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QToolButton>
#include <QSettings>

ConnectionBar::ConnectionBar(CommunityCredentialService *communityService,
                             UsmCredentialService *usmService, QComboBox *mibProfileCombo,
                             QWidget *parent)
    : QWidget(parent), community(communityService), usm(usmService)
{
    setObjectName(QStringLiteral("ConnectionBar"));
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(10, 6, 10, 6);
    layout->setSpacing(6);

    layout->addWidget(new QLabel(tr("Target"), this));
    targetCombo = new QComboBox(this);
    targetCombo->setObjectName(QStringLiteral("ConnectionTarget"));
    targetCombo->setEditable(true);
    targetCombo->setInsertPolicy(QComboBox::NoInsert);
    targetCombo->setMinimumWidth(190);
    targetCombo->lineEdit()->setPlaceholderText(tr("IP address or host name"));
    targetCombo->lineEdit()->setClearButtonEnabled(true);
    targetCombo->setToolTip(tr("Who to talk to: an IP address or host name, optionally :port.\n"
                               "Recent targets are listed. Delete removes the highlighted entry."));
    layout->addWidget(targetCombo, 2);

    layout->addWidget(new QLabel(tr("Credential"), this));
    credentialCombo = new QComboBox(this);
    credentialCombo->setObjectName(QStringLiteral("ConnectionCredential"));
    credentialCombo->setMinimumWidth(190);
    credentialCombo->setToolTip(tr("How to authenticate. Use Manage… to add or edit saved credentials."));
    layout->addWidget(credentialCombo, 2);
    auto *manage = new QToolButton(this);
    manage->setObjectName(QStringLiteral("ConnectionManageCredentials"));
    manage->setText(tr("Manage…"));
    manage->setToolTip(tr("Add, edit or delete saved credentials (SNMPv1/v2c communities and SNMPv3 users)"));
    // Opens the Credentials screen on the tab that matches the credential chosen now.
    connect(manage, &QToolButton::clicked, this, [this]() {
        emit manageCredentialsRequested(credentialProtocol());
    });
    layout->addWidget(manage);

    layout->addWidget(new QLabel(tr("MIB Profile"), this));
    mibProfileCombo->setParent(this);
    mibProfileCombo->setMinimumWidth(190);
    mibProfileCombo->setToolTip(tr("How to interpret what comes back."));
    layout->addWidget(mibProfileCombo, 2);
    detectButton = new QToolButton(this);
    detectButton->setObjectName(QStringLiteral("ConnectionDetect"));
    detectButton->setText(tr("Detect"));
    detectButton->setToolTip(tr("Ask the device what it is (sysObjectID and sysDescr, one request) and pick the MIB Profile "
                                "that holds its MIBs. Nothing is sent unless you click this."));
    connect(detectButton, &QToolButton::clicked, this, &ConnectionBar::detectRequested);
    layout->addWidget(detectButton);

    hint = new QLabel(this);
    hint->setObjectName(QStringLiteral("ConnectionHint"));
    hint->setStyleSheet(QStringLiteral("color: #8a4b00;"));
    hint->hide();
    layout->addWidget(hint);

    // Delete removes the highlighted history entry from the drop-down list.
    auto *removeEntry = new QAction(this);
    removeEntry->setShortcut(QKeySequence::Delete);
    removeEntry->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(removeEntry, &QAction::triggered, this, [this]() {
        if (!targetCombo->view()->isVisible() || targetCombo->view()->currentIndex().row() < 0) return;
        QSettings settings;
        const QString entry = targetCombo->view()->currentIndex().data().toString();
        ConnectionHistory::save(settings, ConnectionHistory::removed(
            ConnectionHistory::load(settings), entry));
        const QString typed = targetCombo->currentText();
        reloadHistory();
        targetCombo->setEditText(typed);
    });
    targetCombo->view()->addAction(removeEntry);
    targetCombo->view()->setContextMenuPolicy(Qt::ActionsContextMenu);
    removeEntry->setText(tr("Remove from recent targets"));

    refreshCredentials();
    reloadHistory();
    applyHistoryCredential();

    connect(targetCombo, &QComboBox::activated, this, [this]() {
        applyHistoryCredential();
        emit selectionChanged();
    });
    connect(targetCombo->lineEdit(), &QLineEdit::editingFinished, this, [this]() {
        if (!ConnectionTarget::parse(target()).valid && !target().trimmed().isEmpty())
            return;
        applyHistoryCredential();
    });
    connect(targetCombo->lineEdit(), &QLineEdit::textChanged, this, [this]() { emit selectionChanged(); });
    connect(credentialCombo, &QComboBox::currentIndexChanged, this, [this]() {
        if (!updating) emit selectionChanged();
    });
    if (community) {
        connect(community, &CommunityCredentialService::credentialsChanged, this,
                &ConnectionBar::refreshCredentials);
    }
    if (usm) {
        connect(usm, &UsmCredentialService::credentialsChanged, this,
                &ConnectionBar::refreshCredentials);
    }
}

QString ConnectionBar::target() const { return targetCombo->currentText().trimmed(); }

QString ConnectionBar::credentialKey() const
{
    return credentialCombo->currentData().toString();
}

QString ConnectionBar::credentialLabel() const { return credentialCombo->currentText(); }

void ConnectionBar::setDetectBusy(bool busy)
{
    detectButton->setEnabled(!busy);
    detectButton->setText(busy ? tr("Detecting…") : tr("Detect"));
}

int ConnectionBar::credentialProtocol() const
{
    const QString key = credentialKey();
    for (const ConnectionCredentialChoice &choice : choices)
        if (choice.key == key) return choice.protocol;
    return -1;
}

void ConnectionBar::reloadHistory()
{
    QSettings settings;
    const QSignalBlocker blocker(targetCombo);
    const QString typed = targetCombo->currentText();
    targetCombo->clear();
    for (const ConnectionHistoryEntry &entry : ConnectionHistory::load(settings))
        targetCombo->addItem(entry.target);
    targetCombo->setCurrentIndex(-1);
    targetCombo->setEditText(typed);
}

void ConnectionBar::applyHistoryCredential()
{
    QSettings settings;
    const QString typed = target();
    for (const ConnectionHistoryEntry &entry : ConnectionHistory::load(settings)) {
        if (entry.target.compare(typed, Qt::CaseInsensitive) != 0) continue;
        const int index = credentialCombo->findData(entry.credentialKey);
        if (index >= 0 && index != credentialCombo->currentIndex()) credentialCombo->setCurrentIndex(index);
        return;
    }
}

void ConnectionBar::rememberCurrentTarget()
{
    if (!ConnectionTarget::parse(target()).valid) return;
    QSettings settings;
    ConnectionHistory::save(settings, ConnectionHistory::remembered(
        ConnectionHistory::load(settings), target(), credentialKey()));
    reloadHistory();
}

void ConnectionBar::refreshCredentials()
{
    const QString previous = credentialKey();
    updating = true;
    choices = ConnectionModel::credentialChoices(community, usm);
    {
        const QSignalBlocker blocker(credentialCombo);
        credentialCombo->clear();
        for (const ConnectionCredentialChoice &choice : std::as_const(choices))
            credentialCombo->addItem(choice.label, choice.key);
        int index = credentialCombo->findData(previous);
        // A credential the user had chosen that no longer exists must not be replaced by a
        // different one silently (that could send a request with the wrong identity).
        // Only a bar with nothing chosen yet picks the first entry.
        if (index < 0 && previous.isEmpty() && credentialCombo->count() > 0) index = 0;
        credentialCombo->setCurrentIndex(index);
    }
    updating = false;
    credentialCombo->setPlaceholderText(choices.isEmpty() ? tr("No saved credentials")
                                                          : tr("Choose a credential"));
    if (choices.isEmpty()) {
        hint->setText(tr("No saved credentials: use Manage…"));
        hint->show();
    } else {
        hint->hide();
    }
    emit selectionChanged();
}
