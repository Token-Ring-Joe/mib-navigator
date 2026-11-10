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
    for (int syntax : {sNMP_SYNTAX_NULL, sNMP_SYNTAX_NOSUCHOBJECT,
                       sNMP_SYNTAX_NOSUCHINSTANCE, sNMP_SYNTAX_ENDOFMIBVIEW}) {
        Vb exception;
        exception.set_syntax(syntax);
        ok &= check(RenderMibValue({}, exception) == SnmpSyntaxName(syntax),
                    "NULL and exception values retain readable names without MIB metadata");
        MibResolvedObject resolved; resolved.node = &status;
        ok &= check(RenderMibValue(resolved, exception) == SnmpSyntaxName(syntax),
                    "NULL and exception values do not acquire enum interpretation");
    }
    // Shared presentation/checking, with raw values retained on every warning.
    const auto render = [](const MibEnvironmentNodeRecord &n, const Vb &vb) {
        MibResolvedObject object; object.node = &n; return RenderMibValue(object, vb);
    };
    auto scaled = intObject; scaled.displayHint = "d-2";
    ok &= check(render(scaled, withValue(SnmpInt32(-3))) == "-0.03", "integer decimal hints retain sign and implied decimals");
    scaled.displayHint = "x";
    ok &= check(render(scaled, withValue(SnmpInt32(-255))) == "-ff", "negative integer hexadecimal hint");
    auto hinted = stringObject; hinted.displayHint = "2d-1d";
    ok &= check(render(hinted, withValue(OctetStr::from_hex_string("010002"))) == "256-2",
                "multi-octet hints use big endian numeric groups");
    hinted.displayHint = "*1x:/1a";
    ok &= check(render(hinted, withValue(OctetStr::from_hex_string("020a0b5a"))) == "0a:0b/Z",
                "repeated hints suppress the separator before their terminator");
    hinted.displayHint = "1x:";
    ok &= check(render(hinted, withValue(OctetStr::from_hex_string("000aff"))) == "00:0a:ff",
                "existing MAC/physical address rendering keeps leading zeroes");
    hinted.displayHint = "255a";
    c = run(&hinted, withValue(OctetStr::from_hex_string("41ff")));
    ok &= check(anyWarning(c, "ASCII") && !c.rawValue.isEmpty(), "ASCII hints warn without dropping raw data");
    hinted.displayHint = "255t";
    const QByteArray utf8 = QString::fromUtf8("réseau").toUtf8();
    Vb utf8Value = withValue(OctetStr(reinterpret_cast<const unsigned char *>(utf8.constData()), utf8.size()));
    ok &= check(render(hinted, utf8Value) == QString::fromUtf8("réseau") && run(&hinted, utf8Value).warnings.isEmpty(),
                "UTF-8 hints render complete characters without warnings");
    ok &= check(anyWarning(run(&hinted, withValue(OctetStr::from_hex_string("c080"))), "UTF-8"),
                "overlong UTF-8 warns");
    ok &= check(render(hinted, withValue(OctetStr::from_hex_string("41c3"))) == "A",
                "RFC hint rendering discards an incomplete final UTF-8 character");
    hinted.displayHint = "1a:1t";
    ok &= check(render(hinted, withValue(OctetStr::from_hex_string("41c3"))) == "A",
                "discarded final UTF-8 cannot leave a trailing hint separator");
    hinted.displayHint = "*1a,/";
    ok &= check(render(hinted, withValue(OctetStr::from_hex_string("0000"))).isEmpty(),
                "zero-repeat groups cannot leave a trailing repeat terminator");
    ok &= check(render(hinted, withValue(OctetStr::from_hex_string("0141000142"))) == "A//B",
                "zero-repeat punctuation remains visible when actual display text follows");
    for (const QString &hint : {QString("0a"), QString("9d"), QString("*"), QString(600, '9')}) {
        hinted.displayHint = hint;
        ok &= check(render(hinted, withValue(OctetStr("plain"))) == "plain", "unsupported or excessive hints retain received data");
    }
    auto displayString = stringObject; displayString.textualConventionAncestry = {"SNMPv2-TC::DisplayString"};
    ok &= check(run(&displayString, withValue(OctetStr(reinterpret_cast<const unsigned char *>("line\r\nnext\r\0"), 12))).warnings.isEmpty(),
                "DisplayString accepts NVT newline and carriage-return sequences");
    ok &= check(anyWarning(run(&displayString, withValue(OctetStr("line\rX"))), "carriage returns"),
                "DisplayString warns about invalid NVT carriage-return sequences");
    displayString.textualConventionAncestry = {"VENDOR-MIB::DisplayString"};
    displayString.syntaxName = "DisplayString";
    ok &= check(run(&displayString, withValue(OctetStr::from_hex_string("ff"))).warnings.isEmpty(),
                "a vendor type sharing a standard's short name does not acquire its rules");
    auto date = stringObject; date.textualConventionId = "SNMPv2-TC::DateAndTime";
    date.displayHint = "2d-1d-1d,1d:1d:1d.1d,1a1d:1d";
    const auto dateValue = withValue(OctetStr::from_hex_string("07e8021d173b3c092b0d3b"));
    ok &= check(run(&date, dateValue).warnings.isEmpty() && render(date, dateValue) == "2024-2-29,23:59:60.9,+13:59",
                "DateAndTime accepts leap day, leap second and its optional UTC offset");
    ok &= check(run(&date, withValue(OctetStr::from_hex_string("0000021d00000000"))).warnings.isEmpty(),
                "DateAndTime year zero remains permitted by its octet specification");
    for (const char *hex : {"07e7021d00000000", "07e80d0100000000", "07e8010120000000", "07e80101000000002a0100", "01"})
        ok &= check(anyWarning(run(&date, withValue(OctetStr::from_hex_string(hex))), "DateAndTime"),
                    "DateAndTime flags invalid fields and lengths");
    auto rowStatus = status; rowStatus.textualConventionId = "SNMPv2-TC::RowStatus";
    rowStatus.namedValues.clear();
    ok &= check(anyWarning(run(&rowStatus, withValue(SnmpInt32(4))), "SET action") &&
                run(&rowStatus, withValue(SnmpInt32(3))).warnings.isEmpty(), "RowStatus distinguishes readable states from SET-only actions");
    auto bounded64 = counter64Object;
    MibEnvironmentConstraint full64; full64.minimum = unsignedValue(0); full64.maximum = unsignedValue(UINT64_MAX - 1);
    bounded64.constraints = {full64};
    ok &= check(anyWarning(run(&bounded64, withValue(Counter64::ll_to_c64(UINT64_MAX))), "Out of range") &&
                run(&bounded64, withValue(Counter64::ll_to_c64(UINT64_MAX - 1))).warnings.isEmpty(),
                "Counter64 constraints retain all 64 bits");
    auto namedBits = bitsObject; namedBits.namedValues = {{"first", unsignedValue(0)}};
    ok &= check(run(&namedBits, withValue(OctetStr::from_hex_string("80"))).warnings.isEmpty() &&
                anyWarning(run(&namedBits, withValue(OctetStr::from_hex_string("81"))), "BITS"),
                "undefined BITS positions warn while known positions remain clean");
    auto setRanges = twoRanges; setRanges.access = MibEnvironmentAccess::ReadWrite; QString setError;
    ok &= check(ValidateMibSetValue(&setRanges, sNMP_SYNTAX_INT32, "12", &setError) &&
                !ValidateMibSetValue(&setRanges, sNMP_SYNTAX_INT32, "7", &setError),
                "SET range alternatives are ORed like received-value checks");
    setRanges.baseType = MibEnvironmentBaseType::OctetString; setRanges.constraints = {range(1, 2, true), range(4, 5, true)};
    ok &= check(ValidateMibSetValue(&setRanges, sNMP_SYNTAX_OCTETS, "four", &setError) &&
                !ValidateMibSetValue(&setRanges, sNMP_SYNTAX_OCTETS, "sixsix", &setError), "SET size alternatives are ORed");
    ok &= check(ValidateMibSetValue(&setRanges, sNMP_SYNTAX_OCTETS, "four", &setError) && setError.isEmpty(),
                "successful SET validation clears an earlier rejection");
    hinted.displayHint = "1x:";
    const QByteArray manyOctets(65535, char(0xab));
    const auto largeValue = withValue(OctetStr(reinterpret_cast<const unsigned char *>(manyOctets.constData()), manyOctets.size()));
    ok &= check(render(hinted, largeValue).size() == 65535 * 3 - 1,
                "maximum supported octet payload renders completely with bounded output");
    quint32 seed = 0x12345678;
    const QString alphabet = "0123456789xdoat*:/,-";
    for (int iteration = 0; iteration < 1000; ++iteration) {
        QString hint;
        for (int i = 0; i < iteration % 30; ++i) { seed = seed * 1664525u + 1013904223u; hint += alphabet[int(seed % alphabet.size())]; }
        hinted.displayHint = hint;
        ok &= check(render(hinted, withValue(OctetStr::from_hex_string("000102ff1020"))).size() < 2048,
                    "deterministic malformed-hint corpus terminates with bounded presentation");
    }
    return ok ? 0 : 1;
}
