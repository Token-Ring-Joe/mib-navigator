# End-to-end check that MIB Navigator keeps loading what it can and explains what it could not.
#
# Each scenario builds an isolated MIB library and profile, starts the real application headless
# in launch-smoke mode, and checks the Environment status and problem report that the application
# writes to launch-smoke.log. Nothing here touches the user's real settings or MIB folders.
#
# Required: SNMPB_EXECUTABLE SNMPB_QT_BIN_DIR SNMPB_SOURCE_DIR SNMPB_TOLERANCE_DIR

# Script mode starts with old policies; IN_LIST and the list() operations below need newer ones.
cmake_minimum_required(VERSION 3.20)

foreach(required SNMPB_EXECUTABLE SNMPB_QT_BIN_DIR SNMPB_SOURCE_DIR SNMPB_TOLERANCE_DIR)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "Tolerance scenarios: ${required} is not set")
    endif()
endforeach()

file(REMOVE_RECURSE "${SNMPB_TOLERANCE_DIR}")
set(failures "")

# ---- MIB source texts -------------------------------------------------------------------------
set(GOOD_MIB [=[GOOD-MIB DEFINITIONS ::= BEGIN
IMPORTS MODULE-IDENTITY, OBJECT-TYPE, Integer32, enterprises FROM SNMPv2-SMI;
goodMIB MODULE-IDENTITY
    LAST-UPDATED "202601010000Z"
    ORGANIZATION "Test"
    CONTACT-INFO "none"
    DESCRIPTION "Good module."
    ::= { enterprises 90001 }
goodValue OBJECT-TYPE
    SYNTAX Integer32
    MAX-ACCESS read-only
    STATUS current
    DESCRIPTION "value"
    ::= { goodMIB 1 }
END
]=])
# The object definition has no "::=", which is a syntax error on line 14.
set(BROKEN_MIB [=[BROKEN-MIB DEFINITIONS ::= BEGIN
IMPORTS MODULE-IDENTITY, OBJECT-TYPE, Integer32, enterprises FROM SNMPv2-SMI;
brokenMIB MODULE-IDENTITY
    LAST-UPDATED "202601010000Z"
    ORGANIZATION "Test"
    CONTACT-INFO "none"
    DESCRIPTION "Module with a syntax error."
    ::= { enterprises 90002 }
brokenValue OBJECT-TYPE
    SYNTAX Integer32
    MAX-ACCESS read-only
    STATUS current
    DESCRIPTION "value"
    { brokenMIB 1 }
END
]=])
# Imports a module that is not in the library (line 3).
set(IMPORTS_ABSENT_MIB [=[IMPORTS-ABSENT-MIB DEFINITIONS ::= BEGIN
IMPORTS MODULE-IDENTITY, enterprises FROM SNMPv2-SMI
        SomethingTC FROM NOT-SHIPPED-MIB;
absentMIB MODULE-IDENTITY
    LAST-UPDATED "202601010000Z"
    ORGANIZATION "Test"
    CONTACT-INFO "none"
    DESCRIPTION "Imports a module that is not in the library."
    ::= { enterprises 90003 }
END
]=])
set(DUP_MIB_A [=[DUP-MIB DEFINITIONS ::= BEGIN
IMPORTS MODULE-IDENTITY, enterprises FROM SNMPv2-SMI;
dupMIB MODULE-IDENTITY
    LAST-UPDATED "202601010000Z"
    ORGANIZATION "A" CONTACT-INFO "none" DESCRIPTION "Copy A."
    ::= { enterprises 90010 }
END
]=])
string(REPLACE "Copy A." "Copy B, different content." DUP_MIB_B "${DUP_MIB_A}")
set(IMPORTER_MIB [=[IMPORTER-MIB DEFINITIONS ::= BEGIN
IMPORTS MODULE-IDENTITY, enterprises FROM SNMPv2-SMI
        dupMIB FROM DUP-MIB;
importerMIB MODULE-IDENTITY
    LAST-UPDATED "202601010000Z"
    ORGANIZATION "T" CONTACT-INFO "none" DESCRIPTION "Imports a duplicated module."
    ::= { dupMIB 1 }
END
]=])

