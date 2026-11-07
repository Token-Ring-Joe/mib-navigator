if(NOT DEFINED SNMPB_EXECUTABLE OR
   NOT DEFINED SNMPB_QT_BIN_DIR OR
   NOT DEFINED SNMPB_RUNTIME_DATA_DIR OR
   NOT DEFINED SNMPB_SOURCE_DIR OR
   NOT DEFINED SNMPB_SMOKE_DIR)
    message(FATAL_ERROR "SNMPB launch smoke test arguments are incomplete")
endif()

file(REMOVE_RECURSE "${SNMPB_SMOKE_DIR}")
set(settings_dir "${SNMPB_SMOKE_DIR}/snmpb.sourceforge.net")
set(library_root "${SNMPB_SMOKE_DIR}/MIB-Library")
file(MAKE_DIRECTORY "${settings_dir}")
file(MAKE_DIRECTORY "${library_root}/Standards")
file(MAKE_DIRECTORY "${library_root}/Unassigned" "${SNMPB_SMOKE_DIR}/editor")
file(WRITE "${library_root}/Unassigned/editor-dependency.txt"
    "SMOKE-EDITOR-DEPENDENCY-MIB DEFINITIONS ::= BEGIN\nIMPORTS enterprises FROM SNMPv2-SMI;\nsmokeEditorBranch OBJECT IDENTIFIER ::= { enterprises 64993 }\nEND\n")
set(editor_fixture "${SNMPB_SMOKE_DIR}/editor/outside-profile.mib")
file(WRITE "${editor_fixture}"
    "SMOKE-EDITOR-ROOT-MIB DEFINITIONS ::= BEGIN\nIMPORTS smokeEditorBranch FROM SMOKE-EDITOR-DEPENDENCY-MIB;\nsmokeEditorLeaf OBJECT IDENTIFIER ::= { smokeEditorBranch 1 }\nEND\n")
set(legacy_root "${SNMPB_SMOKE_DIR}/Legacy-only-MIBs")
file(MAKE_DIRECTORY "${legacy_root}")
file(WRITE "${legacy_root}/legacy-only.mib"
    "LEGACY-ONLY-MIB DEFINITIONS ::= BEGIN\nIMPORTS enterprises FROM SNMPv2-SMI;\nlegacyOnly OBJECT IDENTIFIER ::= { enterprises 900099 }\nEND\n")
file(WRITE "${settings_dir}/SnmpB.ini"
"[ui]\n"
"theme=dark\n"
"\n"
"[mib-library]\n"
"root=${library_root}\n"
"current-profile=builtin.standards-mib2\n"
"\n"
"[mibpaths]\n"
"1\\dir=${legacy_root}\n"
"size=1\n"
"\n"
"[mibpreloads]\n"
"1\\mib=SNMPv2-MIB\n"
"2\\mib=IF-MIB\n"
"size=2\n"
)

if(WIN32)
    get_filename_component(executable_dir "${SNMPB_EXECUTABLE}" DIRECTORY)
    get_filename_component(deployment_root "${executable_dir}" DIRECTORY)
    set(offscreen_plugin "${deployment_root}/plugins/platforms/qoffscreen.dll")
    if(EXISTS "${executable_dir}/qt.conf" AND NOT EXISTS "${offscreen_plugin}")
        message(FATAL_ERROR
            "Installed launch smoke requires the deployed Qt offscreen platform plugin: ${offscreen_plugin}")
    endif()
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
        --unset=SMIPATH
        "QT_QPA_PLATFORM=offscreen"
        "PATH=${SNMPB_QT_BIN_DIR};$ENV{PATH}"
        "${SNMPB_EXECUTABLE}"
        --launch-smoke-test "${SNMPB_SMOKE_DIR}"
        --smoke-mib "${editor_fixture}"
    WORKING_DIRECTORY "${SNMPB_SMOKE_DIR}"
    RESULT_VARIABLE smoke_result
    OUTPUT_VARIABLE smoke_stdout
    ERROR_VARIABLE smoke_stderr
    TIMEOUT 45
)

message(STATUS "SnmpB launch smoke stdout:\n${smoke_stdout}")
message(STATUS "SnmpB launch smoke stderr:\n${smoke_stderr}")

set(smoke_log "${SNMPB_SMOKE_DIR}/launch-smoke.log")
if(EXISTS "${smoke_log}")
    file(READ "${smoke_log}" smoke_details)
    message(STATUS "SnmpB launch smoke checks:\n${smoke_details}")
endif()

if(NOT smoke_result EQUAL 0)
    message(FATAL_ERROR "SnmpB launch smoke failed with exit code ${smoke_result}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
        --unset=SMIPATH
        "QT_QPA_PLATFORM=offscreen"
        "PATH=${SNMPB_QT_BIN_DIR};$ENV{PATH}"
        "${SNMPB_EXECUTABLE}"
        --launch-smoke-test "${SNMPB_SMOKE_DIR}"
        --smoke-mib "${editor_fixture}"
    WORKING_DIRECTORY "${SNMPB_SMOKE_DIR}"
    RESULT_VARIABLE reload_result
    TIMEOUT 45
)

if(NOT reload_result EQUAL 0)
    message(FATAL_ERROR "SnmpB persistence reload failed with exit code ${reload_result}")
endif()

set(expected_files
    "${SNMPB_SMOKE_DIR}/snmpb.sourceforge.net/SnmpB.ini"
    "${SNMPB_SMOKE_DIR}/snmpb.sourceforge.net/smi.conf"
    "${SNMPB_SMOKE_DIR}/snmpb.sourceforge.net/boot_counter.conf"
    "${SNMPB_SMOKE_DIR}/snmpb.sourceforge.net/log.conf"
)

foreach(expected_file IN LISTS expected_files)
    if(NOT EXISTS "${expected_file}")
        message(FATAL_ERROR "Expected isolated configuration file missing: ${expected_file}")
    endif()
endforeach()

# Saved Agent Profiles are retired. The application must neither create nor read agents.conf
# (a user's existing file is left alone), so a fresh configuration never contains one.
foreach(retired_file agents.conf device-tree.conf profile-metadata.conf credential-bindings.conf)
    if(EXISTS "${SNMPB_SMOKE_DIR}/snmpb.sourceforge.net/${retired_file}")
        message(FATAL_ERROR "A retired configuration file was created: ${retired_file}")
    endif()
endforeach()

file(READ "${settings_dir}/SnmpB.ini" saved_settings)
# The Library owns live paths; old settings are retained without writing bundled
# install folders back into the retired preference array.
string(FIND "${saved_settings}" "${legacy_root}" retained_legacy_path)
if(retained_legacy_path EQUAL -1)
    message(FATAL_ERROR "Retired legacy path settings were overwritten or discarded")
endif()
file(READ "${settings_dir}/smi.conf" parser_configuration)
foreach(folder IN ITEMS "${library_root}/Standards" "${library_root}/Unassigned")
    string(FIND "${parser_configuration}" "${folder}" library_path_index)
    if(library_path_index EQUAL -1)
        message(FATAL_ERROR "Startup parser configuration omits Library folder: ${folder}")
    endif()
endforeach()
string(FIND "${parser_configuration}" "${legacy_root}" active_legacy_path)
if(NOT active_legacy_path EQUAL -1)
    message(FATAL_ERROR "Retired legacy path still controls the startup parser")
endif()
