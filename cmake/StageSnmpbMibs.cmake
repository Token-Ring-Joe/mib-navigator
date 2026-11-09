if(NOT DEFINED SNMPB_SOURCE_DIR OR NOT DEFINED SNMPB_RUNTIME_DATA_DIR)
    message(FATAL_ERROR "SnmpB MIB staging arguments are incomplete")
endif()

set(mib_source_dirs
    "${SNMPB_SOURCE_DIR}/libsmi/mibs/iana"
    "${SNMPB_SOURCE_DIR}/libsmi/mibs/ietf"
    "${SNMPB_SOURCE_DIR}/libsmi/mibs/tubs"
)
set(pib_source_dirs
    "${SNMPB_SOURCE_DIR}/libsmi/pibs/ietf"
    "${SNMPB_SOURCE_DIR}/libsmi/pibs/tubs"
)

function(stage_module_files destination)
    file(MAKE_DIRECTORY "${destination}")
    foreach(source_dir IN LISTS ARGN)
        file(GLOB module_files "${source_dir}/*")
        list(FILTER module_files EXCLUDE REGEX "/Makefile[^/]*$")
        list(FILTER module_files EXCLUDE REGEX "-orig$")
        if(module_files)
            file(COPY ${module_files} DESTINATION "${destination}")
        endif()
    endforeach()
endfunction()

stage_module_files("${SNMPB_RUNTIME_DATA_DIR}/mibs" ${mib_source_dirs})
stage_module_files("${SNMPB_RUNTIME_DATA_DIR}/pibs" ${pib_source_dirs})
