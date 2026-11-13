cmake_minimum_required(VERSION 4.0)

pangu_module_library_type(moduleType)
add_library(PanguLogProducer ${moduleType} LogProducer.cpp)
pangu_configure_module(PanguLogProducer)
target_include_directories(PanguLogProducer PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(PanguLogProducer PRIVATE Pangu::EngineHost)
target_compile_definitions(PanguLogProducer PRIVATE $<$<STREQUAL:${PANGU_LINK_MODE},Modular>:PG_TEST_MODULAR=1>)
target_compile_definitions(PanguLogProducer PRIVATE PG_LOG_NATIVE_MARKER="${CMAKE_CURRENT_BINARY_DIR}/../Lifetime/native-unloaded")
pangu_export_module_query(TARGET PanguLogProducer ID pg::log::test::kProducerId ID_HEADER LogProducer.h
    QUERY pg::log::test::QueryLogProducer HEADER LogProducer.h)
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/LogProducerPath.h" CONTENT
"#pragma once
namespace pg::log::test {
inline constexpr auto kProducerPath = R\"($<TARGET_FILE:PanguLogProducer>)\";
}
")
