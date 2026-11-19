cmake_minimum_required(VERSION 4.0)
file(APPEND "${CMAKE_BINARY_DIR}/PluginVisits.txt" "mygame.gameplay\n")
add_library(MyGameGameplayHeaders INTERFACE)
target_include_directories(MyGameGameplayHeaders INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/Public")
target_link_libraries(MyGameGameplayHeaders INTERFACE Pangu::EngineHost)
pangu_module_library_type(moduleType)
add_library(
    MyGameGameplay
    ${moduleType}
    Private/Gameplay.cpp
)
pangu_configure_module(MyGameGameplay EXPORT_MACRO PG_GAMEPLAY_API)
target_link_libraries(
    MyGameGameplay
    PUBLIC
        MyGameGameplayHeaders
        FixtureEnginePhysics
)
pangu_export_module_query(
    TARGET MyGameGameplay
    ID pg::fixture::kGameplayId
    ID_HEADER GameplayIdentity.h
    QUERY pg::fixture::QueryGameplay
    HEADER GameplayModule.h
)

if(PANGU_FIXTURE_BUILD_ECS_HOST)
    add_library(MyGameEcsHeaders INTERFACE)
    target_include_directories(MyGameEcsHeaders INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/Public")
    foreach(name IN ITEMS Gameplay Consumer)
        set(target "MyGameEcs${name}")
        add_library(
            ${target}
            ${moduleType}
            "Private/Ecs${name}.cpp"
        )
        pangu_configure_module(${target})
        target_link_libraries(
            ${target}
            PRIVATE
                MyGameEcsHeaders
                Pangu::ECS
        )
        pangu_export_module_query(
            TARGET ${target}
            ID "pg::fixture::ecs::kEcs${name}Id"
            ID_HEADER EcsFixtureTypes.h
            QUERY "pg::fixture::ecs::QueryEcs${name}"
            HEADER EcsFixtureTypes.h
        )
    endforeach()
    target_link_libraries(MyGameEcsConsumer PRIVATE MyGameEcsGameplay)
    if(PANGU_LINK_MODE STREQUAL "Modular")
        target_compile_definitions(MyGameEcsGameplay PRIVATE PANGU_FIXTURE_MODULAR)
    endif()
endif()

pangu_tests_enabled(pluginTestsEnabled)
if(pluginTestsEnabled)
    add_subdirectory(Tests)
endif()
