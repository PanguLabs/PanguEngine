cmake_minimum_required(VERSION 4.0)

foreach(_required IN ITEMS PANGU_COMPILE_COMMANDS PANGU_AUDIT_SCRIPT PANGU_TEST_DIRECTORY PANGU_RTTI_OPTION)
    if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
        message(FATAL_ERROR "Missing RTTI baseline test input ${_required}.")
    endif()
endforeach()
if(NOT PANGU_RTTI_OPTION STREQUAL "-frtti" AND NOT PANGU_RTTI_OPTION STREQUAL "-fno-rtti")
    message(FATAL_ERROR "Unsupported RTTI baseline option: ${PANGU_RTTI_OPTION}")
endif()

# Keep the actual compiler, working directories and response files; change only RTTI.
file(READ "${PANGU_COMPILE_COMMANDS}" _database)
string(JSON _count LENGTH "${_database}")
math(EXPR _last "${_count} - 1")
foreach(_index RANGE 0 ${_last})
    string(JSON _command GET "${_database}" ${_index} command)
    string(APPEND _command " ${PANGU_RTTI_OPTION}")
    string(REPLACE "\\" "\\\\" _command "${_command}")
    string(REPLACE "\"" "\\\"" _command "${_command}")
    string(JSON _database SET "${_database}" ${_index} command "\"${_command}\"")
endforeach()
file(MAKE_DIRECTORY "${PANGU_TEST_DIRECTORY}")
set(_commands "${PANGU_TEST_DIRECTORY}/compile_commands.json")
set(_evidence "${PANGU_TEST_DIRECTORY}/baseline-evidence.json")
file(WRITE "${_commands}" "${_database}\n")
execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DPANGU_COMPILE_COMMANDS=${_commands}"
        "-DPANGU_EVIDENCE_OUTPUT=${_evidence}"
        -P "${PANGU_AUDIT_SCRIPT}"
    RESULT_VARIABLE _exit
    ERROR_VARIABLE _stderr
)
if(NOT _exit EQUAL 0 OR NOT EXISTS "${_evidence}")
    message(FATAL_ERROR "Consistent RTTI baseline was rejected: ${_stderr}")
endif()

# The negative audit must create a real disagreement for either valid baseline.
execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DPANGU_COMPILE_COMMANDS=${_commands}"
        "-DPANGU_AUDIT_SCRIPT=${PANGU_AUDIT_SCRIPT}"
        "-DPANGU_EVIDENCE_OUTPUT=${PANGU_TEST_DIRECTORY}/negative-evidence.json"
        -DPANGU_NEGATIVE_MODE=RTTI
        -P "${CMAKE_CURRENT_LIST_DIR}/NegativeAudit.cmake"
    RESULT_VARIABLE _exit
    ERROR_VARIABLE _stderr
)
if(NOT _exit EQUAL 0)
    message(FATAL_ERROR "RTTI conflict test failed with ${PANGU_RTTI_OPTION}: ${_stderr}")
endif()
