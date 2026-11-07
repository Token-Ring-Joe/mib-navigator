# End-to-end check that MIB Navigator keeps loading what it can and explains what it could not.
#
# Each scenario builds an isolated MIB library and profile, starts the real application headless
# in launch-smoke mode, and checks the Environment status and problem report that the application
# writes to launch-smoke.log. Nothing here touches the user's real settings or MIB folders.
#
# Optional: SNMPB_TOLERANCE_ONLY selects one named scenario for a focused run.
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
goodTable OBJECT-TYPE
    SYNTAX SEQUENCE OF GoodEntry
    MAX-ACCESS not-accessible
    STATUS current
    DESCRIPTION "table"
    ::= { goodMIB 2 }
goodEntry OBJECT-TYPE
    SYNTAX GoodEntry
    MAX-ACCESS not-accessible
    STATUS current
    DESCRIPTION "row"
    INDEX { goodName }
    ::= { goodTable 1 }
GoodEntry ::= SEQUENCE { goodName OCTET STRING, goodCount Integer32, goodNote OCTET STRING }
goodName OBJECT-TYPE
    SYNTAX OCTET STRING (SIZE (1..16))
    MAX-ACCESS not-accessible
    STATUS current
    DESCRIPTION "index"
    ::= { goodEntry 1 }
goodCount OBJECT-TYPE
    SYNTAX Integer32
    MAX-ACCESS read-only
    STATUS current
    DESCRIPTION "count"
    ::= { goodEntry 2 }
goodNote OBJECT-TYPE
    SYNTAX OCTET STRING
    MAX-ACCESS read-only
    STATUS current
    DESCRIPTION "note"
    ::= { goodEntry 3 }
goodExtTable OBJECT-TYPE
    SYNTAX SEQUENCE OF GoodExtEntry
    MAX-ACCESS not-accessible
    STATUS current
    DESCRIPTION "table that augments goodTable"
    ::= { goodMIB 3 }
goodExtEntry OBJECT-TYPE
    SYNTAX GoodExtEntry
    MAX-ACCESS not-accessible
    STATUS current
    DESCRIPTION "row"
    AUGMENTS { goodEntry }
    ::= { goodExtTable 1 }
GoodExtEntry ::= SEQUENCE { goodExtLevel Integer32 }
goodExtLevel OBJECT-TYPE
    SYNTAX Integer32
    MAX-ACCESS read-only
    STATUS current
    DESCRIPTION "level"
    ::= { goodExtEntry 1 }
goodImpTable OBJECT-TYPE
    SYNTAX SEQUENCE OF GoodImpEntry
    MAX-ACCESS not-accessible
    STATUS current
    DESCRIPTION "table with an IMPLIED index"
    ::= { goodMIB 4 }
goodImpEntry OBJECT-TYPE
    SYNTAX GoodImpEntry
    MAX-ACCESS not-accessible
    STATUS current
    DESCRIPTION "row"
    INDEX { IMPLIED goodImpName }
    ::= { goodImpTable 1 }
GoodImpEntry ::= SEQUENCE { goodImpName OCTET STRING, goodImpValue Integer32 }
goodImpName OBJECT-TYPE
    SYNTAX OCTET STRING (SIZE (1..16))
    MAX-ACCESS not-accessible
    STATUS current
    DESCRIPTION "index"
    ::= { goodImpEntry 1 }
