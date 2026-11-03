#include "snmptransport.h"
#include "snmp_pp/snmpmsg.h"
#include <QCoreApplication>
#include <QByteArray>
#include <cstdio>
#include <future>
#include <atomic>
#include <QThread>
#include <QUdpSocket>
#include <QNetworkDatagram>

namespace {
static_assert(UsmProtocolNone == SNMP_AUTHPROTOCOL_NONE && UsmProtocolNone == SNMP_PRIVPROTOCOL_NONE);
int failures = 0;
void check(bool ok, const char *message) { if (!ok) { ++failures; std::fprintf(stderr, "FAIL: %s\n", message); } }
SnmpRequestConfig config(const char *id, int auth, int privacy, const char *secret)
{
    UsmCredentialRecord record;
    record.identity = {QString::fromLatin1(id), CredentialKind::Usm};
    record.displayName = QString::fromLatin1(id); record.securityName = "shared-user";
    record.authProtocol = auth; record.privacyProtocol = privacy;
    record.authSecret = CredentialSecret(secret); record.privacySecret = CredentialSecret("privacy-test-secret");
    SnmpRequestConfig result; result.version = SnmpRequestVersion::V3;
    result.address = "127.0.0.1"; result.port = "9"; result.timeout = 1; result.retries = 0;
    result.securityName = record.securityName; result.securityLevel = privacy != SNMP_PRIVPROTOCOL_NONE ? 2 : (auth != SNMP_AUTHPROTOCOL_NONE ? 1 : 0);
    result.usmCredential = record; return result;
}
// In-process UDP agents exercise actual discovery, authentication, encryption, replies and retries.
class ReplySession final : public Snmp
{
public:
    ReplySession(int &status, QUdpSocket &socket) : Snmp(status, 0, false), output(socket) {}
    int send_raw_data(unsigned char *data, size_t size, UdpAddress &destination, SnmpSocket = INVALID_SOCKET) override
    {
        return output.writeDatagram(reinterpret_cast<const char *>(data), size, QHostAddress::LocalHost,
            destination.get_port()) == static_cast<qint64>(size) ? 0 : -1;
    }
private:
    QUdpSocket &output;
};
class LoopbackAgent
{
public:
    LoopbackAgent(const SnmpRequestConfig &config, long value, bool dropFirst) {
        auto ready = std::make_shared<std::promise<quint16>>(); auto portReady = ready->get_future();
        thread = QThread::create([this, config, value, dropFirst, ready]() {
            QUdpSocket socket;
            if (!socket.bind(QHostAddress::LocalHost, 0)) { ready->set_value(0); return; }
            int status;
            // Deliberately use the same remote engine ID as the other agent, as well as the same username.
            v3MP model(OctetStr("shared-test-engine-id"), 3, status, false);
            const auto &r = *config.usmCredential;
            const auto secret = [](const CredentialSecret &s) { return OctetStr(
                reinterpret_cast<const unsigned char *>(s.bytes().constData()), s.bytes().size()); };
            model.get_usm()->add_usm_user(OctetStr("shared-user"), r.authProtocol, r.privacyProtocol,
                secret(r.authSecret), secret(r.privacySecret));
            ReplySession receiver(status, socket); receiver.set_v3mp(&model);
            if (status != SNMP_CLASS_SUCCESS) { ready->set_value(0); return; }
            ready->set_value(socket.localPort()); bool dropped = false;
            while (!stopped.load()) {
                if (!socket.waitForReadyRead(50)) continue;
                while (socket.hasPendingDatagrams()) {
                    auto datagram = socket.receiveDatagram(); auto bytes = datagram.data();
                    SnmpMessage message;
                    if (message.load(reinterpret_cast<unsigned char *>(bytes.data()), bytes.size()) != SNMP_CLASS_SUCCESS) continue;
                    Pdu pdu; snmp_version version; OctetStr engine, user; long security;
                    const auto sourceText = QStringLiteral("127.0.0.1/%1").arg(datagram.senderPort()).toLatin1();
                    UdpAddress source(sourceText.constData());
                    const int parsed = message.unloadv3(pdu, version, engine, user, security, source, receiver);
                    // Discovery/authentication reports are emitted through ReplySession's loopback socket.
                    if (parsed != SNMP_CLASS_SUCCESS || pdu.get_type() != sNMP_PDU_GET) continue;
                    if (dropFirst && !dropped) { dropped = true; continue; }
                    ++requests;
                    pdu.set_type(sNMP_PDU_RESPONSE); pdu.set_error_status(0); pdu.set_error_index(0);
                    for (int i = 0; i < pdu.get_vb_count(); ++i) { Vb vb; pdu.get_vb(vb, i); vb.set_value(value); pdu.set_vb(vb, i); }
                    SnmpMessage reply; reply.set_v3mp(&model);
                    if (reply.loadv3(pdu, model.get_local_engine_id(), user, security, version3) == SNMP_CLASS_SUCCESS)
                        socket.writeDatagram(reinterpret_cast<const char *>(reply.data()), reply.len(),
                            QHostAddress::LocalHost, datagram.senderPort());
                }
            }
        });
        thread->start(); port = portReady.get();
    }
    ~LoopbackAgent() { stopped = true; thread->wait(); delete thread; }
    quint16 port = 0;
    std::atomic<int> requests{0};
private:
    std::atomic<bool> stopped{false}; QThread *thread;
};

QByteArray encode(v3MP *model, int level, int requestId)
{
    Pdu pdu; pdu.set_type(sNMP_PDU_GET); pdu.set_request_id(requestId);
    pdu.set_security_level(level); pdu.set_context_engine_id(model->get_local_engine_id());
    Vb vb(Oid("1.3.6.1.2.1.1.1.0")); pdu += vb;
    SnmpMessage message; message.set_v3mp(model);
    const int status = message.loadv3(pdu, model->get_local_engine_id(), OctetStr("shared-user"), SNMP_SECURITY_MODEL_USM, version3);
    check(status == SNMP_CLASS_SUCCESS, "v3 packet encodes with selected context");
    return status == SNMP_CLASS_SUCCESS ? QByteArray(reinterpret_cast<const char *>(message.data()), message.len()) : QByteArray();
}
bool decode(v3MP *model, const QByteArray &packet)
{
    if (packet.isEmpty()) return false;
    int status; Snmp receiver(status, 0, false); if (status != SNMP_CLASS_SUCCESS) return false;
    receiver.set_v3mp(model);
    SnmpMessage message;
    if (message.load(reinterpret_cast<unsigned char *>(const_cast<char *>(packet.constData())), packet.size()) != SNMP_CLASS_SUCCESS) return false;
    Pdu pdu; snmp_version version; OctetStr engine, user; long security;
    UdpAddress source("127.0.0.1/9");
    return message.unloadv3(pdu, version, engine, user, security, source, receiver) == SNMP_CLASS_SUCCESS &&
        version == version3 && user == OctetStr("shared-user") && pdu.get_vb_count() == 1;
}
}
int runTests()
{
    int status; v3MP defaults(OctetStr("credential-test-engine"), 3, status);
    check(status == SNMPv3_MP_OK && v3MP::I == &defaults, "default security context initializes");
    const auto a = config("product-a", SNMP_AUTHPROTOCOL_HMACSHA, SNMP_PRIVPROTOCOL_NONE, "product-a-secret");
    const auto b = config("product-b", SNMP_AUTHPROTOCOL_HMAC192SHA256, SNMP_PRIVPROTOCOL_AES128, "product-b-secret");
    SnmpPlusTransport first(a), second(b);
    auto *firstModel = first.messageProcessingModel(), *secondModel = second.messageProcessingModel();
    check(firstModel && secondModel && firstModel != secondModel && v3MP::I == &defaults,
          "same-username credentials have isolated contexts and preserve the default");
    if (!firstModel || !secondModel) return 1;
    SnmpPlusTransport repeated(a);
    check(repeated.messageProcessingModel() == firstModel, "repeated polls retain discovery and privacy state");
    auto renamed = a; renamed.usmCredential->displayName = "renamed product";
    SnmpPlusTransport labelOnly(renamed);
    check(labelOnly.messageProcessingModel() == firstModel, "display rename preserves security context");
    auto edited = a; edited.usmCredential->authSecret = CredentialSecret("new-product-a-secret");
    SnmpPlusTransport changed(edited);
    check(changed.messageProcessingModel() != firstModel, "secret edits do not retarget existing operations");
    const auto packetA = encode(firstModel, SNMP_SECURITY_LEVEL_AUTH_NOPRIV, 701);
    const auto packetB = encode(secondModel, SNMP_SECURITY_LEVEL_AUTH_PRIV, 702);
    check(packetA.contains("shared-user") && packetB.contains("shared-user"), "wire username remains the configured USM username");
    check(!packetA.contains("product-a-secret") && !packetB.contains("product-b-secret"), "wire messages do not contain passphrases");
    auto firstDecode = std::async(std::launch::async, [&]() { return decode(firstModel, packetA); });
    auto secondDecode = std::async(std::launch::async, [&]() { return decode(secondModel, packetB); });
    check(firstDecode.get() && secondDecode.get(), "concurrent variants authenticate and decrypt with their own protocols and secrets");
    check(!decode(changed.messageProcessingModel(), packetA), "wrong secret cannot authenticate the original packet");
    check(decode(firstModel, encode(firstModel, SNMP_SECURITY_LEVEL_AUTH_NOPRIV, 703)), "using another variant does not corrupt the first context");
    check(v3MP::I == &defaults, "private contexts never replace the notification context");
    {
        LoopbackAgent agentA(a, 111, false), agentB(b, 222, true);
        check(agentA.port && agentB.port, "synthetic v3 agents bind loopback ports");
        if (agentA.port && agentB.port) {
            const auto query = [](SnmpRequestConfig c, quint16 port) {
                c.port = QString::number(port); c.retries = 1;
                SnmpPlusTransport transport(c); SnmpTransportRequest request;
                request.pdu += Vb(Oid("1.3.6.1.2.1.1.1.0"));
                const auto result = transport.execute(request);
                if (result.status != SnmpOperationStatus::Success || result.pdu.get_vb_count() != 1 || result.pdu.get_type() == sNMP_PDU_REPORT) {
                    std::fprintf(stderr, "query failed id=%s transport=%d type=%d count=%d\n",
                        qPrintable(c.usmCredential->identity.credentialId), result.transportStatus, result.pdu.get_type(), result.pdu.get_vb_count());
                    return -1L;
                }
                Vb vb; result.pdu.get_vb(vb, 0); long value = -1; vb.get_value(value); return value;
            };
            auto firstRequest = std::async(std::launch::async, [&]() { return query(a, agentA.port); });
            auto secondRequest = std::async(std::launch::async, [&]() { return query(b, agentB.port); });
            const auto firstValue = firstRequest.get(), secondValue = secondRequest.get();
            if (firstValue != 111 || secondValue != 222) std::fprintf(stderr, "values A=%ld B=%ld requests A=%d B=%d\n",
                firstValue, secondValue, agentA.requests.load(), agentB.requests.load());
            check(firstValue == 111 && secondValue == 222 && agentA.requests > 0 && agentB.requests > 0,
                  "concurrent v3 requests discover engines, authenticate, decrypt and retry independently");
            check(query(edited, agentA.port) == -1, "actual request with the wrong secret is rejected");
            check(query(a, agentA.port) == 111, "failed alternate credentials do not corrupt the original session");
            auto *learned = defaults.get_usm()->get_user(OctetStr("shared-test-engine-id"), OctetStr("shared-user"));
            check(learned && learned->authProtocol == SNMP_AUTHPROTOCOL_HMACSHA &&
                  learned->privProtocol == SNMP_PRIVPROTOCOL_NONE,
                  "successful query registers the selected localized credential for notifications");
            defaults.get_usm()->free_user(learned);
        }
    }
    return failures ? 1 : 0;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv); Snmp::socket_startup();
    const int result = runTests(); Snmp::socket_cleanup(); return result;
}
