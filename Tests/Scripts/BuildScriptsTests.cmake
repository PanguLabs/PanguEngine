cmake_minimum_required(VERSION 4.0)

if(NOT IS_DIRECTORY "${PANGU_SOURCE_ROOT}/Scripts" OR NOT DEFINED PANGU_TEST_ROOT)
    message(FATAL_ERROR "BuildScriptsTests requires PANGU_SOURCE_ROOT and PANGU_TEST_ROOT")
endif()

if(CMAKE_HOST_WIN32)
    find_program(shell NAMES powershell REQUIRED)
    set(script "${PANGU_SOURCE_ROOT}/Scripts/build.ps1")
    set(launcher
        "${shell}"
        -NoProfile
        -ExecutionPolicy
        Bypass
        -File
    )
    set(platform windows)
    set(mobile android)
    set(foreign macos)
    set(dry -DryRun)
    set(platformFlag -Platform)
    set(configFlag -Config)
    set(modeFlag -LinkMode)
    set(targetFlag -Target)
    set(jobsFlag -Jobs)
    set(freshFlag -Fresh)
else()
    find_program(shell NAMES bash REQUIRED)
    set(script "${PANGU_SOURCE_ROOT}/Scripts/build.sh")
    set(launcher "${shell}")
    if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
        set(platform macos)
        set(mobile ios)
        set(foreign linux)
    else()
        set(platform linux)
        set(mobile ios)
        set(foreign macos)
    endif()
    set(dry --dry-run)
    set(platformFlag --platform)
    set(configFlag --config)
    set(modeFlag --link-mode)
    set(targetFlag --target)
    set(jobsFlag --jobs)
    set(freshFlag --fresh)
endif()

if(NOT EXISTS "${script}")
    message(FATAL_ERROR "Missing build entry: ${script}")
endif()

# 仅复制脚本到带空格的最小假仓库，验证 CLI 边界，不复制或编译引擎源码。
set(fixture "${PANGU_TEST_ROOT}/repository with spaces")
set(stubs "${PANGU_TEST_ROOT}/stub tools")
set(log "${PANGU_TEST_ROOT}/calls.log")
file(
    MAKE_DIRECTORY
        "${fixture}/Scripts"
        "${stubs}"
        "${PANGU_TEST_ROOT}/caller"
)
file(COPY "${script}" DESTINATION "${fixture}/Scripts")
file(WRITE "${fixture}/CMakePresets.json" "{}\n")
get_filename_component(scriptName "${script}" NAME)
set(script "${fixture}/Scripts/${scriptName}")

function(_pangu_run_build_script expected)
    execute_process(
        COMMAND
            ${launcher} "${script}" ${ARGN}
        WORKING_DIRECTORY "${PANGU_TEST_ROOT}/caller"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error
        TIMEOUT 30
    )
    if(NOT "${result}" STREQUAL "${expected}")
        message(FATAL_ERROR "Unexpected exit ${result}, expected ${expected}: ${ARGN}\n${output}\n${error}")
    endif()
    set(output "${output}\n${error}" PARENT_SCOPE)
endfunction()

function(_pangu_require_build_output needle)
    string(
        FIND "${output}"
        "${needle}"
        index
    )
    if(index LESS 0)
        message(FATAL_ERROR "Missing '${needle}':\n${output}")
    endif()
endfunction()

function(_pangu_reject_build_output needle)
    string(
        FIND "${output}"
        "${needle}"
        index
    )
    if(NOT index LESS 0)
        message(FATAL_ERROR "Unexpected '${needle}':\n${output}")
    endif()
endfunction()

# PATH 替身用于观察真实进程调用顺序及失败传播；无参数帮助和预览也不得执行它们。
set(ENV{PANGU_STUB_LOG} "${log}")
set(ENV{PANGU_STUB_FAIL} "")
if(CMAKE_HOST_WIN32)
    set(ENV{PATH} "${stubs};$ENV{PATH}")
    file(
        WRITE "${stubs}/cmake.cmd"
        [=[@echo off
set stage=configure
if "%~1"=="--build" set stage=build
echo %stage%>>"%PANGU_STUB_LOG%"
echo %CD%>>"%PANGU_STUB_LOG%"
if "%PANGU_STUB_FAIL%"=="%stage%" exit /b 37
exit /b 0
]=]
    )
    file(
        WRITE "${stubs}/ctest.cmd"
        [=[@echo off
echo test>>"%PANGU_STUB_LOG%"
if "%PANGU_STUB_FAIL%"=="test" exit /b 37
exit /b 0
]=]
    )
