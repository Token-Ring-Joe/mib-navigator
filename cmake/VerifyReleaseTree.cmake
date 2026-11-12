cmake_minimum_required(VERSION 3.22)

if(NOT DEFINED SNMPB_PACKAGE_ROOT)
    message(FATAL_ERROR "SNMPB_PACKAGE_ROOT must name an installed release tree")
endif()

cmake_path(ABSOLUTE_PATH SNMPB_PACKAGE_ROOT NORMALIZE OUTPUT_VARIABLE root)

set(required_files
    "share/snmpb/licenses/SnmpB/COPYING"
    "share/snmpb/licenses/libsmi/COPYING"
    "share/snmpb/licenses/LibTomCrypt/LICENSE"
    "share/snmpb/licenses/SNMP++/LICENSE"
    "share/snmpb/licenses/icons/open-iconic/LICENSE.txt"
    "share/snmpb/licenses/icons/devicons/LICENSE.txt"
    "share/snmpb/licenses/icons/feathericons/LICENSE.txt"
    "share/snmpb/licenses/icons/ionicons/LICENSE.txt"
    "portable-package-readme.txt"
    "release-candidate-validation.md"
    "release-notes-1.0.0-rc2.md"
    "getting-started.md"
    "security.md"
    "settings-schema.md"
    "maintenance-five-validation.md"
    "query-macros-milestone-1.md"
    "query-macros-milestone-2.md"
    "manual-real-device-acceptance.md"
    "third-party-dependencies.md"
    "NOTICE.md"
)

if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
    list(APPEND required_files
        "bin/mib-navigator.exe"
        "bin/qt.conf"
        "bin/mibs/SNMPv2-MIB"
        "bin/pibs/COPS-PR-SPPI"
        "plugins/platforms/qwindows.dll"
        "bin/Qt6Core.dll"
        "bin/Qt6Gui.dll"
        "bin/Qt6Widgets.dll"
        "bin/Qt6Svg.dll"
    )
    set(mib_root "bin")
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
    list(APPEND required_files
        "MIB Navigator.app/Contents/MacOS/MIB Navigator"
        "MIB Navigator.app/Contents/Resources/mibs/SNMPv2-MIB"
        "MIB Navigator.app/Contents/Resources/pibs/COPS-PR-SPPI"
        "MIB Navigator.app/Contents/Frameworks/QtCore.framework"
        "MIB Navigator.app/Contents/PlugIns/platforms/libqcocoa.dylib"
    )
    set(mib_root "MIB Navigator.app/Contents/Resources")
else()
    list(APPEND required_files
        "bin/mib-navigator"
        "share/snmpb/mibs/SNMPv2-MIB"
        "share/snmpb/pibs/COPS-PR-SPPI"
    )
    set(mib_root "share/snmpb")
endif()

foreach(relative_path IN LISTS required_files)
    if(NOT EXISTS "${root}/${relative_path}")
        message(FATAL_ERROR "Required release file is missing: ${relative_path}")
    endif()
endforeach()

file(GLOB_RECURSE packaged_entries RELATIVE "${root}" "${root}/*")
set(private_names
    agents.conf device-tree.conf profile-metadata.conf graphs.conf
    credential-identities.conf credential-identities.conf.v2.json community-credentials.conf
    credential-bindings.conf usm_users.conf log.conf SnmpB.ini
    boot_counter.conf smi.conf watches-v1.json watches-v1.json.lock
    query-macros-v1.json query-macros-v1.json.lock
    profiles-v1.json profile-folders-v1.json dependency-index-v1.json
)
list(TRANSFORM private_names TOLOWER)
foreach(relative_path IN LISTS packaged_entries)
    get_filename_component(name "${relative_path}" NAME)
    string(TOLOWER "${name}" lower_name)
    if(lower_name IN_LIST private_names OR lower_name MATCHES "^mib-navigator-.*\\.log$")
        message(FATAL_ERROR "Private runtime configuration was packaged: ${relative_path}")
    endif()
    if(relative_path MATCHES "(^|/)(CMakeFiles|_CPack_Packages)(/|$)" OR
       relative_path MATCHES "(CMakeCache\\.txt|\\.(obj|pdb|ilk|exp|lib))$")
        message(FATAL_ERROR "Build/development artifact was packaged: ${relative_path}")
    endif()
    if(relative_path MATCHES "(^|/)Qt6[^/]*d\\.dll$")
        message(FATAL_ERROR "Debug Qt runtime was packaged: ${relative_path}")
    endif()
    if(relative_path MATCHES "(^|/)(lib)?qwt[^/]*\\.(dll|dylib|so)(\\.[0-9]+)*$")
        message(FATAL_ERROR "Unexpected dynamically linked Qwt runtime: ${relative_path}")
    endif()
endforeach()

file(GLOB mib_files "${root}/${mib_root}/mibs/*")
file(GLOB pib_files "${root}/${mib_root}/pibs/*")
list(LENGTH mib_files mib_count)
list(LENGTH pib_files pib_count)
if(mib_count LESS 1 OR pib_count LESS 1)
    message(FATAL_ERROR "Bundled MIB/PIB directories are empty")
endif()

message(STATUS "Verified release tree: ${root}")
message(STATUS "Bundled modules: ${mib_count} MIBs, ${pib_count} PIBs")
