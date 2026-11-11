cmake_minimum_required(VERSION 4.0)

foreach(scenario IN ITEMS Matrix Switch)
    pangu_register_test(NAME "ModuleLinkMode${scenario}"
        COMMAND "${CMAKE_COMMAND}"
            "-DPANGU_SOURCE_ROOT=${PROJECT_SOURCE_DIR}"
            "-DPANGU_TEST_ROOT=${CMAKE_BINARY_DIR}/Tests/Work/ModuleLinkMode${scenario}"
            "-DPANGU_TEST_BUILD_JOBS=${PANGU_TEST_BUILD_JOBS}"
            "-DPANGU_META_SDK_CONFIG=${PANGU_META_SDK_CONFIG}"
            "-DPANGU_META_HOST_BUILD_ROOT=${CMAKE_BINARY_DIR}/Tests"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/Mode${scenario}Tests.cmake"
        BOUNDARY system AREA module TIER full TYPES regression
        REQUIRES subprocess nested-build PROCESSORS "${PANGU_TEST_BUILD_JOBS}" TIMEOUT 900)
endforeach()

if(PANGU_LINK_MODE STREQUAL "Modular" AND NOT PANGU_TEST_MODULE_MATRIX_CHILD)
    pangu_register_test(NAME ModuleRegression
        COMMAND "${CMAKE_COMMAND}"
            "-DPANGU_SOURCE_ROOT=${PROJECT_SOURCE_DIR}"
            "-DPANGU_TEST_ROOT=${CMAKE_BINARY_DIR}/Tests/Work/ModuleRegression"
            "-DPANGU_TEST_BUILD_JOBS=${PANGU_TEST_BUILD_JOBS}"
            "-DPANGU_META_SDK_CONFIG=${PANGU_META_SDK_CONFIG}"
            "-DPANGU_META_HOST_BUILD_ROOT=${CMAKE_BINARY_DIR}/Tests"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/ModuleRegressionTests.cmake"
        BOUNDARY system AREA module TIER precommit TYPES regression
        REQUIRES subprocess nested-build PROCESSORS "${PANGU_TEST_BUILD_JOBS}" TIMEOUT 900)
endif()
