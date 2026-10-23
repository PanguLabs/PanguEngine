cmake_minimum_required(VERSION 4.0)
foreach(_required IN ITEMS PANGU_COMPILE_COMMANDS PANGU_AUDIT_SCRIPT PANGU_EVIDENCE_OUTPUT PANGU_NEGATIVE_MODE)
    if(NOT DEFINED ${_required})
        message(FATAL_ERROR "Missing negative audit input ${_required}")
    endif()
endforeach()
if(NOT PANGU_NEGATIVE_MODE STREQUAL "RTTI" AND NOT PANGU_NEGATIVE_MODE STREQUAL "EXCEPTIONS")
    message(FATAL_ERROR "Unsupported negative audit mode")
endif()
get_filename_component(_output_directory "${PANGU_EVIDENCE_OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${_output_directory}")
file(WRITE "${PANGU_EVIDENCE_OUTPUT}" "{\"stale\": true}\n")
execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DPANGU_COMPILE_COMMANDS=${PANGU_COMPILE_COMMANDS}"
        "-DPANGU_EVIDENCE_OUTPUT=${PANGU_EVIDENCE_OUTPUT}"
        "-DPANGU_AUDIT_INJECT_${PANGU_NEGATIVE_MODE}=ON"
        -P "${PANGU_AUDIT_SCRIPT}"
    RESULT_VARIABLE _exit
    OUTPUT_VARIABLE _stdout
    ERROR_VARIABLE _stderr
)
if(_exit EQUAL 0 OR EXISTS "${PANGU_EVIDENCE_OUTPUT}")
    message(FATAL_ERROR "Conflicting ${PANGU_NEGATIVE_MODE} option was accepted or left stale evidence.")
endif()
if(PANGU_NEGATIVE_MODE STREQUAL "RTTI" AND NOT _stderr MATCHES "ABI macro disagreement")
    message(FATAL_ERROR "RTTI override was not detected as a cross-target macro mismatch: ${_stderr}")
endif()
if(PANGU_NEGATIVE_MODE STREQUAL "EXCEPTIONS" AND NOT _stderr MATCHES "Pangu Module exception support must be off")
    message(FATAL_ERROR "Exception override was not rejected by the compiled policy source: ${_stderr}")
endif()
message(STATUS "Conflicting ${PANGU_NEGATIVE_MODE} compiler option rejected (fail closed).")
