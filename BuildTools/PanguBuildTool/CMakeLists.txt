cmake_minimum_required(VERSION 4.0)
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
    set(nativeLinuxToolchain "${CMAKE_CURRENT_LIST_DIR}/../../cmake/Platform/Linux/Toolchain.cmake")
    if(NOT CMAKE_TOOLCHAIN_FILE)
        set(CMAKE_TOOLCHAIN_FILE "${nativeLinuxToolchain}" CACHE FILEPATH "Native Linux build-tool toolchain")
    endif()
    # 旧缓存的 CMakeSystem 可能未记录 Toolchain，仍须在 project 前初始化宿主策略。
    include("${nativeLinuxToolchain}")
endif()
project(PanguBuildTool LANGUAGES CXX)

if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux" AND
   (NOT CMAKE_CXX_FLAGS MATCHES "-nostdinc[+][+]" OR
    NOT CMAKE_EXE_LINKER_FLAGS MATCHES "-stdlib=libc[+][+]"))
    message(FATAL_ERROR "PanguBuildTool requires the native Linux libc++ toolchain; use a fresh BuildRoot or reset stale CMAKE_CXX_FLAGS/CMAKE_EXE_LINKER_FLAGS")
endif()

if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR
   NOT CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL "23.0" OR
   CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL "24.0")
    message(FATAL_ERROR "PanguBuildTool requires host Clang 23.x, got ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
endif()
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../Source/ThirdParty/TomlPlusPlus"
                 "${CMAKE_CURRENT_BINARY_DIR}/ThirdParty/TomlPlusPlus")

add_library(PanguPluginManifest STATIC Private/PluginManifest.cpp)
target_include_directories(PanguPluginManifest PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Private")
target_link_libraries(PanguPluginManifest PRIVATE ThirdParty::TomlPlusPlus)
target_compile_options(PanguPluginManifest PRIVATE -fno-exceptions)

add_library(PanguPluginDiscovery STATIC
    Private/PluginDiscovery.cpp
    Private/PluginJson.cpp
    Private/PluginPathInputs.cpp
)
target_link_libraries(PanguPluginDiscovery PUBLIC PanguPluginManifest)
target_compile_options(PanguPluginDiscovery PRIVATE -fno-exceptions)

add_executable(PanguBuildTool Private/Main.cpp)
target_link_libraries(PanguBuildTool PRIVATE PanguPluginDiscovery)
target_compile_options(PanguBuildTool PRIVATE -fno-exceptions)

include(CTest)
if(BUILD_TESTING)
    get_filename_component(PANGU_ENGINE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
    include("${PANGU_ENGINE_ROOT}/cmake/PanguTestDependencies.cmake")
    include("${PANGU_ENGINE_ROOT}/cmake/PanguTesting.cmake")
    pangu_test_context(OWNER engine ENABLED "${BUILD_TESTING}")
    add_subdirectory("${PANGU_ENGINE_ROOT}/Tests/BuildTools/PanguBuildTool"
        "${CMAKE_CURRENT_BINARY_DIR}/Tests/BuildTools/PanguBuildTool")
endif()
