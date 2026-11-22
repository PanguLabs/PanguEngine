cmake_minimum_required(VERSION 4.0)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(engineStandalone ON)
    include(cmake/ValidateBuildEnvironment.cmake)
    project(
        PanguEngine
        LANGUAGES
            C
            CXX
    )
else()
    set(engineStandalone OFF)
    project(PanguEngine LANGUAGES CXX)
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(cmake/Platform/Common/BuildOptions.cmake)
include(cmake/PanguPlatform.cmake)
include(cmake/PanguLinkMode.cmake)
pangu_initialize_link_mode()
include(cmake/PanguBuildLayout.cmake)
pangu_initialize_build_layout()
include(cmake/PanguRuntimeDeployment.cmake)
include(cmake/PanguApplication.cmake)
include(cmake/Platform/Common/Module.cmake)

option(PANGU_BUILD_BENCHMARKS "Build native performance benchmarks" OFF)
set(flecsAssertBenchmarks OFF)
if(
    engineStandalone
    AND PANGU_BUILD_BENCHMARKS
    AND NOT CMAKE_CROSSCOMPILING
    AND PANGU_LINK_MODE
        STREQUAL
        "Monolithic"
    AND CMAKE_BUILD_TYPE
        STREQUAL
        "Release"
)
    set(flecsAssertBenchmarks ON)
endif()

block(
    SCOPE_FOR
        VARIABLES
    PROPAGATE
        CMAKE_ARCHIVE_OUTPUT_DIRECTORY
        CMAKE_LIBRARY_OUTPUT_DIRECTORY
        CMAKE_RUNTIME_OUTPUT_DIRECTORY
)
    pangu_owned_build_root(engineBuildRoot engine)
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${engineBuildRoot}/Lib")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${engineBuildRoot}/Bin")
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${engineBuildRoot}/Bin")
endblock()
foreach(
    directory
    IN
    ITEMS
        Source/ThirdParty/NlohmannJson
        Source/ThirdParty/Fmt
        Source/ThirdParty/Spdlog
        Source/ThirdParty/Flecs
        Source/Runtime/Foundation
        Source/Runtime/Log
        Source/Runtime/Module
        Source/Runtime/EngineHost
        Source/Runtime/ECS
)
    pangu_add_owned_subdirectory(
        SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/${directory}"
        OWNER engine
        RELATIVE_BINARY_DIR "${directory}"
    )
endforeach()

if(engineStandalone)
    include(CTest)
    if(NOT BUILD_TESTING)
        # CMake 不会在关闭测试时覆盖旧注册，清空根入口以避免复用构建树时暴露旧测试。
        file(WRITE "${CMAKE_BINARY_DIR}/CTestTestfile.cmake" "# Testing disabled\n")
    endif()
endif()
option(PANGU_BUILD_ENGINE_TESTS "Build PanguEngine's own CTest targets" ${engineStandalone})
unset(PANGU_VALIDATE_TOOLCHAIN CACHE)
include(cmake/PanguTesting.cmake)
if(engineStandalone AND NOT CMAKE_CROSSCOMPILING)
    pangu_require_full_testing(OWNER engine)
endif()
# Engine 核心和 Engine 根下 Plugin 共用此策略；嵌入时不改变父 Project 的上下文。
pangu_test_context(OWNER engine ENABLED "${PANGU_BUILD_ENGINE_TESTS}")

if(engineStandalone)
    set(PANGU_ENGINE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    include(cmake/PanguPlugins.cmake)
    set(PANGU_ENABLED_PLUGINS "" CACHE STRING "Enabled source Plugin IDs")
    pangu_add_enabled_plugins(ENGINE_ROOT "${PANGU_ENGINE_SOURCE_DIR}" ENABLED ${PANGU_ENABLED_PLUGINS})
endif()

if(BUILD_TESTING AND PANGU_BUILD_ENGINE_TESTS)
    pangu_add_owned_subdirectory(SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Tests" OWNER engine RELATIVE_BINARY_DIR Tests)
endif()

if(PANGU_BUILD_BENCHMARKS AND NOT CMAKE_CROSSCOMPILING)
    include(cmake/PanguTestDependencies.cmake)
    pangu_require_googlebenchmark()
    pangu_add_owned_subdirectory(
        SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Tests/Module/Benchmarks"
        OWNER engine
        RELATIVE_BINARY_DIR Tests/Module/Benchmarks
    )
    pangu_add_owned_subdirectory(
        SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Tests/Log/Benchmarks"
        OWNER engine
        RELATIVE_BINARY_DIR Tests/Log/Benchmarks
    )
    if(flecsAssertBenchmarks)
        pangu_add_owned_subdirectory(
            SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Tests/ECS/Benchmarks"
            OWNER engine
            RELATIVE_BINARY_DIR Tests/ECS/Benchmarks
        )
    endif()
endif()
