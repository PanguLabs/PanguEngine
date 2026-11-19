cmake_minimum_required(VERSION 4.0)
include("${CMAKE_CURRENT_LIST_DIR}/TestInfrastructureHelpers.cmake")
_pangu_configure_test_infrastructure_fixture(
    NAME benchmark-only
    SOURCE "${PANGU_ENGINE_ROOT}"
    EXPECT pass
    ARGS
        -DBUILD_TESTING=OFF
        -DPANGU_BUILD_ENGINE_TESTS=OFF
        -DPANGU_BUILD_BENCHMARKS=ON
)
set(binary "${testingBinary}")
_pangu_run_test_infrastructure_command(
    EXPECT pass
    COMMAND
        "${CMAKE_COMMAND}"
        --build
        "${binary}"
        --target
        help
)
if(
    NOT testingOutput
        MATCHES
        "PanguModuleBenchmarks"
    OR testingOutput
        MATCHES
        "(^|[/ ])g(test|mock)([_ :/]|$)"
    OR testingOutput
        MATCHES
        "PanguMinimal(Host|Services)"
)
    message(FATAL_ERROR "benchmark-only must provide benchmarks without GoogleTest/GoogleMock")
endif()
_pangu_list_fixture_tests(BINARY "${binary}" OUTPUT tests)
string(JSON count LENGTH "${tests}" tests)
if(NOT count EQUAL 0)
    message(FATAL_ERROR "benchmark-only has ${count} correctness tests")
endif()
_pangu_run_test_infrastructure_command(
    EXPECT pass
    COMMAND
        "${CMAKE_COMMAND}"
        --build
        "${binary}"
        --target
        PanguModuleBenchmarks
        --parallel
        "${PANGU_TEST_BUILD_JOBS}"
)
if(CMAKE_HOST_WIN32)
    set(exeSuffix .exe)
endif()
_pangu_run_test_infrastructure_command(
    EXPECT pass
    COMMAND
        "${binary}/Bin/PanguModuleBenchmarks${exeSuffix}"
        --benchmark_dry_run
        "--benchmark_out=${PANGU_TEST_ROOT}/benchmarks.json"
        --benchmark_out_format=json
)
file(READ "${PANGU_TEST_ROOT}/benchmarks.json" json)
foreach(
    name
    IN
    ITEMS
        ModuleIdentityParse
        ModuleIdentityFormat
        ScopeAccessBorrowRelease
        CatalogCreate/1
        CatalogCreate/8
        CatalogCreate/32
)
    if(NOT json MATCHES "\"name\": \"${name}\"")
        message(FATAL_ERROR "Missing benchmark ${name}")
    endif()
endforeach()
if(
    json
        MATCHES
        "\"error_occurred\": true"
    OR NOT json
        MATCHES
        "\"iterations\""
    OR NOT json
        MATCHES
        "\"time_unit\""
    OR NOT json
        MATCHES
        "\"context\""
)
    message(FATAL_ERROR "Invalid benchmark output")
endif()
_pangu_configure_test_infrastructure_fixture(
    NAME benchmark-only
    SOURCE "${PANGU_ENGINE_ROOT}"
    EXPECT pass
    ARGS
        -DBUILD_TESTING=ON
        -DPANGU_BUILD_ENGINE_TESTS=ON
        -DPANGU_BUILD_BENCHMARKS=OFF
)
_pangu_list_fixture_tests(BINARY "${testingBinary}" OUTPUT correctnessTests)
string(JSON correctnessCount LENGTH "${correctnessTests}" tests)
_pangu_run_test_infrastructure_command(
    EXPECT pass
    COMMAND
        "${CMAKE_COMMAND}"
        --build
        "${testingBinary}"
        --target
        help
)
if(testingOutput MATCHES "PanguModuleBenchmarks")
    message(FATAL_ERROR "Correctness-only must not create benchmark target")
endif()
if(
    NOT testingOutput
        MATCHES
        "PanguMinimalHost"
    OR NOT testingOutput
        MATCHES
        "PanguMinimalServices"
    OR NOT correctnessTests
        MATCHES
        "MinimalHostSmoke"
)
    message(FATAL_ERROR "Engine tests must include MinimalHost targets and smoke without a separate sample option")
