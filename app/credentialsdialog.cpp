#include "dialoglayout.h"
#include "credentialsdialog.h"
#include "communitycredentialservice.h"
#include "usmcredentialservice.h"
#include "snmp_pp/snmp_pp.h"
#include <QListWidget>
#include <QApplication>
#include <QLineEdit>
#include <QComboBox>
#include <QLabel>
#include <QStackedWidget>
#include <QPushButton>
#include <QToolButton>
#include <QMenu>
#include <QFormLayout>
#include <QScrollArea>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QDialogButtonBox>
#include <QSignalBlocker>
#include <QMessageBox>
#include <QUuid>
#include <QStyledItemDelegate>
#include <QPainter>
#include <QStyle>

namespace {
constexpr int credentialNameRole = Qt::UserRole + 2;
constexpr int credentialVersionRole = Qt::UserRole + 3;
int versionWidth(const QFontMetrics &metrics) { return metrics.horizontalAdvance(QStringLiteral("SNMPv1 / v2c")) + 20; }
class CredentialColumns final : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QStyleOptionViewItem item(option); initStyleOption(&item, index); item.text.clear();
        const QStyle *style = item.widget ? item.widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &item, painter, item.widget);
        const int width = versionWidth(item.fontMetrics);
        QRect name = item.rect.adjusted(10, 0, -width, 0);
        QRect version = item.rect.adjusted(qMax(0, item.rect.width() - width), 0, -8, 0);
        const auto group = !(item.state & QStyle::State_Enabled) ? QPalette::Disabled : QPalette::Active;
        painter->save(); painter->setFont(item.font);
        painter->setPen(item.palette.color(group, item.state & QStyle::State_Selected ? QPalette::HighlightedText : QPalette::Text));
        painter->drawText(name, Qt::AlignLeft | Qt::AlignVCenter,
            item.fontMetrics.elidedText(index.data(credentialNameRole).toString(), Qt::ElideRight, qMax(0, name.width()-8)));
        painter->drawText(version, Qt::AlignRight | Qt::AlignVCenter, index.data(credentialVersionRole).toString());
        painter->restore();
    }
};
void setCredentialLabel(QListWidgetItem *item, const QString &name, const QString &version)
{
    item->setText(name);
    item->setData(credentialNameRole, name); item->setData(credentialVersionRole, version);
    item->setData(Qt::AccessibleTextRole, QObject::tr("%1, %2").arg(name, version));
    item->setToolTip(QObject::tr("%1 · %2").arg(name, version));
}
}

