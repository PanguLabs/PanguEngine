cmake_minimum_required(VERSION 4.0)
include("${CMAKE_CURRENT_LIST_DIR}/TestInfrastructureHelpers.cmake")
set(fixture "${CMAKE_CURRENT_LIST_DIR}/Fixtures/Registration")
_pangu_configure_test_infrastructure_fixture(NAME valid SOURCE "${fixture}" EXPECT pass)
set(binary "${testingBinary}")
_pangu_run_test_infrastructure_command(
    EXPECT pass
    COMMAND
        "${CMAKE_COMMAND}"
        --build
        "${binary}"
        --target
        PanguTestsFast
        --parallel
        "${PANGU_TEST_BUILD_JOBS}"
)
_pangu_list_fixture_tests(BINARY "${binary}" OUTPUT tests)
string(JSON discoveryCount LENGTH "${tests}" tests)
set(fastNames)
set(precommitNames)
set(fullNames)
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
    set(platform macos)
else()
    string(TOLOWER "${CMAKE_HOST_SYSTEM_NAME}" platform)
endif()
foreach(index RANGE 0 1)
    string(
        JSON name
        GET "${tests}"
        tests
        ${index}
        name
    )
    if(NOT name MATCHES "^Registration[.]Registration[.](First|Second)$")
        message(FATAL_ERROR "Unexpected discovered name: ${name}")
    endif()
    string(
        JSON count
        LENGTH "${tests}"
        tests
        ${index}
        properties
    )
    math(EXPR last "${count} - 1")
    set(work)
    set(labels)
    set(timeout)
    foreach(property RANGE 0 ${last})
        string(
            JSON key
            GET "${tests}"
            tests
            ${index}
            properties
            ${property}
            name
        )
        string(
            JSON value
            GET "${tests}"
            tests
            ${index}
            properties
            ${property}
            value
        )
        if(key STREQUAL "WORKING_DIRECTORY")
            set(work "${value}")
        elseif(key STREQUAL "LABELS")
            string(JSON labelCount LENGTH "${value}")
            math(EXPR lastLabel "${labelCount} - 1")
            foreach(label RANGE 0 ${lastLabel})
                string(JSON item GET "${value}" ${label})
                list(APPEND labels "${item}")
            endforeach()
        elseif(key STREQUAL "TIMEOUT")
            set(timeout "${value}")
        endif()
    endforeach()
    if(NOT timeout STREQUAL "30.0" AND NOT timeout STREQUAL "30")
        message(FATAL_ERROR "Wrong or missing unit timeout: ${timeout}")
    endif()
    foreach(
        label
        IN
        ITEMS
            boundary.unit
            area.foundation
            owner.engine
            type.contract
            type.regression
            requires.subprocess
            run.fast
            run.precommit
            run.full
            "platform.${platform}"
    )
        if(NOT label IN_LIST labels)
            message(FATAL_ERROR "${name} lacks ${label}: ${labels}")
        endif()
    endforeach()
    foreach(tier IN ITEMS fast precommit full)
        if("run.${tier}" IN_LIST labels)
            list(APPEND ${tier}Names "${name}")
        endif()
    endforeach()
    string(SHA256 hash "${name}")
    if(NOT work STREQUAL "${binary}/Tests/Work/Registration/${hash}" OR NOT IS_DIRECTORY "${work}")
        message(FATAL_ERROR "Unexpected work directory: ${work}")
    endif()
    if(index EQUAL 0)
        set(firstWorkDir "${work}")
    else()
        set(secondWorkDir "${work}")
    endif()
endforeach()
if(NOT fastNames STREQUAL precommitNames OR NOT fastNames STREQUAL fullNames)
    message(FATAL_ERROR "fast fixture must also appear in precommit and full")
endif()
if(NOT discoveryCount EQUAL 2 OR firstWorkDir STREQUAL secondWorkDir)
    message(FATAL_ERROR "two independently discovered cases need isolated work directories")
endif()
foreach(iteration RANGE 1 2)
    _pangu_run_test_infrastructure_command(
        EXPECT pass
        COMMAND
            "${CMAKE_CTEST_COMMAND}"
            --test-dir
            "${binary}"
            --no-tests=error
            --output-on-failure
            --parallel
            "${PANGU_TEST_BUILD_JOBS}"
    )
endforeach()
file(GLOB xml "${binary}/Tests/Results/Registration/*.xml")
list(LENGTH xml xmlCount)
if(NOT xmlCount EQUAL 2)
    message(FATAL_ERROR "Each discovered test needs its own XML: ${xml}")
endif()
foreach(mode IN ITEMS duplicate invalid-tier missing-owner)
    if(mode STREQUAL "duplicate")
        set(diagnostic "already registered")
    elseif(mode STREQUAL "invalid-tier")
        set(diagnostic "TIER")
    else()
        set(diagnostic "OWNER")
    endif()
    _pangu_configure_test_infrastructure_fixture(
        NAME "${mode}"
        SOURCE "${fixture}"
        EXPECT fail
        DIAGNOSTIC "${diagnostic}"
        ARGS
            "-DMODE=${mode}"
    )
endforeach()

_pangu_configure_test_infrastructure_fixture(NAME empty SOURCE "${fixture}" EXPECT pass ARGS -DMODE=empty)
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
    EXPECT fail
    DIAGNOSTIC "no cases|No tests were found|No tests"
    COMMAND
        "${CMAKE_CTEST_COMMAND}"
        --test-dir
        "${testingBinary}"
        --no-tests=error
        --output-on-failure
)

