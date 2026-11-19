cmake_minimum_required(VERSION 4.0)
string(RANDOM LENGTH 16 ALPHABET 0123456789abcdef runId)
set(marker "${PANGU_TEST_ROOT}/scope-destructor-${runId}.txt")
execute_process(
    COMMAND
        "${PANGU_SCOPE_TEST}" "${marker}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr
    TIMEOUT 15
)
if(CMAKE_HOST_WIN32)
    set(expectedResult "Exit code 0xc0000409")
else()
    set(expectedResult "Subprocess killed")
endif()
if(NOT "${result}" STREQUAL "${expectedResult}" OR NOT EXISTS "${marker}.ready" OR EXISTS "${marker}")
    message(
        FATAL_ERROR
        "Busy Host destruction must terminate without destroying borrowed T: ${result}\n${stdout}\n${stderr}"
    )
endif()
