#ifndef FAKESNMPAGENT_H
#define FAKESNMPAGENT_H

#include "snmptransport.h"

#include <functional>
#include <map>

// An in-memory SNMP agent for deterministic tests: a sorted OID -> value map answering GET,
// GETNEXT and GETBULK the way a real agent does (SNMPv1 reports the missing varbind with
// noSuchName and its index; SNMPv2c/v3 use noSuchInstance / endOfMibView exceptions).
class FakeSnmpAgent final : public ISnmpTransport
{
public:
    explicit FakeSnmpAgent(const SnmpRequestConfig &config) : requestConfig(config) {}

    void addInteger(const char *oid, long value)
    {
        Vb vb{Oid(oid)};
        vb.set_value(value);
        values[Oid(oid)] = vb;
    }
    void addString(const char *oid, const char *value)
    {
        Vb vb{Oid(oid)};
        vb.set_value(value);
        values[Oid(oid)] = vb;
    }
    // Most varbinds the agent will put in one response; more makes it answer tooBig.
    void setMaxVarbinds(int count) { maxVarbinds = count; }
    // Makes GETNEXT answer with the OID asked about when it exists instead of the one after
    // it (a broken agent that would make a naive walk loop forever).
    void setStuck(bool value) { stuck = value; }
    // Lets a test make every request fail, e.g. to simulate a timeout.
    void setFailure(std::function<bool(int requestNumber, SnmpTransportResult *)> hook)
    {
        failure = std::move(hook);
    }

    SnmpTransportResult execute(const SnmpTransportRequest &request) override
    {
        ++observedCount;
        operations.push_back(request.operation);
        SnmpTransportResult result;
        result.transportStatus = SNMP_CLASS_SUCCESS;
        if (failure && failure(observedCount, &result)) return result;

        const bool v1 = requestConfig.version == SnmpRequestVersion::V1;
        const int asked = request.pdu.get_vb_count();
        std::vector<Vb> answers;
        if (request.operation == SnmpTransportOperation::Get) {
            for (int i = 0; i < asked; ++i) {
                Vb vb;
                request.pdu.get_vb(vb, i);
                const auto found = values.find(vb.get_oid());
                if (found != values.end()) answers.push_back(found->second);
                else if (v1) return noSuchName(i + 1);
                else {
                    Vb missing{vb.get_oid()};
                    missing.set_exception_status(sNMP_SYNTAX_NOSUCHINSTANCE);
                    answers.push_back(missing);
                }
            }
        } else if (request.operation == SnmpTransportOperation::GetNext) {
            for (int i = 0; i < asked; ++i) {
                Vb vb;
                request.pdu.get_vb(vb, i);
                Vb next;
                if (!nextOf(vb.get_oid(), &next)) {
                    if (v1) return noSuchName(i + 1);
                    next = Vb{vb.get_oid()};
                    next.set_exception_status(sNMP_SYNTAX_ENDOFMIBVIEW);
                }
                answers.push_back(next);
            }
        } else if (request.operation == SnmpTransportOperation::GetBulk) {
            std::vector<Oid> cursors;
            for (int i = 0; i < asked; ++i) {
                Vb vb;
                request.pdu.get_vb(vb, i);
                cursors.push_back(vb.get_oid());
            }
            for (int repetition = 0; repetition < request.maxRepetitions; ++repetition) {
                bool any = false;
                for (Oid &cursor : cursors) {
                    Vb next;
                    if (nextOf(cursor, &next)) { any = true; cursor = next.get_oid(); }
                    else {
                        next = Vb{cursor};
                        next.set_exception_status(sNMP_SYNTAX_ENDOFMIBVIEW);
                    }
                    answers.push_back(next);
                }
                if (!any) break;
            }
        }
        if (maxVarbinds > 0 && static_cast<int>(answers.size()) > maxVarbinds) {
            result.status = SnmpOperationStatus::SnmpError;
            result.snmpErrorStatus = SNMP_ERROR_TOO_BIG;
            result.pdu.set_error_status(SNMP_ERROR_TOO_BIG);
            return result;
        }
        result.status = SnmpOperationStatus::Success;
        for (const Vb &vb : answers) result.pdu += vb;
        return result;
    }
    const SnmpRequestConfig &config() const override { return requestConfig; }

    int requestCount() const { return observedCount; }
    int countOf(SnmpTransportOperation operation) const
    {
        int count = 0;
        for (const SnmpTransportOperation seen : operations) count += seen == operation;
        return count;
    }

private:
    struct OidLess { bool operator()(const Oid &a, const Oid &b) const { return a < b; } };

    bool nextOf(const Oid &from, Vb *next) const
    {
        const auto it = stuck ? values.lower_bound(from) : values.upper_bound(from);
        if (it == values.end()) return false;
        *next = it->second;
        return true;
    }
    SnmpTransportResult noSuchName(int index) const
    {
        SnmpTransportResult result;
        result.status = SnmpOperationStatus::SnmpError;
        result.transportStatus = SNMP_CLASS_SUCCESS;
        result.snmpErrorStatus = SNMP_ERROR_NO_SUCH_NAME;
        result.snmpErrorIndex = index;
        result.pdu.set_error_status(SNMP_ERROR_NO_SUCH_NAME);
        result.pdu.set_error_index(index);
        return result;
    }

    SnmpRequestConfig requestConfig;
    std::map<Oid, Vb, OidLess> values;
    std::vector<SnmpTransportOperation> operations;
    std::function<bool(int, SnmpTransportResult *)> failure;
    int observedCount = 0;
    int maxVarbinds = 0;
    bool stuck = false;
};

#endif
