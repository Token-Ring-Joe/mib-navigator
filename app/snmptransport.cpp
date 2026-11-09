#include "snmptransport.h"
#include "snmprequestconfigadapter.h"

namespace
{
SnmpOperationStatus classify(int transportStatus, const Pdu &pdu)
{
    if (transportStatus == SNMP_CLASS_TIMEOUT)
        return SnmpOperationStatus::Timeout;
    if (transportStatus != SNMP_CLASS_SUCCESS)
        return SnmpOperationStatus::TransportFailure;
    return pdu.get_error_status() ? SnmpOperationStatus::SnmpError
                                  : SnmpOperationStatus::Success;
}
}

SnmpPlusTransport::SnmpPlusTransport(const SnmpRequestConfig &config)
    : requestConfig(config)
{
    const UdpAddress endpoint(config.endpoint().toLatin1().constData());
    const bool ipv6 = endpoint.valid() &&
                      endpoint.get_ip_version() == Address::version_ipv6;
    session = std::make_unique<Snmp>(sessionStatus, 0, ipv6);
}

SnmpTransportResult SnmpPlusTransport::execute(
    const SnmpTransportRequest &request)
{
    SnmpTransportResult result;
    result.pdu = request.pdu;
    if (!session || sessionStatus != SNMP_CLASS_SUCCESS)
    {
        result.transportStatus = sessionStatus;
        return result;
    }

    UdpAddress address(requestConfig.endpoint().toLatin1().constData());
    if (!address.valid())
        return result;

    std::unique_ptr<SnmpTarget> target;
    if (requestConfig.version == SnmpRequestVersion::V3)
        target = std::make_unique<UTarget>(address);
    else
        target = std::make_unique<CTarget>(address);
    ApplySnmpRequestConfig(requestConfig, *target);
    ApplySnmpV3PduConfig(requestConfig, result.pdu);

    switch (request.operation)
    {
    case SnmpTransportOperation::Get:
        result.transportStatus = session->get(result.pdu, *target);
        break;
    case SnmpTransportOperation::GetNext:
        result.transportStatus = session->get_next(result.pdu, *target);
        break;
    case SnmpTransportOperation::GetBulk:
        result.transportStatus = session->get_bulk(
            result.pdu, *target, request.nonRepeaters, request.maxRepetitions);
        break;
    case SnmpTransportOperation::Set:
        result.transportStatus = session->set(result.pdu, *target);
        break;
    }
    result.snmpErrorStatus = result.pdu.get_error_status();
    result.snmpErrorIndex = result.pdu.get_error_index();
    result.status = classify(result.transportStatus, result.pdu);
    return result;
}

const SnmpRequestConfig &SnmpPlusTransport::config() const
{
    return requestConfig;
}

ScriptedSnmpTransport::ScriptedSnmpTransport(const SnmpRequestConfig &config)
    : requestConfig(config) {}

void ScriptedSnmpTransport::append(const SnmpTransportResult &result)
{
    script.append(result);
}

SnmpTransportResult ScriptedSnmpTransport::execute(
    const SnmpTransportRequest &request)
{
    observedRequests.append(request);
    if (script.isEmpty())
        return {};
    return script.takeFirst();
}

const SnmpRequestConfig &ScriptedSnmpTransport::config() const
{
    return requestConfig;
}

const QList<SnmpTransportRequest> &ScriptedSnmpTransport::requests() const
{
    return observedRequests;
}
