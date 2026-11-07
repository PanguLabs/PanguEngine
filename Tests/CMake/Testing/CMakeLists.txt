cmake_minimum_required(VERSION 4.0)

foreach(name IN ITEMS Registration DependencyIsolation BenchmarkIsolation BuildTools BuildToolsFailurePropagation ScriptClassification WorkflowCompleteness)
    set(testName "Testing${name}")
    if(name STREQUAL "BuildTools")
        set(testName TestingBuildToolsTests)
    endif()
    pangu_register_test(NAME "${testName}"
        COMMAND "${CMAKE_COMMAND}" -E env "CMAKE_BUILD_PARALLEL_LEVEL=${PANGU_TEST_BUILD_JOBS}"
            "${CMAKE_COMMAND}"
            "-DPANGU_ENGINE_ROOT=${PROJECT_SOURCE_DIR}"
            "-DPANGU_TEST_ROOT=${CMAKE_BINARY_DIR}/Tests/Work/${testName}"
            "-DPANGU_TEST_COMPILER=${CMAKE_CXX_COMPILER}"
            "-DPANGU_TEST_TOOLCHAIN=${CMAKE_TOOLCHAIN_FILE}"
            "-DPANGU_TEST_BUILD_ROOT=${CMAKE_BINARY_DIR}"
            "-DPANGU_TEST_ENGINE_BINARY_DIR=${PROJECT_BINARY_DIR}"
            "-DPANGU_TEST_LINK_MODE=${PANGU_LINK_MODE}"
            "-DPANGU_TEST_BUILD_JOBS=${PANGU_TEST_BUILD_JOBS}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/${name}Tests.cmake"
        BOUNDARY system AREA testing TIER full TYPES regression
        REQUIRES subprocess nested-build PROCESSORS "${PANGU_TEST_BUILD_JOBS}" TIMEOUT 900)
endforeach()
