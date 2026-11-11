cmake_minimum_required(VERSION 4.0)
if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    message(FATAL_ERROR "incompatible_sdk: configure PanguMetaTool as an independent host project")
endif()
set(PANGU_META_SDK_CONFIG "" CACHE FILEPATH "Prepared host SDK configuration")
get_filename_component(defaultEngineSource "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(PANGU_ENGINE_SOURCE_DIR "${defaultEngineSource}" CACHE PATH "Engine support sources for the independent host tool")
if(PANGU_META_SDK_CONFIG)
    if(NOT EXISTS "${PANGU_META_SDK_CONFIG}")
        message(FATAL_ERROR "incompatible_sdk: SDK configuration does not exist")
    endif()
    include("${PANGU_META_SDK_CONFIG}")
endif()
if(PANGU_META_HOST_COMPILER)
    file(REAL_PATH "${PANGU_META_HOST_COMPILER}" requestedHostCompiler)
    set(CMAKE_CXX_COMPILER "${PANGU_META_HOST_COMPILER}" CACHE FILEPATH "Host Clang" FORCE)
endif()
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
    if(NOT CMAKE_TOOLCHAIN_FILE)
        set(CMAKE_TOOLCHAIN_FILE "${PANGU_ENGINE_SOURCE_DIR}/cmake/Platform/Linux/Toolchain.cmake"
            CACHE FILEPATH "Native Linux toolchain")
    endif()
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
    if(NOT CMAKE_TOOLCHAIN_FILE)
        set(CMAKE_TOOLCHAIN_FILE "${PANGU_ENGINE_SOURCE_DIR}/cmake/Platform/macOS/Toolchain.cmake"
            CACHE FILEPATH "Native macOS toolchain")
    endif()
endif()
if("${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_INIT} $ENV{CXXFLAGS}" MATCHES "-stdlib=libstdc[+][+]")
    message(FATAL_ERROR "sdk_stdlib_mismatch: libstdc++ is not supported")
endif()
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build configuration" FORCE)
endif()
project(PanguMetaTool LANGUAGES C CXX)
if(requestedHostCompiler)
    file(REAL_PATH "${CMAKE_CXX_COMPILER}" actualHostCompiler)
    if(NOT actualHostCompiler STREQUAL requestedHostCompiler)
        message(FATAL_ERROR "incompatible_sdk: LLVM_ROOT/toolchain differs from PANGU_META_HOST_COMPILER")
    endif()
endif()
if(CMAKE_CROSSCOMPILING OR NOT CMAKE_GENERATOR STREQUAL "Ninja" OR CMAKE_CONFIGURATION_TYPES)
    message(FATAL_ERROR "incompatible_sdk: PanguMetaTool requires a native single-config Ninja build")
endif()
if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR
   CMAKE_CXX_COMPILER_VERSION VERSION_LESS "23.0" OR NOT CMAKE_CXX_COMPILER_VERSION VERSION_LESS "24.0")
    message(FATAL_ERROR "incompatible_sdk: host Clang 23.x required")
endif()
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# Windows 的 try_compile 默认 Debug；预检必须使用实际工具目标的 CRT/STL 配置。
set(CMAKE_TRY_COMPILE_CONFIGURATION "${CMAKE_BUILD_TYPE}")
foreach(path IN ITEMS PANGU_META_LLVM_DIR PANGU_META_CLANG_DIR PANGU_META_RESOURCE_DIR)
    set(${path} "${${path}}" CACHE PATH "Explicit host SDK path")
    if(NOT IS_ABSOLUTE "${${path}}" OR NOT IS_DIRECTORY "${${path}}")
        message(FATAL_ERROR "incompatible_sdk: missing ${path}; prepare the SDK explicitly")
    endif()
endforeach()
set(PANGU_META_HOST_COMPILER "${CMAKE_CXX_COMPILER}" CACHE FILEPATH "SDK host compiler")
# 清除 find_package 的历史路径，SDK 切换必须重新验证。
set(LLVM_DIR "${PANGU_META_LLVM_DIR}" CACHE PATH "LLVM package" FORCE)
set(Clang_DIR "${PANGU_META_CLANG_DIR}" CACHE PATH "Clang package" FORCE)
find_package(LLVM CONFIG REQUIRED PATHS "${PANGU_META_LLVM_DIR}" NO_DEFAULT_PATH)
find_package(Clang CONFIG REQUIRED PATHS "${PANGU_META_CLANG_DIR}" NO_DEFAULT_PATH)
if(NOT LLVM_VERSION_MAJOR EQUAL 23 OR NOT LLVM_VERSION MATCHES "^23[.]")
    message(FATAL_ERROR "incompatible_sdk: LLVM and Clang SDK 23.x required")
