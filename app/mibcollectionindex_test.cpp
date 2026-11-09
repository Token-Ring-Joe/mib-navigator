#include "mibcollectionindex.h"

#include <QCoreApplication>
#include <iostream>

namespace {
bool check(bool value, const char *message)
{ if (!value) std::cerr << "FAIL: " << message << '\n'; return value; }

const char *kSmi = R"(SNMPv2-SMI DEFINITIONS ::= BEGIN
-- the root of everything ::= { bogus 99 } is only a comment
internet OBJECT IDENTIFIER ::= { iso org(3) dod(6) 1 }
mgmt     OBJECT IDENTIFIER ::= { internet 2 }
mib-2    OBJECT IDENTIFIER ::= { mgmt 1 }
private  OBJECT IDENTIFIER ::= { internet 4 }
enterprises OBJECT IDENTIFIER ::= { private 1 }
OBJECT-TYPE MACRO ::=
BEGIN
    TYPE NOTATION ::= "SYNTAX" Syntax
    VALUE NOTATION ::= value(VALUE ObjectName)
END
END
)";
const char *kIfMib = R"(IF-MIB DEFINITIONS ::= BEGIN
IMPORTS MODULE-IDENTITY, OBJECT-TYPE, mib-2 FROM SNMPv2-SMI
        DisplayString FROM SNMPv2-TC;
ifMIB MODULE-IDENTITY
    LAST-UPDATED "200006140000Z"
    DESCRIPTION "contains braces { and ::= { fake 7 } inside a string"
    ::= { mib-2 31 }
interfaces OBJECT IDENTIFIER ::= { mib-2 2 }
ifTable OBJECT-TYPE
    SYNTAX SEQUENCE OF IfEntry
    MAX-ACCESS not-accessible
    STATUS current
    DESCRIPTION "table"
    ::= { interfaces 2 }
ifEntry OBJECT-TYPE
    SYNTAX IfEntry
    MAX-ACCESS not-accessible
    STATUS current
    DESCRIPTION "row"
    INDEX { ifIndex }
    ::= { ifTable 1 }
IfEntry ::= SEQUENCE { ifIndex INTEGER, ifInOctets Counter32 }
ifIndex OBJECT-TYPE
    SYNTAX INTEGER (1..2147483647)
    MAX-ACCESS read-only
    STATUS current
    DESCRIPTION "index"
    ::= { ifEntry 1 }
ifInOctets OBJECT-TYPE
    SYNTAX Counter32
    MAX-ACCESS read-only
    STATUS current
    DESCRIPTION "octets"
    ::= { ifEntry 10 }
linkDown NOTIFICATION-TYPE
    OBJECTS { ifIndex }
    STATUS current
    DESCRIPTION "down"
    ::= { snmpTraps 3 }
END
)";
// Two modules in one file, an enterprise MIB that imports a module that is not present, an old
// v1 trap, and a circular definition.
const char *kVendor = R"(ACME-MIB DEFINITIONS ::= BEGIN
IMPORTS enterprises FROM SNMPv2-SMI
        MissingThing FROM NOT-HERE-MIB;
acme OBJECT IDENTIFIER ::= { enterprises 90001 }
acmeCpu OBJECT-TYPE
    SYNTAX INTEGER { low(1), high(2) }
    ACCESS read-only
    STATUS mandatory
    DESCRIPTION "cpu"
    ::= { acme 1 }
acmeLost OBJECT-TYPE
    SYNTAX INTEGER
    ACCESS read-only
    STATUS mandatory
    DESCRIPTION "parent is missing"
    ::= { missingParent 5 }
loopA OBJECT IDENTIFIER ::= { loopB 1 }
loopB OBJECT IDENTIFIER ::= { loopA 1 }
acmeFailed TRAP-TYPE
    ENTERPRISE acme
    VARIABLES { acmeCpu }
    DESCRIPTION "v1 trap"
    ::= 7
END

ACME-EXTRA DEFINITIONS ::= BEGIN
IMPORTS acme FROM ACME-MIB;
acmeExtra OBJECT IDENTIFIER ::= { acme 2 }
sysName OBJECT IDENTIFIER ::= { acme 3 }
Percent ::= TEXTUAL-CONVENTION
    STATUS current
    DESCRIPTION "a type"
    SYNTAX INTEGER (0..100)
