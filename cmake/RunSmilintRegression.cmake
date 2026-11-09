if(NOT DEFINED SMILINT)
    message(FATAL_ERROR "SMILINT was not specified")
endif()

if(NOT DEFINED LIBSMI_DIR)
    message(FATAL_ERROR "LIBSMI_DIR was not specified")
endif()

if(NOT DEFINED MIB)
    message(FATAL_ERROR "MIB was not specified")
endif()

if(WIN32)
    set(PATH_SEP ";")
    set(NULL_DEVICE "NUL")
else()
    set(PATH_SEP ":")
    set(NULL_DEVICE "/dev/null")
endif()

#
# Put the generated SMIv2 regression MIBs first.
# Imported modules will then be found in the normal libsmi MIB directories.
#
string(CONCAT SMI_PATH
    "${LIBSMI_DIR}/test/dumps/smiv2${PATH_SEP}"
    "${LIBSMI_DIR}/mibs/ietf${PATH_SEP}"
    "${LIBSMI_DIR}/mibs/iana${PATH_SEP}"
    "${LIBSMI_DIR}/mibs/irtf${PATH_SEP}"
    "${LIBSMI_DIR}/mibs/site${PATH_SEP}"
    "${LIBSMI_DIR}/mibs/tubs"
)

set(ENV{SMIPATH} "${SMI_PATH}")

set(EXPECTED_FILE
    "${LIBSMI_DIR}/test/dumps/smilint-smiv2/${MIB}"
)

execute_process(
    COMMAND
        "${SMILINT}"
        "-c${NULL_DEVICE}"
        "-l9"
        "${MIB}"
    RESULT_VARIABLE RESULT
    OUTPUT_VARIABLE STDOUT
    ERROR_VARIABLE STDERR
)

if(NOT RESULT EQUAL 0)
    message(FATAL_ERROR
        "smilint returned ${RESULT} while testing ${MIB}\n"
        "${STDOUT}${STDERR}"
    )
endif()

file(READ "${EXPECTED_FILE}" EXPECTED)

set(ACTUAL "${STDOUT}${STDERR}")

# Normalize line endings.
string(REPLACE "\r\n" "\n" ACTUAL "${ACTUAL}")
string(REPLACE "\r\n" "\n" EXPECTED "${EXPECTED}")

#
# Normalize Windows path separators.
#
string(REPLACE "\\" "/" ACTUAL "${ACTUAL}")

#
# Convert Windows absolute source paths to the historical
# relative path forms stored in libsmi's reference output.
#
string(REPLACE
    "${LIBSMI_DIR}/test/dumps/smiv2/"
    "../dumps/smiv2/"
    ACTUAL
    "${ACTUAL}"
)

foreach(DIR ietf iana irtf site tubs)
    string(REPLACE
        "${LIBSMI_DIR}/mibs/${DIR}/"
        "../../mibs/${DIR}/"
        ACTUAL
        "${ACTUAL}"
    )
endforeach()

if(NOT ACTUAL STREQUAL EXPECTED)
    message(FATAL_ERROR
        "Regression detected while parsing ${MIB}\n\n"
        "EXPECTED:\n${EXPECTED}\n"
        "ACTUAL:\n${ACTUAL}"
    )
endif()

message(STATUS
    "${MIB}: parser output matches historical libsmi baseline"
)