else()
    set(ENV{PATH} "${stubs}:$ENV{PATH}")
    file(
        WRITE "${stubs}/cmake"
        [=[#!/bin/sh
stage=configure
if [ "$1" = --build ]; then stage=build; fi
printf '%s\n%s\n' "$stage" "$PWD" >> "$PANGU_STUB_LOG"
if [ "$PANGU_STUB_FAIL" = "$stage" ]; then exit 37; fi
]=]
    )
    file(
        WRITE "${stubs}/ctest"
        [=[#!/bin/sh
printf 'test\n' >> "$PANGU_STUB_LOG"
if [ "$PANGU_STUB_FAIL" = test ]; then exit 37; fi
exit 0
]=]
    )
    file(
        CHMOD
            "${stubs}/cmake"
            "${stubs}/ctest"
        PERMISSIONS
            OWNER_READ
            OWNER_WRITE
            OWNER_EXECUTE
            GROUP_READ
            GROUP_EXECUTE
            WORLD_READ
            WORLD_EXECUTE
    )
endif()
file(WRITE "${log}" "")

_pangu_run_build_script(0)
foreach(
    action
    IN
    ITEMS
        configure
        build
        runtime
        fast
        precommit
        full
        benchmarks
        mobile-link
)
    _pangu_require_build_output("${action}")
endforeach()
_pangu_run_build_script(0 help)
foreach(
    action
    IN
    ITEMS
        configure
        build
        runtime
        fast
        precommit
        full
        benchmarks
)
    _pangu_run_build_script(
        0
        "${action}"
        "${dry}"
    )
    _pangu_require_build_output("--preset")
    _pangu_require_build_output("${platform}")
    _pangu_require_build_output("-DCMAKE_BUILD_TYPE=Release")
    _pangu_require_build_output("-DPANGU_LINK_MODE=Modular")
    if(action STREQUAL "runtime")
        _pangu_require_build_output("-DBUILD_TESTING=OFF")
        _pangu_require_build_output("-DPANGU_BUILD_ENGINE_TESTS=OFF")
    else()
        _pangu_require_build_output("-DBUILD_TESTING=ON")
        _pangu_require_build_output("-DPANGU_BUILD_ENGINE_TESTS=ON")
    endif()
    _pangu_reject_build_output("PANGU_BUILD_SAMPLES")
    if(action STREQUAL "benchmarks")
        _pangu_require_build_output("-DPANGU_BUILD_BENCHMARKS=ON")
        _pangu_require_build_output("-DPANGU_BUILD_META_BENCHMARKS=ON")
        _pangu_require_build_output("PanguModuleBenchmarks")
        _pangu_require_build_output("PanguLogBenchmarks")
        _pangu_require_build_output("PanguMetaAccessBenchmarks")
    else()
        _pangu_require_build_output("-DPANGU_BUILD_BENCHMARKS=OFF")
        _pangu_require_build_output("-DPANGU_BUILD_META_BENCHMARKS=OFF")
    endif()
    if(action MATCHES "^(fast|precommit|full)$")
        _pangu_require_build_output("${platform}-${action}")
        _pangu_require_build_output("ctest")
    else()
        _pangu_reject_build_output("ctest --preset")
    endif()
    if(action STREQUAL "configure")
        _pangu_reject_build_output("--build")
    else()
        _pangu_require_build_output("--build")
    endif()