endif()

include(CheckCXXSourceCompiles)
include(CheckCXXSourceRuns)
set(CMAKE_REQUIRED_FLAGS "-fno-exceptions -frtti -Wall -Wextra -Wpedantic -Werror")
unset(PG_STDLIB_MATCHES CACHE)
check_cxx_source_compiles([=[
#include <string>
#if defined(_WIN32)
#if !defined(_CPPLIB_VER) || defined(_LIBCPP_VERSION) || defined(__GLIBCXX__)
#error sdk_stdlib_mismatch
#endif
#else
#if !defined(_LIBCPP_VERSION) || defined(__GLIBCXX__)
#error sdk_stdlib_mismatch
#endif
#endif
int main() { return std::string("sdk").empty(); }
]=] PG_STDLIB_MATCHES)
if(NOT PG_STDLIB_MATCHES)
    message(FATAL_ERROR "sdk_stdlib_mismatch: host requires Windows/MSVC STL or Linux/macOS/libc++")
endif()
if(NOT LLVM_ENABLE_RTTI)
    message(FATAL_ERROR "sdk_rtti_required: LLVM/Clang SDK must be built with LLVM_ENABLE_RTTI=ON")
endif()
foreach(sdkInclude IN LISTS LLVM_INCLUDE_DIRS CLANG_INCLUDE_DIRS)
    string(APPEND CMAKE_REQUIRED_FLAGS " -isystem \"${sdkInclude}\"")
endforeach()
set(CMAKE_REQUIRED_LIBRARIES clangBasic)
unset(PG_STDLIB_LINKS CACHE)
check_cxx_source_compiles([=[
#include <clang/Basic/Version.h>
#include <string>
int main()
{
    const std::string version = clang::getClangFullVersion();
    return version.empty();
}
]=] PG_STDLIB_LINKS)
if(NOT PG_STDLIB_LINKS)
    message(FATAL_ERROR "sdk_stdlib_mismatch: cannot link the SDK C++ string ABI; see CMakeConfigureLog.yaml")
endif()
set(CMAKE_REQUIRED_LIBRARIES clangFrontend clangSema)
unset(PG_RTTI_LINKS CACHE)
unset(PG_RTTI_LINKS_EXITCODE CACHE)
unset(PG_RTTI_LINKS_COMPILED CACHE)
check_cxx_source_runs([=[
#include <clang/Basic/ParsedAttrInfo.h>
#include <clang/Frontend/FrontendAction.h>
#include <clang/Frontend/FrontendActions.h>
#include <typeinfo>
class SdkAction final : public clang::SyntaxOnlyAction
{
};
int main()
{
    const auto builtins = clang::ParsedAttrInfo::getAllBuiltin();
    if (builtins.empty())
    {
        return 1;
    }
    const auto* builtin = builtins.front();
    if (typeid(*builtin).name()[0] == 0)
    {
        return 1;
    }
    SdkAction action;
    const clang::FrontendAction& base = action;
    return typeid(base) != typeid(SdkAction) || dynamic_cast<const SdkAction*>(&base) == nullptr;
}
]=] PG_RTTI_LINKS)
if(NOT PG_RTTI_LINKS)
    message(FATAL_ERROR "sdk_rtti_required: SDK base RTTI/derived linking failed; see CMakeConfigureLog.yaml")
endif()
unset(CMAKE_REQUIRED_FLAGS)
unset(CMAKE_REQUIRED_LIBRARIES)

function(pangu_meta_target target)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror -fno-exceptions -frtti)
    target_include_directories(${target} PUBLIC "${CMAKE_CURRENT_FUNCTION_LIST_DIR}")
endfunction()
# SDK、编译器、标准库与无异常策略均已在上方完成独立宿主验证。
set(PANGU_VALIDATED_STANDALONE_HOST ON)
add_subdirectory("${PANGU_ENGINE_SOURCE_DIR}/Source/ThirdParty/NlohmannJson" ThirdParty/NlohmannJson)
add_library(PanguMetaModel STATIC Driver/GenerationRequest.cpp Driver/InputOwnership.cpp Driver/OutputMapping.cpp MetaIR/Normalize.cpp)
pangu_meta_target(PanguMetaModel)
target_include_directories(PanguMetaModel PUBLIC "${PANGU_ENGINE_SOURCE_DIR}/Source/Runtime/Foundation/Public")
target_link_libraries(PanguMetaModel PRIVATE ThirdParty::NlohmannJson)
target_compile_definitions(PanguMetaModel PRIVATE JSON_NOEXCEPTION=1)
add_library(PanguMetaOutput STATIC Output/Publish.cpp Driver/OutputMappingValidation.cpp)
if(WIN32)
    target_sources(PanguMetaOutput PRIVATE Output/Platform/Windows/FileReplace.cpp)