goodImpValue OBJECT-TYPE
    SYNTAX Integer32
    MAX-ACCESS read-only
    STATUS current
    DESCRIPTION "value"
    ::= { goodImpEntry 2 }
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
# A TEXTUAL-CONVENTION whose SYNTAX is TimeTicks, in a library whose SNMPv2-SMI lacks TimeTicks
# (see BROKEN_SMI). The parser used to dereference NULL here and crash the whole application; it
# did so on a real vendor ATM-TC-MIB.
set(BAD_TC_MIB [=[BAD-TC-MIB DEFINITIONS ::= BEGIN
IMPORTS MODULE-IDENTITY, enterprises, TimeTicks FROM SNMPv2-SMI
        TEXTUAL-CONVENTION FROM SNMPv2-TC;
badTcMIB MODULE-IDENTITY
    LAST-UPDATED "202601010000Z"
    ORGANIZATION "Test"
    CONTACT-INFO "none"
    DESCRIPTION "Uses TimeTicks, which the library's SNMPv2-SMI does not define."
    ::= { enterprises 90020 }
BadTicks ::= TEXTUAL-CONVENTION
    STATUS current
    DESCRIPTION "TimeTicks is unknown in this library."
    SYNTAX TimeTicks
END
]=])
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
#   [NO_STANDARDS_SCOPE]                   the profile names only the Acme folder, not Standards, as a
#                                          profile made from one vendor folder does
#   [BROKEN_SMI]                           the Standards SNMPv2-SMI is a bad copy that lacks TimeTicks
#   [LIVE]                                 also start the loopback-only test agent and issue a real Get
#                                          through the connection bar (needs SNMPB_PYTHON)
#   REQUIRE_EXIT_ZERO | ALLOW_EXIT_ONE
#   EXPECT <regex> ...)                    each must match launch-smoke.log
function(run_scenario name)
    if(DEFINED SNMPB_TOLERANCE_ONLY)
        if(NOT name STREQUAL SNMPB_TOLERANCE_ONLY)
            return()
        endif()
        set(SNMPB_TOLERANCE_SELECTED TRUE PARENT_SCOPE)
    endif()
    cmake_parse_arguments(S "DEPENDENCY_CHECK;REQUIRE_EXIT_ZERO;ALLOW_EXIT_ONE;LIVE;LIVE_SINGLE;NO_STANDARDS_SCOPE;BROKEN_SMI" ""
        "FILES;ROOTS;MODIFIED_ROOTS;UNRESOLVED;EXPECT;REJECT;EDIT_FILE;EDIT_FROM;EDIT_TO;LIVE_TABLE;LIVE_WALK;LIVE_BIGWALK;LIVE_CREDENTIAL" ${ARGN})
    set(dir "${SNMPB_TOLERANCE_DIR}/${name}")
    set(library "${dir}/MIB-Library")
    file(MAKE_DIRECTORY "${library}/Standards" "${dir}/snmpb.sourceforge.net" "${dir}/state/mibs")
    foreach(module SNMPv2-SMI SNMPv2-TC SNMPv2-CONF SNMPv2-MIB)
        file(COPY "${SNMPB_SOURCE_DIR}/libsmi/mibs/ietf/${module}" DESTINATION "${library}/Standards")
    endforeach()
    if(S_BROKEN_SMI)
        file(READ "${library}/Standards/SNMPv2-SMI" smi_text)
        string(REPLACE "TimeTicks ::=" "TimeTicksGone ::=" smi_text "${smi_text}")
        file(WRITE "${library}/Standards/SNMPv2-SMI" "${smi_text}")
    endif()
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
    # The live scenario also needs a second, empty profile to switch away from when it tests Detect.
    set(extra_profiles_json "")
    if(S_LIVE)
        string(CONCAT extra_profiles_json
            ",{\"id\":\"custom.other\",\"manifestVersion\":1,\"name\":\"Other profile\",\"roots\":[],"
            "\"scope\":[{\"id\":\"collection:Standards\",\"path\":\"${library}/Standards\"}],\"type\":\"custom\"}")
    endif()
    set(standards_scope ",{\"id\":\"collection:Standards\",\"path\":\"${library}/Standards\"}")
    if(S_NO_STANDARDS_SCOPE)
        set(standards_scope "")
    endif()
    file(WRITE "${dir}/state/mibs/profiles-v1.json"
        "{\"ordinaryProfilesMigrated\":true,\"schemaVersion\":5,\"profiles\":[{\"id\":\"custom.test\","
        "\"manifestVersion\":1,\"name\":\"Test profile\",\"roots\":[${roots_json}],"
        "\"scope\":[{\"id\":\"collection:Acme\",\"path\":\"${library}/Acme\"}${standards_scope}],"
        "\"type\":\"custom\"${unresolved_json}}${extra_profiles_json}]}")
    file(WRITE "${dir}/snmpb.sourceforge.net/SnmpB.ini"
        "[mib-library]\nroot=${library}\ncurrent-profile=custom.test\n")

    # Exercise real Get/timeout/Watch/Log/Detect workflows in persisted Dark mode;
    # other scenarios retain Follow System coverage.
    if(name STREQUAL "live-get-through-connection-bar")
        file(APPEND "${dir}/snmpb.sourceforge.net/SnmpB.ini" "\n[ui]\ntheme=dark\n")
    endif()
    set(smoke_mib "${library}/Standards/SNMPv2-MIB")
    set(edit_args "")
    if(S_EDIT_FILE)
        set(smoke_mib "${library}/Acme/${S_EDIT_FILE}")
        set(edit_args --smoke-edit-from "${S_EDIT_FROM}" --smoke-edit-to "${S_EDIT_TO}")
    endif()
    # PATH is passed to the application as one argument. It is kept in a list variable, so every
    # ";" in it (Windows PATH is full of them) is escaped first or it would split into arguments.
    string(REPLACE ";" "\\;" escaped_path "PATH=${SNMPB_QT_BIN_DIR};$ENV{PATH}")
    set(app_command "${CMAKE_COMMAND}" -E env --unset=SMIPATH "QT_QPA_PLATFORM=offscreen"
        "${escaped_path}"
        "${SNMPB_EXECUTABLE}" --launch-smoke-test "${dir}"
        --smoke-mib "${smoke_mib}" ${edit_args})
    set(live_credential "Smoke lab - SNMPv2c")
    if(S_LIVE_CREDENTIAL)
        set(live_credential "${S_LIVE_CREDENTIAL}")
    endif()
    set(scenario_timeout 90)
    if(S_DEPENDENCY_CHECK)
        file(WRITE "${dir}/extra-fixture.mib" "${EXTRA_MIB}")
        list(APPEND app_command --smoke-dependency-check)
        # This workflow performs repeated full Library checks and profile restores,
        # rather than the usual single launch. Leave room for parallel CTest jobs.
        set(scenario_timeout 180)
    endif()
    set(live_table_args "")
    if(S_LIVE_TABLE)
        set(live_table_args --smoke-live-table "${S_LIVE_TABLE}")
    endif()
    if(S_LIVE_WALK)
        list(APPEND live_table_args --smoke-live-walk "${S_LIVE_WALK}")
    endif()
    if(S_LIVE_BIGWALK)
        list(APPEND live_table_args --smoke-live-bigwalk "${S_LIVE_BIGWALK}")
    endif()
    if(S_LIVE_SINGLE)
        list(APPEND live_table_args --smoke-live-single)
    endif()
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
                --smoke-live-credential "${live_credential}" ${live_table_args}
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
            TIMEOUT ${scenario_timeout})
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
string(REPLACE "GOOD-MIB" "EXTRA-MIB" EXTRA_MIB "${GOOD_MIB}")
string(REPLACE "good" "extra" EXTRA_MIB "${EXTRA_MIB}")
string(REPLACE "Good" "Extra" EXTRA_MIB "${EXTRA_MIB}")
string(REPLACE "90001" "90009" EXTRA_MIB "${EXTRA_MIB}")
run_scenario(dependency-check-isolation REQUIRE_EXIT_ZERO DEPENDENCY_CHECK
    FILES Prod/GOOD-MIB=GOOD_MIB Copies/good-copy.mib=GOOD_MIB
    ROOTS GOOD-MIB@Prod/GOOD-MIB
    EXPECT "DEPENDENCY-CHECK completed"
    REJECT "launch-smoke: FAIL")

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
           "category=\"Conflicting definitions\" module=DUP-MIB"
           "Alpha/DUP-MIB \\(revision 2026-01-01 00:00\\) — used"
           "Beta/DUP-MIB \\(revision 2026-01-01 00:00\\)")

