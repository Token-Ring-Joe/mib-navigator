cmake_minimum_required(VERSION 3.22)

if(NOT DEFINED SNMPB_PACKAGE_DIR OR NOT DEFINED SNMPB_EXTRACT_DIR)
    message(FATAL_ERROR
        "SNMPB_PACKAGE_DIR and SNMPB_EXTRACT_DIR must be defined")
endif()

file(GLOB archives
    "${SNMPB_PACKAGE_DIR}/MIB-Navigator-1.0.0-rc1-*.zip"
    "${SNMPB_PACKAGE_DIR}/MIB-Navigator-1.0.0-rc1-*.tar.gz"
)
list(LENGTH archives archive_count)
if(NOT archive_count EQUAL 1)
    message(FATAL_ERROR
        "Expected exactly one RC archive in ${SNMPB_PACKAGE_DIR}; found ${archive_count}")
endif()

file(REMOVE_RECURSE "${SNMPB_EXTRACT_DIR}")
file(MAKE_DIRECTORY "${SNMPB_EXTRACT_DIR}")
list(GET archives 0 archive)
file(ARCHIVE_EXTRACT INPUT "${archive}" DESTINATION "${SNMPB_EXTRACT_DIR}")

file(GLOB extracted_roots LIST_DIRECTORIES true "${SNMPB_EXTRACT_DIR}/*")
list(FILTER extracted_roots INCLUDE REGEX "[/\\\\]MIB-Navigator-1\\.0\\.0-rc1-")
list(LENGTH extracted_roots extracted_root_count)
if(NOT extracted_root_count EQUAL 1)
    message(FATAL_ERROR
        "Expected one top-level RC directory; found ${extracted_root_count}")
endif()

list(GET extracted_roots 0 SNMPB_PACKAGE_ROOT)
include("${CMAKE_CURRENT_LIST_DIR}/VerifyReleaseTree.cmake")
message(STATUS "Verified release archive: ${archive}")
