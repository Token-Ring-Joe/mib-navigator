#ifndef PRODUCTIDENTITY_H
#define PRODUCTIDENTITY_H

#include <QString>

namespace ProductIdentity
{
inline constexpr auto Name = "MIB Navigator";
inline constexpr auto ProjectUrl = "https://github.com/transientuser/snmpb-next";
inline constexpr auto OriginalProjectUrl = "https://sourceforge.net/projects/snmpb/";
inline constexpr auto OriginalSourceUrl =
    "https://sourceforge.net/p/snmpb/code/ci/master/tree/";
inline constexpr auto LegacySettingsDomain = "snmpb.sourceforge.net";
inline constexpr auto LegacySettingsApplication = "SnmpB";

QString aboutHtml(const QString &version, const QString &dependencyVersions);
}

#endif
