cmake_minimum_required(VERSION 4.0)

pangu_module_library_type(moduleType)
add_library(PanguScopeResourceFixture ${moduleType} ResourceAccessFixture.cpp)
pangu_configure_module(PanguScopeResourceFixture)
target_include_directories(PanguScopeResourceFixture PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(PanguScopeResourceFixture PRIVATE PanguModule PanguScopeTestSupport)
target_compile_definitions(PanguScopeResourceFixture PRIVATE $<$<STREQUAL:${PANGU_LINK_MODE},Modular>:PG_TEST_MODULAR=1>)
pangu_export_module_query(TARGET PanguScopeResourceFixture QUERY pg::module::test::QueryResourceAccessFixture HEADER ResourceAccessFixture.h)

file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/ResourceAccessFixturePath.h" CONTENT
"#pragma once
namespace pg::module::test::resource_access {
inline constexpr auto kFixturePath = R\"($<TARGET_FILE:PanguScopeResourceFixture>)\";
}
")
