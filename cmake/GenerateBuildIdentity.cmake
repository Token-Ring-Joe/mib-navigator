cmake_minimum_required(VERSION 3.22)
if(NOT DEFINED SNMPB_SOURCE_DIR OR NOT DEFINED SNMPB_BUILD_INFO)
    message(FATAL_ERROR "Build identity requires source and output paths")
endif()
string(TIMESTAMP built_utc "%Y-%m-%d %H:%M:%S UTC" UTC)
set(revision "unavailable")
set(source_state "source archive")
if(SNMPB_GIT AND EXISTS "${SNMPB_GIT}")
    execute_process(COMMAND "${SNMPB_GIT}" rev-parse --short=12 HEAD
        WORKING_DIRECTORY "${SNMPB_SOURCE_DIR}" OUTPUT_VARIABLE git_revision
        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET RESULT_VARIABLE git_result)
    if(git_result EQUAL 0 AND git_revision MATCHES "^[0-9a-fA-F]+$")
        set(revision "${git_revision}")
        execute_process(COMMAND "${SNMPB_GIT}" status --porcelain --untracked-files=normal
                -- app cmake CMakeLists.txt libsmi snmp++ libtomcrypt
            WORKING_DIRECTORY "${SNMPB_SOURCE_DIR}" OUTPUT_VARIABLE git_status
            ERROR_QUIET RESULT_VARIABLE status_result)
        if(NOT status_result EQUAL 0)
            set(source_state "source state unavailable")
        elseif(git_status STREQUAL "")
            set(source_state "clean source")
        else()
            set(source_state "with local changes")
        endif()
    endif()
endif()
get_filename_component(output_dir "${SNMPB_BUILD_INFO}" DIRECTORY)
file(MAKE_DIRECTORY "${output_dir}")
set(header [=[#pragma once
#define SNMPB_BUILD_UTC "@built_utc@"
#define SNMPB_BUILD_REVISION "@revision@"
#define SNMPB_BUILD_SOURCE_STATE "@source_state@"
]=])
string(CONFIGURE "${header}" header @ONLY)
file(WRITE "${SNMPB_BUILD_INFO}.tmp" "${header}")
configure_file("${SNMPB_BUILD_INFO}.tmp" "${SNMPB_BUILD_INFO}" COPYONLY)
