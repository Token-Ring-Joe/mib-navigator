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
file(WRITE "${settings_dir}/SnmpB.ini"
"[mib-library]\n"
"root=${library_root}\n"
"current-profile=builtin.standards-mib2\n"
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
        --smoke-mib "${SNMPB_RUNTIME_DATA_DIR}/mibs/SNMPv2-MIB"
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
        --smoke-mib "${SNMPB_RUNTIME_DATA_DIR}/mibs/SNMPv2-MIB"
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
foreach(runtime_subdir IN ITEMS mibs pibs)
    string(FIND "${saved_settings}"
        "${SNMPB_RUNTIME_DATA_DIR}/${runtime_subdir}" runtime_path_index)
    if(runtime_path_index EQUAL -1)
        message(FATAL_ERROR
            "Fresh configuration did not persist the runtime ${runtime_subdir} path"
        )
    endif()
endforeach()
