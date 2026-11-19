cmake_minimum_required(VERSION 4.0)
include("${CMAKE_CURRENT_LIST_DIR}/TestInfrastructureHelpers.cmake")
set(fixture "${CMAKE_CURRENT_LIST_DIR}/Fixtures/Registration")
foreach(mode IN ITEMS include-only repeat disabled)
    _pangu_configure_test_infrastructure_fixture(NAME "${mode}" SOURCE "${fixture}" EXPECT pass ARGS "-DMODE=${mode}")
endforeach()
_pangu_configure_test_infrastructure_fixture(
    NAME rtti-off
    SOURCE "${fixture}"
    EXPECT fail
    ARGS
        -DMODE=rtti-off
    DIAGNOSTIC "RTTI OFF is not supported"
)
_pangu_configure_test_infrastructure_fixture(NAME rtti-on SOURCE "${fixture}" EXPECT pass)
_pangu_run_test_infrastructure_command(
    EXPECT pass
    COMMAND
        "${CMAKE_COMMAND}"
        --build
        "${testingBinary}"
        --target
        Registration
        --parallel
        "${PANGU_TEST_BUILD_JOBS}"
)
_pangu_run_test_infrastructure_command(
    EXPECT pass
    COMMAND
        "${CMAKE_CTEST_COMMAND}"
        --test-dir
        "${testingBinary}"
        --no-tests=error
        --output-on-failure
)
foreach(
    mode
    IN
    ITEMS
        conflict
        foreign
        contract-conflict
        definitions-conflict
        rtti-definitions-conflict
)
    _pangu_configure_test_infrastructure_fixture(
        NAME "${mode}"
        SOURCE "${fixture}"
        EXPECT fail
        DIAGNOSTIC "conflict"
        ARGS
            "-DMODE=${mode}"
    )
endforeach()
set(missing "${PANGU_TEST_ROOT}/MissingEngine")
file(MAKE_DIRECTORY "${missing}/cmake")
file(
    COPY
        "${PANGU_ENGINE_ROOT}/cmake/PanguTestDependencies.cmake"
        "${PANGU_ENGINE_ROOT}/cmake/PanguTesting.cmake"
    DESTINATION "${missing}/cmake"
)
_pangu_configure_test_infrastructure_fixture(
    NAME missing
    SOURCE "${fixture}"
    EXPECT fail
    DIAGNOSTIC "GoogleTest source missing"
    ARGS
        "-DPANGU_ENGINE_ROOT=${missing}"
)
