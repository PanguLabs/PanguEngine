cmake_minimum_required(VERSION 4.0)
include("${CMAKE_CURRENT_LIST_DIR}/TestingTestHelpers.cmake")
set(runner "${PANGU_ENGINE_ROOT}/cmake/Testing/RunBuildToolsTests.cmake")
if(NOT EXISTS "${runner}")
    message(FATAL_ERROR "BuildTools runner is missing")
endif()
TestingConfigure(NAME fixture SOURCE "${CMAKE_CURRENT_LIST_DIR}/Fixtures/Registration" EXPECT pass)
set(binary "${Testing_BINARY}")
TestingRun(EXPECT pass COMMAND "${CMAKE_COMMAND}" --build "${binary}" --target Registration --parallel "${PANGU_TEST_BUILD_JOBS}")
foreach(mode IN ITEMS pass fail skip empty)
    set(regex "^Registration[.]")
    set(expect fail)
    set(diagnostic "Registration[.]Registration[.]Second")
    if(mode STREQUAL "pass")
        set(expect pass)
    elseif(mode STREQUAL "empty")
        set(regex "^DoesNotExist$")
        set(diagnostic "No tests")
    endif()
    TestingRun(EXPECT "${expect}" DIAGNOSTIC "${diagnostic}"
        COMMAND "${CMAKE_COMMAND}" -E env "PANGU_TEST_FIXTURE_RESULT=${mode}"
        "${CMAKE_COMMAND}" "-DPANGU_BUILD_TOOLS_TEST_BINARY_DIR=${binary}"
        "-DPANGU_BUILD_TOOLS_TEST_REGEX=${regex}"
        "-DPANGU_BUILD_TOOLS_TEST_RESULT_DIR=${PANGU_TEST_ROOT}/Results/${mode}"
        "-DPANGU_BUILD_TOOLS_TEST_JOBS=${PANGU_TEST_BUILD_JOBS}" -P "${runner}")
endforeach()
TestingConfigure(NAME missing SOURCE "${CMAKE_CURRENT_LIST_DIR}/Fixtures/Registration" EXPECT pass)
TestingRun(EXPECT fail DIAGNOSTIC "Registration_NOT_BUILT" COMMAND "${CMAKE_COMMAND}"
    "-DPANGU_BUILD_TOOLS_TEST_BINARY_DIR=${Testing_BINARY}"
    "-DPANGU_BUILD_TOOLS_TEST_REGEX=Registration"
    "-DPANGU_BUILD_TOOLS_TEST_RESULT_DIR=${PANGU_TEST_ROOT}/Results/missing"
    "-DPANGU_BUILD_TOOLS_TEST_JOBS=${PANGU_TEST_BUILD_JOBS}" -P "${runner}")