CredentialsDialog::CredentialsDialog(CommunityCredentialService *community,
    UsmCredentialService *service, QWidget *parent)
    : QDialog(parent), communityService(community), userService(service)
{
    setObjectName("CredentialsDialog"); setWindowTitle(tr("Credentials")); resize(860, 560);
    auto *outer = new QVBoxLayout(this);
    auto *body = new QHBoxLayout;
    auto *left = new QVBoxLayout;
    auto *actions = new QHBoxLayout;
    auto *add = new QToolButton(this);
    add->setObjectName("CredentialNew"); add->setText(tr("New"));
    add->setPopupMode(QToolButton::InstantPopup);
    auto *menu = new QMenu(add);
    const QStringList labels{tr("SNMPv1"), tr("SNMPv2c"), tr("SNMPv3")};
    for (int version = 0; version < labels.size(); ++version) {
        auto *action = menu->addAction(labels[version]);
        action->setObjectName(QStringLiteral("CredentialNewV%1").arg(version));
        connect(action, &QAction::triggered, this, [this, version]() { addCredential(version); });
    }
    add->setMenu(menu); actions->addWidget(add);
    duplicateButton = new QPushButton(tr("Duplicate"), this);
    duplicateButton->setObjectName("CredentialDuplicate"); actions->addWidget(duplicateButton);
    removeButton = new QPushButton(tr("Delete"), this);
    removeButton->setObjectName("CredentialDelete"); actions->addWidget(removeButton);
    left->addLayout(actions);
    list = new QListWidget(this); list->setObjectName("CredentialList");
    list->setMinimumWidth(230); list->setItemDelegate(new CredentialColumns(list));
    list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *headings = new QHBoxLayout; headings->setContentsMargins(10, 0, 8, 0);
    auto *nameHeading = new QLabel(tr("Name"), this); nameHeading->setProperty("caption", true);
    auto *versionHeading = new QLabel(tr("Version"), this); versionHeading->setProperty("caption", true);
    versionHeading->setFixedWidth(versionWidth(versionHeading->fontMetrics())); versionHeading->setAlignment(Qt::AlignRight);
    headings->addWidget(nameHeading, 1); headings->addWidget(versionHeading); left->addLayout(headings);
    left->addWidget(list); body->addLayout(left, 1);
    editor = new QWidget(this);
    auto *right = new QVBoxLayout(editor);
    right->setContentsMargins(0, 0, 0, 0); right->setSpacing(12);
    auto *common = new QFormLayout; common->setVerticalSpacing(12); common->setHorizontalSpacing(16);
    displayName = new QLineEdit(editor); displayName->setObjectName("CredentialDisplayName");
    versionLabel = new QLabel(editor); versionLabel->setObjectName("CredentialVersion");
    common->addRow(tr("Display name"), displayName); common->addRow(tr("Version"), versionLabel);
    right->addLayout(common);
    pages = new QStackedWidget(editor); pages->setObjectName("CredentialFields");
    auto *communityPage = new QWidget(pages); auto *communityForm = new QFormLayout(communityPage);
    readCommunity = new QLineEdit(communityPage); readCommunity->setObjectName("CredentialReadCommunity");
    writeCommunity = new QLineEdit(communityPage); writeCommunity->setObjectName("CredentialWriteCommunity");
    readCommunity->setEchoMode(QLineEdit::Password); writeCommunity->setEchoMode(QLineEdit::Password);
    communityForm->addRow(tr("Read community"), readCommunity);
    communityForm->addRow(tr("Write community"), writeCommunity); pages->addWidget(communityPage);
    auto *userPage = new QWidget(pages); auto *userForm = new QFormLayout(userPage);
    securityName = new QLineEdit(userPage); securityName->setObjectName("CredentialUsmUser");
    authProtocol = new QComboBox(userPage); authProtocol->setObjectName("CredentialAuthProtocol");
    authSecret = new QLineEdit(userPage); authSecret->setObjectName("CredentialAuthSecret");
    privacyProtocol = new QComboBox(userPage); privacyProtocol->setObjectName("CredentialPrivacyProtocol");
    privacySecret = new QLineEdit(userPage); privacySecret->setObjectName("CredentialPrivacySecret");
    authSecret->setEchoMode(QLineEdit::Password); privacySecret->setEchoMode(QLineEdit::Password);
    authProtocol->addItem(tr("None"), SNMP_AUTHPROTOCOL_NONE);
    authProtocol->addItem("MD5", SNMP_AUTHPROTOCOL_HMACMD5);
    authProtocol->addItem("SHA-1", SNMP_AUTHPROTOCOL_HMACSHA);
    authProtocol->addItem("SHA-224", SNMP_AUTHPROTOCOL_HMAC128SHA224);
    authProtocol->addItem("SHA-256", SNMP_AUTHPROTOCOL_HMAC192SHA256);
    authProtocol->addItem("SHA-384", SNMP_AUTHPROTOCOL_HMAC256SHA384);
    authProtocol->addItem("SHA-512", SNMP_AUTHPROTOCOL_HMAC384SHA512);
    privacyProtocol->addItem(tr("None"), SNMP_PRIVPROTOCOL_NONE);
    privacyProtocol->addItem("DES", SNMP_PRIVPROTOCOL_DES);
    privacyProtocol->addItem("3DES", SNMP_PRIVPROTOCOL_3DESEDE);
    privacyProtocol->addItem("IDEA", SNMP_PRIVPROTOCOL_IDEA);
    privacyProtocol->addItem("AES-128", SNMP_PRIVPROTOCOL_AES128);
    privacyProtocol->addItem("AES-192", SNMP_PRIVPROTOCOL_AES192);
    privacyProtocol->addItem("AES-256", SNMP_PRIVPROTOCOL_AES256);
    userForm->addRow(tr("USM username"), securityName);
    userForm->addRow(tr("Authentication"), authProtocol); userForm->addRow(tr("Authentication secret"), authSecret);
    userForm->addRow(tr("Privacy"), privacyProtocol); userForm->addRow(tr("Privacy secret"), privacySecret);
    auto *hint = new QLabel(tr("The display name identifies this saved credential. The USM username is sent to the device and may be shared by several credentials."), userPage);
    hint->setWordWrap(true); userForm->addRow(hint); pages->addWidget(userPage);
    for (auto *form : {common, communityForm, userForm}) {
        form->setContentsMargins(0, 0, 0, 0);
        form->setVerticalSpacing(12); form->setHorizontalSpacing(16);
        form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    }
    right->addWidget(pages); right->addStretch();
    auto *scroll = new QScrollArea(this); scroll->setObjectName("CredentialEditorScroll");
    scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame);
    scroll->setMinimumWidth(260); scroll->setWidget(editor);
    body->setSpacing(16); body->addWidget(scroll, 2); outer->addLayout(body, 1);
    duplicateButton->setAutoDefault(false); removeButton->setAutoDefault(false);
    status = new QLabel(this); status->setObjectName("CredentialStatus"); status->setWordWrap(true); outer->addWidget(status);
    buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    outer->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &CredentialsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(list, &QListWidget::currentRowChanged, this, &CredentialsDialog::selectRow);
    connect(duplicateButton, &QPushButton::clicked, this, &CredentialsDialog::duplicateCredential);
    connect(removeButton, &QPushButton::clicked, this, &CredentialsDialog::deleteCredential);
    connect(authProtocol, &QComboBox::currentIndexChanged, this, [this]() {
        if (authProtocol->currentData().toInt() == SNMP_AUTHPROTOCOL_NONE) privacyProtocol->setCurrentIndex(0);
        updateSecurityFields();
    });
    connect(privacyProtocol, &QComboBox::currentIndexChanged, this, &CredentialsDialog::updateSecurityFields);
    connect(displayName, &QLineEdit::editingFinished, this, &CredentialsDialog::storeFields);
    DialogLayout::initialize(*this, QSize(860, 560));
}

