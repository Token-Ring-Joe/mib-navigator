#ifndef PRODUCTIDENTITY_H
#define PRODUCTIDENTITY_H

#include <QString>

namespace ProductIdentity
{
inline constexpr auto Name = "MIB Navigator";
inline constexpr auto ProjectUrl = "https://github.com/Token-Ring-Joe/mib-navigator";
inline constexpr auto OriginalProjectUrl = "https://sourceforge.net/projects/snmpb/";
inline constexpr auto OriginalSourceUrl =
    "https://sourceforge.net/p/snmpb/code/ci/master/tree/";
inline constexpr auto LegacySettingsDomain = "snmpb.sourceforge.net";
inline constexpr auto LegacySettingsApplication = "SnmpB";

// Identifies this exact build. A clean (committed) build is stable across runs; a build with
// local changes also carries its build time, so a development build never trusts an older
// build's saved data.
QString buildFingerprint();

QString aboutHtml(const QString &version, const QString &dependencyVersions);
}

#endif
