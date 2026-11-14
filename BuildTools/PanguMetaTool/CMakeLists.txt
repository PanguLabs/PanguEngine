cmake_minimum_required(VERSION 4.0)
if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    message(FATAL_ERROR "toolchain_error: configure PanguMetaTool as an independent host project")
endif()
get_filename_component(defaultEngineSource "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(PANGU_ENGINE_SOURCE_DIR "${defaultEngineSource}" CACHE PATH "Engine support sources for the independent host tool")
if("$ENV{LLVM_ROOT}" STREQUAL "" OR NOT IS_DIRECTORY "$ENV{LLVM_ROOT}")
    message(FATAL_ERROR "toolchain_error: set LLVM_ROOT to an installed Clang 23 development package")
endif()
file(REAL_PATH "$ENV{LLVM_ROOT}" llvmRoot)
set(hostSuffix "")
if(CMAKE_HOST_WIN32)
    set(hostSuffix .exe)
endif()
set(PANGU_META_HOST_COMPILER "${llvmRoot}/bin/clang++${hostSuffix}")
if(NOT EXISTS "${PANGU_META_HOST_COMPILER}")
    message(FATAL_ERROR "toolchain_error: LLVM_ROOT has no host clang++")
endif()
set(PANGU_META_LLVM_DIR "${llvmRoot}/lib/cmake/llvm")
set(PANGU_META_CLANG_DIR "${llvmRoot}/lib/cmake/clang")
if(NOT EXISTS "${PANGU_META_LLVM_DIR}/LLVMConfig.cmake" OR NOT EXISTS "${PANGU_META_CLANG_DIR}/ClangConfig.cmake")
    message(FATAL_ERROR "toolchain_error: LLVM/Clang development packages are missing under LLVM_ROOT")
endif()
if(CMAKE_TOOLCHAIN_FILE)
    message(FATAL_ERROR "toolchain_error: standalone Meta tool selects its host toolchain; configure --fresh without CMAKE_TOOLCHAIN_FILE")
endif()
file(REAL_PATH "${PANGU_META_HOST_COMPILER}" hostCompiler)
set(CMAKE_CXX_COMPILER "${hostCompiler}" CACHE FILEPATH "Installed host Clang" FORCE)
set(CMAKE_CXX_COMPILER_ARG1 "--driver-mode=g++" CACHE STRING "Keep C++ linking after resolving compiler aliases" FORCE)
file(REAL_PATH "${llvmRoot}/bin/clang${hostSuffix}" hostCCompiler)
set(CMAKE_C_COMPILER "${hostCCompiler}" CACHE FILEPATH "Installed host Clang C" FORCE)
set(CMAKE_ASM_COMPILER "${hostCCompiler}" CACHE FILEPATH "Installed host assembler" FORCE)
set(hostCxxFlags "${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_INIT} $ENV{CXXFLAGS}")
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
    if(hostCxxFlags MATCHES "-stdlib=libc[+][+]")
        message(FATAL_ERROR "stdlib_mismatch: Linux Clang host requires libstdc++")
    endif()
    string(APPEND CMAKE_CXX_FLAGS_INIT " -stdlib=libstdc++")
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
    if(hostCxxFlags MATCHES "-stdlib=libstdc[+][+]")
        message(FATAL_ERROR "stdlib_mismatch: macOS Clang host requires libc++")
    endif()
    string(APPEND CMAKE_CXX_FLAGS_INIT " -stdlib=libc++")
endif()
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build configuration" FORCE)
endif()
project(PanguMetaTool LANGUAGES C CXX)
# 旧 CMake 编译器信息可能覆盖 ARG1 的普通变量，生成阶段仍明确使用 C++ 驱动。
set(CMAKE_CXX_COMPILER_ARG1 --driver-mode=g++)
if(CMAKE_CROSSCOMPILING OR NOT CMAKE_GENERATOR STREQUAL "Ninja" OR CMAKE_CONFIGURATION_TYPES)
    message(FATAL_ERROR "toolchain_error: PanguMetaTool requires a native single-config Ninja build")
endif()
if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR CMAKE_CXX_COMPILER_VERSION VERSION_LESS "23.0" OR
   NOT CMAKE_CXX_COMPILER_VERSION VERSION_LESS "24.0")
    message(FATAL_ERROR "toolchain_error: host Clang 23.x required")
