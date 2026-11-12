cmake_minimum_required(VERSION 3.22)

# The version and release label come from one file; see cmake/ReleaseLabel.cmake.
include(cmake/ReleaseLabel.cmake)

project(SnmpBNext
    VERSION ${SNMPB_BASE_VERSION}
    LANGUAGES C CXX
)

configure_file(
    cmake/snmpb_version.h.in
    generated/snmpb_version.h
    @ONLY
)

find_package(Git QUIET)
add_custom_target(snmpb_build_identity
    COMMAND ${CMAKE_COMMAND}
        "-DSNMPB_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
        "-DSNMPB_BUILD_INFO=${PROJECT_BINARY_DIR}/generated/snmpb_build_info.h"
        "-DSNMPB_GIT=${GIT_EXECUTABLE}"
        -P "${PROJECT_SOURCE_DIR}/cmake/GenerateBuildIdentity.cmake"
    BYPRODUCTS "${PROJECT_BINARY_DIR}/generated/snmpb_build_info.h"
    COMMENT "Recording MIB Navigator build identity"
    VERBATIM
)

include(CTest)
if(BUILD_TESTING)
    add_test(NAME snmpb.release-tree-policy
        COMMAND ${CMAKE_COMMAND}
            "-DSNMPB_FIXTURE_DIR=${PROJECT_BINARY_DIR}/release-tree-policy-fixture"
            -P "${PROJECT_SOURCE_DIR}/cmake/VerifyReleaseTree_test.cmake")
endif()
include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.10 REQUIRED COMPONENTS Core Widgets Network Svg)
# Optional: only the loopback SNMP test scenario needs Python.
find_package(Python3 COMPONENTS Interpreter QUIET)

message(STATUS "Qt version: ${Qt6_VERSION}")
message(STATUS "MIB Navigator Qt/CMake environment configured successfully")

if(WIN32)
    # windows.h defines min/max macros that break std::min/std::max and numeric_limits.
    add_compile_definitions(NOMINMAX)
endif()

add_subdirectory(libsmi)
add_subdirectory(libtomcrypt)
add_subdirectory(snmp++)
add_subdirectory(app)

set(SNMPB_LICENSE_INSTALL_DIR "${CMAKE_INSTALL_DATADIR}/snmpb/licenses")
install(FILES license.txt
    DESTINATION "${SNMPB_LICENSE_INSTALL_DIR}/SnmpB"
    RENAME COPYING
)
install(FILES libsmi/COPYING
    DESTINATION "${SNMPB_LICENSE_INSTALL_DIR}/libsmi"
)
install(FILES libtomcrypt/LICENSE
    DESTINATION "${SNMPB_LICENSE_INSTALL_DIR}/LibTomCrypt"
)
install(FILES snmp++/LICENSE
    DESTINATION "${SNMPB_LICENSE_INSTALL_DIR}/SNMP++"
)
install(FILES app/icons/open-iconic/LICENSE.txt
    DESTINATION "${SNMPB_LICENSE_INSTALL_DIR}/icons/open-iconic")
install(FILES app/icons/devicons/LICENSE.txt
    DESTINATION "${SNMPB_LICENSE_INSTALL_DIR}/icons/devicons")
install(FILES app/icons/feathericons/LICENSE.txt
    DESTINATION "${SNMPB_LICENSE_INSTALL_DIR}/icons/feathericons")
install(FILES app/icons/ionicons/LICENSE.txt
    DESTINATION "${SNMPB_LICENSE_INSTALL_DIR}/icons/ionicons")
install(FILES NOTICE.md DESTINATION .)
# Qt is distributed under the LGPL-3.0 (with the GPL-3.0 it builds on). The texts ship with
# every archive so the licence terms travel with the binaries; NOTICE.md explains how Qt is
# linked and where its source is.
install(FILES licenses/LGPL-3.0.txt licenses/GPL-3.0.txt
    DESTINATION "${SNMPB_LICENSE_INSTALL_DIR}/Qt")
install(FILES
    docs/portable-package-readme.txt
    docs/getting-started.md
    docs/release-candidate-validation.md
    docs/release-notes.md
    docs/security.md
    docs/settings-schema.md
    docs/maintenance-five-validation.md
    docs/query-macros-milestone-1.md
    docs/query-macros-milestone-2.md
    docs/manual-real-device-acceptance.md
    docs/third-party-dependencies.md
    docs/mib-library-v1.md
    DESTINATION .
)

# Qt binary SDKs provide machine-readable module license inventories. Stage
# the inventories for runtime modules when the SDK supplies them, without
# encoding a developer-specific Qt installation path.
# The Qt installation prefix comes from the Qt6 package location (<prefix>/lib/cmake/Qt6),
# which is the same on every platform; the library path differs (macOS frameworks sit three
# directories deeper), so it is not used.
get_filename_component(SNMPB_QT_PREFIX "${Qt6_DIR}/../../.." ABSOLUTE)
file(GLOB SNMPB_QT_LICENSE_INVENTORIES
    "${SNMPB_QT_PREFIX}/sbom/qtbase-*.spdx"
    "${SNMPB_QT_PREFIX}/sbom/qtbase-*.spdx.json"
    "${SNMPB_QT_PREFIX}/sbom/qtsvg-*.spdx"
    "${SNMPB_QT_PREFIX}/sbom/qtsvg-*.spdx.json"
)
if(SNMPB_QT_LICENSE_INVENTORIES)
    install(FILES ${SNMPB_QT_LICENSE_INVENTORIES}
        DESTINATION "${SNMPB_LICENSE_INSTALL_DIR}/Qt"
    )
else()
    message(STATUS "No Qt licence inventories (SPDX) found under ${SNMPB_QT_PREFIX}/sbom")
endif()

if(WIN32)
    set(CPACK_GENERATOR ZIP)
    set(CPACK_PACKAGE_FILE_NAME
        "MIB-Navigator-${SNMPB_RELEASE_LABEL}-windows-x64")
elseif(APPLE)
    set(CPACK_GENERATOR TGZ)
    set(CPACK_PACKAGE_FILE_NAME
        "MIB-Navigator-${SNMPB_RELEASE_LABEL}-macos-${CMAKE_SYSTEM_PROCESSOR}")
else()
    set(CPACK_GENERATOR TGZ)
    set(CPACK_PACKAGE_FILE_NAME
        "MIB-Navigator-${SNMPB_RELEASE_LABEL}-linux-${CMAKE_SYSTEM_PROCESSOR}")
endif()
set(CPACK_PACKAGE_NAME "MIB Navigator")
set(CPACK_PACKAGE_VENDOR "MIB Navigator contributors")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "Cross-platform graphical SNMP and MIB browser")
set(CPACK_PACKAGE_VERSION "${SNMPB_RELEASE_LABEL}")
set(CPACK_RESOURCE_FILE_LICENSE "${PROJECT_SOURCE_DIR}/license.txt")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
include(CPack)
