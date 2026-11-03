#include "csvformat.h"
#include "fakesnmpagent.h"
#include "snmptableoperation.h"

#include <QCoreApplication>
#include <QSet>
#include <iostream>
#include <map>
#include <set>
#include <string>

namespace
{
int failures = 0;
bool check(bool value, const char *message)
{
    if (!value) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
    return value;
}

const char *const kRow = "1.3.6.1.4.1.999.1.1";   // the row (entry) object of a made-up table

SnmpRequestConfig config(SnmpRequestVersion version = SnmpRequestVersion::V2c,
                         int maxRepetitions = 1)
{
    SnmpRequestConfig value;
    value.version = version;
    value.address = "192.0.2.55";
    value.port = "161";
    value.readCommunity = "public";
    value.maxRepetitions = maxRepetitions;
    return value;
}

SnmpTablePlan plan(int columns)
{
    SnmpTablePlan value;
    value.rowOid = Oid(kRow);
    for (int c = 1; c <= columns; ++c)
        value.columns.append({QString("col%1").arg(c),
                              Oid((std::string(kRow) + "." + std::to_string(c)).c_str())});
    return value;
}

void put(FakeSnmpAgent &agent, int column, const std::string &instance, long value)
{
    agent.addInteger((std::string(kRow) + "." + std::to_string(column) + "." + instance).c_str(),
                     value);
}

SnmpTableResult read(FakeSnmpAgent &agent, const SnmpTablePlan &tablePlan,
                     SnmpCancellationToken *token = nullptr)
{
    const SnmpRequestContext context(agent.config(), SnmpRequestOperation::Walk);
    SnmpCancellationToken local;
    return SnmpTableOperation(context, tablePlan).execute(agent, token ? *token : local);
}

long cellValue(const SnmpTableCell &cell)
{
    long value = -1;
    cell.varbind.get_value(value);
    return value;
}

void selectedRow()
{
    for (auto version : {SnmpRequestVersion::V1, SnmpRequestVersion::V2c, SnmpRequestVersion::V3}) {
        FakeSnmpAgent agent(config(version));
        auto p = plan(12); p.instance = "7.4.101.116.104.48";
        for (int c = 1; c <= 12; ++c) {
            if (c != 3) put(agent, c, p.instance.toStdString(), c * 10);
            put(agent, c, "8.4.101.116.104.48", 999);
        }
        agent.setMaxVarbinds(3);
        const auto result = read(agent, p);
        check(result.status == SnmpOperationStatus::Complete && result.rows.size() == 1,
              "row: one exact compound instance across v1/v2c/v3 and tooBig retries");
        if (result.rows.size() == 1) {
            check(result.rows[0].instance == p.instance && !result.rows[0].cells[2].available &&
                  cellValue(result.rows[0].cells[11]) == 120, "row: sparse cells stay missing, later columns still returned");
            for (const auto &cell : result.rows[0].cells)
                if (cell.available) check(QString::fromLatin1(cell.varbind.get_oid().get_printable()).endsWith("." + p.instance),
                                          "row: every cell belongs to the exact requested instance");
        }
        check(agent.countOf(SnmpTransportOperation::Get) == agent.requestCount(), "row: only exact GETs, no traversal");
        p.instance = "6";
        const auto absent = read(agent, p);
        check(absent.status == SnmpOperationStatus::Complete && absent.rows.isEmpty() && !absent.problem.isEmpty(),
              "row: vanished instance reports empty and never substitutes the next row");
        p.instance = "1..2"; const int before = agent.requestCount();
        check(read(agent, p).status == SnmpOperationStatus::TransportFailure && agent.requestCount() == before,
              "row: invalid suffix makes no requests");
    }
    auto p = plan(12); p.instance = "7";
    FakeSnmpAgent cancelled(config()); SnmpCancellationToken token; token.cancel();
    check(read(cancelled, p, &token).status == SnmpOperationStatus::Cancelled && !cancelled.requestCount(),
          "row: pre-cancellation makes no request");
    FakeSnmpAgent partial(config());
    for (int c = 1; c <= 12; ++c) put(partial, c, "7", c);
    partial.setFailure([](int n, SnmpTransportResult *r) {
        if (n != 2) return false;
        r->status = SnmpOperationStatus::Timeout; return true;
    });
    auto result = read(partial, p);
    check(result.status == SnmpOperationStatus::Timeout && result.rows.size() == 1 &&
          result.rows[0].cells[0].available && !result.rows[0].cells[11].available,
          "row: timeout keeps only confirmed cells");
    FakeSnmpAgent wrong(config());
    wrong.setFailure([](int, SnmpTransportResult *r) {
        r->status = SnmpOperationStatus::Success;
        r->pdu += Vb(Oid("1.3.6.1.4.1.999.1.1.1.8")); return true;
    });
    p = plan(1); p.instance = "7";
    result = read(wrong, p);
    check(result.status == SnmpOperationStatus::TransportFailure && result.rows.isEmpty() && !result.problem.isEmpty(),
          "row: a response for the wrong OID is rejected");
    FakeSnmpAgent report(config());
    report.setFailure([](int, SnmpTransportResult *r) {
        r->status = SnmpOperationStatus::Success;
        Vb vb(Oid("1.3.6.1.6.3.15.1.1.3.0")); vb.set_value(1UL); r->pdu += vb; return true;
    });
    const auto refused = read(report,p);
    check(refused.status == SnmpOperationStatus::SnmpError && refused.rows.isEmpty() && !refused.problem.isEmpty(),
          "row: SNMPv3 REPORT is a refusal, not a cell value");
    FakeSnmpAgent shortReply(config());
    shortReply.setFailure([](int, SnmpTransportResult *r) { r->status = SnmpOperationStatus::Success; return true; });
    check(read(shortReply,p).status == SnmpOperationStatus::TransportFailure, "row: truncated response is reported");
    FakeSnmpAgent during(config()); SnmpCancellationToken stop;
    during.setFailure([&](int, SnmpTransportResult *r) { stop.cancel(); r->status = SnmpOperationStatus::Success; return true; });
    check(read(during, p, &stop).status == SnmpOperationStatus::Cancelled, "row: cancellation during transport is retained");
}

void denseTable()
{
    for (const SnmpRequestVersion version : {SnmpRequestVersion::V1, SnmpRequestVersion::V2c}) {
        FakeSnmpAgent agent(config(version));
        for (int row = 1; row <= 3; ++row) {
            put(agent, 1, std::to_string(row), row);
            put(agent, 2, std::to_string(row), row * 10);
        }
        // Objects beyond the table must not be read into it.
        agent.addInteger("1.3.6.1.4.1.999.2.1.0", 5);
        const SnmpTableResult result = read(agent, plan(2));
        check(result.status == SnmpOperationStatus::Complete && result.rows.size() == 3,
              "dense: three rows");
        check(result.rows.size() == 3 && result.rows[2].instance == "3" &&
                  result.rows[2].cells[1].available && cellValue(result.rows[2].cells[1]) == 30,
              "dense: instance and value");
        // One request per row plus the one that finds the end, not one per cell.
        check(agent.requestCount() == 4, "dense: one GETNEXT per row plus the end");
        check(agent.countOf(SnmpTransportOperation::Get) == 0, "dense: no per-cell GET");
        check(result.problem.isEmpty(), "dense: no problem reported");
    }
}

void bulkIsFaster()
{
    FakeSnmpAgent agent(config(SnmpRequestVersion::V2c, 25));
    for (int row = 1; row <= 100; ++row) {
        put(agent, 1, std::to_string(row), row);
        put(agent, 2, std::to_string(row), row);
    }
    const SnmpTableResult result = read(agent, plan(2));
    check(result.rows.size() == 100 && result.status == SnmpOperationStatus::Complete,
          "bulk: all 100 rows");
    check(agent.requestCount() <= 6, "bulk: a handful of requests");
    check(agent.countOf(SnmpTransportOperation::GetBulk) == agent.requestCount(),
          "bulk: only GETBULK used");

    FakeSnmpAgent v1(config(SnmpRequestVersion::V1, 25));
    put(v1, 1, "1", 1);
    read(v1, plan(1));
    check(v1.countOf(SnmpTransportOperation::GetBulk) == 0, "SNMPv1 never uses GETBULK");
}

void sparseTable()
{
    for (const SnmpRequestVersion version : {SnmpRequestVersion::V1, SnmpRequestVersion::V2c}) {
        for (const int reps : {1, 10}) {
            FakeSnmpAgent agent(config(version, reps));
            // Row 2 has no first column; row 3 has no second column; column 3 only has row 4.
            put(agent, 1, "1", 11); put(agent, 2, "1", 21);
            put(agent, 2, "2", 22);
            put(agent, 1, "3", 13);
            put(agent, 1, "4", 14); put(agent, 2, "4", 24); put(agent, 3, "4", 34);
            const SnmpTableResult result = read(agent, plan(3));
            check(result.rows.size() == 4, "sparse: all four rows found");
            if (result.rows.size() != 4) continue;
            check(!result.rows[1].cells[0].available && result.rows[1].cells[1].available &&
                      cellValue(result.rows[1].cells[1]) == 22,
                  "sparse: row whose first column is missing is kept");
            check(result.rows[2].cells[0].available && !result.rows[2].cells[1].available,
                  "sparse: gap in the middle");
            check(!result.rows[0].cells[2].available && result.rows[3].cells[2].available,
                  "sparse: column with a single row");
        }
    }
}

void emptyAndMissingTables()
{
    for (const SnmpRequestVersion version : {SnmpRequestVersion::V1, SnmpRequestVersion::V2c}) {
        FakeSnmpAgent agent(config(version));
        agent.addInteger("1.3.6.1.4.1.999.2.1.0", 5);
        const SnmpTableResult result = read(agent, plan(2));
        check(result.status == SnmpOperationStatus::Complete && result.rows.isEmpty() &&
                  result.problem.isEmpty(),
              "empty table completes with no rows");
    }
}

void numericInstanceOrder()
{
    // Instances compare number by number, not as text: 2 comes before 10, and 9.1 before 10.
    FakeSnmpAgent agent(config());
    for (const char *instance : {"2", "10", "9.1", "100"}) {
        put(agent, 1, instance, 1);
        put(agent, 2, instance, 2);
    }
    const SnmpTableResult result = read(agent, plan(2));
    QStringList order;
    for (const SnmpTableRow &row : result.rows) order << row.instance;
    check(order == QStringList({"2", "9.1", "10", "100"}), "instances in numeric order");
}

void chunkingAndTooBig()
{
    // More columns than fit in one request.
    FakeSnmpAgent wide(config());
    wide.setMaxVarbinds(8);
    for (int row = 1; row <= 3; ++row)
        for (int column = 1; column <= 20; ++column) put(wide, column, std::to_string(row), column);
    SnmpTableResult result = read(wide, plan(20));
    check(result.rows.size() == 3 && result.rows[1].cells.size() == 20 &&
              result.rows[1].cells[19].available,
          "20 columns read across several requests");

    // An agent with a tiny response limit forces the walk to shrink its requests.
    FakeSnmpAgent small(config(SnmpRequestVersion::V2c, 20));
    small.setMaxVarbinds(2);
    for (int row = 1; row <= 30; ++row)
        for (int column = 1; column <= 4; ++column) put(small, column, std::to_string(row), row);
    result = read(small, plan(4));
    check(result.status == SnmpOperationStatus::Complete && result.rows.size() == 30,
          "tooBig: recovers by asking for less");

    FakeSnmpAgent tiny(config());
    tiny.setMaxVarbinds(0);
    tiny.setFailure([](int, SnmpTransportResult *result) {
        result->status = SnmpOperationStatus::SnmpError;
        result->snmpErrorStatus = SNMP_ERROR_TOO_BIG;
        result->pdu.set_error_status(SNMP_ERROR_TOO_BIG);
        return true;
    });
    result = read(tiny, plan(3));
    check(result.status == SnmpOperationStatus::SnmpError &&
              result.snmpErrorStatus == SNMP_ERROR_TOO_BIG && tiny.requestCount() < 20,
          "tooBig that cannot be fixed is reported, not retried forever");
}

void misbehavingAgents()
{
    FakeSnmpAgent stuck(config());
    put(stuck, 1, "1", 1); put(stuck, 1, "2", 2);
    stuck.setStuck(true);
    const SnmpTableResult result = read(stuck, plan(1));
    check(!result.problem.isEmpty() && stuck.requestCount() < 10,
          "agent repeating an OID ends the walk with a reason");
    check(result.rows.size() == 1, "rows read before the fault are kept");

    // Silence mid-table keeps what was read and says Timeout.
    FakeSnmpAgent quiet(config());
    for (int row = 1; row <= 5; ++row) put(quiet, 1, std::to_string(row), row);
    quiet.setFailure([](int number, SnmpTransportResult *failed) {
        if (number < 4) return false;
        failed->status = SnmpOperationStatus::Timeout;
        failed->transportStatus = SNMP_CLASS_TIMEOUT;
        return true;
    });
    const SnmpTableResult partial = read(quiet, plan(1));
    check(partial.status == SnmpOperationStatus::Timeout && partial.rows.size() == 3,
          "timeout keeps the rows already read");

    // Empty successful response ends the walk.
    FakeSnmpAgent empty(config());
    empty.setFailure([](int, SnmpTransportResult *response) {
        response->status = SnmpOperationStatus::Success;
        return true;
    });
    check(read(empty, plan(2)).status == SnmpOperationStatus::Complete,
          "response without varbinds completes");
}

void snmpv3Report()
{
    // A device that rejects an SNMPv3 request answers with a REPORT carrying an error counter.
    // That must read as a refusal, not as an empty table.
    SnmpRequestConfig v3 = config();
    v3.version = SnmpRequestVersion::V3;
    v3.securityName = "operator";
    FakeSnmpAgent agent(v3);
    agent.setFailure([](int, SnmpTransportResult *response) {
        response->status = SnmpOperationStatus::Success;
        response->transportStatus = SNMP_CLASS_SUCCESS;
        Vb report{Oid("1.3.6.1.6.3.15.1.1.5.0")};   // usmStatsWrongDigests
        report.set_value(1UL);
        response->pdu += report;
        return true;
    });
    const SnmpTableResult result = read(agent, plan(2));
    check(result.status == SnmpOperationStatus::SnmpError && result.rows.isEmpty() &&
              result.problem.contains("operator") && result.problem.contains("authentication"),
          "SNMPv3 REPORT is reported as a refusal naming the user, not as an empty table");
}

void localSnmpv3Failures()
{
    // SNMP++ returns its own USM error codes (positive, 1400+) when a request fails on this side
    // of the wire. They are not device error-status values and must read as plain-English reasons.
    struct Case { int code; const char *cause; };
    const Case cases[] = {
        {SNMPv3_USM_DECRYPTION_ERROR, "privacy"},
        {SNMPv3_USM_UNKNOWN_SECURITY_NAME, "credentials"},
        {SNMPv3_USM_AUTHENTICATION_FAILURE, "authentication"},
    };
    for (const Case &c : cases) {
        SnmpRequestConfig v3 = config();
        v3.version = SnmpRequestVersion::V3;
        v3.securityName = "operator";
        FakeSnmpAgent agent(v3);
        const int code = c.code;
        agent.setFailure([code](int, SnmpTransportResult *failed) {
            failed->status = SnmpOperationStatus::TransportFailure;
            failed->transportStatus = code;
            return true;
        });
        const SnmpTableResult result = read(agent, plan(2));
        check(result.status == SnmpOperationStatus::TransportFailure && result.transportStatus == code &&
                  result.rows.isEmpty() && result.problem.contains(c.cause) &&
                  !result.problem.contains("error 0"),
              "a local SNMPv3 failure in a table read names its cause");
    }
}

void cancellation()
{
    SnmpCancellationToken token;
    token.cancel();
    FakeSnmpAgent never(config());
    put(never, 1, "1", 1);
    check(read(never, plan(1), &token).status == SnmpOperationStatus::Cancelled &&
              never.requestCount() == 0,
          "cancelled before the first request sends nothing");

    SnmpCancellationToken midway;
    FakeSnmpAgent agent(config());
    for (int row = 1; row <= 50; ++row) put(agent, 1, std::to_string(row), row);
    agent.setFailure([&](int number, SnmpTransportResult *) {
        if (number == 5) midway.cancel();
        return false;
    });
    const SnmpTableResult result = read(agent, plan(1), &midway);
    check(result.status == SnmpOperationStatus::Cancelled && result.rows.size() < 50 &&
              !result.rows.isEmpty(),
          "cancelled during the walk keeps the partial rows");
}

// A larger, irregular table checked against the answer computed directly.
void randomizedAgainstTruth()
{
    unsigned state = 12345;
    const auto next = [&state] { state = state * 1103515245u + 12345u; return (state >> 16) & 0x7fff; };
    for (const SnmpRequestVersion version : {SnmpRequestVersion::V1, SnmpRequestVersion::V2c}) {
        for (const int reps : {1, 7, 40}) {
            for (const int limit : {0, 3, 9}) {
                const int columns = 1 + next() % 12;
                FakeSnmpAgent agent(config(version, reps));
                agent.setMaxVarbinds(limit);
                std::map<std::string, std::set<int>> truth;   // instance -> columns present
                for (int i = 0; i < 300; ++i) {
                    const std::string instance = std::to_string(1 + next() % 150);
                    for (int column = 1; column <= columns; ++column)
                        if (next() % 3 != 0) {
                            put(agent, column, instance, column * 1000 + 1);
                            truth[instance].insert(column);
                        }
                }
                const SnmpTableResult result = read(agent, plan(columns));
                // Instances are compared numerically, which for single numbers is plain order.
                std::map<long, std::set<int>> byNumber;
                for (const auto &entry : truth) byNumber[std::stol(entry.first)] = entry.second;
                bool same = result.status == SnmpOperationStatus::Complete &&
                            static_cast<size_t>(result.rows.size()) == byNumber.size();
                size_t index = 0;
                for (const auto &entry : byNumber) {
                    if (!same) break;
                    const SnmpTableRow &row = result.rows[static_cast<int>(index++)];
                    same &= row.instance.toLong() == entry.first;
                    for (int column = 1; column <= columns && same; ++column)
                        same &= row.cells[column - 1].available == (entry.second.count(column) == 1);
                }
                check(same, "random sparse table matches the truth");
            }
        }
    }
}

TableIndexSpec spec(const char *name, MibEnvironmentBaseType type, int fixed = -1,
                    bool implied = false, bool ip = false)
{
    TableIndexSpec value;
    value.name = name; value.baseType = type; value.fixedSize = fixed;
    value.implied = implied; value.ipAddress = ip;
    return value;
}

QString decode(const QVector<TableIndexSpec> &specs, std::initializer_list<quint32> suffix)
{
    QString text = "<unchanged>";
    DecodeTableInstance(specs, QVector<quint32>(suffix), &text);
    return text;
}

void instanceDecoding()
{
    using T = MibEnvironmentBaseType;
    check(decode({spec("ifIndex", T::Integer32)}, {7}) == "ifIndex=7", "decode: integer index");
    check(decode({spec("name", T::OctetString)}, {4, 101, 116, 104, 48}) == "name=\"eth0\"",
          "decode: length-prefixed string");
    // IMPLIED: no length prefix, the string takes the rest.
    check(decode({spec("name", T::OctetString, -1, true)}, {101, 116, 104, 48}) == "name=\"eth0\"",
          "decode: IMPLIED string takes the rest");
    check(decode({spec("addr", T::OctetString, 4, false, true), spec("port", T::Integer32)},
                 {10, 1, 2, 3, 161}) == "addr=10.1.2.3, port=161",
          "decode: IpAddress then integer");
    check(decode({spec("mac", T::OctetString, 6)}, {0, 17, 34, 51, 68, 85}) == "mac=00:11:22:33:44:55",
          "decode: fixed-size MAC address has no length prefix");
    check(decode({spec("vlan", T::Integer32), spec("mac", T::OctetString, -1)},
                 {5, 6, 0, 1, 2, 3, 4, 5}) == "vlan=5, mac=00:01:02:03:04:05",
          "decode: integer then variable-size string");
    check(decode({spec("owner", T::OctetString), spec("oid", T::ObjectIdentifier, -1, true)},
                 {2, 65, 66, 1, 3, 6, 1}) == "owner=\"AB\", oid=1.3.6.1",
          "decode: string then IMPLIED OID");
    check(decode({spec("a", T::Integer32)}, {1, 2}) == "<unchanged>", "decode: trailing sub-identifiers rejected");
    check(decode({spec("a", T::OctetString)}, {9, 1}) == "<unchanged>", "decode: length beyond the instance rejected");
    check(decode({spec("a", T::OctetString, 4, false, true)}, {10, 1, 300, 3}) == "<unchanged>",
          "decode: byte value above 255 rejected");
    check(decode({spec("a", T::Bits)}, {1}) == "<unchanged>", "decode: unsupported type rejected");

    // Through the walk: rows carry the decoded text.
    FakeSnmpAgent agent(config());
    put(agent, 1, "4.101.116.104.48", 1);
    SnmpTablePlan tablePlan = plan(1);
    tablePlan.indexSpecs = {spec("ifName", T::OctetString)};
    const SnmpTableResult result = read(agent, tablePlan);
    check(result.rows.size() == 1 && result.rows[0].indexText == "ifName=\"eth0\"" &&
              result.rows[0].instance == "4.101.116.104.48",
          "walk: rows carry decoded INDEX values and the raw instance");
}

void csv()
{
    check(FormatCsv({{"a", "b"}, {"1", "2"}}) == "a,b\r\n1,2\r\n", "csv: plain records");
    check(FormatCsv({{"x,y", "say \"hi\"", "two\nlines", ""}}) ==
              "\"x,y\",\"say \"\"hi\"\"\",\"two\nlines\",\r\n",
          "csv: quoting, doubled quotes, line breaks, empty field");
    check(FormatCsv({}).isEmpty(), "csv: nothing to write");
}
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    selectedRow();
    denseTable();
    bulkIsFaster();
    sparseTable();
    emptyAndMissingTables();
    numericInstanceOrder();
    chunkingAndTooBig();
    misbehavingAgents();
    snmpv3Report();
    localSnmpv3Failures();
    cancellation();
    randomizedAgainstTruth();
    instanceDecoding();
    csv();
    if (failures) { std::cerr << failures << " check(s) failed\n"; return 1; }
    std::cout << "table tests passed\n";
    return 0;
}
