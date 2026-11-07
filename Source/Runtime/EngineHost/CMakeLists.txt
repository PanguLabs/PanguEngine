cmake_minimum_required(VERSION 4.0)

pangu_module_library_type(moduleType)
add_library(PanguEngineHost ${moduleType}
    Private/EngineHost.cpp
    Private/HostServiceAccess.cpp
    Private/EngineHostModule.cpp
)
add_library(Pangu::EngineHost ALIAS PanguEngineHost)
pangu_configure_module(PanguEngineHost EXPORT_MACRO PG_ENGINE_HOST_API)
target_link_libraries(PanguEngineHost PUBLIC PanguModule)
target_include_directories(PanguEngineHost PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Public")
target_include_directories(PanguEngineHost PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Private")
set_property(TARGET PanguEngineHost PROPERTY CXX_SCAN_FOR_MODULES OFF)
pangu_export_module_query(TARGET PanguEngineHost QUERY pg::engine::QueryEngineHostModule HEADER EngineHostModule.h)
