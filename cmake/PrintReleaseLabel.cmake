# Prints the release label (for example 1.0.0-rc2) on stdout, for scripts and CI.
include("${CMAKE_CURRENT_LIST_DIR}/ReleaseLabel.cmake")
execute_process(COMMAND "${CMAKE_COMMAND}" -E echo "${SNMPB_RELEASE_LABEL}")