void CredentialsDialog::prepare(Tab preferred)
{
    currentId.clear(); communities = communityService->records(); users = userService->records();
    QString selected;
    if (preferred == Users && !users.isEmpty()) selected = users.first().identity.credentialId;
    else if (!communities.isEmpty()) selected = communities.first().identity.credentialId;
    rebuildList(selected, preferred == Users ? CredentialKind::Usm : CredentialKind::Community);
    const QString error = communityService->storageError() + userService->storageError();
    status->setText(error);
    buttons->button(QDialogButtonBox::Ok)->setEnabled(error.isEmpty());
}
void CredentialsDialog::execute(Tab preferred) { prepare(preferred); exec(); }

void CredentialsDialog::storeFields()
{
    if (currentId.isEmpty()) return;
    if (currentKind == CredentialKind::Community) {
        for (auto &r : communities) if (r.identity.credentialId == currentId) {
            r.displayName = displayName->text().trimmed();
            r.readCommunity = CredentialSecret(readCommunity->text().toUtf8());
            r.writeCommunity = CredentialSecret(writeCommunity->text().toUtf8());
        }
    } else for (auto &r : users) if (r.identity.credentialId == currentId) {
        r.displayName = displayName->text().trimmed(); r.securityName = securityName->text().trimmed();
        r.authProtocol = authProtocol->currentData().toInt(); r.privacyProtocol = privacyProtocol->currentData().toInt();
        r.authSecret = CredentialSecret(authSecret->text().toLatin1());
        r.privacySecret = CredentialSecret(privacySecret->text().toLatin1());
    }
    for (int row = 0; row < list->count(); ++row) {
        auto *item = list->item(row);
        if (item->data(Qt::UserRole).toString() == currentId &&
            item->data(Qt::UserRole + 1).toInt() == static_cast<int>(currentKind))
            setCredentialLabel(item, displayName->text(), versionLabel->text());
    }
}