endif()
_pangu_configure_test_infrastructure_fixture(
    NAME benchmark-only
    SOURCE "${PANGU_ENGINE_ROOT}"
    EXPECT pass
    ARGS
        -DBUILD_TESTING=ON
        -DPANGU_BUILD_ENGINE_TESTS=ON
        -DPANGU_BUILD_BENCHMARKS=ON
)
_pangu_list_fixture_tests(BINARY "${testingBinary}" OUTPUT combinedTests)
string(JSON combinedCount LENGTH "${combinedTests}" tests)
if(NOT combinedCount EQUAL correctnessCount)
    message(FATAL_ERROR "Enabling benchmarks changed correctness test count")
endif()
_pangu_configure_test_infrastructure_fixture(
    NAME benchmark-only
    SOURCE "${PANGU_ENGINE_ROOT}"
    EXPECT pass
    ARGS
        -DBUILD_TESTING=OFF
        -DPANGU_BUILD_ENGINE_TESTS=OFF
        -DPANGU_BUILD_BENCHMARKS=ON
)
_pangu_run_test_infrastructure_command(
    EXPECT pass
    COMMAND
        "${CMAKE_COMMAND}"
        --build
        "${testingBinary}"
        --target
        help
)
if(
    NOT testingOutput
        MATCHES
        "PanguModuleBenchmarks"
    OR testingOutput
        MATCHES
        "(^|[/ ])g(test|mock)([_ :/]|$)"
    OR testingOutput
        MATCHES
        "PanguMinimal(Host|Services)"
)
    message(FATAL_ERROR "Switching to benchmark-only retained correctness targets")
endif()
_pangu_list_fixture_tests(BINARY "${testingBinary}" OUTPUT disabledTests)
string(JSON disabledCount LENGTH "${disabledTests}" tests)
if(NOT disabledCount EQUAL 0)
    message(FATAL_ERROR "Switching to benchmark-only retained ${disabledCount} tests")
endif()
_pangu_configure_test_infrastructure_fixture(
    NAME benchmark-only
    SOURCE "${PANGU_ENGINE_ROOT}"
    EXPECT pass
    ARGS
        -DBUILD_TESTING=ON
        -DPANGU_BUILD_ENGINE_TESTS=ON
        -DPANGU_BUILD_BENCHMARKS=OFF
)
_pangu_list_fixture_tests(BINARY "${testingBinary}" OUTPUT restoredTests)
string(JSON restoredCount LENGTH "${restoredTests}" tests)
if(NOT restoredCount EQUAL correctnessCount OR restoredCount EQUAL 0)
    message(FATAL_ERROR "Restoring testing did not restore registrations")
endif()
_pangu_configure_test_infrastructure_fixture(
    NAME benchmark-only
    SOURCE "${PANGU_ENGINE_ROOT}"
    EXPECT pass
    ARGS
        -DBUILD_TESTING=OFF
        -DPANGU_BUILD_ENGINE_TESTS=OFF
        -DPANGU_BUILD_BENCHMARKS=OFF
)
_pangu_list_fixture_tests(BINARY "${testingBinary}" OUTPUT tests)
string(JSON count LENGTH "${tests}" tests)
if(NOT count EQUAL 0)
    message(FATAL_ERROR "Disabled testing retained ${count} tests")
endif()
set(missing "${PANGU_TEST_ROOT}/MissingEngine")
file(
    MAKE_DIRECTORY
        "${missing}/cmake"
        "${PANGU_TEST_ROOT}/MissingFixture"
)
file(COPY "${PANGU_ENGINE_ROOT}/cmake/PanguTestDependencies.cmake" DESTINATION "${missing}/cmake")
file(
    WRITE "${PANGU_TEST_ROOT}/MissingFixture/CMakeLists.txt"
    "cmake_minimum_required(VERSION 4.0)\nproject(Missing LANGUAGES CXX)\ninclude(\"${missing}/cmake/PanguTestDependencies.cmake\")\npangu_require_googlebenchmark()\n"
)
_pangu_configure_test_infrastructure_fixture(
    NAME missing
    SOURCE "${PANGU_TEST_ROOT}/MissingFixture"
    EXPECT fail
    DIAGNOSTIC "Google Benchmark source missing"
)
