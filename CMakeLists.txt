cmake_minimum_required(VERSION 3.22)

project(SnmpBNext
    VERSION 1.0.0
    LANGUAGES C CXX
)

set(SNMPB_RELEASE_LABEL "${PROJECT_VERSION}-rc1" CACHE STRING
    "MIB Navigator display and package release label")
configure_file(
    cmake/snmpb_version.h.in
    generated/snmpb_version.h
    @ONLY
)

include(CTest)
include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.10 REQUIRED COMPONENTS Core Widgets Network Svg)
# Optional: only the loopback SNMP test scenario needs Python.
find_package(Python3 COMPONENTS Interpreter QUIET)

message(STATUS "Qt version: ${Qt6_VERSION}")
message(STATUS "MIB Navigator Qt/CMake environment configured successfully")

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
install(FILES
    docs/portable-package-readme.txt
    docs/release-candidate-validation.md
    docs/release-notes-1.0.0-rc1.md
    docs/security.md
    docs/manual-real-device-acceptance.md
    docs/third-party-dependencies.md
    docs/mib-library-v1.md
    DESTINATION .
)

# Qt binary SDKs provide machine-readable module license inventories. Stage
# the inventories for runtime modules when the SDK supplies them, without
# encoding a developer-specific Qt installation path.
get_target_property(SNMPB_QT_CORE_LOCATION Qt6::Core IMPORTED_LOCATION_RELEASE)
if(NOT SNMPB_QT_CORE_LOCATION)
    get_target_property(SNMPB_QT_CORE_LOCATION Qt6::Core IMPORTED_LOCATION)
endif()
if(SNMPB_QT_CORE_LOCATION)
    get_filename_component(SNMPB_QT_BIN_DIR "${SNMPB_QT_CORE_LOCATION}" DIRECTORY)
    get_filename_component(SNMPB_QT_PREFIX "${SNMPB_QT_BIN_DIR}" DIRECTORY)
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
    endif()
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
