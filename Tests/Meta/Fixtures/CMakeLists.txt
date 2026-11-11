cmake_minimum_required(VERSION 4.0)

pangu_module_library_type(moduleType)
add_library(PanguManualMetaFixture ${moduleType} ManualModule.cpp)
pangu_configure_module(PanguManualMetaFixture EXPORT_MACRO PG_MANUAL_META_API)
target_link_libraries(PanguManualMetaFixture PUBLIC PanguModule)
target_include_directories(PanguManualMetaFixture PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}")
pangu_export_module_query(TARGET PanguManualMetaFixture ID pg::test::kManualModuleId ID_HEADER ManualModule.h QUERY pg::test::QueryManualModule HEADER ManualModule.h)
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/ManualModulePath.h" CONTENT
"#pragma once
namespace pg::test {
inline constexpr auto kManualModulePath = R\"($<TARGET_FILE:PanguManualMetaFixture>)\";
}
")

foreach(target IN ITEMS PanguMetaFixture PanguMetaServiceFixture)
    if(target STREQUAL "PanguMetaFixture")
        set(source MetaModule.cpp)
        set(header MetaActor.h)
        set(identity pg::test::meta_fixture::kModuleId)
        set(query pg::test::meta_fixture::QueryMetaModule)
    else()
        set(source MetaService.cpp)
        set(header MetaService.h)
        set(identity pg::test::meta_fixture::kServiceModuleId)
        set(query pg::test::meta_fixture::QueryMetaService)
    endif()
    add_library(${target} ${moduleType} ${source})
    pangu_configure_module(${target} EXPORT_MACRO PG_META_FIXTURE_API)
    target_link_libraries(${target} PRIVATE PanguEngineHost PanguHostTestSupport)
    target_include_directories(${target} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_definitions(${target} PRIVATE $<$<STREQUAL:${PANGU_LINK_MODE},Modular>:PG_TEST_MODULAR=1>)
    pangu_meta_module(TARGET ${target} ID ${identity} ID_HEADER MetaModule.h QUERY ${query} HEADER MetaModule.h)
    pangu_meta_headers(TARGET ${target} ROOT "${CMAKE_CURRENT_SOURCE_DIR}" VISIBILITY public EMIT both HEADERS ${header})
endforeach()
pangu_meta_consume(TARGET PanguMetaServiceFixture MODULE PanguMetaFixture HEADERS MetaActor.h EMIT typed_access)
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/MetaModulePaths.h" CONTENT
"#pragma once
namespace pg::test::meta_fixture
{
inline constexpr auto kModulePath = R\"($<TARGET_FILE:PanguMetaFixture>)\";
inline constexpr auto kServiceModulePath = R\"($<TARGET_FILE:PanguMetaServiceFixture>)\";
} // namespace pg::test::meta_fixture
")