# ---- scenario runner --------------------------------------------------------------------------
# run_scenario(<name>
#   FILES  <relative/path=VARNAME> ...     files created under <library>/Acme
#   ROOTS  <identity@relative/path> ...    profile roots (the file need not exist)
#   [MODIFIED_ROOTS <identity> ...]        record a wrong hash so the root counts as edited
#   [UNRESOLVED <identity> ...]            modules the profile lists that the Library lacks
#   [EDIT_FILE <relative/path> EDIT_FROM <text> EDIT_TO <text>]
#                                          open that file in the editor, replace the text, save, and
#                                          wait for the profile to rebuild (logged as BEFORE/AFTER-EDIT)
#   [LIVE]                                 also start the loopback-only test agent and issue a real Get
#                                          through the connection bar (needs SNMPB_PYTHON)
#   REQUIRE_EXIT_ZERO | ALLOW_EXIT_ONE
#   EXPECT <regex> ...)                    each must match launch-smoke.log
function(run_scenario name)
    cmake_parse_arguments(S "REQUIRE_EXIT_ZERO;ALLOW_EXIT_ONE;LIVE" ""
        "FILES;ROOTS;MODIFIED_ROOTS;UNRESOLVED;EXPECT;REJECT;EDIT_FILE;EDIT_FROM;EDIT_TO" ${ARGN})
    set(dir "${SNMPB_TOLERANCE_DIR}/${name}")
    set(library "${dir}/MIB-Library")
    file(MAKE_DIRECTORY "${library}/Standards" "${dir}/snmpb.sourceforge.net" "${dir}/state/mibs")
    foreach(module SNMPv2-SMI SNMPv2-TC SNMPv2-CONF SNMPv2-MIB)
        file(COPY "${SNMPB_SOURCE_DIR}/libsmi/mibs/ietf/${module}" DESTINATION "${library}/Standards")
    endforeach()
    foreach(spec IN LISTS S_FILES)
        string(REPLACE "=" ";" parts "${spec}")
        list(GET parts 0 relative)
        list(GET parts 1 variable)
        file(WRITE "${library}/Acme/${relative}" "${${variable}}")
    endforeach()
    set(roots_json "")
    foreach(spec IN LISTS S_ROOTS)
        string(REPLACE "@" ";" parts "${spec}")
        list(GET parts 0 identity)
        list(GET parts 1 relative)
        set(path "${library}/Acme/${relative}")
        if(EXISTS "${path}")
            file(SHA256 "${path}" hash)
        else()
            set(hash "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa")
        endif()
        if(identity IN_LIST S_MODIFIED_ROOTS)
            set(hash "0000000000000000000000000000000000000000000000000000000000000000")
        endif()
        if(roots_json)
            string(APPEND roots_json ",")
        endif()
        string(APPEND roots_json
            "{\"identities\":[\"${identity}\"],\"path\":\"${path}\",\"reason\":\"added\",\"sha256\":\"${hash}\"}")
    endforeach()
    set(unresolved_json "")
    if(S_UNRESOLVED)
        list(TRANSFORM S_UNRESOLVED PREPEND "\"")
        list(TRANSFORM S_UNRESOLVED APPEND "\"")
        list(JOIN S_UNRESOLVED "," joined)
        set(unresolved_json ",\"unresolvedLegacyModules\":[${joined}]")
    endif()
    file(WRITE "${dir}/state/mibs/profiles-v1.json"
        "{\"ordinaryProfilesMigrated\":true,\"schemaVersion\":5,\"profiles\":[{\"id\":\"custom.test\","
        "\"manifestVersion\":1,\"name\":\"Test profile\",\"roots\":[${roots_json}],"
        "\"scope\":[{\"id\":\"collection:Acme\",\"path\":\"${library}/Acme\"},"
        "{\"id\":\"collection:Standards\",\"path\":\"${library}/Standards\"}],"
        "\"type\":\"custom\"${unresolved_json}}]}")
    file(WRITE "${dir}/snmpb.sourceforge.net/SnmpB.ini"
        "[mib-library]\nroot=${library}\ncurrent-profile=custom.test\n")

    set(smoke_mib "${library}/Standards/SNMPv2-MIB")
    set(edit_args "")
    if(S_EDIT_FILE)
        set(smoke_mib "${library}/Acme/${S_EDIT_FILE}")
        set(edit_args --smoke-edit-from "${S_EDIT_FROM}" --smoke-edit-to "${S_EDIT_TO}")
    endif()
    # The ";" in PATH must survive being stored in a list variable, so it is escaped.
    set(app_command "${CMAKE_COMMAND}" -E env --unset=SMIPATH "QT_QPA_PLATFORM=offscreen"
        "PATH=${SNMPB_QT_BIN_DIR}\\;$ENV{PATH}"
        "${SNMPB_EXECUTABLE}" --launch-smoke-test "${dir}"
        --smoke-mib "${smoke_mib}" ${edit_args})
    if(S_LIVE)
        # The test agent listens on 127.0.0.1 only and answers until asked to exit (or idle for 60 seconds). Both processes run
        # at the same time.
        string(RANDOM LENGTH 4 ALPHABET 123456789 port_seed)
        math(EXPR live_port "20000 + ${port_seed} % 20000")
        execute_process(
            COMMAND "${SNMPB_PYTHON}" -I "${SNMPB_SOURCE_DIR}/tools/loopback_snmp_agent.py"
                ${live_port} smokero "${dir}/agent.log" 0 60
            COMMAND ${app_command} --smoke-network
                --smoke-live-target "127.0.0.1:${live_port}"
                --smoke-live-credential "Smoke lab - SNMPv2c"
            WORKING_DIRECTORY "${dir}"
            RESULT_VARIABLE exit_codes
            OUTPUT_VARIABLE out ERROR_VARIABLE err
            TIMEOUT 120)
        list(GET exit_codes -1 exit_code)
    else()
        execute_process(
            COMMAND ${app_command}
            WORKING_DIRECTORY "${dir}"
            RESULT_VARIABLE exit_code
            OUTPUT_VARIABLE out ERROR_VARIABLE err
            TIMEOUT 90)
    endif()

    set(problems "")
    set(log "")
    if(EXISTS "${dir}/launch-smoke.log")
        file(READ "${dir}/launch-smoke.log" log)
    else()
        list(APPEND problems "no launch-smoke.log was written (exit ${exit_code})")
    endif()
    if(S_REQUIRE_EXIT_ZERO AND NOT exit_code EQUAL 0)
        list(APPEND problems "expected the application to start and finish normally, exit code was ${exit_code}")
    endif()
    if(NOT exit_code STREQUAL "0" AND NOT exit_code STREQUAL "1")
        list(APPEND problems "application crashed or timed out: ${exit_code}")
    endif()
    foreach(pattern IN LISTS S_EXPECT)
        if(NOT log MATCHES "${pattern}")
            list(APPEND problems "log does not match: ${pattern}")
        endif()
    endforeach()
    foreach(pattern IN LISTS S_REJECT)
        if(log MATCHES "${pattern}")
            list(APPEND problems "log unexpectedly matches: ${pattern}")
        endif()
    endforeach()
    if(problems)
        string(REPLACE ";" "\n    - " detail "${problems}")
        message(SEND_ERROR "Scenario ${name} FAILED:\n    - ${detail}\n--- log ---\n${log}")
    else()
        message(STATUS "Scenario ${name}: ok")
    endif()