void CredentialsDialog::selectRow()
{
    // The previous working record is identified by stable ID, independently of row position.
    storeFields();
    auto *item = list->currentItem(); currentId = item ? item->data(Qt::UserRole).toString() : QString();
    editor->setEnabled(item); removeButton->setEnabled(item); duplicateButton->setEnabled(item);
    if (!item) { displayName->clear(); return; }
    currentKind = static_cast<CredentialKind>(item->data(Qt::UserRole + 1).toInt());
    if (currentKind == CredentialKind::Community) {
        pages->setCurrentIndex(0);
        for (const auto &r : communities) if (r.identity.credentialId == currentId) {
            displayName->setText(r.displayName); readCommunity->setText(QString::fromUtf8(r.readCommunity.bytes()));
            writeCommunity->setText(QString::fromUtf8(r.writeCommunity.bytes()));
            versionLabel->setText(r.protocol < 0 ? tr("SNMPv1 / v2c") : r.protocol == 0 ? tr("SNMPv1") : tr("SNMPv2c"));
        }
    } else {
        pages->setCurrentIndex(1);
        for (const auto &r : users) if (r.identity.credentialId == currentId) {
            const QSignalBlocker a(authProtocol), p(privacyProtocol);
            displayName->setText(r.displayName); securityName->setText(r.securityName);
            const auto select = [](QComboBox *combo, int value) {
                int index = combo->findData(value);
                if (index < 0) { combo->addItem(QObject::tr("Unsupported (%1)").arg(value), value); index = combo->count()-1; }
                combo->setCurrentIndex(index);
            };
            select(authProtocol, r.authProtocol); select(privacyProtocol, r.privacyProtocol);
            authSecret->setText(QString::fromLatin1(r.authSecret.bytes()));
            privacySecret->setText(QString::fromLatin1(r.privacySecret.bytes())); versionLabel->setText(tr("SNMPv3"));
        }
        updateSecurityFields();
    }
}
void CredentialsDialog::updateSecurityFields()
{
    const bool auth = authProtocol->currentData().toInt() != SNMP_AUTHPROTOCOL_NONE;
    authSecret->setEnabled(auth); privacyProtocol->setEnabled(auth);
    privacySecret->setEnabled(auth && privacyProtocol->currentData().toInt() != SNMP_PRIVPROTOCOL_NONE);
}