_pangu_configure_test_infrastructure_fixture(NAME plain SOURCE "${fixture}" EXPECT pass ARGS -DMODE=plain)
set(binary "${testingBinary}")
file(REMOVE "${binary}/dependency-built")
_pangu_run_test_infrastructure_command(
    EXPECT pass
    COMMAND
        "${CMAKE_COMMAND}"
        --build
        "${binary}"
        --target
        PanguTestsFast
)
if(EXISTS "${binary}/dependency-built")
    message(FATAL_ERROR "fast build included a precommit dependency")
endif()
_pangu_run_test_infrastructure_command(
    EXPECT pass
    COMMAND
        "${CMAKE_COMMAND}"
        --build
        "${binary}"
        --target
        PanguTestsPrecommit
)
if(NOT EXISTS "${binary}/dependency-built")
    message(FATAL_ERROR "precommit build omitted its dependency")
endif()
_pangu_list_fixture_tests(BINARY "${binary}" OUTPUT plain)
string(JSON plainCount LENGTH "${plain}" tests)
if(NOT plainCount EQUAL 3)
    message(FATAL_ERROR "Expected three plain tests")
endif()
foreach(index RANGE 0 2)
    string(
        JSON name
        GET "${plain}"
        tests
        ${index}
        name
    )
    string(
        JSON properties
        GET "${plain}"
        tests
        ${index}
        properties
    )
    if(
        NOT properties
            MATCHES
            "owner.project"
        OR NOT properties
            MATCHES
            "plugin.fixture.tests"
        OR NOT properties
            MATCHES
            "run.full"
    )
        message(FATAL_ERROR "Plain registration lost project/plugin/full metadata: ${properties}")
    endif()
    if(name STREQUAL "PlainFast")
        if(NOT properties MATCHES "run.fast" OR NOT properties MATCHES "run.precommit")
            message(FATAL_ERROR "Fast plain test lost cumulative tiers")
        endif()
    elseif(name STREQUAL "PlainPrecommit")
        if(properties MATCHES "run.fast" OR NOT properties MATCHES "run.precommit")
            message(FATAL_ERROR "Precommit plain test has wrong tiers")
        endif()
    elseif(name STREQUAL "PlainFull" AND properties MATCHES "run.fast|run.precommit")
        message(FATAL_ERROR "Full plain test has wrong tiers")
    endif()
endforeach()
_pangu_run_test_infrastructure_command(
    EXPECT pass
    COMMAND
        "${CMAKE_CTEST_COMMAND}"
        --test-dir
        "${binary}"
        --no-tests=error
        --output-on-failure
        --parallel
        "${PANGU_TEST_BUILD_JOBS}"
)

# CTest 必须恢复自身预留预算，子配置继承它，显式参数和已有 cache 仍优先。
set(budgetFixture "${CMAKE_CURRENT_LIST_DIR}/Fixtures/Budget")
_pangu_configure_test_infrastructure_fixture(
    NAME budget
    SOURCE "${budgetFixture}"
    EXPECT pass
    ARGS
        -DPANGU_TEST_BUILD_JOBS=3
)
_pangu_run_test_infrastructure_command(
    EXPECT pass
    COMMAND
        "${CMAKE_COMMAND}"
        -E
        env
        PANGU_TEST_BUILD_JOBS=1
        CMAKE_BUILD_PARALLEL_LEVEL=1
        "${CMAKE_CTEST_COMMAND}"
        --test-dir
        "${testingBinary}"
        --no-tests=error
        --output-on-failure
)
foreach(
    case
    IN
    ITEMS
        default
        inherited
        explicit
        cached
        invalid
)
    set(environment PANGU_TEST_BUILD_JOBS=5)
    set(options)
    set(expected 5)
    set(expectedResult pass)
    set(diagnostic)
    set(budgetBinary "${PANGU_TEST_ROOT}/budget-${case}")
    if(case STREQUAL "default")
        set(environment --unset=PANGU_TEST_BUILD_JOBS)
        set(expected 2)
    elseif(case STREQUAL "explicit")
        set(options -DPANGU_TEST_BUILD_JOBS=1)
        set(expected 1)
    elseif(case STREQUAL "cached")
        set(environment PANGU_TEST_BUILD_JOBS=6)
        set(budgetBinary "${PANGU_TEST_ROOT}/budget-explicit")
        set(expected 1)
    elseif(case STREQUAL "invalid")
        set(environment PANGU_TEST_BUILD_JOBS=0)
        set(expectedResult fail)
        set(diagnostic
            DIAGNOSTIC
            "must be a positive integer"
        )
    endif()
    _pangu_run_test_infrastructure_command(
        EXPECT "${expectedResult}"
        ${diagnostic}
        COMMAND
            "${CMAKE_COMMAND}"
            -E
            env
            ${environment}
            "${CMAKE_COMMAND}"
            -S
            "${budgetFixture}"
            -B
            "${budgetBinary}"
            -G
            Ninja
            "-DPANGU_ENGINE_ROOT=${PANGU_ENGINE_ROOT}"
            -DBUDGET_LEAF=ON
            ${options}
    )
    if(expectedResult STREQUAL "pass")
        file(READ "${budgetBinary}/effective-budget.txt" actual)
        if(NOT actual STREQUAL expected)
            message(FATAL_ERROR "${case}: expected processor budget ${expected}, got ${actual}")
        endif()
    endif()
endforeach()
