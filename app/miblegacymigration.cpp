#include "miblegacymigration.h"

#include "mibdependencyindex.h"
#include "mibprofile.h"

#include <QSettings>

QString MibLegacyMigration::markerKey()
{ return QStringLiteral("mib-library/legacy-preloads-migration-version"); }
QString MibLegacyMigration::profileId()
{ return QStringLiteral("migration.legacy-mibpreloads.v1"); }
QString MibLegacyMigration::profileName()
{ return QObject::tr("Imported Legacy MIBs"); }

MibLegacyMigrationResult MibLegacyMigration::migrate(QSettings &settings,
    MibProfileService &profiles, const MibDependencyIndex &index)
{
    MibLegacyMigrationResult result;
    result.alreadyComplete = settings.value(markerKey(), 0).toInt() >= Version;
    if (result.alreadyComplete) return result;
    QStringList legacy;
    const int size = settings.beginReadArray(QStringLiteral("mibpreloads"));
    for (int i = 0; i < size; ++i) {
        settings.setArrayIndex(i);
        legacy.append(settings.value(QStringLiteral("mib")).toString());
    }
    settings.endArray();
    const MibRuntimeRequestNormalization normalized =
        MibNormalizeRuntimeRequests(legacy, index);
    result.inputCount = normalized.inputCount;
    result.resolvedCount = normalized.identities.size() - normalized.unresolvedCount;
    result.duplicateCount = normalized.duplicateCount;
    result.unresolvedCount = normalized.unresolvedCount;
    result.needed = !normalized.identities.isEmpty();
    if (!result.needed) return result;
    result.profileId = profileId();
    if (!profiles.importCustomProfile(profileId(), profileName(), normalized.identities, &result.error))
        return result;
    if (!profiles.migrateLegacyProfiles(index, &result.error)) return result;
    settings.setValue(markerKey(), Version);
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        result.error = QObject::tr("Unable to persist legacy MIB migration marker");
        return result;
    }
    result.migrated = true;
    return result;
}