endforeach()
_pangu_run_build_script(
    2
    samples
    "${dry}"
)
_pangu_run_build_script(
    0
    build
    "${dry}"
    "${targetFlag}"
    PanguMinimalHost
)
_pangu_require_build_output("PanguMinimalHost")
_pangu_require_build_output("-DPANGU_BUILD_ENGINE_TESTS=ON")
_pangu_reject_build_output("PANGU_BUILD_SAMPLES")
_pangu_run_build_script(
    0
    build
    "${dry}"
    "${configFlag}"
    Debug
    "${modeFlag}"
    Monolithic
    "${freshFlag}"
    "${jobsFlag}"
    2
    "${targetFlag}"
    "Target With Spaces"
)
_pangu_require_build_output("-DCMAKE_BUILD_TYPE=Debug")
_pangu_require_build_output("-DPANGU_LINK_MODE=Monolithic")
_pangu_require_build_output("--fresh")
_pangu_require_build_output("--parallel")
if(CMAKE_HOST_WIN32)
    _pangu_require_build_output("'Target With Spaces'")
else()
    _pangu_require_build_output("Target\\ With\\ Spaces")
endif()

if(CMAKE_HOST_WIN32 OR CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
    _pangu_run_build_script(
        0
        build
        "${dry}"
        "${platformFlag}"
        "${mobile}"
    )
    _pangu_require_build_output("${mobile}-arm64")
    _pangu_require_build_output("-DPANGU_LINK_MODE=Monolithic")
    _pangu_require_build_output("-DBUILD_TESTING=OFF")
    _pangu_run_build_script(
        0
        mobile-link
        "${dry}"
        "${platformFlag}"
        "${mobile}"
        "${modeFlag}"
        Modular
    )
    _pangu_require_build_output("CrossCompileTests.cmake")
    _pangu_require_build_output("-DPANGU_LINK_MODE=Modular")
    _pangu_require_build_output("-DBUILD_TESTING=ON")
    _pangu_run_build_script(
        2
        mobile-link
        "${dry}"
        "${platformFlag}"
        "${mobile}"
        "${configFlag}"
        Debug
    )
    foreach(
        action
        IN
        ITEMS
            fast
            precommit
            full
            samples
            benchmarks
    )
        _pangu_run_build_script(
            2
            "${action}"
            "${dry}"
            "${platformFlag}"
            "${mobile}"
        )
    endforeach()
endif()
_pangu_run_build_script(
    2
    build
    "${dry}"
    "${platformFlag}"
    "${foreign}"
)
_pangu_run_build_script(
    2
    mobile-link
    "${dry}"
)
_pangu_run_build_script(
    2
    full
    "${dry}"
    "${targetFlag}"
    PanguFoundation
)
_pangu_run_build_script(
    2
    configure
    "${dry}"
    "${targetFlag}"
    PanguFoundation
)
_pangu_run_build_script(
    2
    build
    "${dry}"
    "${jobsFlag}"
    0
)
_pangu_run_build_script(
    2
    build
    "${dry}"
    "${configFlag}"
    Unsupported
)
_pangu_run_build_script(
    2
    build
    "${dry}"
    "${modeFlag}"
    Unsupported
)
_pangu_run_build_script(2 unknown-action)
_pangu_run_build_script(2 --unknown-option)
if(NOT CMAKE_HOST_WIN32)
    _pangu_run_build_script(
        2
        build
        --platform
    )
endif()
file(READ "${log}" calls)
if(NOT calls STREQUAL "")
    message(FATAL_ERROR "Help/dry-run/rejected input executed tools: ${calls}")
endif()

foreach(failure IN ITEMS configure build test none)
    file(WRITE "${log}" "")
    set(ENV{PANGU_STUB_FAIL} "${failure}")
    if(failure STREQUAL "none")
        _pangu_run_build_script(0 fast)
    else()
        _pangu_run_build_script(37 fast)
    endif()
    file(STRINGS "${log}" calls)
    if(CMAKE_HOST_WIN32)
        string(
            REPLACE "/"
            "\\"
            expectedRoot
            "${fixture}"
        )
    else()
        set(expectedRoot "${fixture}")
    endif()
    set(expected
        configure
        "${expectedRoot}"
    )
    if(NOT failure STREQUAL "configure")
        list(
            APPEND expected
            build
            "${expectedRoot}"
        )
    endif()
    if(failure STREQUAL "test" OR failure STREQUAL "none")
        list(APPEND expected test)
    endif()
    if(NOT calls STREQUAL expected)
        message(FATAL_ERROR "Wrong phase order/root for ${failure}: '${calls}', expected '${expected}'")
    endif()
endforeach()

message(STATUS "Build script CLI, preview, configuration isolation and failure propagation passed (${platform})")