void CredentialsDialog::rebuildList(const QString &selectedId, CredentialKind kind)
{
    currentId.clear();
    {
        QSignalBlocker block(list); list->clear();
        const auto append = [this](const QString &id, CredentialKind kind, const QString &name, const QString &version) {
            auto *item = new QListWidgetItem(list);
            setCredentialLabel(item, name, version);
            item->setData(Qt::UserRole, id); item->setData(Qt::UserRole + 1, static_cast<int>(kind));
        };
        for (const auto &r : communities) append(r.identity.credentialId, CredentialKind::Community, r.displayName,
            r.protocol < 0 ? tr("SNMPv1 / v2c") : r.protocol == 0 ? tr("SNMPv1") : tr("SNMPv2c"));
        for (const auto &r : users) append(r.identity.credentialId, CredentialKind::Usm, r.displayName, tr("SNMPv3"));
        int selected = 0;
        for (int i = 0; i < list->count(); ++i) if (list->item(i)->data(Qt::UserRole).toString() == selectedId &&
            list->item(i)->data(Qt::UserRole + 1).toInt() == static_cast<int>(kind)) selected = i;
        list->setCurrentRow(list->count() ? selected : -1);
    }
    selectRow();
}
QString CredentialsDialog::uniqueName(const QString &base) const
{
    QString name = base; int suffix = 2;
    const auto exists = [&](const QString &value) {
        for (const auto &r : communities) if (r.displayName == value) return true;
        for (const auto &r : users) if (r.displayName == value) return true;
        return false;
    };
    while (exists(name)) name = base + QStringLiteral(" %1").arg(suffix++);
    return name;
}
void CredentialsDialog::addCredential(int version)
{
    storeFields(); const QString name = uniqueName(tr("New credential")); QString id;
    if (version == 2) {
        auto r = userService->createWorkingRecord(QString()); r.displayName = name;
        id = r.identity.credentialId; users.append(r);
    } else {
        CommunityCredentialRecord r; r.identity = {QUuid::createUuid().toString(QUuid::WithoutBraces), CredentialKind::Community};
        r.displayName = name; r.protocol = version; id = r.identity.credentialId; communities.append(r);
    }
    rebuildList(id, version == 2 ? CredentialKind::Usm : CredentialKind::Community); displayName->setFocus(); displayName->selectAll(); status->clear();
}
void CredentialsDialog::duplicateCredential()
{
    storeFields(); QString id;
    if (currentKind == CredentialKind::Community) {
        for (auto r : communities) if (r.identity.credentialId == currentId) {
            r.identity.credentialId = QUuid::createUuid().toString(QUuid::WithoutBraces);
            r.displayName = uniqueName(r.displayName + tr(" copy")); id = r.identity.credentialId; communities.append(r); break;
        }
    } else for (auto r : users) if (r.identity.credentialId == currentId) {
        r.identity.credentialId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        r.displayName = uniqueName(r.displayName + tr(" copy")); id = r.identity.credentialId; users.append(r); break;
    }
    rebuildList(id, currentKind);
}
void CredentialsDialog::deleteCredential()
{
    if (currentKind == CredentialKind::Community)
        for (int i = communities.size()-1; i >= 0; --i) if (communities[i].identity.credentialId == currentId) communities.removeAt(i);
    if (currentKind == CredentialKind::Usm)
        for (int i = users.size()-1; i >= 0; --i) if (users[i].identity.credentialId == currentId) users.removeAt(i);
    rebuildList();
}

bool CredentialsDialog::applyChanges()
{
    storeFields(); auto *service = userService;
    if (!service->storageError().isEmpty() || !communityService->storageError().isEmpty()) return false;
    const auto beforeUsers = service->records(); const auto beforeCommunities = communityService->records();
    const bool usersChanged = users != beforeUsers || userService->needsMigration();
    const bool communitiesChanged = communities != beforeCommunities;
    if (!usersChanged && !communitiesChanged) return true;
    const auto error = [this](const QString &text) { status->setText(text); return false; };
    if (usersChanged && !service->validateWorkingCopy(users))
        return error(tr("Each SNMPv3 credential needs a display name and USM username. Authentication and privacy require their secrets; privacy also requires authentication."));
    for (const auto &r : communities) {
        bool changed = true;
        for (const auto &old : beforeCommunities) if (old == r) { changed = false; break; }
        if (changed && (r.displayName.isEmpty() || r.readCommunity.isEmpty()))
            return error(tr("Each community credential needs a display name and read community."));
    }
    if (usersChanged && !service->saveWorkingCopy(users)) return error(tr("Could not save SNMPv3 credentials. No changes were applied."));
    if (communitiesChanged && !communityService->saveWorkingCopy(communities)) {
        const bool restored = !usersChanged || service->saveWorkingCopy(beforeUsers);
        return error(restored ? tr("Could not save community credentials. The previous configuration was restored.")
                              : tr("Could not save credentials or restore the previous file. No changes were applied in memory; check the credential files before restarting."));
    }
    if (usersChanged) service->applyCommitted(users);
    if (communitiesChanged) communityService->applyCommitted(communities);
    status->clear(); return true;
}
void CredentialsDialog::accept() { if (applyChanges()) QDialog::accept(); }