# 6b. Two selected files declare the same module (two revisions side by side in one folder, as in
#     real vendor collections): one is used, the other is set aside, and the profile still loads.
run_scenario(duplicate-selected-files REQUIRE_EXIT_ZERO
    FILES "Prod/DUP-REV-A=DUP_MIB_A" "Prod/DUP-REV-B=DUP_MIB_B" "Prod/GOOD-MIB=GOOD_MIB"
    ROOTS "DUP-MIB@Prod/DUP-REV-A" "DUP-MIB@Prod/DUP-REV-B" "GOOD-MIB@Prod/GOOD-MIB"
    EXPECT "ENVIRONMENT status=[A-Za-z]+ loaded=[1-9]"
           "category=\"Conflicting definitions\" module=DUP-MIB"
           "DUP-REV-A \\(revision 2026-01-01 00:00\\) — used"
    REJECT "Profile Environment build failed" "Multiple exact providers")

string(REPLACE "202601010000Z" "202610010000Z" DUP_MIB_NEW "${DUP_MIB_B}")
foreach(kind IN ITEMS selected dependency)
    if(kind STREQUAL "selected")
        set(revision_roots "DUP-MIB@Prod/A-old" "DUP-MIB@Prod/Z-new")
    else()
        set(revision_roots "IMPORTER-MIB@Prod/IMPORTER-MIB")
    endif()
    run_scenario(newest-revision-${kind} REQUIRE_EXIT_ZERO
        FILES "Prod/A-old=DUP_MIB_A" "Prod/Z-new=DUP_MIB_NEW" "Prod/IMPORTER-MIB=IMPORTER_MIB"
        ROOTS ${revision_roots}
        EXPECT "category=\"Conflicting definitions\" module=DUP-MIB"
               "Z-new.*revision 2026-10-01 00:00.*— used"
               "Selection: newest LAST-UPDATED revision"
        REJECT "Profile Environment build failed")
