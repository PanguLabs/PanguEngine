cmake_minimum_required(VERSION 4.0)

pangu_module_library_type(moduleType)
add_library(
    PanguECS
    ${moduleType}
    Private/EcsModule.cpp
    Private/World.cpp
    Private/WorldAccess.cpp
)
add_library(Pangu::ECS ALIAS PanguECS)
pangu_configure_module(PanguECS EXPORT_MACRO PG_ECS_API)
target_link_libraries(
    PanguECS
    PUBLIC
        Pangu::EngineHost
        ThirdParty::Flecs
)
target_include_directories(PanguECS PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Public")
set_property(
    TARGET
        PanguECS
    PROPERTY
        CXX_SCAN_FOR_MODULES
            OFF
)
pangu_export_module_query(
    TARGET PanguECS
    ID pg::ecs::kEcsModuleId
    ID_HEADER EcsModule.h
    QUERY pg::ecs::QueryEcsModule
    HEADER EcsModule.h
)
