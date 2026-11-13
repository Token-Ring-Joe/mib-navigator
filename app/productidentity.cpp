#include "productidentity.h"
#include "snmpb_build_info.h"

QString ProductIdentity::buildFingerprint()
{
    QString text = QStringLiteral("%1|%2").arg(QString::fromLatin1(SNMPB_BUILD_REVISION),
                                               QString::fromLatin1(SNMPB_BUILD_SOURCE_STATE));
    if (text.contains(QStringLiteral("local changes")) || text.contains(QStringLiteral("unknown")))
        text += QLatin1Char('|') + QString::fromLatin1(SNMPB_BUILD_UTC);
    return text;
}

QString ProductIdentity::aboutHtml(const QString &version,
                                   const QString &dependencyVersions)
{
    return QStringLiteral(
        "<h2>MIB Navigator</h2><p><b>Version %1</b></p>"
        "<p>A free, cross-platform SNMP/MIB engineering tool with MIB browsing, "
        "device queries, tables, value watching and traps.</p>"
        "<p><b>Build (UTC):</b> %6<br><b>Source revision:</b> %7 (%8)</p>"
        "<h3>Project</h3><p>MIB Navigator source/project: "
        "<a href=\"%2\">%2</a></p>"
        "<h3>Original SnmpB project</h3>"
        "<p>MIB Navigator is derived from the GPL-licensed SnmpB project. "
        "It is not an official SnmpB release.</p>"
        "<p>Original SnmpB author: Martin Jolicoeur<br>"
        "Later SnmpB maintenance: Max ulidtko<br>"
        "<a href=\"%3\">Original SnmpB Project</a><br>"
        "<a href=\"%4\">Original SnmpB Source</a></p>"
        "<h3>MIB Navigator development</h3>"
        "<p>MIB Navigator modifications and continued development: "
        "Joseph Wood and contributors</p>"
        "<h3>License</h3>"
        "<p>MIB Navigator is free software distributed under the GNU General "
        "Public License, version 2 or (at your option) any later version. "
        "This software is provided without warranty.</p>"
        "<p><a href=\"license.txt\">View License</a> | "
        "<a href=\"third-party-dependencies.md\">Third-Party Notices</a></p>"
        "<p>%5</p>")
        .arg(version.toHtmlEscaped(), QString::fromLatin1(ProjectUrl),
             QString::fromLatin1(OriginalProjectUrl),
             QString::fromLatin1(OriginalSourceUrl), dependencyVersions.toHtmlEscaped(),
             QString::fromLatin1(SNMPB_BUILD_UTC),
             QString::fromLatin1(SNMPB_BUILD_REVISION),
             QString::fromLatin1(SNMPB_BUILD_SOURCE_STATE));
}