else()
    target_sources(PanguMetaOutput PRIVATE Output/Platform/Posix/FileReplace.cpp)
endif()
pangu_meta_target(PanguMetaOutput)
target_link_libraries(PanguMetaOutput PRIVATE PanguMetaModel ThirdParty::NlohmannJson)
target_compile_definitions(PanguMetaOutput PRIVATE JSON_NOEXCEPTION=1)
add_library(PanguMetaFrontend STATIC Driver/SdkInfo.cpp Driver/CompilationContext.cpp Attr/Scan.cpp Attr/TypeAttribute.cpp Attr/FieldAttribute.cpp Attr/OptionsReader.cpp Attr/ConditionGuard.cpp)
pangu_meta_target(PanguMetaFrontend)
target_include_directories(PanguMetaFrontend SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS} ${CLANG_INCLUDE_DIRS})
target_compile_definitions(PanguMetaFrontend PRIVATE
    "PANGU_META_RESOURCE_PATH=\"${PANGU_META_RESOURCE_DIR}\""
    "PANGU_META_HOST_COMPILER_PATH=\"${PANGU_META_HOST_COMPILER}\"")
target_link_libraries(PanguMetaFrontend PRIVATE clangTooling clangFrontend clangSema clangAST clangBasic)
target_link_libraries(PanguMetaFrontend PRIVATE PanguMetaModel PanguMetaOutput ThirdParty::NlohmannJson)
target_compile_definitions(PanguMetaFrontend PRIVATE JSON_NOEXCEPTION=1)
add_library(PanguMetaCodeGen STATIC CodeGen/Generate.cpp CodeGen/CppNames.cpp CodeGen/TypeEmitter.cpp CodeGen/ModuleEmitter.cpp
    CodeGen/QueryEmitter.cpp CodeGen/CatalogEmitter.cpp)
pangu_meta_target(PanguMetaCodeGen)
target_link_libraries(PanguMetaCodeGen PUBLIC PanguMetaModel)
target_sources(PanguMetaFrontend PRIVATE Driver/RunGeneration.cpp)
target_link_libraries(PanguMetaFrontend PRIVATE PanguMetaCodeGen)
add_executable(PanguMetaTool Private/Main.cpp)
pangu_meta_target(PanguMetaTool)
target_link_libraries(PanguMetaTool PRIVATE PanguMetaFrontend PanguMetaModel)

# 配置期只调用 SDK 接口，避免后续 CLI 增加生成入口时把 Model/CodeGen 带入预检。
set(sdkProbeSource [=[
#include "Driver/SdkInfo.h"
#include <iostream>
int main()
{
    const auto result = pg::meta::tool::CheckSdk();
    if (result)
    {
        return 0;
    }
    for (const auto& diagnostic : result.error())
    {
        std::cerr << diagnostic.Code << ": " << diagnostic.Message << '\n';
    }
    return 1;
}
]=])
set(probeFlags "-Wall -Wextra -Wpedantic -Werror -fno-exceptions -frtti")
foreach(sdkInclude IN LISTS LLVM_INCLUDE_DIRS CLANG_INCLUDE_DIRS)
    string(APPEND probeFlags " -isystem \"${sdkInclude}\"")
endforeach()
try_run(sdkRun sdkCompiled SOURCE_FROM_VAR SdkCheck.cpp sdkProbeSource
    SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/Driver/SdkInfo.cpp" NO_CACHE
    CMAKE_FLAGS "-DINCLUDE_DIRECTORIES:STRING=${CMAKE_CURRENT_SOURCE_DIR}"
        "-DCOMPILE_DEFINITIONS:STRING=${probeFlags}"
    COMPILE_DEFINITIONS
        "-DPANGU_META_RESOURCE_PATH=\"${PANGU_META_RESOURCE_DIR}\""
        "-DPANGU_META_HOST_COMPILER_PATH=\"${PANGU_META_HOST_COMPILER}\""
    LINK_LIBRARIES clangTooling clangFrontend clangSema clangAST clangBasic
    COMPILE_OUTPUT_VARIABLE sdkCompileOutput RUN_OUTPUT_VARIABLE sdkOutput)
