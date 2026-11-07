#include "preferencesettings.h"

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>

namespace {
int failures = 0;

void check(bool condition, const char *description)
{
    if (!condition)
    {
        QTextStream(stderr) << "FAIL: " << description << Qt::endl;
        ++failures;
    }
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir temporaryDirectory;
    check(temporaryDirectory.isValid(), "temporary settings directory created");
    const QString file = temporaryDirectory.filePath("SnmpB.ini");

    {
        QSettings empty(file, QSettings::IniFormat);
        const PreferencesSettings defaults = PreferencesSettings::load(empty);
        check(defaults.enableIpv4 && defaults.enableIpv6, "IP defaults enabled");
        check(defaults.trapPort4 == 162 && defaults.trapPort6 == 162,
              "trap ports default to 162");
        check(defaults.horizontalSplit && defaults.expandTrapBinding,
              "UI defaults preserved");
        check(!defaults.showAgentName && defaults.automaticLoading == 2,
              "trap display and automatic loading defaults preserved");
        check(defaults.trapHistoryLimit == 1000,
              "trap history has a bounded default");
        check(defaults.requestPort == 161 && defaults.requestTimeout == 3 && defaults.requestRetries == 1 &&
              defaults.bulkNonRepeaters == 0 &&
              defaults.bulkMaxRepetitions == 10,
              "SNMP request defaults preserved");
        check(defaults.mibPaths.isEmpty() && defaults.mibPreloads.isEmpty(),
              "MIB arrays are empty before application default initialization");
    }

    PreferencesSettings expected;
    expected.enableIpv4 = false;
    expected.trapPort4 = 10162;
    expected.enableIpv6 = true;
    expected.trapPort6 = 20162;
    expected.horizontalSplit = false;
    expected.expandTrapBinding = false;
    expected.showAgentName = true;
    expected.trapHistoryLimit = 250;
    expected.automaticLoading = 3;
    expected.requestPort = 1161;
    expected.requestTimeout = 11;
    expected.requestRetries = 5;
    expected.bulkNonRepeaters = 2;
    expected.bulkMaxRepetitions = 45;
    expected.mibPaths = {"C:/MIBs/vendor", "D:/MIBs/ietf"};
    expected.mibPreloads = {"SNMPv2-MIB", "IF-MIB"};
    expected.windowSize = QSize(1280, 800);
    expected.windowPosition = QPoint(40, 60);

    {
        QSettings settings(file, QSettings::IniFormat);
        expected.save(settings);
        settings.sync();
        check(settings.status() == QSettings::NoError, "complete settings saved");
        check(settings.contains("network/trapport4") &&
              !settings.contains("trapport") && !settings.contains("trapport4"),
              "network settings use canonical qualified keys");
        check(settings.value("mibpaths/size").toInt() == 2 &&
              settings.value("mibpreloads/size").toInt() == 2,
              "MIB arrays use the legacy QSettings array schema");
        const bool oldRestartCheck =
            expected.trapPort4 != settings.value("trapport", 162).toInt() ||
            expected.trapPort6 != settings.value("trapport6", 162).toInt() ||
            expected.enableIpv4 != settings.value("enableipv4", true).toBool() ||
            expected.enableIpv6 != settings.value("enableipv6", true).toBool();
        check(oldRestartCheck,
              "old unqualified lookup reproduces the false restart warning");
    }

    {
        QSettings settings(file, QSettings::IniFormat);
        const PreferencesSettings actual = PreferencesSettings::load(settings);
        check(actual.enableIpv4 == expected.enableIpv4 &&
              actual.enableIpv6 == expected.enableIpv6, "IP enablement round-trips");
        check(actual.trapPort4 == expected.trapPort4 &&
              actual.trapPort6 == expected.trapPort6, "trap ports round-trip");
        check(actual.mibPaths == expected.mibPaths, "MIB paths round-trip");
        check(actual.mibPreloads == expected.mibPreloads,
              "automatic MIB preload list round-trips");
        check(actual.automaticLoading == expected.automaticLoading,
              "automatic MIB loading mode round-trips");
        check(actual.requestPort == expected.requestPort &&
              actual.requestTimeout == expected.requestTimeout &&
              actual.requestRetries == expected.requestRetries &&
              actual.bulkNonRepeaters == expected.bulkNonRepeaters &&
              actual.bulkMaxRepetitions == expected.bulkMaxRepetitions,
              "SNMP request defaults round-trip");
        check(actual.expandTrapBinding == expected.expandTrapBinding &&
              actual.showAgentName == expected.showAgentName &&
              actual.trapHistoryLimit == expected.trapHistoryLimit,
              "trap-display settings round-trip");
        check(actual.horizontalSplit == expected.horizontalSplit,
              "splitter orientation round-trips");
        check(actual.windowSize == expected.windowSize &&
              actual.windowPosition == expected.windowPosition,
              "window geometry round-trips");
        check(!expected.networkRestartRequired(actual),
              "canonical persisted network values do not request a restart");
        PreferencesSettings changed = actual;
        changed.trapPort4++;
        check(changed.networkRestartRequired(actual),
              "a network setting change requests a restart");
    }

    const QString legacyFile = temporaryDirectory.filePath("legacy-SnmpB.ini");
    {
        QFile legacy(legacyFile);
        check(legacy.open(QIODevice::WriteOnly | QIODevice::Text),
              "legacy INI fixture created");
        QTextStream(&legacy)
            << "[network]\ntrapport4=1162\ntrapport6=2162\n"
               "enableipv4=false\nenableipv6=true\n"
               "[ui]\nhorizontalsplit=true\nexpandtrapbinding=false\n"
               "selectedprofile=legacy-router\nselectedproto=1\n"
               "[misc]\nshowagentname=true\nautomaticloading=1\n"
               "[mibpaths]\nsize=1\n1\\dir=C:/legacy/mibs\n"
               "[mibpreloads]\nsize=1\n1\\mib=IF-MIB\n";
    }
    {
        QSettings legacy(legacyFile, QSettings::IniFormat);
        const PreferencesSettings loaded = PreferencesSettings::load(legacy);
        check(loaded.trapPort4 == 1162 && loaded.trapPort6 == 2162 &&
              !loaded.enableIpv4 && loaded.enableIpv6,
              "existing network section is compatible");
        check(loaded.trapHistoryLimit == 1000,
              "legacy settings receive bounded trap-history default");
        check(loaded.requestTimeout == 3 && loaded.requestRetries == 1 &&
              loaded.bulkNonRepeaters == 0 && loaded.bulkMaxRepetitions == 10,
              "legacy settings receive established request defaults");
        check(loaded.mibPaths == QStringList{"C:/legacy/mibs"} &&
              loaded.mibPreloads == QStringList{"IF-MIB"},
              "existing MIB arrays are compatible");
        check(!loaded.networkRestartRequired(PreferencesSettings::load(legacy)),
              "legacy canonical network settings do not produce a false restart");
    }

    {
        QSettings legacy(legacyFile, QSettings::IniFormat);
        const QVariant pathsSize = legacy.value("mibpaths/size");
        const QVariant firstPath = legacy.value("mibpaths/1/dir");
        auto edited = PreferencesSettings::load(legacy);
        edited.mibPaths = {"replacement-must-not-be-written"};
        edited.mibPreloads = {"replacement-must-not-be-written"};
        edited.save(legacy, true, true);
        check(legacy.value("mibpaths/size") == pathsSize &&
              legacy.value("mibpaths/1/dir") == firstPath &&
              PreferencesSettings::load(legacy).mibPreloads == QStringList{"IF-MIB"},
              "saving preferences preserves retired paths and preload settings");
    }

    if (failures == 0)
        QTextStream(stdout) << "All Preferences settings tests passed." << Qt::endl;
    return failures == 0 ? 0 : 1;
}
