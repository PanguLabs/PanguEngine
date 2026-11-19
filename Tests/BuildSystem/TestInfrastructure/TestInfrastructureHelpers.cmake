cmake_minimum_required(VERSION 4.0)
include("${CMAKE_CURRENT_LIST_DIR}/../CommandTiming.cmake")

foreach(required IN ITEMS PANGU_ENGINE_ROOT PANGU_TEST_ROOT PANGU_TEST_COMPILER)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()
file(MAKE_DIRECTORY "${PANGU_TEST_ROOT}")

function(_pangu_run_test_infrastructure_command)
    cmake_parse_arguments(PARSE_ARGV 0 arg "" "EXPECT;DIAGNOSTIC" "COMMAND")
    if(
        arg_UNPARSED_ARGUMENTS
        OR arg_KEYWORDS_MISSING_VALUES
        OR NOT arg_EXPECT
            MATCHES
            "^(pass|fail)$"
        OR NOT arg_COMMAND
    )
        message(FATAL_ERROR "Invalid TestingRun arguments")
    endif()
    string(TIMESTAMP commandStarted "%s%f" UTC)
    execute_process(
        COMMAND
            ${arg_COMMAND}
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error
        TIMEOUT 900
    )
    _pangu_record_test_command_time("${commandStarted}" "${result}" ${arg_COMMAND})
    set(log "${output}\n${error}")
    file(APPEND "${PANGU_TEST_ROOT}/commands.log" "${arg_COMMAND}\nresult=${result}\n${log}\n")
    if((arg_EXPECT STREQUAL "pass" AND NOT result STREQUAL "0") OR (arg_EXPECT STREQUAL "fail" AND result STREQUAL "0"))
        message(FATAL_ERROR "Unexpected result ${result}: ${arg_COMMAND}\n${log}")
    endif()
    if(arg_DIAGNOSTIC AND NOT log MATCHES "${arg_DIAGNOSTIC}")
        message(FATAL_ERROR "Missing diagnostic '${arg_DIAGNOSTIC}':\n${log}")
    endif()
    set(testingOutput "${log}" PARENT_SCOPE)
    set(testingStdout "${output}" PARENT_SCOPE)
endfunction()

function(_pangu_configure_test_infrastructure_fixture)
    cmake_parse_arguments(PARSE_ARGV 0 arg "" "NAME;SOURCE;EXPECT;DIAGNOSTIC" "ARGS")
    set(binary "${PANGU_TEST_ROOT}/${arg_NAME}")
    set(options)
    if(PANGU_TEST_TOOLCHAIN)
        list(APPEND options "-DCMAKE_TOOLCHAIN_FILE=${PANGU_TEST_TOOLCHAIN}")
    endif()
    set(diagnostic)
    if(arg_DIAGNOSTIC)
        list(
            APPEND diagnostic
            DIAGNOSTIC
            "${arg_DIAGNOSTIC}"
        )
    endif()
    _pangu_run_test_infrastructure_command(
        EXPECT "${arg_EXPECT}"
        ${diagnostic}
        COMMAND
            "${CMAKE_COMMAND}"
            -S
            "${arg_SOURCE}"
            -B
            "${binary}"
            -G
            Ninja
            "-DCMAKE_CXX_COMPILER=${PANGU_TEST_COMPILER}"
            -DCMAKE_BUILD_TYPE=Release
            "-DPANGU_ENGINE_ROOT=${PANGU_ENGINE_ROOT}"
            "-DPANGU_META_HOST_BUILD_ROOT=${PANGU_META_HOST_BUILD_ROOT}"
            "-DPANGU_TEST_BUILD_JOBS=${PANGU_TEST_BUILD_JOBS}"
            ${options}
            ${arg_ARGS}
    )
    set(testingBinary "${binary}" PARENT_SCOPE)
endfunction()

function(_pangu_list_fixture_tests)
    cmake_parse_arguments(PARSE_ARGV 0 arg "" "BINARY;OUTPUT" "")
    _pangu_run_test_infrastructure_command(
        EXPECT pass
        COMMAND
            "${CMAKE_CTEST_COMMAND}"
            --test-dir
            "${arg_BINARY}"
            --show-only=json-v1
    )
    set(${arg_OUTPUT} "${testingStdout}" PARENT_SCOPE)
endfunction()
