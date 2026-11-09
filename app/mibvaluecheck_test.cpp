#include "mibvaluesemantics.h"

#include <QCoreApplication>
#include <iostream>

namespace {
bool check(bool value, const char *message)
{ if (!value) std::cerr << "FAIL: " << message << '\n'; return value; }

MibEnvironmentValue signedValue(qint64 v)
{ MibEnvironmentValue r; r.isSigned = true; r.signedValue = v; return r; }
MibEnvironmentValue unsignedValue(quint64 v)
{ MibEnvironmentValue r; r.isSigned = false; r.unsignedValue = v; return r; }
MibEnvironmentConstraint range(qint64 lo, qint64 hi, bool size = false)
{ MibEnvironmentConstraint c; c.minimum = signedValue(lo); c.maximum = signedValue(hi); c.isSizeConstraint = size; return c; }

MibEnvironmentNodeRecord node(const char *name, MibEnvironmentBaseType base, const char *syntax)
{ MibEnvironmentNodeRecord n; n.name = name; n.baseType = base; n.syntaxName = syntax; return n; }

MibValueCheck run(const MibEnvironmentNodeRecord *n, const Vb &vb)
{ MibResolvedObject object; object.node = n; return CheckMibValue(object, vb); }
bool anyWarning(const MibValueCheck &c, const char *text)
{ for (const QString &w : c.warnings) if (w.contains(QLatin1String(text))) return true; return false; }
Vb withValue(const SnmpSyntax &value) { Vb vb; vb.set_value(value); return vb; }
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    bool ok = true;

    // ---- wire type versus the MIB's type ------------------------------------------------
    const auto gaugeObject = node("ifSpeed", MibEnvironmentBaseType::Unsigned32, "Gauge32");
    MibValueCheck c = run(&gaugeObject, withValue(Gauge32(100)));
    ok &= check(c.warnings.isEmpty() && c.receivedType == "Gauge32" && c.rawValue == "100", "matching Gauge32 is clean");
    c = run(&gaugeObject, withValue(Counter32(100)));
    ok &= check(c.receivedType == "Counter32" && anyWarning(c, "sent Counter32") && anyWarning(c, "ifSpeed") &&
                anyWarning(c, "Gauge32"), "Counter32 sent for a Gauge32 object is a wrong-type warning naming both types");

    const auto counter64Object = node("ifHCInOctets", MibEnvironmentBaseType::Unsigned64, "Counter64");
    c = run(&counter64Object, withValue(Counter32(5)));
    ok &= check(anyWarning(c, "Wrong type") && anyWarning(c, "Counter64"), "a 32-bit counter for a Counter64 object is flagged");
    Counter64 big(0xFFFFFFFFull);
    c = run(&counter64Object, withValue(big));
    ok &= check(c.warnings.isEmpty() && c.receivedType == "Counter64", "matching Counter64 is clean");

    const auto intObject = node("ifMtu", MibEnvironmentBaseType::Integer32, "Integer32");
    c = run(&intObject, withValue(Gauge32(1500)));
    ok &= check(anyWarning(c, "Wrong type") && anyWarning(c, "Integer32"), "Gauge32 for an Integer32 object is flagged");

    const auto stringObject = node("sysName", MibEnvironmentBaseType::OctetString, "OCTET STRING");
    c = run(&stringObject, withValue(OctetStr("router1")));
    ok &= check(c.warnings.isEmpty() && c.receivedType == "OCTET STRING", "matching OCTET STRING is clean");
    c = run(&stringObject, withValue(Gauge32(7)));
    ok &= check(anyWarning(c, "sent Gauge32") && anyWarning(c, "OCTET STRING"), "a number for a string object is flagged");

    const auto bitsObject = node("someBits", MibEnvironmentBaseType::Bits, "BITS");
    c = run(&bitsObject, withValue(OctetStr("\x80")));
    ok &= check(c.warnings.isEmpty(), "BITS legitimately arrive as OCTET STRING");

    // ---- enumerations ---------------------------------------------------------------------
    auto status = node("ifAdminStatus", MibEnvironmentBaseType::Enumeration, "INTEGER");
    status.namedValues = {{"up", signedValue(1)}, {"down", signedValue(2)}, {"testing", signedValue(3)}};
    c = run(&status, withValue(SnmpInt32(2)));
    ok &= check(c.warnings.isEmpty(), "a defined enumeration value is clean");
    c = run(&status, withValue(SnmpInt32(7)));
    ok &= check(anyWarning(c, "Undefined value") && anyWarning(c, "7") && anyWarning(c, "up(1)") &&
                anyWarning(c, "testing(3)"), "an undefined enumeration value lists the defined ones");
    ok &= check(c.rawValue == "7", "the raw value is shown even when it is undefined");