endif()
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_TRY_COMPILE_CONFIGURATION "${CMAKE_BUILD_TYPE}")
set(CMAKE_POLICY_DEFAULT_CMP0091 NEW)
add_subdirectory("${PANGU_ENGINE_SOURCE_DIR}/Source/ThirdParty/Zlib" ThirdParty/Zlib)
add_subdirectory("${PANGU_ENGINE_SOURCE_DIR}/Source/ThirdParty/Zstd" ThirdParty/Zstd)
list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
if(CMAKE_HOST_WIN32)
    if(NOT MSVC_DIA_SDK_DIR AND DEFINED ENV{VSINSTALLDIR})
        set(MSVC_DIA_SDK_DIR "$ENV{VSINSTALLDIR}/DIA SDK")
    endif()
    if(NOT MSVC_DIA_SDK_DIR)
        find_program(vswhere NAMES vswhere PATHS
            "$ENV{ProgramFiles\(x86\)}/Microsoft Visual Studio/Installer"
            "$ENV{ProgramFiles}/Microsoft Visual Studio/Installer")
        if(vswhere)
            execute_process(COMMAND "${vswhere}" -latest -products *
                -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
                OUTPUT_VARIABLE visualStudio OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
            if(visualStudio)
                set(MSVC_DIA_SDK_DIR "${visualStudio}/DIA SDK")
            endif()
        endif()
    endif()
    unset(DIASDK_INCLUDE_DIR CACHE)
    unset(DIASDK_LIBRARIES CACHE)
endif()
set(LLVM_DIR "${PANGU_META_LLVM_DIR}" CACHE PATH "Selected LLVM package" FORCE)
set(Clang_DIR "${PANGU_META_CLANG_DIR}" CACHE PATH "Selected Clang package" FORCE)
find_package(LLVM CONFIG REQUIRED PATHS "${PANGU_META_LLVM_DIR}" NO_DEFAULT_PATH)
find_package(Clang CONFIG REQUIRED PATHS "${PANGU_META_CLANG_DIR}" NO_DEFAULT_PATH)
if(NOT LLVM_VERSION_MAJOR EQUAL 23 OR NOT LLVM_VERSION MATCHES "^23[.]")
    message(FATAL_ERROR "toolchain_error: LLVM/Clang 23.x development package required")
endif()
if(CMAKE_HOST_WIN32)
    if(LLVM_ENABLE_DIA_SDK AND NOT DIASDK_FOUND)
        message(FATAL_ERROR "toolchain_error: install the Visual Studio DIA SDK or set MSVC_DIA_SDK_DIR")
    endif()
    set_target_properties(zlibstatic libzstd_static PROPERTIES MSVC_RUNTIME_LIBRARY "${CMAKE_MSVC_RUNTIME_LIBRARY}")
endif()
set(PANGU_META_RESOURCE_DIR "${llvmRoot}/lib/clang/${LLVM_VERSION_MAJOR}")
if(NOT EXISTS "${PANGU_META_RESOURCE_DIR}/include/stddef.h")
    message(FATAL_ERROR "toolchain_error: Clang resource include/stddef.h is missing under LLVM_ROOT")
endif()
set(panguMetaClangRttiFlag -fno-rtti)
if(LLVM_ENABLE_RTTI)
    set(panguMetaClangRttiFlag -frtti)
endif()
set(panguMetaClangLibraries clangTooling clangFrontend clangSema clangAST clangBasic)
if(TARGET clang-cpp)
    set(panguMetaClangLibraries clang-cpp)
    if(TARGET LLVM)
        list(APPEND panguMetaClangLibraries LLVM)
    endif()
endif()
include(CheckCXXSourceCompiles)
set(CMAKE_REQUIRED_FLAGS "-fno-exceptions -frtti -Wall -Wextra -Wpedantic -Werror")
unset(PG_STDLIB_MATCHES CACHE)
check_cxx_source_compiles([=[
#include <string>
#if defined(_WIN32)
#if !defined(_CPPLIB_VER) || defined(_LIBCPP_VERSION) || defined(__GLIBCXX__) || _ITERATOR_DEBUG_LEVEL != 0
#error stdlib_mismatch
#endif
#elif defined(__linux__)
#if !defined(__GLIBCXX__) || defined(_LIBCPP_VERSION)
#error stdlib_mismatch
#endif
#else
#if !defined(_LIBCPP_VERSION) || defined(__GLIBCXX__)
#error stdlib_mismatch
#endif
#endif
int main() { return std::string("clang-check").empty(); }
]=] PG_STDLIB_MATCHES)
if(NOT PG_STDLIB_MATCHES)
    message(FATAL_ERROR "stdlib_mismatch: incompatible host standard library or MSVC iterator ABI")
endif()
unset(CMAKE_REQUIRED_FLAGS)

function(pangu_meta_target target)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror -fno-exceptions -frtti)
    target_include_directories(${target} PUBLIC "${CMAKE_CURRENT_FUNCTION_LIST_DIR}")
endfunction()
# Clang、编译器、标准库与无异常策略均已在上方完成独立宿主验证。
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
add_library(PanguMetaFrontend STATIC Driver/ClangInfo.cpp Driver/CompilationContext.cpp Attr/Scan.cpp Attr/TypeAttribute.cpp Attr/FieldAttribute.cpp Attr/OptionsReader.cpp Attr/ConditionGuard.cpp)
pangu_meta_target(PanguMetaFrontend)
target_compile_options(PanguMetaFrontend PRIVATE ${panguMetaClangRttiFlag})
target_include_directories(PanguMetaFrontend SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS} ${CLANG_INCLUDE_DIRS})
target_compile_definitions(PanguMetaFrontend PRIVATE
    "PANGU_META_CLANG_HAS_RTTI=$<BOOL:${LLVM_ENABLE_RTTI}>"
    "PANGU_META_RESOURCE_PATH=\"${PANGU_META_RESOURCE_DIR}\""
    "PANGU_META_HOST_COMPILER_PATH=\"${PANGU_META_HOST_COMPILER}\"")
