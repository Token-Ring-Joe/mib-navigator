set(SNMP_PP_SOURCES
    src/address.cpp
    src/asn1.cpp
    src/auth_priv.cpp
    src/counter.cpp
    src/ctr64.cpp
    src/eventlist.cpp
    src/eventlistholder.cpp
    src/gauge.cpp
    src/idea.cpp
    src/integer.cpp
    src/IPv6Utility.cpp
    src/log.cpp
    src/md5c.cpp
    src/mp_v3.cpp
    src/msec.cpp
    src/msgqueue.cpp
    src/notifyqueue.cpp
    src/octet.cpp
    src/oid.cpp
    src/pdu.cpp
    src/reentrant.cpp
    src/sha.cpp
    src/snmpmsg.cpp
    src/target.cpp
    src/timetick.cpp
    src/usm_v3.cpp
    src/uxsnmp.cpp
    src/v3.cpp
    src/vb.cpp
)

add_library(snmpb_snmp_pp STATIC ${SNMP_PP_SOURCES})

# Native MSVC cannot use the Unix headers selected by the historical static
# Autoconf output. Supply the equivalent Windows umbrella header from the
# build tree while preserving the bundled configuration and public headers.
if(MSVC)
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/libsnmp-msvc.h.in
        ${CMAKE_CURRENT_BINARY_DIR}/generated/libsnmp.h
        COPYONLY
    )
    set(SNMP_PP_GENERATED_INCLUDE_DIR
        ${CMAKE_CURRENT_BINARY_DIR}/generated
    )
endif()

target_include_directories(snmpb_snmp_pp
    PUBLIC
        ${SNMP_PP_GENERATED_INCLUDE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(snmpb_snmp_pp
    PRIVATE
        snmpb_libtomcrypt
)

if(WIN32)
    target_compile_definitions(snmpb_snmp_pp
        PRIVATE
            WIN32
    )
    target_link_libraries(snmpb_snmp_pp
        PUBLIC
            ws2_32
    )
endif()

if(MSVC)
    target_compile_definitions(snmpb_snmp_pp
        PRIVATE
            _HAS_AUTO_PTR_ETC=1
    )
endif()

if(BUILD_TESTING)
    add_executable(snmpb_snmp_pp_smoke
        test/snmp_pp_smoke.cpp
    )

    target_link_libraries(snmpb_snmp_pp_smoke
        PRIVATE
            snmpb_snmp_pp
    )

    add_test(
        NAME snmp++.smoke
        COMMAND snmpb_snmp_pp_smoke
    )
endif()
