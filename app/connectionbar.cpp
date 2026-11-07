#include "connectionbar.h"

#include "communitycredentialservice.h"
#include "usmcredentialservice.h"

#include <QAbstractItemView>
#include <QAction>
#include <QComboBox>
#include <QHBoxLayout>
#include <QVBoxLayout>
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
    layout->setContentsMargins(12, 10, 8, 10);
    layout->setSpacing(12);

    const auto addField = [this, layout](const QString &title, QComboBox *combo, QToolButton *action) {
        auto *group = new QWidget(this);
        auto *column = new QVBoxLayout(group);
        column->setContentsMargins(0, 0, 0, 0); column->setSpacing(4);
        auto *caption = new QLabel(title, group);
        caption->setProperty("caption", true); caption->setBuddy(combo);
        combo->setAccessibleName(title);
        column->addWidget(caption);
        auto *row = new QHBoxLayout; row->setSpacing(6);
        row->addWidget(combo, 1);
        if (action) row->addWidget(action);
        column->addLayout(row);
        combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        combo->setMinimumContentsLength(12);
        layout->addWidget(group, 1);
    };
    targetCombo = new QComboBox(this);
    targetCombo->setObjectName(QStringLiteral("ConnectionTarget"));
    targetCombo->setEditable(true);
    targetCombo->setInsertPolicy(QComboBox::NoInsert);
    targetCombo->setMinimumWidth(140);
    targetCombo->lineEdit()->setPlaceholderText(tr("IP address or host name"));
    targetCombo->lineEdit()->setClearButtonEnabled(true);
    targetCombo->setToolTip(tr("Who to talk to: an IP address or host name, optionally :port.\n"
                               "Recent targets are listed. Delete removes the highlighted entry."));
    addField(tr("Target"), targetCombo, nullptr);

    credentialCombo = new QComboBox(this);
    credentialCombo->setObjectName(QStringLiteral("ConnectionCredential"));
    credentialCombo->setMinimumWidth(150);
    credentialCombo->setToolTip(tr("How to authenticate. Use Manage… to add or edit saved credentials."));
    auto *manage = new QToolButton(this);
    manage->setObjectName(QStringLiteral("ConnectionManageCredentials"));
    manage->setText(tr("Manage…"));
    manage->setToolTip(tr("Add, edit or delete saved credentials (SNMPv1/v2c communities and SNMPv3 users)"));
    // Opens the Credentials screen on the tab that matches the credential chosen now.
    connect(manage, &QToolButton::clicked, this, [this]() {
        emit manageCredentialsRequested(credentialProtocol());
    });
    addField(tr("Credential"), credentialCombo, manage);

    mibProfileCombo->setParent(this);
    mibProfileCombo->setMinimumWidth(170);
    mibProfileCombo->setToolTip(tr("How to interpret what comes back."));
    detectButton = new QToolButton(this);
    detectButton->setObjectName(QStringLiteral("ConnectionDetect"));
    detectButton->setText(tr("Detect"));
    detectButton->setToolTip(tr("Ask the device what it is (sysObjectID and sysDescr, one request) and pick the MIB Profile "
                                "that holds its MIBs. Nothing is sent unless you click this."));
    connect(detectButton, &QToolButton::clicked, this, &ConnectionBar::detectRequested);
    addField(tr("MIB Profile"), mibProfileCombo, detectButton);

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

    QWidget::setTabOrder(targetCombo, credentialCombo);
    QWidget::setTabOrder(credentialCombo, manage);
    QWidget::setTabOrder(manage, mibProfileCombo);
    QWidget::setTabOrder(mibProfileCombo, detectButton);
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
    credentialCombo->setToolTip(choices.isEmpty()
        ? tr("No saved credentials. Use Manage… to add an SNMPv1/v2c community or SNMPv3 user.")
        : tr("How to authenticate. Use Manage… to add or edit saved credentials."));
    credentialCombo->setAccessibleDescription(credentialCombo->toolTip());
    emit selectionChanged();
}
