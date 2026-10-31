add_library(snmpb_libsmi STATIC
    lib/common.c
    lib/error.c
    lib/util.c
    lib/snprintf.c

    lib/smi.c
    lib/smi-data.c
    lib/smi-check.c

    lib/parser-smi.c
    lib/scanner-smi.c
)

if(WIN32)
    target_sources(snmpb_libsmi PRIVATE win/win.c)
    # The feature checks below are skipped on Windows. Without these two definitions util.h falls
    # back to "#define strtoull strtoul", which is only 32 bits wide on Windows, so 64-bit numbers
    # such as Counter64's 18446744073709551615 in SNMPv2-SMI were reported as out of range.
    # MSVC's runtime provides both functions.
    target_compile_definitions(snmpb_libsmi PRIVATE HAVE_STRTOULL=1 HAVE_STRTOLL=1)
else()
    include(CheckFunctionExists)
    include(CheckIncludeFile)

    check_include_file(limits.h HAVE_LIMITS_H)
    check_include_file(pwd.h HAVE_PWD_H)
    check_include_file(regex.h HAVE_REGEX_H)
    check_include_file(stdint.h HAVE_STDINT_H)
    check_include_file(strings.h HAVE_STRINGS_H)
    check_include_file(unistd.h HAVE_UNISTD_H)
    check_function_exists(asprintf HAVE_ASPRINTF)
    check_function_exists(snprintf HAVE_SNPRINTF)
    check_function_exists(strtoll HAVE_STRTOLL)
    check_function_exists(strtoq HAVE_STRTOQ)
    check_function_exists(strtoull HAVE_STRTOULL)
    check_function_exists(strtouq HAVE_STRTOUQ)
    check_function_exists(timegm HAVE_TIMEGM)
    check_function_exists(vasprintf HAVE_VASPRINTF)
    check_function_exists(vsnprintf HAVE_VSNPRINTF)

    set(LIBSMI_DEFAULT_GLOBALCONFIG "${CMAKE_INSTALL_FULL_SYSCONFDIR}/smi.conf")
    set(LIBSMI_DEFAULT_SMIPATH
        "${CMAKE_INSTALL_FULL_DATADIR}/mibs/ietf:${CMAKE_INSTALL_FULL_DATADIR}/mibs/iana:${CMAKE_INSTALL_FULL_DATADIR}/mibs/tubs:${CMAKE_INSTALL_FULL_DATADIR}/pibs/ietf:${CMAKE_INSTALL_FULL_DATADIR}/pibs/tubs")
    configure_file(cmake/config.h.in ${CMAKE_CURRENT_BINARY_DIR}/config.h @ONLY)
endif()

target_include_directories(snmpb_libsmi
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/lib
)

if(WIN32)
    target_include_directories(snmpb_libsmi PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/win)
    target_compile_definitions(snmpb_libsmi PRIVATE
        _CRT_SECURE_NO_WARNINGS
        _CRT_NONSTDC_NO_WARNINGS
        YY_NO_UNISTD_H
        YYMALLOC=malloc
        YYFREE=free
    )
    set_source_files_properties(lib/scanner-smi.c PROPERTIES
        COMPILE_DEFINITIONS "_Win32;__STDC__;YY_NEVER_INTERACTIVE"
    )
else()
    target_include_directories(snmpb_libsmi PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
    # The generated parser declares smiparse() with an empty parameter list.
    # C23 (the GCC 15 default) reads that as (void), so build as C17.
    set_target_properties(snmpb_libsmi PROPERTIES C_STANDARD 17 C_EXTENSIONS ON)
endif()

# Build libsmi's native MIB validation utility.
# This gives SnmpB Next a regression tool for verifying that the
# patched MIB parser continues to behave correctly during modernization.

add_executable(snmpb_smilint
    tools/smilint.c
    tools/shhopt.c
)

target_include_directories(snmpb_smilint
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/lib
        ${CMAKE_CURRENT_SOURCE_DIR}/tools
)

if(WIN32)
    target_include_directories(snmpb_smilint PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/win)
    target_compile_definitions(snmpb_smilint PRIVATE
        _CRT_SECURE_NO_WARNINGS
        _CRT_NONSTDC_NO_WARNINGS
    )
else()
    target_include_directories(snmpb_smilint PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
    set_target_properties(snmpb_smilint PROPERTIES C_STANDARD 17 C_EXTENSIONS ON)
endif()

target_link_libraries(snmpb_smilint
    PRIVATE
        snmpb_libsmi
)

if(BUILD_TESTING)

    set(SMILINT_REGRESSION_MIBS
        SNMPv2-MIB
        IF-MIB
        MAU-MIB
        RMON2-MIB
        BRIDGE-MIB
    )

    foreach(MIB IN LISTS SMILINT_REGRESSION_MIBS)

        add_test(
            NAME "libsmi.smilint.${MIB}"

            COMMAND
                ${CMAKE_COMMAND}
                "-DSMILINT=$<TARGET_FILE:snmpb_smilint>"
                "-DLIBSMI_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
                "-DMIB=${MIB}"
                -P "${PROJECT_SOURCE_DIR}/cmake/RunSmilintRegression.cmake"
        )

    endforeach()

endif()