endforeach()

# A Modified note on an edited duplicate must not hide its conflicting-definition warning.
run_scenario(edited-conflicting-roots REQUIRE_EXIT_ZERO
    FILES "Prod/A-old=DUP_MIB_A" "Prod/Z-new=DUP_MIB_NEW"
    ROOTS "DUP-MIB@Prod/A-old" "DUP-MIB@Prod/Z-new" MODIFIED_ROOTS "DUP-MIB"
    EXPECT "category=\"Conflicting definitions\" module=DUP-MIB"
           "category=\"Modified\" module=DUP-MIB file=Z-new"
           "Selection: newest LAST-UPDATED revision"
    REJECT "Profile Environment build failed")

# 6c. A MIB the parser cannot fully understand must never crash the application.
run_scenario(unknown-type-in-textual-convention REQUIRE_EXIT_ZERO BROKEN_SMI
    FILES "Prod/GOOD-MIB=GOOD_MIB" "Prod/BAD-TC-MIB=BAD_TC_MIB"
    ROOTS "GOOD-MIB@Prod/GOOD-MIB" "BAD-TC-MIB@Prod/BAD-TC-MIB"
    EXPECT "ENVIRONMENT status=[A-Za-z]+ loaded=[1-9]"
           "module=BAD-TC-MIB"
    REJECT "Profile Environment build failed")

# 6d. A profile made from one vendor folder still gets the Library's Standards folder as the
#     last place to look for imports, so it loads completely instead of reporting the standard
#     MIBs as missing.
run_scenario(standards-fallback REQUIRE_EXIT_ZERO NO_STANDARDS_SCOPE
    FILES "Prod/GOOD-MIB=GOOD_MIB" ROOTS "GOOD-MIB@Prod/GOOD-MIB"
    EXPECT "ENVIRONMENT status=Complete loaded=2 failed=0 errors=0 warnings=0"
    REJECT "Missing dependency" "only outside this profile")

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
               "PASS: live: a watch continues polling with its workspace hidden and panel collapsed"
               "PASS: live: the watch kept its own target after the connection bar changed"
               "PASS: live: the Watch tab shows the row and its change"
               "PASS: live: removing a watch removes its row"
               "PASS: live: the history remembers the credential used with the target"
               "PASS: live: Detect read the device and switched to the Automatic profile that holds its MIBs"
               "PASS: live: the Detect button is available again"
               "PASS: live: the detection is in the Log with the profile it chose"
               "PASS: live: the Log tab lists every request"
               "PASS: live: a successful Get is logged with its target, identity and object count"
               "PASS: live: a request that got no answer is logged as a timeout"
               "PASS: live: the log never contains a community string"
               "PASS: live: a wrong-type answer shows a plain-English warning naming both types"
               "PASS: live: the Results table shows the received type and raw value beside the interpretation"
        REJECT "FAIL")
    if(NOT DEFINED SNMPB_TOLERANCE_ONLY OR
       SNMPB_TOLERANCE_ONLY STREQUAL "live-get-through-connection-bar")
        set(agent_log "")
        if(EXISTS "${SNMPB_TOLERANCE_DIR}/live-get-through-connection-bar/agent.log")
            file(READ "${SNMPB_TOLERANCE_DIR}/live-get-through-connection-bar/agent.log" agent_log)
        endif()
        if(NOT agent_log MATCHES "request type=0xa0 version=1 community=smokero oid=1.3.6.1.2.1.1.1.0")
            message(SEND_ERROR "live scenario: the agent did not see an SNMPv2c Get with the saved community:\n${agent_log}")
        endif()
    endif()
