cmake_minimum_required(VERSION 4.0)
string(RANDOM LENGTH 16 ALPHABET 0123456789abcdef _run)
set(_marker "${PANGU_TEST_ROOT}/scope-destructor-${_run}.txt")
execute_process(COMMAND "${PANGU_SCOPE_TEST}" --destroy-busy "${_marker}"
    RESULT_VARIABLE _result OUTPUT_VARIABLE _stdout ERROR_VARIABLE _stderr TIMEOUT 15)
if(NOT "${_result}" STREQUAL "91" OR NOT EXISTS "${_marker}.ready" OR EXISTS "${_marker}")
    message(FATAL_ERROR "Busy Host destruction must terminate without destroying borrowed T: ${_result}\n${_stdout}\n${_stderr}")
endif()
