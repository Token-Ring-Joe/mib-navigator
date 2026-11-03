#ifndef SNMPTRANSPORT_H
#define SNMPTRANSPORT_H

#include "snmprequestconfig.h"
#include "snmp_pp/snmp_pp.h"

#include <QList>
#include <memory>

enum class SnmpTransportOperation { Get, GetNext, GetBulk, Set };
enum class SnmpOperationStatus
{
    Success,
    Timeout,
    SnmpError,
    TransportFailure,
    Cancelled,
    Complete
};

struct SnmpTransportRequest
{
    SnmpTransportOperation operation = SnmpTransportOperation::Get;
    Pdu pdu;
    int nonRepeaters = 0;
    int maxRepetitions = 0;
};

struct SnmpTransportResult
{
    SnmpOperationStatus status = SnmpOperationStatus::TransportFailure;
    Pdu pdu;
    int transportStatus = 0;
    int snmpErrorStatus = 0;
    int snmpErrorIndex = 0;
};

// Maps SNMP++'s return code and the response PDU to an operation status. A positive code is the
// device's SNMP error-status only when it is in the error-status range (1-18) and the PDU says
// the same; the other positive codes (SNMPv3_USM_*, 1400 and up) are failures on this side.
SnmpOperationStatus ClassifySnmpTransportStatus(int transportStatus, const Pdu &pdu);

class ISnmpTransport
{
public:
    virtual ~ISnmpTransport() = default;
    virtual SnmpTransportResult execute(const SnmpTransportRequest &request) = 0;
    virtual const SnmpRequestConfig &config() const = 0;
};

class SnmpPlusTransport final : public ISnmpTransport
{
public:
    explicit SnmpPlusTransport(const SnmpRequestConfig &config);
    SnmpTransportResult execute(const SnmpTransportRequest &request) override;
    const SnmpRequestConfig &config() const override;

    v3MP *messageProcessingModel() const { return securityContext.get(); }

private:
    SnmpRequestConfig requestConfig;
    std::shared_ptr<v3MP> securityContext;
    std::unique_ptr<Snmp> session;
    int sessionStatus = SNMP_CLASS_ERROR;
};

class ScriptedSnmpTransport final : public ISnmpTransport
{
public:
    explicit ScriptedSnmpTransport(const SnmpRequestConfig &config);
    void append(const SnmpTransportResult &result);
    SnmpTransportResult execute(const SnmpTransportRequest &request) override;
    const SnmpRequestConfig &config() const override;
    const QList<SnmpTransportRequest> &requests() const;

private:
    SnmpRequestConfig requestConfig;
    QList<SnmpTransportResult> script;
    QList<SnmpTransportRequest> observedRequests;
};

#endif
