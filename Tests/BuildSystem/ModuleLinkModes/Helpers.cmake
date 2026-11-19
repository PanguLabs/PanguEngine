cmake_minimum_required(VERSION 4.0)
include("${PANGU_SOURCE_ROOT}/cmake/PanguTestBuildJobs.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/../CommandTiming.cmake")
string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef runId)
set(PANGU_TEST_ROOT "${PANGU_TEST_ROOT}/${runId}")
file(MAKE_DIRECTORY "${PANGU_TEST_ROOT}")
if(CMAKE_HOST_WIN32)
    set(compilerArgs
        "-DCMAKE_C_COMPILER:FILEPATH=$ENV{LLVM_ROOT}/bin/clang.exe"
        "-DCMAKE_CXX_COMPILER:FILEPATH=$ENV{LLVM_ROOT}/bin/clang++.exe"
    )
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
    set(compilerArgs "-DCMAKE_TOOLCHAIN_FILE:FILEPATH=${PANGU_SOURCE_ROOT}/cmake/Platform/Linux/Toolchain.cmake")
else()
    set(compilerArgs "-DCMAKE_TOOLCHAIN_FILE:FILEPATH=${PANGU_SOURCE_ROOT}/cmake/Platform/macOS/Toolchain.cmake")
endif()
function(_pangu_run_link_mode_command expected diagnostic)
    string(TIMESTAMP commandStarted "%s%f" UTC)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    _pangu_record_test_command_time("${commandStarted}" "${result}" ${ARGN})
    file(APPEND "${PANGU_TEST_ROOT}/commands.log" "${ARGN}\nexit=${result}\n${output}\n${error}\n")
    if(expected STREQUAL "PASS" AND NOT result EQUAL 0)
        message(FATAL_ERROR "Command failed: ${ARGN}\n${output}\n${error}")
    elseif(expected STREQUAL "FAIL" AND result EQUAL 0)
        message(FATAL_ERROR "Command unexpectedly passed: ${ARGN}")
    endif()
    if(NOT diagnostic STREQUAL "")
        string(
            FIND "${output}\n${error}"
            "${diagnostic}"
            found
        )
        if(found LESS 0)
            message(FATAL_ERROR "Missing '${diagnostic}':\n${output}\n${error}")
        endif()
    endif()
    set(commandOutput "${output}\n${error}" PARENT_SCOPE)
endfunction()
function(_pangu_configure_link_mode_fixture name expected diagnostic)
    _pangu_run_link_mode_command(
        "${expected}"
        "${diagnostic}"
        "${CMAKE_COMMAND}"
        -S
        "${CMAKE_CURRENT_LIST_DIR}/Fixtures"
        -B
        "${PANGU_TEST_ROOT}/${name}"
        -G
        Ninja
        ${compilerArgs}
        -DCMAKE_BUILD_TYPE=Release
        "-DPANGU_SOURCE_ROOT=${PANGU_SOURCE_ROOT}"
        "-DPANGU_META_HOST_BUILD_ROOT=${PANGU_META_HOST_BUILD_ROOT}"
        ${ARGN}
    )
endfunction()
function(_pangu_build_and_run_link_mode_fixture name)
    _pangu_run_link_mode_command(
        PASS
        ""
        "${CMAKE_COMMAND}"
        --build
        "${PANGU_TEST_ROOT}/${name}"
        --parallel
        "${PANGU_TEST_BUILD_JOBS}"
        --target
        ModeConsumer
    )
    _pangu_run_link_mode_command(
        PASS
        "100% tests passed"
        "${CMAKE_CTEST_COMMAND}"
        --test-dir
        "${PANGU_TEST_ROOT}/${name}"
        --output-on-failure
        --no-tests=error
    )
    _pangu_run_link_mode_command(
        PASS
        "no work to do"
        "${CMAKE_COMMAND}"
        --build
        "${PANGU_TEST_ROOT}/${name}"
        --parallel
        "${PANGU_TEST_BUILD_JOBS}"
        --target
        ModeConsumer
    )
endfunction()
