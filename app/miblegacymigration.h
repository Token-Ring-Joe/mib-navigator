#ifndef MIBLEGACYMIGRATION_H
#define MIBLEGACYMIGRATION_H

#include <QStringList>

class QSettings;
class MibDependencyIndex;
class MibProfileService;

struct MibLegacyMigrationResult {
    bool alreadyComplete = false;
    bool needed = false;
    bool migrated = false;
    int inputCount = 0;
    int resolvedCount = 0;
    int duplicateCount = 0;
    int unresolvedCount = 0;
    QString profileId;
    QString error;
};

class MibLegacyMigration
{
public:
    static constexpr int Version = 1;
    static QString markerKey();
    static QString profileId();
    static QString profileName();
    static MibLegacyMigrationResult migrate(QSettings &settings,
        MibProfileService &profiles, const MibDependencyIndex &index);
};

#endif
