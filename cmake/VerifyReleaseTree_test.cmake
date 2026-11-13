cmake_minimum_required(VERSION 3.22)
if(NOT DEFINED SNMPB_FIXTURE_DIR)
    message(FATAL_ERROR "SNMPB_FIXTURE_DIR must be an isolated test directory")
endif()
set(verifier "${CMAKE_CURRENT_LIST_DIR}/VerifyReleaseTree.cmake")
# These are deliberately inert placeholders. This test exercises the packaging
# policy; actual executable/deployment validation runs against the installed tree.
file(STRINGS "${verifier}" quoted REGEX "^[ ]+\"[^\"]+\"$")
file(REMOVE_RECURSE "${SNMPB_FIXTURE_DIR}")
file(MAKE_DIRECTORY "${SNMPB_FIXTURE_DIR}")
foreach(token IN LISTS quoted)
    string(STRIP "${token}" token)
    string(REGEX REPLACE "^\"|\"$" "" path "${token}")
    if(path MATCHES "^(share/snmpb/|bin/|plugins/|MIB Navigator\\.app/)" OR
       path MATCHES "^(portable-package-readme\\.txt|.*\\.md)$")
        if(path MATCHES "[$]" OR path MATCHES "[*]")
            continue()
        endif()
        get_filename_component(parent "${SNMPB_FIXTURE_DIR}/${path}" DIRECTORY)
        file(MAKE_DIRECTORY "${parent}")
        file(WRITE "${SNMPB_FIXTURE_DIR}/${path}" "inert fixture")
    endif()
endforeach()
# The Qt licence inventories are required by a glob, which the scrape above cannot see.
file(MAKE_DIRECTORY "${SNMPB_FIXTURE_DIR}/share/snmpb/licenses/Qt")
file(WRITE "${SNMPB_FIXTURE_DIR}/share/snmpb/licenses/Qt/qtbase-fixture.spdx" "inert fixture")
file(WRITE "${SNMPB_FIXTURE_DIR}/share/snmpb/licenses/Qt/qtsvg-fixture.spdx" "inert fixture")
function(check_policy expected)
    execute_process(COMMAND "${CMAKE_COMMAND}" "-DSNMPB_PACKAGE_ROOT=${SNMPB_FIXTURE_DIR}"
        -P "${verifier}" RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(expected STREQUAL "clean")
        if(NOT result EQUAL 0)
            message(FATAL_ERROR "Clean fixture rejected: ${out}${err}")
        endif()
    elseif(result EQUAL 0 OR NOT "${err}" MATCHES "Private runtime configuration was packaged")
        message(FATAL_ERROR "Private fixture was not rejected: ${out}${err}")
    endif()
endfunction()
check_policy(clean)
foreach(name IN ITEMS credential-identities.conf.v2.json CREDENTIAL-IDENTITIES.CONF.V2.JSON
        watches-v1.json watches-v1.json.lock query-macros-v1.json QUERY-MACROS-V1.JSON.LOCK profiles-v1.json profile-folders-v1.json
        dependency-index-v1.json MIB-Navigator-fixture.log)
    file(MAKE_DIRECTORY "${SNMPB_FIXTURE_DIR}/nested")
    file(WRITE "${SNMPB_FIXTURE_DIR}/nested/${name}" "private fixture")
    check_policy(private)
    file(REMOVE "${SNMPB_FIXTURE_DIR}/nested/${name}")
endforeach()
check_policy(clean)

# The Qt-inventory rule, forced on so every host checks it: present passes, missing is rejected.
execute_process(COMMAND "${CMAKE_COMMAND}" "-DSNMPB_PACKAGE_ROOT=${SNMPB_FIXTURE_DIR}" -DSNMPB_REQUIRE_QT_INVENTORIES=ON
    -P "${verifier}" RESULT_VARIABLE with_result OUTPUT_VARIABLE with_out ERROR_VARIABLE with_err)
if(NOT with_result EQUAL 0)
    message(FATAL_ERROR "A tree with Qt inventories was rejected: ${with_out}${with_err}")
endif()
file(GLOB inventories "${SNMPB_FIXTURE_DIR}/share/snmpb/licenses/Qt/qtbase-*.spdx")
file(REMOVE ${inventories})
execute_process(COMMAND "${CMAKE_COMMAND}" "-DSNMPB_PACKAGE_ROOT=${SNMPB_FIXTURE_DIR}" -DSNMPB_REQUIRE_QT_INVENTORIES=ON
    -P "${verifier}" RESULT_VARIABLE without_result OUTPUT_VARIABLE without_out ERROR_VARIABLE without_err)
if(without_result EQUAL 0 OR NOT "${without_err}" MATCHES "Qt licence inventories")
    message(FATAL_ERROR "A tree without Qt inventories was accepted: ${without_out}${without_err}")
endif()
# Without the forced rule a Linux-style tree needs none.
execute_process(COMMAND "${CMAKE_COMMAND}" "-DSNMPB_PACKAGE_ROOT=${SNMPB_FIXTURE_DIR}"
    -P "${verifier}" RESULT_VARIABLE linux_result OUTPUT_VARIABLE linux_out ERROR_VARIABLE linux_err)
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux" AND NOT linux_result EQUAL 0)
    message(FATAL_ERROR "A Linux tree without Qt inventories was rejected: ${linux_out}${linux_err}")
endif()
