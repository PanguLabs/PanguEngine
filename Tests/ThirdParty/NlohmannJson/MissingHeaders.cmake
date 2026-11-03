cmake_minimum_required(VERSION 4.0)

if(NOT DEFINED PANGU_SOURCE_ROOT OR NOT DEFINED PANGU_TEST_ROOT)
    message(FATAL_ERROR "PANGU_SOURCE_ROOT and PANGU_TEST_ROOT are required.")
endif()

set(_wrapper "${PANGU_SOURCE_ROOT}/Source/ThirdParty/NlohmannJson/CMakeLists.txt")
if(NOT EXISTS "${_wrapper}")
    message(FATAL_ERROR "NlohmannJson wrapper is missing: ${_wrapper}")
endif()

string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef _run)
foreach(_missing IN ITEMS json.hpp json_fwd.hpp)
    set(_source "${PANGU_TEST_ROOT}/${_run}/missing-${_missing}")
    set(_module "${_source}/NlohmannJson")
    set(_headers "${_module}/Repo/single_include/nlohmann")
    file(MAKE_DIRECTORY "${_headers}")
    file(COPY_FILE "${_wrapper}" "${_module}/CMakeLists.txt")
    if(_missing STREQUAL "json.hpp")
        file(WRITE "${_headers}/json_fwd.hpp" "// fixture\n")
    else()
        file(WRITE "${_headers}/json.hpp" "// fixture\n")
    endif()
    file(WRITE "${_source}/CMakeLists.txt"
        "cmake_minimum_required(VERSION 4.0)\n"
        "project(PanguNlohmannMissingHeaders LANGUAGES NONE)\n"
        "add_library(PanguBuildOptions INTERFACE)\n"
        "add_subdirectory(NlohmannJson)\n")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -S "${_source}" -B "${_source}/build" -G Ninja
        RESULT_VARIABLE _exit
        OUTPUT_VARIABLE _out
        ERROR_VARIABLE _err
    )
    set(_diagnostic "${_out}\n${_err}")
    if(_exit EQUAL 0 OR NOT _diagnostic MATCHES "${_missing}"
            OR NOT _diagnostic MATCHES "git submodule update --init --recursive")
        message(FATAL_ERROR "Missing ${_missing} was not rejected as expected (exit=${_exit}):\n${_diagnostic}")
    endif()
endforeach()
