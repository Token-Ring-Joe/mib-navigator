#ifndef CONNECTIONMODEL_H
#define CONNECTIONMODEL_H

#include <QList>
#include <QSettings>
#include <QString>

#include "agentrequestselection.h"
#include "credentialrecords.h"
#include "preferencesettings.h"

class CommunityCredentialService;
class UsmCredentialService;

// The connection bar is three independent choices: Target, Credential, MIB Profile.
// This file holds the parts that need no widgets: the recent-target history, the
// list of selectable credentials, target parsing, and building a request from them.

// One recent target. It remembers the credential last used with it, never the MIB
// Profile (a lab IP address may be a different vendor's device next time).
struct ConnectionHistoryEntry
{
    QString target;
    QString credentialKey;
};

class ConnectionHistory
{
public:
    static constexpr int kMaxEntries = 30;

    static QList<ConnectionHistoryEntry> load(QSettings &settings);
    static void save(QSettings &settings, const QList<ConnectionHistoryEntry> &entries);
    // Most recent first; an existing entry for the same target is moved to the front.
    static QList<ConnectionHistoryEntry> remembered(
        QList<ConnectionHistoryEntry> entries, const QString &target,
        const QString &credentialKey);
    static QList<ConnectionHistoryEntry> removed(
        QList<ConnectionHistoryEntry> entries, const QString &target);
};

// A selectable credential: a saved community with a chosen SNMP version, or a saved
// SNMPv3 user. The key is stable and is what history stores.
struct ConnectionCredentialChoice
{
    QString key;      // "community:<id>:v1", "community:<id>:v2c", or "usm:<id>"
    QString label;
    int protocol = 1; // 0 = SNMPv1, 1 = SNMPv2c, 2 = SNMPv3
};

struct ConnectionTarget
{
    QString host;
    QString port = QStringLiteral("161");
    bool valid = false;
    // Accepts "host", "host:port", "[ipv6]" and "[ipv6]:port"; a bare IPv6 address is
    // accepted when it contains more than one colon. Whitespace is trimmed.
    static ConnectionTarget parse(const QString &text, int defaultPort = 161);
};

enum class ConnectionError
{
    None,
    NoTarget,
    BadTarget,
    NoCredential,
    CredentialMissing,
    CredentialIncomplete,
    CredentialAmbiguous   // two SNMPv3 users share one security name
};

class ConnectionModel
{
public:
    static QList<ConnectionCredentialChoice> credentialChoices(
        const CommunityCredentialService *community, const UsmCredentialService *usm);
    static QString communityKey(const QString &credentialId, int protocol);
    static QString usmKey(const QString &credentialId);

    // Builds the request for the typed target and chosen credential. Timeout, retries
    // and GETBULK settings come from the global preferences, never from the window.
    static ConnectionError build(const QString &target, const QString &credentialKey,
                                 const CommunityCredentialService *community,
                                 const UsmCredentialService *usm,
                                 const PreferencesSettings &preferences,
                                 AgentRequestSelection *selection);
    static QString errorText(ConnectionError error);
};

#endif
