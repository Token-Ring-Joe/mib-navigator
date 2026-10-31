#include "communitycredentialmanager.h"
#include "communitycredentialservice.h"
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QUuid>
#include <QVBoxLayout>

CommunityCredentialManager::CommunityCredentialManager(
    CommunityCredentialService *credentialService, QWidget *parent)
    : QDialog(parent), service(credentialService)
{
    setWindowTitle(tr("Community Credentials")); resize(560, 320);
    auto *layout = new QHBoxLayout(this); list = new QListWidget(this);
    layout->addWidget(list, 1); auto *right = new QVBoxLayout;
    auto *form = new QFormLayout; name = new QLineEdit(this);
    readCommunity = new QLineEdit(this); writeCommunity = new QLineEdit(this);
    readCommunity->setEchoMode(QLineEdit::Password);
    writeCommunity->setEchoMode(QLineEdit::Password);
    form->addRow(tr("Display name"), name);
    form->addRow(tr("Read community"), readCommunity);
    form->addRow(tr("Write community"), writeCommunity); right->addLayout(form);
    auto *actions = new QHBoxLayout;
    auto *add = new QPushButton(tr("New"), this);
    auto *duplicate = new QPushButton(tr("Duplicate"), this);
    auto *remove = new QPushButton(tr("Delete"), this);
    actions->addWidget(add); actions->addWidget(duplicate); actions->addWidget(remove);
    right->addLayout(actions); right->addStretch();
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok |
                                         QDialogButtonBox::Cancel, this);
    right->addWidget(buttons); layout->addLayout(right, 2);
    connect(list, &QListWidget::currentRowChanged, this, &CommunityCredentialManager::selectRow);
    connect(name, &QLineEdit::editingFinished, this, &CommunityCredentialManager::storeFields);
    connect(readCommunity, &QLineEdit::editingFinished, this, &CommunityCredentialManager::storeFields);
    connect(writeCommunity, &QLineEdit::editingFinished, this, &CommunityCredentialManager::storeFields);
    connect(add, &QPushButton::clicked, this, &CommunityCredentialManager::addCredential);
    connect(duplicate, &QPushButton::clicked, this, &CommunityCredentialManager::duplicateCredential);
    connect(remove, &QPushButton::clicked, this, &CommunityCredentialManager::deleteCredential);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void CommunityCredentialManager::execute()
{
    working = service->records(); rebuildList();
    if (exec() != Accepted) return;
    storeFields();
    QStringList retained;
    for (const auto &record : working)
    {
        retained.append(record.identity.credentialId);
        if (service->find(record.identity.credentialId)) service->update(record);
        else service->create(record);
    }
    const QList<CommunityCredentialRecord> original = service->records();
    for (const auto &record : original)
        if (!retained.contains(record.identity.credentialId)) service->remove(record.identity.credentialId);
}

void CommunityCredentialManager::selectRow(int row)
{
    storeFields(); current = row;
    if (row < 0 || row >= working.size()) { name->clear(); readCommunity->clear(); writeCommunity->clear(); return; }
    name->setText(working[row].displayName);
    readCommunity->setText(QString::fromUtf8(working[row].readCommunity.bytes()));
    writeCommunity->setText(QString::fromUtf8(working[row].writeCommunity.bytes()));
}

void CommunityCredentialManager::storeFields()
{
    if (current < 0 || current >= working.size()) return;
    working[current].displayName = name->text();
    working[current].readCommunity = CredentialSecret(readCommunity->text().toUtf8());
    working[current].writeCommunity = CredentialSecret(writeCommunity->text().toUtf8());
    if (list->item(current)) list->item(current)->setText(working[current].displayName);
}

void CommunityCredentialManager::addCredential()
{
    storeFields(); CommunityCredentialRecord record;
    record.identity = {QUuid::createUuid().toString(QUuid::WithoutBraces), CredentialKind::Community};
    record.displayName = tr("New credential"); working.append(record);
    rebuildList(working.size() - 1);
}

void CommunityCredentialManager::duplicateCredential()
{
    if (current < 0 || current >= working.size()) return; storeFields();
    CommunityCredentialRecord copy = working[current];
    copy.identity.credentialId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    copy.displayName += tr(" copy"); working.append(copy); rebuildList(working.size() - 1);
}

void CommunityCredentialManager::deleteCredential()
{
    if (current < 0 || current >= working.size()) return;
    working.removeAt(current); rebuildList(qMin(current, working.size() - 1));
}

void CommunityCredentialManager::rebuildList(int selected)
{
    current = -1; list->clear();
    for (const auto &record : working) list->addItem(record.displayName);
    if (!working.isEmpty()) list->setCurrentRow(qBound(0, selected, working.size() - 1));
}