target_link_libraries(PanguMetaFrontend PRIVATE ${panguMetaClangLibraries} ThirdParty::Zlib ThirdParty::Zstd)
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

set_property(TARGET PanguMetaTool APPEND PROPERTY LINK_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckMetaTool.cmake")
add_custom_command(TARGET PanguMetaTool POST_BUILD
    COMMAND "${CMAKE_COMMAND}" "-DPG_TOOL=$<TARGET_FILE:PanguMetaTool>" "-DPG_BUILD_ROOT=${CMAKE_BINARY_DIR}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckMetaTool.cmake"
    COMMENT "PanguMetaTool installed Clang validation" VERBATIM)

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
file(GLOB_RECURSE dependencyInputs LIST_DIRECTORIES false
    "${PANGU_ENGINE_SOURCE_DIR}/Source/ThirdParty/Zlib/*.c"
    "${PANGU_ENGINE_SOURCE_DIR}/Source/ThirdParty/Zlib/*.h"
    "${PANGU_ENGINE_SOURCE_DIR}/Source/ThirdParty/Zlib/*.in"
    "${PANGU_ENGINE_SOURCE_DIR}/Source/ThirdParty/Zlib/CMakeLists.txt"
    "${PANGU_ENGINE_SOURCE_DIR}/Source/ThirdParty/Zlib/Repo/CMakeLists.txt"
    "${PANGU_ENGINE_SOURCE_DIR}/Source/ThirdParty/Zstd/*.c"
    "${PANGU_ENGINE_SOURCE_DIR}/Source/ThirdParty/Zstd/*.h"
    "${PANGU_ENGINE_SOURCE_DIR}/Source/ThirdParty/Zstd/*.S"
    "${PANGU_ENGINE_SOURCE_DIR}/Source/ThirdParty/Zstd/*.cmake"
    "${PANGU_ENGINE_SOURCE_DIR}/Source/ThirdParty/Zstd/*CMakeLists.txt"
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/*.cmake"
    "${PANGU_META_RESOURCE_DIR}/include/*.h")
list(APPEND metaBuildInputs ${dependencyInputs})
file(GLOB llvmConfigurationInputs LIST_DIRECTORIES false
    "${PANGU_META_LLVM_DIR}/*.cmake" "${PANGU_META_CLANG_DIR}/*.cmake")
list(APPEND metaBuildInputs ${llvmConfigurationInputs})
foreach(includeDirectory IN LISTS LLVM_INCLUDE_DIRS CLANG_INCLUDE_DIRS)
    file(GLOB_RECURSE clangHeaders LIST_DIRECTORIES false
        "${includeDirectory}/*.h" "${includeDirectory}/*.inc" "${includeDirectory}/*.def")
    list(APPEND metaBuildInputs ${clangHeaders})
endforeach()
if(CMAKE_TOOLCHAIN_FILE)
    cmake_path(ABSOLUTE_PATH CMAKE_TOOLCHAIN_FILE BASE_DIRECTORY "${CMAKE_BINARY_DIR}" NORMALIZE
        OUTPUT_VARIABLE nativeToolchain)
    list(APPEND metaBuildInputs "${nativeToolchain}")
endif()
get_property(importedTargets DIRECTORY PROPERTY IMPORTED_TARGETS)
foreach(importedTarget IN LISTS importedTargets)
    get_target_property(configurations "${importedTarget}" IMPORTED_CONFIGURATIONS)
    foreach(configuration IN ITEMS "" ${configurations})
        set(property IMPORTED_LOCATION)
        if(configuration)
            string(TOUPPER "${configuration}" configuration)
            string(APPEND property "_${configuration}")
        endif()
        get_target_property(location "${importedTarget}" "${property}")
        if(location AND EXISTS "${location}")
            list(APPEND metaBuildInputs "${location}")
        endif()
    endforeach()
endforeach()
list(REMOVE_DUPLICATES metaBuildInputs)
file(CONFIGURE OUTPUT "${CMAKE_BINARY_DIR}/PanguMetaToolBuildInputs.cmake"
    CONTENT "set(PANGU_META_BUILD_SOURCE [==[${CMAKE_CURRENT_SOURCE_DIR}]==])\nset(PANGU_META_RESOURCE_DIR [==[${PANGU_META_RESOURCE_DIR}]==])\nset(PANGU_META_LLVM_ROOT [==[${llvmRoot}]==])\nset(PANGU_META_BUILD_INPUTS [==[${metaBuildInputs}]==])\n" @ONLY)
if(BUILD_TESTING)
    set(PANGU_ENGINE_ROOT "${PANGU_ENGINE_SOURCE_DIR}")
    include("${PANGU_ENGINE_ROOT}/cmake/PanguTestDependencies.cmake")
    include("${PANGU_ENGINE_ROOT}/cmake/PanguTesting.cmake")
    pangu_test_context(OWNER engine ENABLED "${BUILD_TESTING}")
    add_subdirectory("${PANGU_ENGINE_ROOT}/Tests/BuildTools/PanguMetaTool" "Tests")
endif()
