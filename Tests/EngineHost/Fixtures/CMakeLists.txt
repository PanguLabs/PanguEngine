cmake_minimum_required(VERSION 4.0)

pangu_module_library_type(moduleType)
add_library(PanguHostServiceFixture ${moduleType} HostServiceModule.cpp)
pangu_configure_module(PanguHostServiceFixture)
target_include_directories(PanguHostServiceFixture PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(PanguHostServiceFixture PRIVATE PanguEngineHost PanguHostTestSupport)
target_compile_definitions(PanguHostServiceFixture PRIVATE $<$<STREQUAL:${PANGU_LINK_MODE},Modular>:PG_TEST_MODULAR=1>)
pangu_export_module_query(TARGET PanguHostServiceFixture ID pg::engine::test::kModuleId ID_HEADER HostServiceModule.h QUERY pg::engine::test::QueryHostServiceFixture HEADER HostServiceModule.h)
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/HostServiceFixturePath.h" CONTENT
"#pragma once
namespace pg::engine::test {
inline constexpr auto kHostServiceFixturePath = R\"($<TARGET_FILE:PanguHostServiceFixture>)\";
}
")

add_library(PanguHostConsumerFixture ${moduleType} HostConsumerModule.cpp)
pangu_configure_module(PanguHostConsumerFixture)
target_include_directories(PanguHostConsumerFixture PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(PanguHostConsumerFixture PRIVATE PanguEngineHost PanguHostTestSupport)
target_compile_definitions(PanguHostConsumerFixture PRIVATE $<$<STREQUAL:${PANGU_LINK_MODE},Modular>:PG_TEST_MODULAR=1>)
pangu_export_module_query(TARGET PanguHostConsumerFixture ID pg::engine::test::consumer::kModuleId ID_HEADER HostConsumerModule.h QUERY pg::engine::test::QueryHostConsumerFixture HEADER HostConsumerModule.h)
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/HostConsumerFixturePath.h" CONTENT
"#pragma once
namespace pg::engine::test {
inline constexpr auto kHostConsumerFixturePath = R\"($<TARGET_FILE:PanguHostConsumerFixture>)\";
}
")