endfunction()

# ---- scenarios --------------------------------------------------------------------------------
# 1. Control: everything is fine.
run_scenario(good REQUIRE_EXIT_ZERO
    FILES "Prod/GOOD-MIB=GOOD_MIB" ROOTS "GOOD-MIB@Prod/GOOD-MIB"
    EXPECT "ENVIRONMENT status=Complete loaded=2 failed=0 errors=0 warnings=0")

# 2. A MIB with a syntax error still loads; the report names the line and the parser's reason.
run_scenario(syntax-error REQUIRE_EXIT_ZERO
    FILES "Prod/GOOD-MIB=GOOD_MIB" "Prod/BROKEN-MIB=BROKEN_MIB"
    ROOTS "GOOD-MIB@Prod/GOOD-MIB" "BROKEN-MIB@Prod/BROKEN-MIB"
    EXPECT "ENVIRONMENT status=[A-Za-z]+ loaded=3"
           "PROBLEM severity=Warning category=\"Parse error\" module=BROKEN-MIB file=BROKEN-MIB line=14 why=\"syntax error")

# 3. A MIB importing a module that is not in the library still loads and the report says so.
run_scenario(missing-import REQUIRE_EXIT_ZERO
    FILES "Prod/GOOD-MIB=GOOD_MIB" "Prod/IMPORTS-ABSENT-MIB=IMPORTS_ABSENT_MIB"
    ROOTS "GOOD-MIB@Prod/GOOD-MIB" "IMPORTS-ABSENT-MIB@Prod/IMPORTS-ABSENT-MIB"
    EXPECT "ENVIRONMENT status=Partial loaded=3"
           "category=\"Missing dependency\" module=IMPORTS-ABSENT-MIB file=IMPORTS-ABSENT-MIB line=3 why=\"IMPORTS-ABSENT-MIB imports NOT-SHIPPED-MIB")