if(NOT sdkCompiled OR NOT sdkRun EQUAL 0)
    message(FATAL_ERROR "incompatible_sdk: raw TU SDK check failed:\n${sdkCompileOutput}\n${sdkOutput}")
endif()
if(PG_SDK_CONFIG_FILE)
    if(NOT PANGU_META_SDK_SOURCE_VERSION)
        set(PANGU_META_SDK_SOURCE_VERSION "${LLVM_PACKAGE_VERSION}")
    endif()
    set(content "# 本机 SDK 状态；由成功的真实预检生成，不提交版本库。\n")
    foreach(key IN ITEMS PANGU_META_LLVM_DIR PANGU_META_CLANG_DIR PANGU_META_RESOURCE_DIR
                         PANGU_META_HOST_COMPILER PANGU_META_SDK_SOURCE_VERSION)
        string(APPEND content "set(${key} [==[${${key}}]==])\n")
    endforeach()
    file(WRITE "${PG_SDK_CONFIG_FILE}" "${content}")
endif()

include(CTest)
# 原生父图只读此清单，不导入任何 LLVM/Clang target。
file(GLOB_RECURSE metaBuildInputs LIST_DIRECTORIES false
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
    "${PANGU_ENGINE_SOURCE_DIR}/Source/Runtime/Foundation/Public/*.h")
get_target_property(jsonIncludeDirectories ThirdPartyNlohmannJson INTERFACE_INCLUDE_DIRECTORIES)
foreach(includeDirectory IN LISTS jsonIncludeDirectories)
    file(GLOB_RECURSE jsonHeaders LIST_DIRECTORIES false "${includeDirectory}/*.hpp")
    list(APPEND metaBuildInputs ${jsonHeaders})
endforeach()
list(APPEND metaBuildInputs "${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt"
    "${PANGU_ENGINE_SOURCE_DIR}/Source/ThirdParty/NlohmannJson/CMakeLists.txt")
file(GLOB sdkConfigurationInputs LIST_DIRECTORIES false
    "${PANGU_META_LLVM_DIR}/*.cmake" "${PANGU_META_CLANG_DIR}/*.cmake")
list(APPEND metaBuildInputs ${sdkConfigurationInputs})
foreach(includeDirectory IN LISTS LLVM_INCLUDE_DIRS CLANG_INCLUDE_DIRS)
    file(GLOB_RECURSE sdkHeaders LIST_DIRECTORIES false
        "${includeDirectory}/*.h" "${includeDirectory}/*.inc" "${includeDirectory}/*.def")
    list(APPEND metaBuildInputs ${sdkHeaders})
endforeach()
if(CMAKE_TOOLCHAIN_FILE)
    cmake_path(ABSOLUTE_PATH CMAKE_TOOLCHAIN_FILE BASE_DIRECTORY "${CMAKE_BINARY_DIR}" NORMALIZE
        OUTPUT_VARIABLE nativeToolchain)
    list(APPEND metaBuildInputs "${nativeToolchain}")
endif()
get_property(sdkTargets DIRECTORY PROPERTY IMPORTED_TARGETS)
foreach(sdkTarget IN LISTS sdkTargets)
    get_target_property(configurations "${sdkTarget}" IMPORTED_CONFIGURATIONS)
    foreach(configuration IN ITEMS "" ${configurations})
        set(property IMPORTED_LOCATION)
        if(configuration)
            string(TOUPPER "${configuration}" configuration)
            string(APPEND property "_${configuration}")
        endif()
        get_target_property(location "${sdkTarget}" "${property}")
        if(location AND EXISTS "${location}")
            list(APPEND metaBuildInputs "${location}")
        endif()
    endforeach()
endforeach()
list(REMOVE_DUPLICATES metaBuildInputs)
file(CONFIGURE OUTPUT "${CMAKE_BINARY_DIR}/PanguMetaToolBuildInputs.cmake"
    CONTENT "set(PANGU_META_BUILD_SOURCE [==[${CMAKE_CURRENT_SOURCE_DIR}]==])\nset(PANGU_META_BUILD_INPUTS [==[${metaBuildInputs}]==])\n" @ONLY)
if(BUILD_TESTING)
    set(PANGU_ENGINE_ROOT "${PANGU_ENGINE_SOURCE_DIR}")
    include("${PANGU_ENGINE_ROOT}/cmake/PanguTestDependencies.cmake")
    include("${PANGU_ENGINE_ROOT}/cmake/PanguTesting.cmake")
    pangu_test_context(OWNER engine ENABLED "${BUILD_TESTING}")
    add_subdirectory("${PANGU_ENGINE_ROOT}/Tests/BuildTools/PanguMetaTool" "Tests")
endif()