else()
    message(STATUS "Scenario live-get-through-connection-bar: skipped (no Python interpreter)")
endif()

# 13. Query Table through the connection bar, over real SNMP to the loopback agent: a table whose
#     index column is not accessible and whose last column only one row has. SNMPv2c uses
#     GETBULK, SNMPv1 uses GETNEXT and noSuchName; both must give the same three rows.
if(SNMPB_PYTHON)
    foreach(table_credential "Smoke lab - SNMPv2c" "Smoke lab - SNMPv1")
        string(REGEX REPLACE "[^A-Za-z0-9]+" "-" table_name "live-table-${table_credential}")
        run_scenario(${table_name} REQUIRE_EXIT_ZERO LIVE
            LIVE_TABLE "1.3.6.1.4.1.90001.2.1" LIVE_CREDENTIAL "${table_credential}"
            FILES "Prod/GOOD-MIB=GOOD_MIB" ROOTS "GOOD-MIB@Prod/GOOD-MIB"
            EXPECT "LIVE-TABLE-HEADER Instance,Index,goodCount,goodNote"
                   "LIVE-TABLE-ROW 4.101.116.104.48,goodName=\"eth0\",10,"
                   "LIVE-TABLE-ROW 4.101.116.104.49,goodName=\"eth1\",20,uplink"
                   "LIVE-TABLE-ROW 2.108.111,goodName=\"lo\",30,"
                   "LIVE-TABLE-ROWS 3"
                   "PASS: live-table: the table finished with three rows and no error line"
                   "PASS: live-table: the status line summarises the table"
                   "PASS: live-table: Copy table as CSV puts the table on the clipboard"
                   "PASS: live-table: Copy results as CSV puts the Results table on the clipboard"
                   "PASS: live-table: the Log lists the table request with its outcome and row count"
                   "LIVE-LOG op=Table target=127\\.0\\.0\\.1:[0-9]+ identity=\"SNMPv(1|2c)\" result=\"OK\" objects=3"
                   "PASS: live-table: Copy as CSV text matches the table"
                   "PASS: live-row: discovery shows progress without a false empty-result message"
                   "PASS: live-row: Results contains only the selected row with every readable column"
                   "PASS: live-row: cancelling selection preserves previous results"
                   "PASS: live-row: Stop cancels discovery and preserves existing results"
                   "PASS: live-table: the table holds exactly the objects a plain Walk of the same subtree returns"
            REJECT "FAIL")
    endforeach()
    # AUGMENTS (the row shares goodTable's index) and IMPLIED (the last string index has no
    # length prefix), decoded from the MIB and read over SNMPv2c GETBULK.
    run_scenario(live-table-augments-and-implied REQUIRE_EXIT_ZERO LIVE
        LIVE_TABLE "1.3.6.1.4.1.90001.3.1,1.3.6.1.4.1.90001.4.1"
        FILES "Prod/GOOD-MIB=GOOD_MIB" ROOTS "GOOD-MIB@Prod/GOOD-MIB"
        EXPECT "LIVE-TABLE-HEADER Instance,Index,goodExtLevel"
               "LIVE-TABLE-ROW 4.101.116.104.48,goodName=\"eth0\",1"
               "LIVE-TABLE-ROW 2.108.111,goodName=\"lo\",3"
               "LIVE-TABLE-HEADER Instance,Index,goodImpValue"
               "LIVE-TABLE-ROW 97.98,goodImpName=\"ab\",5"
               "LIVE-TABLE-ROW 120.121.122,goodImpName=\"xyz\",6"
               "LIVE-TABLE-ROWS 3"
               "LIVE-TABLE-ROWS 2"
        REJECT "FAIL")
endif()