# 4. The profile lists a MIB the Library cannot supply: skipped and reported.
run_scenario(listed-not-found REQUIRE_EXIT_ZERO
    FILES "Prod/GOOD-MIB=GOOD_MIB" ROOTS "GOOD-MIB@Prod/GOOD-MIB" UNRESOLVED "ABSENT-MIB"
    EXPECT "ENVIRONMENT status=Partial loaded=2"
           "category=\"Listed MIB not in the Library\" module=ABSENT-MIB")

# 5. An edited MIB still loads (its current content) and carries a Modified note, not a problem.
run_scenario(edited-root REQUIRE_EXIT_ZERO
    FILES "Prod/GOOD-MIB=GOOD_MIB" ROOTS "GOOD-MIB@Prod/GOOD-MIB" MODIFIED_ROOTS "GOOD-MIB"
    EXPECT "ENVIRONMENT status=Complete loaded=2 failed=0 errors=0 warnings=0"
           "PROBLEM severity=Note category=\"Modified\" module=GOOD-MIB")

# 6. Two different files declare the same module: one is used, and the report says which and why.
run_scenario(duplicate-providers REQUIRE_EXIT_ZERO
    FILES "Prod/IMPORTER-MIB=IMPORTER_MIB" "Alpha/DUP-MIB=DUP_MIB_A" "Beta/DUP-MIB=DUP_MIB_B"
    ROOTS "IMPORTER-MIB@Prod/IMPORTER-MIB"
    EXPECT "ENVIRONMENT status=Complete loaded=3"
           "category=\"Duplicate providers\" module=DUP-MIB"
           "Alpha/DUP-MIB was used because it comes first by path"
           "Also found: [^\"]*Beta/DUP-MIB")

# 7. One selected file is gone: reported as an error, the other root still loads.
run_scenario(missing-root-file REQUIRE_EXIT_ZERO
    FILES "Prod/GOOD-MIB=GOOD_MIB"
    ROOTS "GOOD-MIB@Prod/GOOD-MIB" "VANISHED-MIB@Prod/VANISHED-MIB"
    EXPECT "ENVIRONMENT status=[A-Za-z]+ loaded=2 .*errors=1"
           "PROBLEM severity=Error category=\"MIB not loaded\" module=VANISHED-MIB")

# 8. An empty profile is a clean, empty environment, not an error.
run_scenario(empty-profile ALLOW_EXIT_ONE
    FILES "Prod/GOOD-MIB=GOOD_MIB" ROOTS ""
    EXPECT "ENVIRONMENT status=Complete loaded=0 failed=0 errors=0 warnings=0"
    REJECT "Profile Environment build failed")

# 9. Nothing in the profile can be loaded: published as empty, with the reason in the report.
run_scenario(only-missing-root ALLOW_EXIT_ONE
    FILES "Prod/GOOD-MIB=GOOD_MIB" ROOTS "VANISHED-MIB@Prod/VANISHED-MIB"
    EXPECT "ENVIRONMENT status=Partial loaded=0 .*errors=1"
           "PROBLEM severity=Error category=\"MIB not loaded\" module=VANISHED-MIB"
    REJECT "Profile Environment build failed")

