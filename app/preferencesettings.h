#ifndef PREFERENCESETTINGS_H
#define PREFERENCESETTINGS_H

#include <QPoint>
#include <QSettings>
#include <QSize>
#include <QStringList>

struct PreferencesSettings
{
    bool enableIpv4 = true;
    int trapPort4 = 162;
    bool enableIpv6 = true;
    int trapPort6 = 162;
    bool horizontalSplit = true;   // Results below the Tree (false: beside it)
    bool expandTrapBinding = true;
    bool showAgentName = false;
    int trapHistoryLimit = 1000;
    int automaticLoading = 2;
    QString selectedProfile = QStringLiteral("localhost");
    QString selectedProfileId;
    int selectedProtocol = 0;
    int requestPort = 161;   // used when the Target has no ":port"
    int requestTimeout = 3;
    int requestRetries = 1;
    int bulkNonRepeaters = 0;
    int bulkMaxRepetitions = 10;
    QStringList mibPaths;
    QStringList mibPreloads;
    QSize windowSize;
    QPoint windowPosition;

    static PreferencesSettings load(QSettings &settings);
    void save(QSettings &settings, bool preserveLegacyMibPreloads = false) const;
    bool networkRestartRequired(const PreferencesSettings &persisted) const;
};

#endif