    // ---- ranges and sizes -------------------------------------------------------------------
    auto percent = node("cpuLoad", MibEnvironmentBaseType::Integer32, "Integer32");
    percent.constraints = {range(0, 100)};
    ok &= check(run(&percent, withValue(SnmpInt32(100))).warnings.isEmpty(), "value at the top of the range is clean");
    c = run(&percent, withValue(SnmpInt32(101)));
    ok &= check(anyWarning(c, "Out of range") && anyWarning(c, "0..100"), "value above the range is flagged with the range");
    c = run(&percent, withValue(SnmpInt32(-1)));
    ok &= check(anyWarning(c, "Out of range"), "negative value below the range is flagged");

    auto twoRanges = node("vlanId", MibEnvironmentBaseType::Integer32, "Integer32");
    twoRanges.constraints = {range(1, 5), range(10, 20)};
    ok &= check(run(&twoRanges, withValue(SnmpInt32(12))).warnings.isEmpty(), "ranges are alternatives: 12 fits 10..20");
    c = run(&twoRanges, withValue(SnmpInt32(7)));
    ok &= check(anyWarning(c, "1..5 | 10..20"), "a value between alternative ranges lists all of them");

    auto unsignedRange = node("queueDepth", MibEnvironmentBaseType::Unsigned32, "Gauge32");
    unsignedRange.constraints = {range(0, 10)};
    ok &= check(anyWarning(run(&unsignedRange, withValue(Gauge32(11))), "Out of range"), "unsigned values are range checked");
    ok &= check(run(&unsignedRange, withValue(Gauge32(10))).warnings.isEmpty(), "unsigned value in range is clean");

    auto shortString = node("owner", MibEnvironmentBaseType::OctetString, "OCTET STRING");
    shortString.constraints = {range(0, 4, true)};
    ok &= check(run(&shortString, withValue(OctetStr("abc"))).warnings.isEmpty(), "string within its size is clean");
    c = run(&shortString, withValue(OctetStr("abcdef")));
    ok &= check(anyWarning(c, "Wrong size") && anyWarning(c, "6") && anyWarning(c, "0..4"),
                "string longer than its size constraint is flagged with the size");
    auto sizeAndValue = node("both", MibEnvironmentBaseType::Integer32, "Integer32");
    sizeAndValue.constraints = {range(0, 4, true)};
    ok &= check(run(&sizeAndValue, withValue(SnmpInt32(900))).warnings.isEmpty(),
                "size constraints are not applied to numbers");

    // ---- things that are not mismatches -------------------------------------------------------
    Vb missing; missing.set_exception_status(sNMP_SYNTAX_NOSUCHINSTANCE);
    c = run(&intObject, missing);
    ok &= check(c.receivedType == "noSuchInstance" && c.rawValue == "noSuchInstance" && c.warnings.isEmpty(),
                "noSuchInstance is reported as such, not as a type mismatch");
    Vb endOfView; endOfView.set_exception_status(sNMP_SYNTAX_ENDOFMIBVIEW);
    ok &= check(run(&intObject, endOfView).receivedType == "endOfMibView", "endOfMibView is named");
    c = run(nullptr, withValue(Gauge32(9)));
    ok &= check(c.warnings.isEmpty() && c.receivedType == "Gauge32" && c.rawValue == "9",
                "an object outside the active MIB Profile shows the raw value and no warnings");
    auto unknownBase = node("odd", MibEnvironmentBaseType::Unknown, "");
    ok &= check(run(&unknownBase, withValue(Gauge32(9))).warnings.isEmpty(),
                "an object whose type the MIB does not define makes no claims");

    ok &= check(SnmpSyntaxName(sNMP_SYNTAX_TIMETICKS) == "TimeTicks" && SnmpSyntaxName(sNMP_SYNTAX_OID) == "OBJECT IDENTIFIER" &&
                SnmpSyntaxName(0x7e).startsWith("unknown type"), "syntax names");
    return ok ? 0 : 1;
}
