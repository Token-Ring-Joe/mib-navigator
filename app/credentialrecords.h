#ifndef CREDENTIALRECORDS_H
#define CREDENTIALRECORDS_H

#include <QByteArray>
#include <QString>

enum class CredentialKind { Community, Usm };

struct CredentialIdentity
{
    QString credentialId;
    CredentialKind kind = CredentialKind::Community;
};

class CredentialSecret
{
public:
    CredentialSecret() = default;
    explicit CredentialSecret(const QByteArray &value) : secret(value) {}

    CredentialSecret copy() const { return CredentialSecret(secret); }
    bool isEmpty() const { return secret.isEmpty(); }
    const QByteArray &bytes() const { return secret; }

private:
    QByteArray secret;
};

struct CommunityCredentialRecord
{
    CredentialIdentity identity;
    QString displayName;
    CredentialSecret readCommunity;
    CredentialSecret writeCommunity;
};

struct UsmCredentialRecord
{
    CredentialIdentity identity;
    QString displayName;
    QString securityName;
    int authProtocol = 0;
    CredentialSecret authSecret;
    int privacyProtocol = 0;
    CredentialSecret privacySecret;
};

struct EffectiveCredentialValues
{
    QString readCommunity;
    QString writeCommunity;
    QString securityName;
    int securityLevel = 0;
};

class CredentialResolver
{
public:
    static EffectiveCredentialValues inlineValues(const QString &readCommunity,
                                                  const QString &writeCommunity,
                                                  const QString &securityName,
                                                  int securityLevel)
    {
        return {readCommunity, writeCommunity, securityName, securityLevel};
    }
    static EffectiveCredentialValues communityValues(
        const CommunityCredentialRecord &credential)
    {
        return {QString::fromUtf8(credential.readCommunity.bytes()),
                QString::fromUtf8(credential.writeCommunity.bytes()), {}, 0};
    }
};

#endif
