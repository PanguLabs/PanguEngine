cmake_minimum_required(VERSION 4.0)
include("${CMAKE_CURRENT_LIST_DIR}/TestingTestHelpers.cmake")
set(fixture "${CMAKE_CURRENT_LIST_DIR}/Fixtures/Registration")
foreach(mode IN ITEMS include-only repeat disabled)
    TestingConfigure(NAME "${mode}" SOURCE "${fixture}" EXPECT pass ARGS "-DMODE=${mode}")
endforeach()
TestingConfigure(NAME rtti-off SOURCE "${fixture}" EXPECT fail ARGS -DMODE=rtti-off
    DIAGNOSTIC "RTTI OFF is not supported")
TestingConfigure(NAME rtti-on SOURCE "${fixture}" EXPECT pass)
TestingRun(EXPECT pass COMMAND "${CMAKE_COMMAND}" --build "${Testing_BINARY}" --target Registration --parallel "${PANGU_TEST_BUILD_JOBS}")
TestingRun(EXPECT pass COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${Testing_BINARY}"
    --no-tests=error --output-on-failure)
foreach(mode IN ITEMS conflict foreign contract-conflict definitions-conflict rtti-definitions-conflict)
    TestingConfigure(NAME "${mode}" SOURCE "${fixture}" EXPECT fail DIAGNOSTIC "conflict"
        ARGS "-DMODE=${mode}")
endforeach()
set(missing "${PANGU_TEST_ROOT}/MissingEngine")
file(MAKE_DIRECTORY "${missing}/cmake")
file(COPY "${PANGU_ENGINE_ROOT}/cmake/PanguTestDependencies.cmake"
    "${PANGU_ENGINE_ROOT}/cmake/PanguTesting.cmake" DESTINATION "${missing}/cmake")
TestingConfigure(NAME missing SOURCE "${fixture}" EXPECT fail DIAGNOSTIC "GoogleTest source missing"
    ARGS "-DPANGU_ENGINE_ROOT=${missing}")
