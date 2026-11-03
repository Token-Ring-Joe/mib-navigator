#include "snmptransport.h"
#include "snmprequestconfigadapter.h"
#include "snmpreport.h"
#include <QCryptographicHash>
#include <QMutex>
#include <QMutexLocker>
#include <QHash>
#include <QList>

namespace {
std::shared_ptr<v3MP> securityContextFor(const UsmCredentialRecord &credential)
{
    // Keep protocol salts and discovery state across polls. A secret/protocol edit receives
    // a new context; operations already running retain their captured context.
    const auto field = [](const QByteArray &value) { return QByteArray::number(value.size()) + ':' + value; };
    const QByteArray fingerprint = QCryptographicHash::hash(
        field(credential.identity.credentialId.toUtf8()) + field(credential.securityName.toUtf8()) +
        field(QByteArray::number(credential.authProtocol)) + field(credential.authSecret.bytes()) +
        field(QByteArray::number(credential.privacyProtocol)) + field(credential.privacySecret.bytes()),
        QCryptographicHash::Sha256);
    static QMutex mutex;
    static QHash<QByteArray, std::shared_ptr<v3MP>> contexts;
    static QList<QByteArray> order;
    QMutexLocker lock(&mutex);
    if (contexts.contains(fingerprint)) {
        order.removeAll(fingerprint); order.append(fingerprint);
        return contexts.value(fingerprint);
    }
    if (!v3MP::I) return {};
    long boots = 0, time = 0;
    v3MP::I->get_usm()->get_local_time(&boots, &time);
    int status = SNMPv3_MP_ERROR;
    auto context = std::make_shared<v3MP>(v3MP::I->get_local_engine_id(), boots, status, false);
    if (status != SNMPv3_MP_OK) return {};
    const auto secret = [](const CredentialSecret &s) {
        return OctetStr(reinterpret_cast<const unsigned char *>(s.bytes().constData()), s.bytes().size());
    };
    if (context->get_usm()->add_usm_user(OctetStr(credential.securityName.toLatin1().constData()),
            credential.authProtocol, credential.privacyProtocol,
            secret(credential.authSecret), secret(credential.privacySecret)) != SNMPv3_USM_OK)
        return {};
    contexts.insert(fingerprint, context); order.append(fingerprint);
    while (order.size() > 64) contexts.remove(order.takeFirst());
    return context;
}
}

SnmpOperationStatus ClassifySnmpTransportStatus(int transportStatus, const Pdu &pdu)
{
    if (transportStatus == SNMP_CLASS_TIMEOUT)
        return SnmpOperationStatus::Timeout;
    // SNMP++ returns the SNMP error-status itself (noSuchName = 2, tooBig = 1, ...) as a
    // positive code when the device answered with an error; negative codes mean no usable
    // answer arrived. Treating the positive ones as "no response" hid every device error.
    // SNMP++'s own SNMPv3 USM errors are positive too (1400 and up, e.g. a REPORT that could
    // not be decrypted); those are not device errors and have no PDU error-status.
    if (transportStatus < SNMP_CLASS_SUCCESS)
        return SnmpOperationStatus::TransportFailure;
    if (transportStatus > SNMP_CLASS_SUCCESS)
        return IsSnmpErrorStatusCode(transportStatus) && pdu.get_error_status() == transportStatus
            ? SnmpOperationStatus::SnmpError : SnmpOperationStatus::TransportFailure;
    return pdu.get_error_status() ? SnmpOperationStatus::SnmpError : SnmpOperationStatus::Success;
}

SnmpPlusTransport::SnmpPlusTransport(const SnmpRequestConfig &config)
    : requestConfig(config)
{
    const UdpAddress endpoint(config.endpoint().toLatin1().constData());
    const bool ipv6 = endpoint.valid() &&
                      endpoint.get_ip_version() == Address::version_ipv6;
    if (config.version == SnmpRequestVersion::V3 && config.usmCredential) {
        securityContext = securityContextFor(*config.usmCredential);
        if (!securityContext) { sessionStatus = SNMPv3_USM_ERROR; return; }
    }
    session = std::make_unique<Snmp>(sessionStatus, 0, ipv6);
    if (securityContext) session->set_v3mp(securityContext.get());
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
    result.status = ClassifySnmpTransportStatus(result.transportStatus, result.pdu);
    if (result.status == SnmpOperationStatus::Success && requestConfig.usmCredential && v3MP::I) {
        OctetStr engine;
        static_cast<UTarget &>(*target).get_engine_id(engine);
        if (engine.len()) {
            const auto &r = *requestConfig.usmCredential;
            const auto secret = [](const CredentialSecret &s) {
                return OctetStr(reinterpret_cast<const unsigned char *>(s.bytes().constData()), s.bytes().size());
            };
            v3MP::I->get_usm()->add_usm_user(OctetStr(r.securityName.toLatin1().constData()),
                OctetStr(r.securityName.toLatin1().constData()), r.authProtocol, r.privacyProtocol,
                secret(r.authSecret), secret(r.privacySecret), engine);
        }
    }

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
