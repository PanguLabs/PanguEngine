cmake_minimum_required(VERSION 4.0)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(_pangu_engine_standalone ON)
    include(cmake/ValidateBuildEnvironment.cmake)
else()
    set(_pangu_engine_standalone OFF)
endif()
project(PanguEngine LANGUAGES CXX)

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
include(cmake/Platform/Common/Module.cmake)

pangu_owned_build_root(_pangu_engine_root engine)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${_pangu_engine_root}/Lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${_pangu_engine_root}/Bin")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${_pangu_engine_root}/Bin")
foreach(directory IN ITEMS Source/ThirdParty/NlohmannJson Source/ThirdParty/Fmt Source/ThirdParty/Spdlog
        Source/Runtime/Foundation Source/Runtime/Log Source/Runtime/Module Source/Runtime/EngineHost)
    pangu_add_owned_subdirectory(SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/${directory}"
        OWNER engine RELATIVE_BINARY_DIR "${directory}")
endforeach()

if(_pangu_engine_standalone)
    include(CTest)
    if(NOT BUILD_TESTING)
        # CMake 不会在关闭测试时覆盖旧注册，清空根入口以避免复用构建树时暴露旧测试。
        file(WRITE "${CMAKE_BINARY_DIR}/CTestTestfile.cmake" "# Testing disabled\n")
    endif()
endif()
option(PANGU_BUILD_ENGINE_TESTS "Build PanguEngine's own CTest targets" ${_pangu_engine_standalone})
unset(PANGU_VALIDATE_TOOLCHAIN CACHE)
include(cmake/PanguTesting.cmake)
if(_pangu_engine_standalone AND NOT CMAKE_CROSSCOMPILING)
    pangu_require_full_testing(OWNER engine)
endif()
# Engine 核心和 Engine 根下 Plugin 共用此策略；嵌入时不改变父 Project 的上下文。
pangu_test_context(OWNER engine ENABLED "${PANGU_BUILD_ENGINE_TESTS}")

if(_pangu_engine_standalone)
    set(PANGU_ENGINE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    include(cmake/PanguPlugins.cmake)
    set(PANGU_ENABLED_PLUGINS "" CACHE STRING "Enabled source Plugin IDs")
    pangu_add_enabled_plugins(ENGINE_ROOT "${PANGU_ENGINE_SOURCE_DIR}" ENABLED ${PANGU_ENABLED_PLUGINS})
endif()

option(PANGU_BUILD_SAMPLES "Build PanguEngine samples" OFF)
if(PANGU_BUILD_SAMPLES)
    pangu_add_owned_subdirectory(SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Samples/MinimalHost"
        OWNER engine RELATIVE_BINARY_DIR Samples/MinimalHost)
endif()

if(BUILD_TESTING AND PANGU_BUILD_ENGINE_TESTS)
    pangu_add_owned_subdirectory(SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Tests"
        OWNER engine RELATIVE_BINARY_DIR Tests)
endif()

option(PANGU_BUILD_BENCHMARKS "Build native performance benchmarks" OFF)
if(PANGU_BUILD_BENCHMARKS AND NOT CMAKE_CROSSCOMPILING)
    include(cmake/PanguTestDependencies.cmake)
    pangu_require_googlebenchmark()
    pangu_add_owned_subdirectory(SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Tests/Module/Benchmarks"
        OWNER engine RELATIVE_BINARY_DIR Tests/Module/Benchmarks)
    pangu_add_owned_subdirectory(SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Tests/Log/Benchmarks"
        OWNER engine RELATIVE_BINARY_DIR Tests/Log/Benchmarks)
endif()
