#include "fakesnmpagent.h"
#include "snmpquerytext.h"
#include "snmpsingleasyncrunner.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QRegularExpression>
#include <QThread>
#include <QTimer>
#include <atomic>
#include <iostream>

namespace
{
int failures = 0;
void check(bool value, const char *message)
{
    if (!value) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}

SnmpRequestConfig config(SnmpRequestVersion version = SnmpRequestVersion::V2c)
{
    SnmpRequestConfig value;
    value.version = version;
    value.address = "192.0.2.55";
    value.port = "161";
    value.readCommunity = "single-test";
    value.maxRepetitions = 7;
    value.nonRepeaters = 1;
    return value;
}

QVector<Vb> varbinds(std::initializer_list<const char *> oids)
{
    QVector<Vb> list;
    for (const char *oid : oids) list.append(Vb{Oid(oid)});
    return list;
}

SnmpSingleResult run(ISnmpTransport &transport, SnmpRequestOperation operation,
                     const QVector<Vb> &list)
{
    return SnmpSingleOperation(SnmpRequestContext(transport.config(), operation), list).execute(transport);
}

SnmpTransportResult response(std::initializer_list<const char *> oids, int syntax = sNMP_SYNTAX_INT32)
{
    SnmpTransportResult result;
    result.status = SnmpOperationStatus::Success;
    result.transportStatus = SNMP_CLASS_SUCCESS;
    for (const char *oid : oids) {
        Vb vb{Oid(oid)};
        if (syntax == sNMP_SYNTAX_INT32) vb.set_value(7);
        else vb.set_exception_status(syntax);
        result.pdu += vb;
    }
    return result;
}

SnmpTransportResult deviceError(int code, int index, std::initializer_list<const char *> oids)
{
    SnmpTransportResult result = response(oids);
    result.status = SnmpOperationStatus::SnmpError;
    result.snmpErrorStatus = code;
    result.snmpErrorIndex = index;
    result.pdu.set_error_status(code);
    result.pdu.set_error_index(index);
    return result;
}

SnmpTransportResult failure(SnmpOperationStatus status, int code)
{
    SnmpTransportResult result;
    result.status = status;
    result.transportStatus = code;
    return result;
}

SnmpObjectResolver unresolved()
{
    return [](const Oid &) { return MibResolvedObject{}; };
}

// A resolver that knows every OID as the same (invented) object.
SnmpObjectResolver resolved(const MibEnvironmentNodeRecord &node)
{
    return [&node](const Oid &) { MibResolvedObject object; object.node = &node; return object; };
}

void operations()
{
    ScriptedSnmpTransport scripted(config());
    scripted.append(response({"1.3.6.1.2.1.1.1.0"}));
    scripted.append(response({"1.3.6.1.2.1.1.2.0"}));
    scripted.append(response({"1.3.6.1.2.1.1.3.0", "1.3.6.1.2.1.1.4.0"}));
    SnmpSingleResult get = run(scripted, SnmpRequestOperation::Get, varbinds({"1.3.6.1.2.1.1.1.0"}));
    SnmpSingleResult next = run(scripted, SnmpRequestOperation::GetNext, varbinds({"1.3.6.1.2.1.1.1"}));
    SnmpSingleResult bulk = run(scripted, SnmpRequestOperation::GetBulk, varbinds({"1.3.6.1.2.1.1"}));
    check(get.status == SnmpOperationStatus::Success && get.pdu.get_vb_count() == 1 &&
              next.status == SnmpOperationStatus::Success && bulk.pdu.get_vb_count() == 2,
          "each operation returns the response");
    check(scripted.requests().size() == 3 &&
              scripted.requests()[0].operation == SnmpTransportOperation::Get &&
              scripted.requests()[1].operation == SnmpTransportOperation::GetNext &&
              scripted.requests()[2].operation == SnmpTransportOperation::GetBulk,
          "Get, GetNext and GetBulk map to their transport operations");
    check(scripted.requests()[2].nonRepeaters == 1 && scripted.requests()[2].maxRepetitions == 7,
          "a single GetBulk uses the global non-repeaters and max-repetitions");

    // The varbind-list variants send the list in order.
    ScriptedSnmpTransport list(config());
    list.append(response({"1.3.6.1.2.1.1.1.0", "1.3.6.1.2.1.1.5.0", "1.3.6.1.2.1.1.3.0"}));
    run(list, SnmpRequestOperation::Get, varbinds({"1.3.6.1.2.1.1.1.0", "1.3.6.1.2.1.1.5.0", "1.3.6.1.2.1.1.3.0"}));
    Vb second;
    list.requests().first().pdu.get_vb(second, 1);
    check(list.requests().first().pdu.get_vb_count() == 3 && second.get_oid() == Oid("1.3.6.1.2.1.1.5.0"),
          "a varbind list is sent whole and in order");
    ScriptedSnmpTransport bulkList(config());
    bulkList.append(response({"1.3.6.1.2.1.1.1.0", "1.3.6.1.2.1.1.5.0"}));
    run(bulkList, SnmpRequestOperation::GetBulk, varbinds({"1.3.6.1.2.1.1.1", "1.3.6.1.2.1.1.4"}));
    check(bulkList.requests().first().pdu.get_vb_count() == 2 &&
              bulkList.requests().first().operation == SnmpTransportOperation::GetBulk,
          "GetBulk accepts a varbind list");
}

void failuresAndErrors()
{
    // The exception syntaxes pass through to the presenter.
    ScriptedSnmpTransport exceptions(config());
    exceptions.append(response({"1.3.6.1.2.1.1.9.0"}, sNMP_SYNTAX_NOSUCHINSTANCE));
    SnmpSingleResult result = run(exceptions, SnmpRequestOperation::Get, varbinds({"1.3.6.1.2.1.1.9.0"}));
    Vb vb; result.pdu.get_vb(vb, 0);
    check(result.status == SnmpOperationStatus::Success && vb.get_syntax() == sNMP_SYNTAX_NOSUCHINSTANCE,
          "noSuchInstance is returned as data for the presenter");

    // A PDU error keeps its error-status and error-index.
    ScriptedSnmpTransport pduError(config(SnmpRequestVersion::V1));
    pduError.append(deviceError(SNMP_ERROR_NO_SUCH_NAME, 2, {"1.3.6.1.2.1.1.1.0", "1.3.6.1.2.1.1.9.0"}));
    result = run(pduError, SnmpRequestOperation::Get, varbinds({"1.3.6.1.2.1.1.1.0", "1.3.6.1.2.1.1.9.0"}));
    check(result.status == SnmpOperationStatus::SnmpError && result.snmpErrorStatus == SNMP_ERROR_NO_SUCH_NAME &&
              result.snmpErrorIndex == 2 && result.pdu.get_error_index() == 2 && result.pdu.get_vb_count() == 2,
          "a device error keeps its error-status, error-index and varbinds");

    ScriptedSnmpTransport timeout(config());
    timeout.append(failure(SnmpOperationStatus::Timeout, SNMP_CLASS_TIMEOUT));
    check(run(timeout, SnmpRequestOperation::Get, varbinds({"1.3.6.1.2.1.1.1.0"})).status ==
              SnmpOperationStatus::Timeout, "a timeout is reported");

    SnmpRequestConfig v3 = config(SnmpRequestVersion::V3);
    v3.securityName = "operator";
    ScriptedSnmpTransport report(v3);
    SnmpTransportResult refused = response({"1.3.6.1.6.3.15.1.1.3.0"});   // usmStatsUnknownUserNames
    refused.pdu.set_type(sNMP_PDU_REPORT);
    report.append(refused);
    result = run(report, SnmpRequestOperation::Get, varbinds({"1.3.6.1.2.1.1.1.0"}));
    check(result.status == SnmpOperationStatus::SnmpError && result.problem.contains("operator"),
          "an SNMPv3 REPORT is explained, not presented as data");

    struct Case { int code; const char *cause; };
    for (const Case &c : {Case{SNMPv3_USM_DECRYPTION_ERROR, "privacy"},
                          Case{SNMPv3_USM_UNKNOWN_SECURITY_NAME, "credentials"},
                          Case{SNMPv3_USM_AUTHENTICATION_FAILURE, "authentication"}}) {
        ScriptedSnmpTransport local(v3);
        local.append(failure(SnmpOperationStatus::TransportFailure, c.code));
        result = run(local, SnmpRequestOperation::Get, varbinds({"1.3.6.1.2.1.1.1.0"}));
        check(result.status == SnmpOperationStatus::TransportFailure && result.transportStatus == c.code &&
                  result.problem.contains(c.cause) && !result.problem.contains("error 0"),
              "a local SNMPv3 failure names its cause");
        const SnmpQueryText text = FormatSingleResult(result, unresolved(), false);
        check(text.paragraphs.size() == 1 && text.paragraphs.first().contains(c.cause) &&
                  text.paragraphs.first().contains("color=red") && text.statusHtml.isEmpty(),
              "and the query pane and status line show it");
    }
}

QString plain(const QString &html)
{
    QString text = html;
    text.remove(QRegularExpression("<[^>]*>"));
    return text;
}

void presentation()
{
    // A normal answer.
    ScriptedSnmpTransport scripted(config());
    scripted.append(response({"1.3.6.1.2.1.1.1.0", "1.3.6.1.2.1.1.3.0"}));
    SnmpSingleResult result = run(scripted, SnmpRequestOperation::Get, varbinds({"1.3.6.1.2.1.1.1.0", "1.3.6.1.2.1.1.3.0"}));
    SnmpQueryText text = FormatSingleResult(result, unresolved(), false);
    check(text.objects == 2 && text.requests == 1 && text.rows.size() == 2 && text.paragraphs.size() == 1 &&
              text.paragraphs.first().startsWith("1: 1.3.6.1.2.1.1.1.0    <font color=blue>7</font><br>2: 1.3.6.1.2.1.1.3.0") &&
              text.paragraphs.first().contains("-----SNMP query finished-----") &&
              text.paragraphs.first().contains("Total # of Requests = 1") &&
              text.paragraphs.first().contains("Total # of Objects = 2") &&
              text.statusHtml == "Finished: 2 object(s) in 1 request(s).",
          "a normal answer: numbered object lines, totals and the status line");
    text = FormatSingleResult(result, unresolved(), true);
    check(text.statusHtml == "Stopped after 2 object(s) in 1 request(s)." &&
              text.paragraphs.last().contains("query stopped") && text.paragraphs.last().contains("color=red"),
          "a request the user stopped says so");

    // Ten objects flush a paragraph, as the Query pane always did.
    ScriptedSnmpTransport many(config());
    SnmpTransportResult twelve = response({});
    for (int i = 1; i <= 12; ++i) {
        Vb vb{Oid((std::string("1.3.6.1.2.1.1.") + std::to_string(i) + ".0").c_str())};
        vb.set_value(i);
        twelve.pdu += vb;
    }
    many.append(twelve);
    text = FormatSingleResult(run(many, SnmpRequestOperation::GetBulk, varbinds({"1.3.6.1.2.1.1"})), unresolved(), false);
    check(text.paragraphs.size() == 2 && text.objects == 12 && text.rows.size() == 12 &&
              !text.paragraphs.first().endsWith("<br>") && text.paragraphs.first().contains("10: "),
          "every tenth object ends a Query pane paragraph");

    // noSuchObject / noSuchInstance, per varbind, for objects the MIB does not know ...
    ScriptedSnmpTransport missing(config());
    SnmpTransportResult mixed = response({"1.3.6.1.2.1.1.1.0"});
    Vb absent{Oid("1.3.6.1.2.1.1.9.0")}; absent.set_exception_status(sNMP_SYNTAX_NOSUCHINSTANCE);
    Vb never{Oid("1.3.6.1.2.1.1.8.0")}; never.set_exception_status(sNMP_SYNTAX_NOSUCHOBJECT);
    Vb after{Oid("1.3.6.1.2.1.1.5.0")}; after.set_value(3);
    mixed.pdu += absent; mixed.pdu += never; mixed.pdu += after;
    missing.append(mixed);
    result = run(missing, SnmpRequestOperation::Get, varbinds({"a"}));
    text = FormatSingleResult(result, unresolved(), false);
    const QString unknownText = plain(text.paragraphs.first());
    check(unknownText.contains("ERROR on varbind #2: 1.3.6.1.2.1.1.9.0") && unknownText.contains("No Such Instance") &&
              unknownText.contains("ERROR on varbind #3: 1.3.6.1.2.1.1.8.0") && unknownText.contains("No Such Object") &&
              unknownText.contains("4: 1.3.6.1.2.1.1.5.0") && text.objects == 4,
          "no-such-object and no-such-instance are named per varbind, and the rest is still listed");

    // ... and for objects the MIB knows, where the first one ends the answer.
    MibEnvironmentNodeRecord node;
    node.name = "sysDescr"; node.baseType = MibEnvironmentBaseType::OctetString;
    text = FormatSingleResult(result, resolved(node), false);
    const QString knownText = plain(text.paragraphs.first());
    check(knownText.contains("ERROR on varbind #2") && knownText.contains("No Such Instance") &&
              !knownText.contains("No Such Object") && knownText.contains("Total # of Objects = 2"),
          "for a known object the answer ends at the first missing varbind");

    // A PDU error highlights the varbind its error-index names.
    ScriptedSnmpTransport pduError(config(SnmpRequestVersion::V1));
    pduError.append(deviceError(SNMP_ERROR_NO_SUCH_NAME, 2, {"1.3.6.1.2.1.1.1.0", "1.3.6.1.2.1.1.9.0", "1.3.6.1.2.1.1.5.0"}));
    result = run(pduError, SnmpRequestOperation::Get, varbinds({"a"}));
    text = FormatSingleResult(result, unresolved(), false);
    const QString errorText = plain(text.paragraphs.first());
    check(errorText.contains("ERROR on varbind #2: 1.3.6.1.2.1.1.9.0") && errorText.contains("Variable does not exist") &&
              !errorText.contains("1: 1.3.6.1.2.1.1.1.0") && text.rows.size() == 2 && text.objects == 3,
          "a device error highlights the varbind its error-index names; earlier varbinds are not repeated");
    text = FormatSingleResult(result, resolved(node), false);
    check(plain(text.paragraphs.first()).contains("ERROR on varbind #2") && text.rows.size() == 1 && text.objects == 2,
          "for a known object the answer ends at the varbind the error names");

    // An error with no varbind to point at.
    SnmpTransportResult noIndex = deviceError(SNMP_ERROR_GENERAL_VB_ERR, 0, {"1.3.6.1.2.1.1.1.0"});
    ScriptedSnmpTransport general(config());
    general.append(noIndex);
    text = FormatSingleResult(run(general, SnmpRequestOperation::Get, varbinds({"a"})), unresolved(), false);
    check(text.paragraphs.size() == 1 && text.paragraphs.first().contains("color=red") && text.rows.isEmpty() &&
              !text.paragraphs.first().contains("error 0"),
          "a device error without an index is shown as an error");

    // Timeout, nothing returned, endOfMibView.
    ScriptedSnmpTransport timeout(config());
    timeout.append(failure(SnmpOperationStatus::Timeout, SNMP_CLASS_TIMEOUT));
    text = FormatSingleResult(run(timeout, SnmpRequestOperation::Get, varbinds({"a"})), unresolved(), false);
    check(text.paragraphs.size() == 1 && text.paragraphs.first().contains("Timeout: no response from the device") &&
              text.statusHtml.isEmpty(), "a timeout is shown in the Query pane and the status line");
    ScriptedSnmpTransport empty(config());
    SnmpTransportResult nothing; nothing.status = SnmpOperationStatus::Success; empty.append(nothing);
    text = FormatSingleResult(run(empty, SnmpRequestOperation::Get, varbinds({"a"})), unresolved(), false);
    check(text.paragraphs.first().contains("Pdu is empty"), "an answer without varbinds is reported");
    ScriptedSnmpTransport end(config());
    end.append(response({"1.3.6.1.2.1.1.9.0"}, sNMP_SYNTAX_ENDOFMIBVIEW));
    text = FormatSingleResult(run(end, SnmpRequestOperation::GetNext, varbinds({"a"})), unresolved(), false);
    check(text.objects == 0 && text.rows.isEmpty() && text.statusHtml == "Finished: 0 object(s) in 1 request(s).",
          "endOfMibView shows nothing and finishes");
    ScriptedSnmpTransport dead(config());
    dead.append(SnmpTransportResult{});
    text = FormatSingleResult(run(dead, SnmpRequestOperation::Get, varbinds({"a"})), unresolved(), false);
    check(text.paragraphs.first().contains("No response received"), "a failed send says no response was received");

    // How a Walk ends uses the same pieces.
    SnmpWalkResult walked;
    walked.status = SnmpOperationStatus::Complete; walked.objects = 3; walked.requests = 2;
    text = FormatWalkEnd(walked);
    check(text.statusHtml == "Finished: 3 object(s) in 2 request(s)." && text.objects == 3 &&
              text.paragraphs.last().contains("Total # of Requests = 2"), "the end of a Walk");
    walked.problem = "not in increasing order";
    check(FormatWalkEnd(walked).statusHtml.startsWith("Ended early after 3 object(s) in 2 request(s).") &&
              FormatWalkEnd(walked).statusHtml.contains("color=red"), "a Walk that ended early says why");
    walked.status = SnmpOperationStatus::Cancelled; walked.problem.clear();
    check(FormatWalkEnd(walked).statusHtml.startsWith("Stopped after 3 object(s)"), "a stopped Walk");
    walked.status = SnmpOperationStatus::SnmpError; walked.snmpErrorStatus = SNMP_ERROR_TOO_BIG;
    check(FormatWalkEnd(walked).paragraphs.last().contains("SNMP error 1"), "a Walk refused by the device");
}

QVector<Vb> setList(std::initializer_list<std::pair<const char *, int>> items)
{
    QVector<Vb> list;
    for (const auto &item : items) {
        Vb vb{Oid(item.first)};
        vb.set_value(item.second);
        list.append(vb);
    }
    return list;
}

void sets()
{
    // The request carries the values to write.
    ScriptedSnmpTransport scripted(config());
    scripted.append(response({"1.3.6.1.4.1.999.1.0", "1.3.6.1.4.1.999.2.0"}));
    SnmpSingleResult result = run(scripted, SnmpRequestOperation::Set,
                                  setList({{"1.3.6.1.4.1.999.1.0", 11}, {"1.3.6.1.4.1.999.2.0", 22}}));
    int written = 0;
    Vb second;
    scripted.requests().first().pdu.get_vb(second, 1);
    second.get_value(written);
    check(scripted.requests().first().operation == SnmpTransportOperation::Set &&
              scripted.requests().first().pdu.get_vb_count() == 2 && written == 22,
          "a Set sends every varbind with its value");

    // A successful Set lists what the device now holds and ends with its own finish line.
    SnmpQueryText text = FormatSetResult(result, unresolved());
    check(text.paragraphs.size() == 1 && text.statusHtml.isEmpty() && text.objects == 2 &&
              text.paragraphs.first().startsWith("1: 1.3.6.1.4.1.999.1.0    <font color=blue>7</font><br>2: ") &&
              text.paragraphs.first().endsWith("-----SNMP set finished-----<br>") &&
              !text.paragraphs.first().contains("Total #"),
          "a successful Set lists the varbinds and ends with 'SNMP set finished'");

    // Each common Set error names the failing varbind and is explained in plain English.
    struct Case { int code; const char *name; const char *meaning; };
    MibEnvironmentNodeRecord node;
    node.name = "sysName"; node.baseType = MibEnvironmentBaseType::OctetString;
    for (const Case &c : {Case{SNMP_ERROR_NOT_WRITEABLE, "notWritable", "cannot be changed"},
                          Case{SNMP_ERROR_WRONG_TYPE, "wrongType", "wrong data type"},
                          Case{SNMP_ERROR_WRONG_VALUE, "wrongValue", "does not accept this value"},
                          Case{SNMP_ERROR_NO_ACCESS, "noAccess", "not allowed to change"},
                          Case{SNMP_ERROR_INCONSIST_VAL, "inconsistentValue", "conflicts with"}}) {
        ScriptedSnmpTransport refused(config());
        refused.append(deviceError(c.code, 2, {"1.3.6.1.4.1.999.1.0", "1.3.6.1.4.1.999.2.0", "1.3.6.1.4.1.999.3.0"}));
        result = run(refused, SnmpRequestOperation::Set, setList({{"a", 1}, {"b", 2}, {"c", 3}}));
        check(result.status == SnmpOperationStatus::SnmpError && result.snmpErrorStatus == c.code &&
                  result.snmpErrorIndex == 2, "a Set error keeps its error-status and error-index");
        text = FormatSetResult(result, unresolved());
        const QString unknown = plain(text.paragraphs.first());
        check(unknown.contains("ERROR on varbind #2: 1.3.6.1.4.1.999.2.0") && unknown.contains(c.name) &&
                  unknown.contains(c.meaning) && unknown.contains(QString("(SNMP error %1)").arg(c.code)) &&
                  !unknown.contains("SNMP: ") && unknown.contains("3: 1.3.6.1.4.1.999.3.0") &&
                  unknown.endsWith("-----SNMP set finished-----") && text.statusHtml.isEmpty(),
              "a Set error names the failing varbind and the status in plain English");
        text = FormatSetResult(result, resolved(node));
        check(plain(text.paragraphs.first()).contains("ERROR on varbind #2") &&
                  plain(text.paragraphs.first()).contains(c.name) && text.objects == 2,
              "and for a known object the answer ends at the failing varbind");
    }
    // A Get keeps the wording it always had.
    ScriptedSnmpTransport get(config(SnmpRequestVersion::V1));
    get.append(deviceError(SNMP_ERROR_NO_SUCH_NAME, 1, {"1.3.6.1.4.1.999.1.0"}));
    check(plain(FormatSingleResult(run(get, SnmpRequestOperation::Get, varbinds({"a"})), unresolved(), false)
                    .paragraphs.first()).contains("Variable does not exist"),
          "Get errors are worded as before");

    // An error that names no varbind.
    ScriptedSnmpTransport noIndex(config());
    noIndex.append(deviceError(SNMP_ERROR_NOT_WRITEABLE, 0, {"1.3.6.1.4.1.999.1.0"}));
    text = FormatSetResult(run(noIndex, SnmpRequestOperation::Set, setList({{"a", 1}})), unresolved());
    check(text.paragraphs.size() == 1 && plain(text.paragraphs.first()).contains("notWritable") &&
              text.paragraphs.first().contains("color=red") && text.rows.isEmpty(),
          "a Set error without an index is explained too");

    // Timeout, REPORT and the item 2 USM codes.
    ScriptedSnmpTransport timeout(config());
    timeout.append(failure(SnmpOperationStatus::Timeout, SNMP_CLASS_TIMEOUT));
    text = FormatSetResult(run(timeout, SnmpRequestOperation::Set, setList({{"a", 1}})), unresolved());
    check(text.paragraphs.first().contains("Timeout: no response from the device"), "a Set timeout");
    SnmpRequestConfig v3 = config(SnmpRequestVersion::V3);
    v3.securityName = "operator";
    ScriptedSnmpTransport report(v3);
    SnmpTransportResult refused = response({"1.3.6.1.6.3.15.1.1.5.0"});   // usmStatsWrongDigests
    refused.pdu.set_type(sNMP_PDU_REPORT);
    report.append(refused);
    text = FormatSetResult(run(report, SnmpRequestOperation::Set, setList({{"a", 1}})), unresolved());
    check(text.paragraphs.first().contains("operator") && text.paragraphs.first().contains("authentication"),
          "a Set refused with an SNMPv3 REPORT is explained");
    struct Usm { int code; const char *cause; };
    for (const Usm &u : {Usm{SNMPv3_USM_DECRYPTION_ERROR, "privacy"},
                         Usm{SNMPv3_USM_UNKNOWN_SECURITY_NAME, "credentials"},
                         Usm{SNMPv3_USM_AUTHENTICATION_FAILURE, "authentication"}}) {
        ScriptedSnmpTransport local(v3);
        local.append(failure(SnmpOperationStatus::TransportFailure, u.code));
        text = FormatSetResult(run(local, SnmpRequestOperation::Set, setList({{"a", 1}})), unresolved());
        check(text.paragraphs.first().contains(u.cause) && !text.paragraphs.first().contains("error 0"),
              "a local SNMPv3 failure in a Set names its cause");
    }
}

class SlowAgent final : public ISnmpTransport
{
public:
    SlowAgent(const SnmpRequestConfig &config, unsigned long delayMs) : agent(config), delay(delayMs) {}
    FakeSnmpAgent &inner() { return agent; }
    SnmpTransportResult execute(const SnmpTransportRequest &request) override
    {
        QThread::msleep(delay);
        return agent.execute(request);
    }
    const SnmpRequestConfig &config() const override { return agent.config(); }
private:
    FakeSnmpAgent agent;
    unsigned long delay;
};

class BlockedTransport final : public ISnmpTransport
{
public:
    BlockedTransport(const SnmpRequestConfig &config, std::shared_ptr<std::atomic<bool>> started,
                     std::shared_ptr<std::atomic<bool>> done, unsigned long blockMs)
        : requestConfig(config), started(std::move(started)), done(std::move(done)), blockMs(blockMs) {}
    SnmpTransportResult execute(const SnmpTransportRequest &) override
    {
        *started = true;
        QThread::msleep(blockMs);
        *done = true;
        return failure(SnmpOperationStatus::Timeout, SNMP_CLASS_TIMEOUT);
    }
    const SnmpRequestConfig &config() const override { return requestConfig; }
private:
    SnmpRequestConfig requestConfig;
    std::shared_ptr<std::atomic<bool>> started, done;
    unsigned long blockMs;
};

void spin(const std::function<bool()> &until, int limitMs)
{
    QEventLoop loop;
    QTimer poll; QObject::connect(&poll, &QTimer::timeout, &loop, [&]() { if (until()) loop.quit(); });
    QTimer limit; limit.setSingleShot(true); QObject::connect(&limit, &QTimer::timeout, &loop, &QEventLoop::quit);
    poll.start(5); limit.start(limitMs);
    if (!until()) loop.exec();
}

void asyncRequests()
{
    SnmpSingleAsyncRunner runner;
    int completions = 0;
    SnmpSingleResult last;
    QObject::connect(&runner, &SnmpSingleAsyncRunner::completed, [&](const SnmpSingleResult &result) {
        ++completions; last = result;
    });

    auto *slow = new SlowAgent(config(), 80);
    slow->inner().addInteger("1.3.6.1.2.1.1.7.0", 5);
    check(runner.start(SnmpRequestContext(slow->config(), SnmpRequestOperation::Get), varbinds({"1.3.6.1.2.1.1.7.0"}),
                       std::unique_ptr<ISnmpTransport>(slow)), "the runner starts");
    QElapsedTimer timer; timer.start();
    check(runner.isBusy(), "it is busy while the request is out");
    auto *extra = new FakeSnmpAgent(config());
    check(!runner.start(SnmpRequestContext(extra->config(), SnmpRequestOperation::Get), varbinds({"1.3.6.1.2.1.1.7.0"}),
                        std::unique_ptr<ISnmpTransport>(extra)), "a second request is refused meanwhile");
    spin([&]() { return completions == 1; }, 10000);
    check(completions == 1 && last.status == SnmpOperationStatus::Success && last.pdu.get_vb_count() == 1 &&
              !runner.isBusy(), "the result arrives on the owner's thread");

    // A new request may start in the handler of the previous result.
    int chained = 0;
    QObject::connect(&runner, &SnmpSingleAsyncRunner::completed, [&](const SnmpSingleResult &) {
        if (++chained == 1) {
            auto *second = new FakeSnmpAgent(config());
            second->addInteger("1.3.6.1.2.1.1.7.0", 6);
            runner.start(SnmpRequestContext(second->config(), SnmpRequestOperation::Get), varbinds({"1.3.6.1.2.1.1.7.0"}),
                         std::unique_ptr<ISnmpTransport>(second));
        }
    });
    auto *first = new FakeSnmpAgent(config());
    first->addInteger("1.3.6.1.2.1.1.7.0", 5);
    runner.start(SnmpRequestContext(first->config(), SnmpRequestOperation::Get), varbinds({"1.3.6.1.2.1.1.7.0"}),
                 std::unique_ptr<ISnmpTransport>(first));
    spin([&]() { return chained >= 2; }, 10000);
    check(chained == 2, "a request can start in the handler of the previous result");
    spin([&]() { return !runner.isRunning(); }, 5000);

    // Shutting down during a request that does not return must not hang.
    auto started = std::make_shared<std::atomic<bool>>(false);
    auto done = std::make_shared<std::atomic<bool>>(false);
    QElapsedTimer shutdown;
    {
        SnmpSingleAsyncRunner stuck;
        stuck.setShutdownWaitMs(200);
        stuck.start(SnmpRequestContext(config(), SnmpRequestOperation::Get), varbinds({"1.3.6.1.2.1.1.7.0"}),
                    std::make_unique<BlockedTransport>(config(), started, done, 1500));
        spin([&]() { return started->load(); }, 5000);
        shutdown.start();
    }
    check(shutdown.elapsed() < 1200 && !done->load(),
          "destroying a runner whose request is stuck returns after the bounded wait");
    spin([&]() { return done->load(); }, 5000);   // let the abandoned worker finish before exit
    QThread::msleep(200);
}
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    operations();
    failuresAndErrors();
    presentation();
    sets();
    asyncRequests();
    if (failures) { std::cerr << failures << " check(s) failed\n"; return 1; }
    std::cout << "single request tests passed\n";
    return 0;
}
