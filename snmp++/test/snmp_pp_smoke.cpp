#include <algorithm>
#include <cstring>
#include <iostream>

#include <snmp_pp/auth_priv.h>
#include <snmp_pp/octet.h>
#include <snmp_pp/oid.h>
#include <snmp_pp/snmperrs.h>
#include <snmp_pp/vb.h>

namespace {

int fail(const char *message)
{
    std::cerr << message << '\n';
    return 1;
}

} // namespace

int main()
{
    Oid oid("1.3.6.1.2.1.1.1.0");
    if (!oid.valid() || oid.len() != 9)
        return fail("OID construction failed");

    OctetStr expected_value("SnmpB Next");
    Vb vb(oid);
    vb.set_value(expected_value);

    Oid actual_oid;
    vb.get_oid(actual_oid);
    if (actual_oid != oid)
        return fail("Vb OID round trip failed");

    OctetStr actual_value;
    if (vb.get_value(actual_value) != SNMP_CLASS_SUCCESS ||
        actual_value != expected_value)
        return fail("Vb value round trip failed");

    int construct_state = SNMPv3_USM_ERROR;
    AuthPriv auth_priv(construct_state);
    if (construct_state != SNMPv3_USM_OK)
        return fail("AuthPriv initialization failed");
    if (auth_priv.add_default_modules() != SNMP_CLASS_SUCCESS)
        return fail("AuthPriv default-module registration failed");

    const unsigned char password[] = "maplesyrup";
    const unsigned char engine_id[] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x02
    };
    unsigned char key_a[SNMPv3_USM_MAX_KEY_LEN] = {};
    unsigned char key_b[SNMPv3_USM_MAX_KEY_LEN] = {};
    unsigned int key_a_len = sizeof(key_a);
    unsigned int key_b_len = sizeof(key_b);

    if (auth_priv.password_to_key_auth(
            SNMP_AUTHPROTOCOL_HMACMD5,
            password,
            static_cast<unsigned int>(std::strlen(
                reinterpret_cast<const char *>(password))),
            engine_id,
            sizeof(engine_id),
            key_a,
            &key_a_len) != SNMPv3_USM_OK)
        return fail("First password-to-key derivation failed");

    if (auth_priv.password_to_key_auth(
            SNMP_AUTHPROTOCOL_HMACMD5,
            password,
            static_cast<unsigned int>(std::strlen(
                reinterpret_cast<const char *>(password))),
            engine_id,
            sizeof(engine_id),
            key_b,
            &key_b_len) != SNMPv3_USM_OK)
        return fail("Second password-to-key derivation failed");

    if (key_a_len == 0 || key_a_len != key_b_len ||
        !std::equal(key_a, key_a + key_a_len, key_b))
        return fail("Password-to-key derivation is not deterministic");

    return 0;
}