# 14a. Get, GetNext, GetBulk, failing Gets and Set through the connection bar, SNMPv2c and SNMPv1.
#      Set is sent to the loopback test agent only, which keeps the new value in memory.
#      The exact text each one shows is checked: the Query pane, the status line and the Log.
#      v1 reports a missing object as noSuchName naming the varbind; this test agent answers a
#      missing object over v2c with endOfMibView, which shows nothing.
if(SNMPB_PYTHON)
    set(single_common
        "LIVE-SINGLE get query=\"-----SNMP query started----- 1: mib-2\\.1\\.1\\.0 Loopback test agent \\(not a real device\\) -----SNMP query finished----- Total # of Requests = 1 Total # of Objects = 1\""
        "LIVE-SINGLE get status failed=0 text=\"Finished: 1 object\\(s\\) in 1 request\\(s\\)\\.\""
        "LIVE-SINGLE get log op=Get oid=1\\.3\\.6\\.1\\.2\\.1\\.1\\.1\\.0 result=\"OK\" objects=1"
        "LIVE-SINGLE getnext status failed=0 text=\"Finished: 1 object\\(s\\) in 1 request\\(s\\)\\.\""
        "LIVE-SINGLE getnext log op=GetNext oid=1\\.3\\.6\\.1\\.2\\.1\\.1\\.1 result=\"OK\" objects=1"
        "LIVE-SINGLE getbulk log op=GetBulk oid=1\\.3\\.6\\.1\\.2\\.1\\.1 result=\"OK\" objects="
        "LIVE-SINGLE set query=\"-----SNMP set started----- 1: mib-2\\.1\\.5\\.0 renamed-by-set -----SNMP set finished-----\""
        "LIVE-SINGLE set status failed=0 text=\"1: mib-2\\.1\\.5\\.0 renamed-by-set SNMP set finished\""
        "LIVE-SINGLE set log op=Set oid=1\\.3\\.6\\.1\\.2\\.1\\.1\\.5\\.0 result=\"OK\" objects=1"
        "LIVE-SINGLE set-readback row SNMPv2-SMI::mib-2\\.1\\.5\\.0 \\| 1\\.5\\.0 \\| 1\\.3\\.6\\.1\\.2\\.1\\.1\\.5\\.0 \\|  \\| renamed-by-set "
        "LIVE-SINGLE set-unknown log op=Set oid=1\\.3\\.6\\.1\\.2\\.1\\.1\\.99\\.0 result=\"Failed\" objects=1")
    run_scenario(live-single-Smoke-lab-SNMPv2c REQUIRE_EXIT_ZERO LIVE LIVE_SINGLE
        LIVE_CREDENTIAL "Smoke lab - SNMPv2c"
        FILES "Prod/GOOD-MIB=GOOD_MIB" ROOTS "GOOD-MIB@Prod/GOOD-MIB"
        EXPECT ${single_common}
               "LIVE-SINGLE getbulk status failed=0 text=\"Finished: 10 object\\(s\\) in 1 request\\(s\\)\\.\""
               "LIVE-SINGLE getbulk row GOOD-MIB::goodCount\\.4\\.101\\.116\\.104\\.49 \\| 4\\.101\\.116\\.104\\.49 "
               "LIVE-SINGLE get-missing status failed=0 text=\"Finished: 0 object\\(s\\) in 1 request\\(s\\)\\. The device returned no objects\\.\""
               "LIVE-SINGLE set-readonly status failed=1 text=\"ERROR on varbind #1: mib-2\\.1\\.1\\.0 notWritable: this object cannot be changed \\(SNMP error 17\\) SNMP set finished\""
               "LIVE-SINGLE set-wrongtype status failed=1 text=\"ERROR on varbind #1: mib-2\\.1\\.5\\.0 wrongType: the value has the wrong data type for this object \\(SNMP error 7\\) SNMP set finished\""
               "LIVE-SINGLE set-wrongvalue status failed=1 text=\"ERROR on varbind #1: goodValue\\.0 wrongValue: the device does not accept this value for this object \\(SNMP error 10\\) SNMP set finished\""
               "LIVE-SINGLE set-unknown status failed=1 text=\"ERROR on varbind #1: mib-2\\.1\\.99\\.0 noAccess: this community or user is not allowed to change this object \\(SNMP error 6\\) SNMP set finished\""
        REJECT "FAIL")
    run_scenario(live-single-Smoke-lab-SNMPv1 REQUIRE_EXIT_ZERO LIVE LIVE_SINGLE
        LIVE_CREDENTIAL "Smoke lab - SNMPv1"
        FILES "Prod/GOOD-MIB=GOOD_MIB" ROOTS "GOOD-MIB@Prod/GOOD-MIB"
        EXPECT ${single_common}
               "LIVE-SINGLE getbulk status failed=0 text=\"Finished: 1 object\\(s\\) in 1 request\\(s\\)\\.\""
               "LIVE-SINGLE get-missing query=\"-----SNMP query started----- ERROR on varbind #1: mib-2\\.1\\.9\\.0 SNMP: Variable does not exist -----SNMP query finished-----"
               "LIVE-SINGLE set-readonly status failed=1 text=\"ERROR on varbind #1: mib-2\\.1\\.1\\.0 readOnly: this object cannot be changed \\(SNMP error 4\\) SNMP set finished\""
               "LIVE-SINGLE set-wrongtype status failed=1 text=\"ERROR on varbind #1: mib-2\\.1\\.5\\.0 badValue: "
               "LIVE-SINGLE set-unknown status failed=1 text=\"ERROR on varbind #1: mib-2\\.1\\.99\\.0 noSuchName: the device does not have this object \\(SNMP error 2\\) SNMP set finished\""
               "LIVE-SINGLE getnext-end query=\"-----SNMP query started----- ERROR on varbind #1: goodMIB\\.9\\.9 SNMP: Variable does not exist"
        REJECT "FAIL")