# 10. Everything at once: every problem is reported and everything loadable still loads.
run_scenario(everything-wrong REQUIRE_EXIT_ZERO
    FILES "Prod/GOOD-MIB=GOOD_MIB" "Prod/BROKEN-MIB=BROKEN_MIB"
          "Prod/IMPORTS-ABSENT-MIB=IMPORTS_ABSENT_MIB"
    ROOTS "GOOD-MIB@Prod/GOOD-MIB" "BROKEN-MIB@Prod/BROKEN-MIB"
          "IMPORTS-ABSENT-MIB@Prod/IMPORTS-ABSENT-MIB" "VANISHED-MIB@Prod/VANISHED-MIB"
    MODIFIED_ROOTS "GOOD-MIB" UNRESOLVED "ABSENT-MIB"
    EXPECT "ENVIRONMENT status=Partial loaded=4"
           "category=\"Parse error\" module=BROKEN-MIB"
           "category=\"Missing dependency\" module=IMPORTS-ABSENT-MIB"
           "category=\"Listed MIB not in the Library\" module=ABSENT-MIB"
           "category=\"MIB not loaded\" module=VANISHED-MIB"
           "category=\"Modified\" module=GOOD-MIB")

# 11. Edit -> Save -> Rebuild: the user opens the MIB that has the syntax error, fixes it in the
#     editor and saves. The active profile rebuilds, the Parse error is gone, the fixed object is
#     in the Tree, and the profile records the new file hash (so there is no Modified note either).
run_scenario(edit-save-rebuild REQUIRE_EXIT_ZERO
    FILES "Prod/GOOD-MIB=GOOD_MIB" "Prod/BROKEN-MIB=BROKEN_MIB"
    ROOTS "GOOD-MIB@Prod/GOOD-MIB" "BROKEN-MIB@Prod/BROKEN-MIB"
    EDIT_FILE "Prod/BROKEN-MIB" EDIT_FROM "    { brokenMIB 1 }" EDIT_TO "    ::= { brokenMIB 1 }"
    EXPECT "BEFORE-EDIT PROBLEM severity=Warning category=\"Parse error\" module=BROKEN-MIB line=14"
           "AFTER-EDIT ENVIRONMENT loaded=3 failed=0 errors=0 warnings=0"
           "PASS: edit: the editor listed the syntax error before the fix"
           "PASS: edit: editor diagnostics never show the internal staging file name"
           "PASS: edit: the editor does not keep showing the pre-save verification result"
           "PASS: edit: saving rebuilt the active profile's environment"
           "PASS: edit: the object defined by the fixed MIB is now in the Tree"
           "PASS: edit: the profile records the saved file's new hash"
           "PASS: edit: the diagnostic log records the save as rebuilding the active profile"
    REJECT "AFTER-EDIT PROBLEM severity=(Warning|Error)" "AFTER-EDIT PROBLEM severity=Note category=\"Modified\"" "FAIL")

# 12. A real request through the connection bar: Target + Credential -> SNMPv2c Get against a
#     loopback-only test agent. Proves the bar, not the old device list, drives requests, that
#     the credential's version and community are used, and that the target is remembered.
if(SNMPB_PYTHON)
    run_scenario(live-get-through-connection-bar REQUIRE_EXIT_ZERO LIVE
        FILES "Prod/GOOD-MIB=GOOD_MIB" ROOTS "GOOD-MIB@Prod/GOOD-MIB"
        EXPECT "PASS: live: the Get through the connection bar returned the agent's sysDescr"
               "PASS: live: the target was remembered in the recent-target history"
               "PASS: live: a request that gets no answer shows an error line above the Results table"
               "PASS: live: a watch polls repeatedly and shows change and rate"
               "PASS: live: the watch kept its own target after the connection bar changed"
               "PASS: live: the Watch tab shows the row and its change"
               "PASS: live: removing a watch removes its row"
               "PASS: live: the history remembers the credential used with the target"
               "PASS: live: a wrong-type answer shows a plain-English warning naming both types"
               "PASS: live: the Results table shows the received type and raw value beside the interpretation"
        REJECT "FAIL")
    set(agent_log "")
    if(EXISTS "${SNMPB_TOLERANCE_DIR}/live-get-through-connection-bar/agent.log")
        file(READ "${SNMPB_TOLERANCE_DIR}/live-get-through-connection-bar/agent.log" agent_log)
    endif()
    if(NOT agent_log MATCHES "request type=0xa0 version=1 community=smokero oid=1.3.6.1.2.1.1.1.0")
        message(SEND_ERROR "live scenario: the agent did not see an SNMPv2c Get with the saved community:\n${agent_log}")
    endif()
else()
    message(STATUS "Scenario live-get-through-connection-bar: skipped (no Python interpreter)")
endif()

if(failures)
    message(FATAL_ERROR "Tolerance scenarios failed")
endif()
