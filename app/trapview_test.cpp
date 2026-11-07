#include "trapview.h"
#include "trapservice.h"
#include "trappresenter.h"
#include "mibenvironmentextractor.h"
#include "mibenvironmentregistry.h"
#include "appearancecolors.h"
#include "smi.h"

#include <QApplication>
#include <QAction>
#include <QClipboard>
#include <QElapsedTimer>
#include <QFile>
#include <QDir>
#include <QPushButton>
#include <QLabel>
#include <QTemporaryDir>
#include <QTextEdit>
#include <QTextStream>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {
int failures = 0, assertions = 0;
void check(bool value, const char *message)
{
    ++assertions;
    if (!value) { QTextStream(stderr) << "FAIL: " << message << Qt::endl; ++failures; }
}
MibEnvironmentPtr snapshot()
{
    MibEffectivePlan plan;
    plan.sha256 = "trap-view-test";
    for (SmiModule *module = smiGetFirstModule(); module; module = smiGetNextModule(module)) {
        MibEffectivePlanMember member;
        member.identity = QString::fromLatin1(module->name);
        member.provider.canonicalPath = QString::fromLocal8Bit(module->path);
        plan.members << member;
        plan.effectiveModules << member.identity;
    }
    return MibEnvironmentExtractor().extract(plan);
}
Vb value(const char *oid, const SnmpSyntax &syntax)
{
    Vb binding{Oid(oid)};
    binding.set_value(syntax);
    return binding;
}
Pdu notification()
{
    Pdu pdu;
    pdu.set_type(sNMP_PDU_TRAP);
    pdu.set_notify_id(Oid("1.3.6.1.4.1.53864.99.0.1"));
    pdu += value("1.3.6.1.4.1.53864.99.1.0", SnmpInt32(1));
    pdu += value("1.3.6.1.4.1.53864.99.1.1", SnmpInt32(7));
    pdu += value("1.3.6.1.4.1.53864.99.2.0", Gauge32(9));
    pdu += value("1.3.6.1.4.1.53864.99.2.1", SnmpInt32(101));
    const unsigned char binary[] = {0, 0xff, 'A', 0, 0x80};
    pdu += value("1.3.6.1.4.1.53864.99.3.0", OctetStr(binary, sizeof binary));
    pdu += value("1.3.6.1.2.1.31.1.1.1.6.1", Counter64(0xffffffffffffffffULL));
    pdu += value("1.3.6.1.2.1.2.2.1.6.1", OctetStr(binary, sizeof binary));
    pdu += value("1.3.6.1.2.1.1.2.0", Oid("1.3.6.1.4.1.53864.99.0.1"));
    pdu += value("1.3.6.1.4.1.53864.99.4.0", OctetStr("<b>device text</b>"));
    return pdu;
}
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QTemporaryDir temporary;
    check(temporary.isValid(), "isolated MIB fixture directory exists");
    QFile file(temporary.filePath("different-filename.mib"));
    check(file.open(QIODevice::WriteOnly), "synthetic MIB fixture opens");
    file.write(R"MIB(TRAP-VIEW-TEST-MIB DEFINITIONS ::= BEGIN
IMPORTS MODULE-IDENTITY, OBJECT-TYPE, NOTIFICATION-TYPE, enterprises FROM SNMPv2-SMI;
trapView MODULE-IDENTITY
 LAST-UPDATED "202601010000Z" ORGANIZATION "Test" CONTACT-INFO "Test"
 DESCRIPTION "Synthetic fixture" ::= { enterprises 53864 99 }
notifications OBJECT IDENTIFIER ::= { trapView 0 }
state OBJECT-TYPE SYNTAX INTEGER { up(1), down(2) } MAX-ACCESS read-only
 STATUS current DESCRIPTION "State" ::= { trapView 1 }
load OBJECT-TYPE SYNTAX INTEGER (0..100) UNITS "percent" MAX-ACCESS read-only
 STATUS current DESCRIPTION "Load" ::= { trapView 2 }
shortBytes OBJECT-TYPE SYNTAX OCTET STRING (SIZE (0..4)) MAX-ACCESS read-only
 STATUS current DESCRIPTION "Short bytes" ::= { trapView 3 }
textValue OBJECT-TYPE SYNTAX OCTET STRING MAX-ACCESS read-only
 STATUS current DESCRIPTION "Untrusted text" ::= { trapView 4 }
changed NOTIFICATION-TYPE OBJECTS { state, load } STATUS current
 DESCRIPTION "A <b>literal</b> description & reference." ::= { notifications 1 }
END
)MIB");
    file.close();
    smiInit("trap-view-test");
    const QByteArray paths = (temporary.path() + QDir::listSeparator() +
        QStringLiteral(SNMPB_SOURCE_DIR "/libsmi/mibs/ietf")).toLocal8Bit();
    smiSetPath(paths.constData());
    check(smiLoadModule(file.fileName().toLocal8Bit().constData()), "filename != module identity loads");
    check(smiLoadModule("IF-MIB"), "standard profile includes table and Counter64 objects");
    const auto environment = snapshot();
    check(environment->nodes().size() > 100, "profile contains realistic standard MIB metadata");
    smiExit();
    check(file.open(QIODevice::ReadOnly), "second profile fixture reads");
    QByteArray alternateSource = file.readAll(); file.close();
    alternateSource.replace("up(1)", "ready(1)");
    alternateSource.replace("(0..100)", "(0..200)");
    alternateSource.replace("(0..4)", "(0..10)");
    check(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "second profile fixture writes");
    file.write(alternateSource); file.close();
    smiInit("trap-view-alternate-profile");
    smiSetPath(paths.constData());
    check(smiLoadModule(file.fileName().toLocal8Bit().constData()), "different profile loads alternative definitions for same OIDs");
    const auto alternate = snapshot();
    smiExit();
    MibEnvironmentRegistry::publish(environment);

    TrapService service(3);
    QWidget host;
    auto *layout = new QVBoxLayout(&host);
    auto *log = new QTreeWidget(&host);
    log->setColumnCount(9);
    auto *content = new QTreeWidget(&host);
    auto *info = new QTextEdit(&host);
    layout->addWidget(log); layout->addWidget(content); layout->addWidget(info);
    bool expand = true;
    TrapView view(&service, log, content, info, [&expand] { return expand; });
    auto *copy = host.findChild<QAction *>("CopyTrapSourceIP");
    auto *clear = host.findChild<QPushButton *>("ClearTrapHistory");
    check(copy && !copy->isEnabled() && clear && !clear->isEnabled(), "empty view disables copy and clear");
    auto *emptyHint = host.findChild<QLabel *>("TrapHistoryHint");
    check(emptyHint && !emptyHint->isHidden() && emptyHint->text().contains("Preferences > Traps"),
          "empty trap history explains listener configuration without claiming it is running");
    TrapEndpoint endpoint{"192.0.2.10", 9162, TrapSnmpVersion::V2c, "public", {}};
    Pdu pdu = notification();
    check(service.receive(pdu, endpoint), "synthetic PDU flows through decoder, history and widget");
    check(log->topLevelItemCount() == 1 && log->currentItem(), "first trap is selected");
    check(emptyHint->isHidden(), "first trap removes empty-history guidance");
    check(content->columnCount() == 6 && !content->isHeaderHidden(), "raw and interpreted columns are visible together");
    check(content->topLevelItemCount() == 2, "metadata and bindings sections are present");
    auto *bindings = content->topLevelItem(1);
    check(bindings && bindings->childCount() == 9 && bindings->isExpanded(), "all bindings appear in received order");
    if (!bindings || bindings->childCount() != 9) return 1;
    check(bindings->child(0)->text(0) == "state.0" && bindings->child(0)->text(1) == "up(1)" &&
        bindings->child(0)->text(3) == "1", "enumeration shows interpretation beside raw integer");
    check(bindings->child(0)->text(2) == "Integer32" && bindings->child(0)->text(5).isEmpty(), "matching enum has received type and no warning");
    check(bindings->child(1)->text(5).contains("Undefined value"), "undefined enum warning reaches widget");
    check(bindings->child(2)->text(5).contains("Wrong type") && bindings->child(2)->text(4) == "percent", "wrong-type warning and units reach widget");
    check(bindings->child(3)->text(5).contains("Out of range"), "out-of-range warning reaches widget");
    check(bindings->child(4)->text(5).contains("Wrong size") && bindings->child(4)->text(3).contains("00 FF 41 00 80"), "embedded NUL and high-bit bytes survive size checking and rendering");
    check(bindings->child(1)->data(0, AppearanceColors::ToneRole).toInt() == AppearanceColors::Warning &&
        !bindings->child(1)->icon(0).isNull(), "warning row has semantic tint and icon");
    check(bindings->child(4)->toolTip(0).contains("Received OCTET STRING") && bindings->child(4)->toolTip(0).contains("Wrong size"), "warning tooltip includes wire data");
    check(bindings->child(5)->text(1) == "18446744073709551615" && bindings->child(5)->text(3) == "18446744073709551615", "Counter64 precision survives both display modes");
    check(bindings->child(6)->text(1) == "00:ff:41:00:80", "MAC display hint uses original binary octets");
    check(bindings->child(7)->text(1) == "changed" && bindings->child(7)->text(3) == "1.3.6.1.4.1.53864.99.0.1", "OID values show label alongside original numeric OID");
    check(bindings->child(8)->text(1) == "<b>device text</b>", "device text remains literal in cells");
    check(info->toPlainText().contains("<b>literal</b>"), "notification description escapes MIB markup");
    check(log->topLevelItem(0)->text(4) == "changed", "notification summary uses active profile");
    const quint64 selected = log->currentItem()->data(0, Qt::UserRole).toULongLong();
    copy->trigger();
    check(QApplication::clipboard()->text() == "192.0.2.10", "copy action copies source IP");
    // Destroy the input value and parser before reinterpreting history.
    pdu.clear();
    MibEnvironmentRegistry::publish({});
    view.refreshEnvironment();
    check(log->currentItem()->data(0, Qt::UserRole).toULongLong() == selected && service.history().count() == 1, "profile change preserves selection and history");
    check(log->topLevelItem(0)->text(4) == "1.3.6.1.4.1.53864.99.0.1" && info->toPlainText().isEmpty(), "missing profile uses numeric notification and clears stale metadata");
    bindings = content->topLevelItem(1);
    check(bindings->child(0)->text(1) == "1" && bindings->child(1)->text(5).isEmpty() && bindings->child(4)->text(3).contains("00 FF 41 00 80"), "missing profile drops MIB warnings and retains original bytes");
    MibEnvironmentRegistry::publish(environment);
    view.refreshEnvironment();
    check(content->topLevelItem(1)->child(0)->text(1) == "up(1)" && content->topLevelItem(1)->child(4)->text(5).contains("Wrong size"), "restored profile reinterprets same typed history after parser destruction");
    MibEnvironmentRegistry::publish(alternate);
    view.refreshEnvironment();
    bindings = content->topLevelItem(1);
    check(bindings->child(0)->text(1) == "ready(1)" && bindings->child(0)->text(3) == "1",
        "another product profile changes labels for the same historical wire value");
    check(bindings->child(3)->text(5).isEmpty() && bindings->child(4)->text(5).isEmpty(),
        "another profile applies its own ranges and sizes instead of stale warnings");
    check(log->currentItem()->data(0, Qt::UserRole).toULongLong() == selected && service.history().count() == 1,
        "switching between conflicting profile definitions retains exact selected history");
    MibEnvironmentRegistry::publish(environment);
    view.refreshEnvironment();
    check(content->topLevelItem(1)->child(0)->text(1) == "up(1)" &&
        content->topLevelItem(1)->child(3)->text(5).contains("Out of range"),
        "switching back restores original interpretation without changing stored values");
    expand = false;
    view.refreshEnvironment();
    check(!content->topLevelItem(1)->isExpanded(), "live expansion preference is read when presenting a trap");
    expand = true;
    service.receive(notification(), endpoint);
    check(log->currentItem()->data(0, Qt::UserRole).toULongLong() == selected, "incoming trap preserves selected ID");
    service.setMaximumRecords(1);
    check(log->topLevelItemCount() == 1 && !log->currentItem() && content->topLevelItemCount() == 0 && !copy->isEnabled(), "eviction clears selected details without silently selecting a different trap");
    log->setCurrentItem(log->topLevelItem(0));
    const quint64 second = log->currentItem()->data(0, Qt::UserRole).toULongLong();
    service.setMaximumRecords(3);
    service.receive(notification(), endpoint);
    check(log->currentItem()->data(0, Qt::UserRole).toULongLong() == second, "stable ID selection survives shifted history rows");
    clear->click();
    check(!emptyHint->isHidden(), "clearing populated history restores its empty-state guidance");
    check(service.history().count() == 0 && log->topLevelItemCount() == 0 && content->topLevelItemCount() == 0 && info->toPlainText().isEmpty() && !copy->isEnabled() && !clear->isEnabled(), "clear resets owned history and all presentation");

    Pdu relayed = notification();
    relayed.set_type(sNMP_PDU_V1TRAP);
    relayed.set_v1_trap_address(IpAddress("198.51.100.7"));
    endpoint.version = TrapSnmpVersion::V1;
    service.receive(relayed, endpoint);
    copy->trigger();
    check(QApplication::clipboard()->text() == "198.51.100.7", "real copy action prefers relayed v1 agent address");
    service.clear();
    endpoint.version = TrapSnmpVersion::V3;
    endpoint.community.clear(); endpoint.securityName = "operator";
    Pdu inform = notification();
    inform.set_type(sNMP_PDU_INFORM);
    inform.set_security_level(SNMP_SECURITY_LEVEL_AUTH_PRIV);
    service.receive(inform, endpoint);
    check(content->topLevelItem(0)->text(0).contains("USM user: operator") &&
        content->topLevelItem(0)->text(0).contains("AuthPriv") && !content->topLevelItem(0)->text(0).contains("Community"), "v3 displays non-secret security metadata instead of empty community");

    service.clear(); service.setMaximumRecords(1000);
    QElapsedTimer timer; timer.start();
    for (int i = 0; i < 1100; ++i) service.receive(inform, endpoint);
    check(service.history().count() == 1000 && log->topLevelItemCount() == 1000, "burst keeps bounded service and view history aligned");
    log->setCurrentItem(log->topLevelItem(500));
    const auto burstId = log->currentItem()->data(0, Qt::UserRole).toULongLong();
    view.refreshEnvironment();
    check(log->currentItem()->data(0, Qt::UserRole).toULongLong() == burstId, "large history profile refresh preserves exact selected trap");
    QTextStream(stdout) << "1100-trap burst and profile refresh: " << timer.elapsed() << " ms" << Qt::endl;
    // Queued/profile refreshes arriving after widgets disappear must be harmless.
    delete log; delete content; delete info;
    view.refreshEnvironment();
    service.receive(inform, endpoint); service.clear();
    check(true, "destroyed views tolerate later receive, clear and profile callbacks");
    MibEnvironmentRegistry::publish({});
    QTextStream(stdout) << assertions << " assertions; " << failures << " failures" << Qt::endl;
    return failures ? 1 : 0;
}