endif()

# 14. Walk against an agent that answers out of order: after 9.1 and 9.2 it returns 9.1 again.
#     The walk must end by itself with a plain reason and keep the two objects already read
#     (it used to run until Stop was pressed). SNMPv2c uses GETBULK, SNMPv1 GETNEXT.
if(SNMPB_PYTHON)
    foreach(walk_credential "Smoke lab - SNMPv2c" "Smoke lab - SNMPv1")
        string(REGEX REPLACE "[^A-Za-z0-9]+" "-" walk_name "live-walk-out-of-order-${walk_credential}")
        run_scenario(${walk_name} REQUIRE_EXIT_ZERO LIVE
            LIVE_WALK "1.3.6.1.4.1.90001.9" LIVE_CREDENTIAL "${walk_credential}"
            FILES "Prod/GOOD-MIB=GOOD_MIB" ROOTS "GOOD-MIB@Prod/GOOD-MIB"
            EXPECT "LIVE-WALK rows=2 oids=1\\.3\\.6\\.1\\.4\\.1\\.90001\\.9\\.1,1\\.3\\.6\\.1\\.4\\.1\\.90001\\.9\\.2 stopEnabled=0"
                   "LIVE-STATUS-AFTER-WALK failed=1 text=\"[^\"]*not in increasing order"
                   "LIVE-LOG op=Walk target=127\\.0\\.0\\.1:[0-9]+ identity=\"SNMPv(1|2c)\" result=\"Failed\" objects=2"
                   "PASS: live: Walk lists the subtree and finishes"
            REJECT "FAIL" "LIVE-WALK-NEEDED-STOP")
    endforeach()
endif()

# 15. A large Walk (3000 objects made up by the test agent) must not freeze the window, and the
#     Stop action must end it promptly. The window freezing used to make the desktop report that
#     the application was not responding.
if(SNMPB_PYTHON)
    run_scenario(live-walk-large-and-stop REQUIRE_EXIT_ZERO LIVE
        LIVE_BIGWALK "1.3.6.1.4.1.90001.10" LIVE_CREDENTIAL "Smoke lab - SNMPv2c"
        FILES "Prod/GOOD-MIB=GOOD_MIB" ROOTS "GOOD-MIB@Prod/GOOD-MIB"
        EXPECT "PASS: live: a large Walk lists every object and finishes"
               "PASS: live: the window stays responsive during a large Walk"
               "PASS: live: Stop ends a large Walk promptly and says it was stopped"
               "PASS: live: the window stays responsive while a Walk is stopped"
        REJECT "FAIL")
endif()

if(failures)
    message(FATAL_ERROR "Tolerance scenarios failed")
endif()

if(DEFINED SNMPB_TOLERANCE_ONLY AND NOT SNMPB_TOLERANCE_SELECTED)
    message(FATAL_ERROR "Unknown tolerance scenario: ${SNMPB_TOLERANCE_ONLY}")
endif()
