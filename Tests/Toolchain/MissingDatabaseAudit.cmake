cmake_minimum_required(VERSION 4.0)

foreach(_required IN ITEMS PANGU_AUDIT_SCRIPT PANGU_TEST_DIRECTORY)
    if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
        message(FATAL_ERROR "Missing audit test input ${_required}.")
    endif()
endforeach()
file(MAKE_DIRECTORY "${PANGU_TEST_DIRECTORY}")
set(_database "${PANGU_TEST_DIRECTORY}/missing-compile-commands.json")
set(_evidence "${PANGU_TEST_DIRECTORY}/stale-evidence.json")
file(REMOVE "${_database}")
file(WRITE "${_evidence}" "{\"stale\": true}\n")
execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DPANGU_COMPILE_COMMANDS=${_database}"
        "-DPANGU_EVIDENCE_OUTPUT=${_evidence}"
        -P "${PANGU_AUDIT_SCRIPT}"
    RESULT_VARIABLE _exit
    ERROR_VARIABLE _stderr
)
if(_exit EQUAL 0 OR NOT _stderr MATCHES "Compiler command database not found")
    message(FATAL_ERROR "Missing database was not rejected: ${_stderr}")
endif()
if(EXISTS "${_evidence}")
    message(FATAL_ERROR "Missing database left stale audit evidence.")
endif()
