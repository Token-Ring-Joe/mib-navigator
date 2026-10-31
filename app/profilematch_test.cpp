#include "profilematch.h"

#include <QCoreApplication>
#include <iostream>

namespace {
bool check(bool value, const char *message)
{ if (!value) std::cerr << "FAIL: " << message << '\n'; return value; }

QByteArray mib(const char *module, const char *body)
{ return QByteArray(module) + " DEFINITIONS ::= BEGIN\nIMPORTS enterprises FROM SNMPv2-SMI;\n" + body + "\nEND\n"; }
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    bool ok = true;
    MibCollectionIndex index;
    index.setFiles({
        MibCollectionIndex::parse("/c/SNMPv2-SMI",
            "SNMPv2-SMI DEFINITIONS ::= BEGIN\ninternet OBJECT IDENTIFIER ::= { iso org(3) dod(6) 1 }\n"
            "private OBJECT IDENTIFIER ::= { internet 4 }\nenterprises OBJECT IDENTIFIER ::= { private 1 }\nEND\n"),
        // Two product lines of one vendor (enterprise 100); a second vendor (200); a vendor whose
        // MIBs no profile includes (300).
        MibCollectionIndex::parse("/c/ALPHA-SWITCH", mib("ALPHA-SWITCH", "alpha OBJECT IDENTIFIER ::= { enterprises 100 }\nalphaSwitch OBJECT IDENTIFIER ::= { alpha 1 }\nalphaSwitchModel OBJECT IDENTIFIER ::= { alphaSwitch 5 }")),
        MibCollectionIndex::parse("/c/ALPHA-ROUTER", mib("ALPHA-ROUTER", "alphaRouter OBJECT IDENTIFIER ::= { enterprises 100 2 }")),
        MibCollectionIndex::parse("/c/BETA-MIB", mib("BETA-MIB", "beta OBJECT IDENTIFIER ::= { enterprises 200 }\nbetaProducts OBJECT IDENTIFIER ::= { beta 1 }")),
        MibCollectionIndex::parse("/c/GAMMA-MIB", mib("GAMMA-MIB", "gamma OBJECT IDENTIFIER ::= { enterprises 300 }")),
    });

    // Which profiles hold which modules. The built-in Standards profile is never offered.
    const auto profiles = [](const QString &module) {
        QList<ProfileMatchProfile> list;
        if (module == "ALPHA-SWITCH") list << ProfileMatchProfile{"p.switch", "Alpha Switch"};
        if (module == "ALPHA-ROUTER") list << ProfileMatchProfile{"p.router", "Alpha Router"};
        if (module == "ALPHA-SWITCH" || module == "ALPHA-ROUTER") list << ProfileMatchProfile{"p.lab", "Lab: all Alpha"};
        if (module == "BETA-MIB") list << ProfileMatchProfile{"p.beta", "Beta"};
        return list;
    };

    // ---- exact registration, longest prefix ------------------------------------------------------------
    auto r = MatchProfileForSysObjectId(index, "1.3.6.1.4.1.100.1.5", profiles);
    ok &= check(r.kind == ProfileMatchResult::Kind::Exact && r.matchedOid == "1.3.6.1.4.1.100.1.5" &&
                r.candidates.size() == 2 && r.candidates.at(0).profileId == "p.switch" && r.candidates.at(1).profileId == "p.lab",
                "an exact registration finds the profiles holding that module, and not the router profile");
    r = MatchProfileForSysObjectId(index, "1.3.6.1.4.1.100.2", profiles);
    ok &= check(r.kind == ProfileMatchResult::Kind::Exact && r.candidates.at(0).profileId == "p.router", "the other product line resolves to its own profile");
    r = MatchProfileForSysObjectId(index, "1.3.6.1.4.1.100.1.5.7.3", profiles);
    ok &= check(r.kind == ProfileMatchResult::Kind::Prefix && r.matchedOid == "1.3.6.1.4.1.100.1.5" && r.candidates.at(0).profileId == "p.switch" &&
                r.explanation.contains("closest registration"), "a sysObjectID below the defined OID uses the longest defined prefix");
    r = MatchProfileForSysObjectId(index, ".1.3.6.1.4.1.200.1.9", profiles);
    ok &= check(r.kind == ProfileMatchResult::Kind::Prefix && r.matchedOid == "1.3.6.1.4.1.200.1" && r.candidates.size() == 1 && r.candidates.first().profileId == "p.beta",
                "a leading dot is accepted");

    // ---- enterprise number only -------------------------------------------------------------------------
    r = MatchProfileForSysObjectId(index, "1.3.6.1.4.1.100.9.9", profiles);
    ok &= check(r.kind == ProfileMatchResult::Kind::Enterprise && r.matchedOid == "1.3.6.1.4.1.100" && r.candidates.size() == 3,
                "an unregistered product of a known vendor offers every profile with that vendor's MIBs");

    r = MatchProfileForSysObjectId(index, "1.3.6.1.4.1.100", profiles);
    ok &= check(r.kind == ProfileMatchResult::Kind::Enterprise && r.candidates.size() == 3,
                "a sysObjectID that is just the enterprise number offers every profile of that vendor");

    // ---- nothing to match ----------------------------------------------------------------------------------
    r = MatchProfileForSysObjectId(index, "1.3.6.1.4.1.300.1", profiles);
    ok &= check(r.kind == ProfileMatchResult::Kind::NoMatch && r.candidates.isEmpty() && r.modulesWithoutProfile.contains("GAMMA-MIB") &&
                r.explanation.contains("no saved profile includes them"), "MIBs exist but no profile has them: say so");
    r = MatchProfileForSysObjectId(index, "1.3.6.1.4.1.999.1", profiles);
    ok &= check(r.kind == ProfileMatchResult::Kind::NoMatch && r.explanation.contains("nothing to match"), "an unknown vendor");
    r = MatchProfileForSysObjectId(index, "1.3.6.1.2.1.1.1", profiles);
    ok &= check(r.kind == ProfileMatchResult::Kind::NotEnterprise && r.candidates.isEmpty(), "a sysObjectID outside the enterprises tree is not matched");
    r = MatchProfileForSysObjectId(index, "1.3.6.1.4.1", profiles);
    ok &= check(r.kind == ProfileMatchResult::Kind::NotEnterprise, "the enterprises node itself is never a match");
    r = MatchProfileForSysObjectId(index, "", profiles);
    ok &= check(r.kind == ProfileMatchResult::Kind::NotEnterprise && !r.explanation.isEmpty(), "an empty answer is handled");

    // The standard prefixes (enterprises, private, internet) must never be what matches.
    const auto everything = [](const QString &) { return QList<ProfileMatchProfile>{{"p.all", "Everything"}}; };
    r = MatchProfileForSysObjectId(index, "1.3.6.1.4.1.999.1", everything);
    ok &= check(r.kind == ProfileMatchResult::Kind::NoMatch, "a profile holding the SMI modules does not match every device");
    return ok ? 0 : 1;
}
