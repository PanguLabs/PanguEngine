cmake_minimum_required(VERSION 4.0)

# Monolithic 外层可显式配置此独立 Modular 入口；不向外层登记原生运行测试。
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR AND NOT TARGET PanguModule)
    project(PanguSharedModuleFixture LANGUAGES CXX)
    include(CTest)
    set(PANGU_LINK_MODE Modular CACHE STRING "Pangu Module linkage" FORCE)
    set(PANGU_BUILD_ENGINE_TESTS ON CACHE BOOL "Build engine tests" FORCE)
    add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../../../.." Engine)
    return()
endif()

foreach(name IN ITEMS Provider Consumer)
    add_library(PanguShared${name} SHARED ${name}.cpp)
    string(TOUPPER "${name}" upperName)
    pangu_configure_module(PanguShared${name} EXPORT_MACRO "PG_SHARED_${upperName}_API")
    target_include_directories(PanguShared${name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
endforeach()
# 建立真实原生导入；宿主仅链接路径与观察器，不直接链接业务库。
target_link_libraries(PanguSharedConsumer PRIVATE PanguSharedProvider)
# 协议负例保持独立，缺 Query 的库不得间接导入 Runtime Query。
add_library(PanguMissingQuery SHARED MissingQuery.cpp)
pangu_configure_module(PanguMissingQuery EXPORT_MACRO PG_MISSING_QUERY_API)
add_library(PanguNullQuery SHARED NullQuery.cpp)
pangu_configure_module(PanguNullQuery EXPORT_MACRO PG_NULL_QUERY_API)
target_include_directories(PanguNullQuery PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
foreach(target IN ITEMS PanguSharedProvider PanguSharedConsumer PanguNullQuery)
    target_link_libraries(${target} PRIVATE PanguModule PanguScopeTestSupport)
endforeach()
foreach(name IN ITEMS Provider Consumer)
    add_library(PanguScope${name} SHARED Scope${name}.cpp)
    pangu_configure_module(PanguScope${name})
    target_include_directories(PanguScope${name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(PanguScope${name} PRIVATE PanguModule PanguScopeTestSupport)
    pangu_export_module_query(TARGET PanguScope${name} ID pg::module::fixture::kScope${name}Id ID_HEADER SharedFixtureContract.h QUERY pg::module::fixture::QueryScope${name} HEADER SharedFixtureContract.h)
endforeach()
pangu_export_module_query(TARGET PanguSharedProvider ID pg::module::fixture::kSharedProviderId ID_HEADER SharedFixtureContract.h QUERY pg::module::fixture::QuerySharedProvider HEADER SharedFixtureContract.h)
pangu_export_module_query(TARGET PanguSharedConsumer ID pg::module::fixture::kSharedConsumerId ID_HEADER SharedFixtureContract.h QUERY pg::module::fixture::QuerySharedConsumer HEADER SharedFixtureContract.h)
add_library(PanguDiagnosticsCleanup SHARED DiagnosticsCleanup.cpp)
pangu_configure_module(PanguDiagnosticsCleanup)
target_include_directories(PanguDiagnosticsCleanup PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(PanguDiagnosticsCleanup PRIVATE PanguModule PanguScopeTestSupport)
pangu_export_module_query(TARGET PanguDiagnosticsCleanup ID pg::module::fixture::kDiagnosticsCleanupId ID_HEADER SharedFixtureContract.h QUERY pg::module::fixture::QueryDiagnosticsCleanup HEADER SharedFixtureContract.h)
# 无身份的恶意 ABI 夹具保留手写空包装，不参与正常 Module 登记。

file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/SharedFixturePaths.h" CONTENT
"#pragma once
namespace pg::module::fixture {
inline constexpr auto kSharedProviderPath = R\"($<TARGET_FILE:PanguSharedProvider>)\";
inline constexpr auto kSharedConsumerPath = R\"($<TARGET_FILE:PanguSharedConsumer>)\";
inline constexpr auto kMissingQueryPath = R\"($<TARGET_FILE:PanguMissingQuery>)\";
inline constexpr auto kNullQueryPath = R\"($<TARGET_FILE:PanguNullQuery>)\";
inline constexpr auto kScopeProviderPath = R\"($<TARGET_FILE:PanguScopeProvider>)\";
inline constexpr auto kScopeConsumerPath = R\"($<TARGET_FILE:PanguScopeConsumer>)\";
inline constexpr auto kDiagnosticsCleanupPath = R\"($<TARGET_FILE:PanguDiagnosticsCleanup>)\";
}
")
add_executable(PanguNativeModuleLibraryTests ../../NativeModuleLibraryTests.cpp)
target_include_directories(PanguNativeModuleLibraryTests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}"
    "${PROJECT_SOURCE_DIR}/Source/Runtime/Module/Private")
target_link_libraries(PanguNativeModuleLibraryTests PRIVATE
    PanguBuildOptions PanguModule PanguScopeTestSupport ThirdParty::GoogleTestMain)
add_dependencies(PanguNativeModuleLibraryTests PanguSharedProvider PanguSharedConsumer PanguMissingQuery PanguNullQuery)
set_target_properties(PanguNativeModuleLibraryTests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
pangu_register_gtest(TARGET PanguNativeModuleLibraryTests BOUNDARY integration AREA module TIER precommit)
