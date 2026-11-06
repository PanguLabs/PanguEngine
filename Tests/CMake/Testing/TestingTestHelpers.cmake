cmake_minimum_required(VERSION 4.0)

foreach(required IN ITEMS PANGU_ENGINE_ROOT PANGU_TEST_ROOT PANGU_TEST_COMPILER)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()
file(MAKE_DIRECTORY "${PANGU_TEST_ROOT}")

function(TestingRun)
    cmake_parse_arguments(PARSE_ARGV 0 arg "" "EXPECT;DIAGNOSTIC" "COMMAND")
    if(arg_UNPARSED_ARGUMENTS OR arg_KEYWORDS_MISSING_VALUES OR
       NOT arg_EXPECT MATCHES "^(pass|fail)$" OR NOT arg_COMMAND)
        message(FATAL_ERROR "Invalid TestingRun arguments")
    endif()
    execute_process(COMMAND ${arg_COMMAND} RESULT_VARIABLE result
        OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 900)
    set(log "${output}\n${error}")
    file(APPEND "${PANGU_TEST_ROOT}/commands.log" "${arg_COMMAND}\nresult=${result}\n${log}\n")
    if((arg_EXPECT STREQUAL "pass" AND NOT result STREQUAL "0") OR
       (arg_EXPECT STREQUAL "fail" AND result STREQUAL "0"))
        message(FATAL_ERROR "Unexpected result ${result}: ${arg_COMMAND}\n${log}")
    endif()
    if(arg_DIAGNOSTIC AND NOT log MATCHES "${arg_DIAGNOSTIC}")
        message(FATAL_ERROR "Missing diagnostic '${arg_DIAGNOSTIC}':\n${log}")
    endif()
    set(Testing_OUTPUT "${log}" PARENT_SCOPE)
    set(Testing_STDOUT "${output}" PARENT_SCOPE)
endfunction()

function(TestingConfigure)
    cmake_parse_arguments(PARSE_ARGV 0 arg "" "NAME;SOURCE;EXPECT;DIAGNOSTIC" "ARGS")
    set(binary "${PANGU_TEST_ROOT}/${arg_NAME}")
    set(options)
    if(PANGU_TEST_TOOLCHAIN)
        list(APPEND options "-DCMAKE_TOOLCHAIN_FILE=${PANGU_TEST_TOOLCHAIN}")
    endif()
    set(diagnostic)
    if(arg_DIAGNOSTIC)
        list(APPEND diagnostic DIAGNOSTIC "${arg_DIAGNOSTIC}")
    endif()
    TestingRun(EXPECT "${arg_EXPECT}" ${diagnostic} COMMAND "${CMAKE_COMMAND}"
        -S "${arg_SOURCE}" -B "${binary}" -G Ninja
        "-DCMAKE_CXX_COMPILER=${PANGU_TEST_COMPILER}" -DCMAKE_BUILD_TYPE=Release
        "-DPANGU_ENGINE_ROOT=${PANGU_ENGINE_ROOT}"
        "-DPANGU_TEST_BUILD_JOBS=${PANGU_TEST_BUILD_JOBS}" ${options} ${arg_ARGS})
    set(Testing_BINARY "${binary}" PARENT_SCOPE)
endfunction()

function(TestingListTests)
    cmake_parse_arguments(PARSE_ARGV 0 arg "" "BINARY;OUTPUT" "")
    TestingRun(EXPECT pass COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${arg_BINARY}" --show-only=json-v1)
    set(${arg_OUTPUT} "${Testing_STDOUT}" PARENT_SCOPE)
endfunction()
