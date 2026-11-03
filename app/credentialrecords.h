#ifndef CREDENTIALRECORDS_H
#define CREDENTIALRECORDS_H

#include <QByteArray>
#include <QString>
#include <optional>

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
    bool operator==(const CredentialSecret &other) const { return secret == other.secret; }

private:
    QByteArray secret;
};

struct CommunityCredentialRecord
{
    CredentialIdentity identity;
    QString displayName;
    int protocol = -1; // legacy shared v1/v2c; new entries select 0 (v1) or 1 (v2c)
    CredentialSecret readCommunity;
    CredentialSecret writeCommunity;
};

// SNMP++ protocol IDs (not combo-box indexes); kept independent of its headers.
inline constexpr int UsmProtocolNone = 1;

struct UsmCredentialRecord
{
    CredentialIdentity identity;
    QString displayName;
    QString securityName;
    int authProtocol = UsmProtocolNone;
    CredentialSecret authSecret;
    int privacyProtocol = UsmProtocolNone;
    CredentialSecret privacySecret;
};

inline bool operator==(const CommunityCredentialRecord &a, const CommunityCredentialRecord &b)
{
    return a.identity.credentialId == b.identity.credentialId && a.displayName == b.displayName &&
        a.protocol == b.protocol && a.readCommunity == b.readCommunity && a.writeCommunity == b.writeCommunity;
}
inline bool operator==(const UsmCredentialRecord &a, const UsmCredentialRecord &b)
{
    return a.identity.credentialId == b.identity.credentialId && a.displayName == b.displayName &&
        a.securityName == b.securityName && a.authProtocol == b.authProtocol &&
        a.privacyProtocol == b.privacyProtocol && a.authSecret == b.authSecret && a.privacySecret == b.privacySecret;
}

struct EffectiveCredentialValues
{
    QString readCommunity;
    QString writeCommunity;
    QString securityName;
    int securityLevel = 0;
    std::optional<UsmCredentialRecord> usmCredential;
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