END
)";
const char *kOtherSysName = R"(SNMPv2-MIB DEFINITIONS ::= BEGIN
IMPORTS mib-2 FROM SNMPv2-SMI;
system OBJECT IDENTIFIER ::= { mib-2 1 }
sysName OBJECT IDENTIFIER ::= { system 5 }
END
)";
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    bool ok = true;

    const auto smi = MibCollectionIndex::parse("/c/SNMPv2-SMI", kSmi);
    ok &= check(smi.modules.size() == 1 && smi.modules.first().name == "SNMPv2-SMI" &&
                smi.modules.first().definitions.size() == 5, "the SMI file yields its 5 nodes and skips the MACRO body");
    const auto vendor = MibCollectionIndex::parse("/c/ACME", kVendor);
    ok &= check(vendor.modules.size() == 2 && vendor.modules.at(1).name == "ACME-EXTRA",
                "one file can hold two modules");

    MibCollectionIndex index;
    index.setFiles({smi, MibCollectionIndex::parse("/c/IF-MIB", kIfMib), vendor,
                    MibCollectionIndex::parse("/c/SNMPv2-MIB", kOtherSysName)});
    ok &= check(index.fileCount() == 4 && index.moduleCount() == 5, "files and modules are counted");

    // ---- OID resolution ----------------------------------------------------------------------------
    auto one = [&](const QString &q) { const auto r = index.search(q); return r.entries.isEmpty() ? MibCollectionEntry{} : r.entries.first(); };
    ok &= check(one("internet").oid == "1.3.6.1", "named-number form { iso org(3) dod(6) 1 }");
    ok &= check(one("enterprises").oid == "1.3.6.1.4.1", "a chain across definitions");
    ok &= check(one("ifInOctets").oid == "1.3.6.1.2.1.2.2.1.10" && one("ifInOctets").module == "IF-MIB" &&
                one("ifInOctets").kind == "Object" && one("ifInOctets").line > 0,
                "an object resolves through an import in another file");
    ok &= check(one("ifMIB").oid == "1.3.6.1.2.1.31" && one("ifMIB").kind == "Module identity",
                "a MODULE-IDENTITY whose DESCRIPTION contains braces and ::=");
    ok &= check(one("acmeCpu").oid == "1.3.6.1.4.1.90001.1" && one("acmeExtra").oid == "1.3.6.1.4.1.90001.2",
                "an enterprise MIB and a second module in the same file that imports from the first");
    ok &= check(one("acmeFailed").oid == "1.3.6.1.4.1.90001.0.7" && one("acmeFailed").kind == "Trap",
                "an SNMPv1 TRAP-TYPE is enterprise.0.number");
    ok &= check(one("Percent").kind == "Type" && one("Percent").oid.isEmpty() && one("Percent").unresolved.isEmpty(),
                "a TEXTUAL-CONVENTION is searchable and not an unresolved problem");

    // ---- honest about what it cannot work out ---------------------------------------------------------
    const auto lost = one("acmeLost");
    ok &= check(lost.oid.isEmpty() && lost.unresolved.contains("missingParent"), "a missing parent is reported, not guessed");
    const auto loop = one("loopA");
    ok &= check(loop.oid.isEmpty() && loop.unresolved.contains("itself"), "a circular definition is reported");
    ok &= check(index.unresolvedCount() >= 3, "unresolved definitions are counted");
    ok &= check(one("linkDown").oid.isEmpty() && one("linkDown").unresolved.contains("snmpTraps"),
                "a definition whose parent lives in a module that is not in the collection is unresolved");

    // ---- name searches -----------------------------------------------------------------------------------
    auto names = [&](const QString &q) { QStringList out; for (const auto &e : index.search(q).entries) out << e.name; return out; };
    ok &= check(names("ifinoctets") == QStringList{"ifInOctets"}, "name search ignores case");
    ok &= check(names("ifIn") == QStringList({"ifIndex", "ifInOctets"}), "a prefix finds every name starting with it, in name order");
    ok &= check(names("octets").contains("ifInOctets"), "part of a name finds it");
    const auto sys = index.search("sysName");
    ok &= check(sys.entries.size() == 2 && sys.entries.at(0).module == "ACME-EXTRA" && sys.entries.at(1).module == "SNMPv2-MIB",
                "the same name in two modules gives two results, each with its module");
    ok &= check(index.search("SNMPv2-MIB::sysName").entries.size() == 1 &&
                index.search("SNMPv2-MIB::sysName").entries.first().oid == "1.3.6.1.2.1.1.5",
                "Module::name selects one module");
    ok &= check(index.search("IF-MIB.ifIndex").entries.size() == 1 && index.search("IF-MIB.ifIndex").entries.first().oid == "1.3.6.1.2.1.2.2.1.1",
                "Module.name works too");
    ok &= check(names("IF-MIB::ifIn") == QStringList({"ifIndex", "ifInOctets"}) && names("IF-MIB::sys").isEmpty(), "a qualified prefix stays inside the module");
    ok &= check(index.search("nothing-like-this").entries.isEmpty(), "no match gives an empty result");
    ok &= check(index.search("   ").entries.isEmpty(), "a blank query gives an empty result");
    ok &= check(index.modulesDefining("sysName") == QStringList({"ACME-EXTRA", "SNMPv2-MIB"}), "modulesDefining");

    // ---- numeric searches -----------------------------------------------------------------------------------
    ok &= check(index.search("1.3.6.1.2.1.2.2.1.10").entries.first().name == "ifInOctets" && index.search("1.3.6.1.2.1.2.2.1.10").note == "exact",
                "an exact numeric OID");
    ok &= check(index.search(".1.3.6.1.2.1.2.2.1.10").entries.first().name == "ifInOctets", "a leading dot is accepted");
    const auto instance = index.search("1.3.6.1.2.1.2.2.1.10.4");
    ok &= check(instance.entries.size() == 1 && instance.entries.first().name == "ifInOctets" && instance.note == "1.3.6.1.2.1.2.2.1.10",
                "an instance finds the definition above it and says which OID it matched");
    ok &= check(index.search("9.9.9.9").entries.isEmpty(), "an OID nothing defines gives an empty result");

    // ---- limits ---------------------------------------------------------------------------------------------------
    ok &= check(index.search("i", 2).truncated && index.search("i", 2).entries.size() == 2, "results are limited and flagged");

    // ---- hostile input never crashes ------------------------------------------------------------------------------
    for (const char *junk : {"", "DEFINITIONS", "X DEFINITIONS ::= BEGIN foo OBJECT-TYPE", "X DEFINITIONS ::= BEGIN a OBJECT IDENTIFIER ::= {",
                             "X DEFINITIONS ::= BEGIN IMPORTS a FROM", "\"unterminated string", "-- only a comment",
                             "X DEFINITIONS ::= BEGIN a OBJECT-TYPE ::= { b 1 } END"})
        MibCollectionIndex::parse("/c/junk", junk);
    MibCollectionIndex empty; empty.setFiles({});
    ok &= check(empty.search("anything").entries.isEmpty() && empty.definitionCount() == 0, "an empty collection is fine");
    return ok ? 0 : 1;
}
