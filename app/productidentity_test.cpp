#include "productidentity.h"
#include "snmpb_version.h"

#include <iostream>

int main()
{
    const QString html = ProductIdentity::aboutHtml(SNMPB_VERSION_STRING,
                                                     QStringLiteral("dependencies"));
    const bool valid = QString::fromLatin1(ProductIdentity::Name) == "MIB Navigator" &&
        QString::fromLatin1(ProductIdentity::LegacySettingsDomain) ==
            "snmpb.sourceforge.net" &&
        QString::fromLatin1(ProductIdentity::LegacySettingsApplication) == "SnmpB" &&
        html.contains("MIB Navigator") && html.contains("Version " + QString::fromLatin1(SNMPB_VERSION_STRING)) &&
        html.contains(ProductIdentity::ProjectUrl) &&
        html.contains(ProductIdentity::OriginalProjectUrl) &&
        html.contains(ProductIdentity::OriginalSourceUrl) &&
        html.contains("Martin Jolicoeur") && html.contains("Max ulidtko") &&
        html.contains("Joseph Wood and contributors") &&
        html.contains("Build (UTC):") && html.contains("UTC") &&
        html.contains("Source revision:") && html.contains("value watching") &&
        html.contains("GNU General Public License") &&
        html.contains("version 2 or (at your option) any later version");
    if (!valid)
        std::cerr << "product identity or About attribution regression" << std::endl;
    return valid ? 0 : 1;
}
