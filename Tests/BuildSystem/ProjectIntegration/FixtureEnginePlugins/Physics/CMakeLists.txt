cmake_minimum_required(VERSION 4.0)
file(APPEND "${CMAKE_BINARY_DIR}/PluginVisits.txt" "fixture.physics\n")
add_library(FixtureEnginePhysicsHeaders INTERFACE)
target_include_directories(FixtureEnginePhysicsHeaders INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/Public")
target_link_libraries(FixtureEnginePhysicsHeaders INTERFACE Pangu::Module)
pangu_module_library_type(moduleType)
add_library(
    FixtureEnginePhysics
    ${moduleType}
    Private/Physics.cpp
)
pangu_configure_module(FixtureEnginePhysics EXPORT_MACRO PG_PHYSICS_API)
target_link_libraries(FixtureEnginePhysics PUBLIC FixtureEnginePhysicsHeaders)
pangu_export_module_query(
    TARGET FixtureEnginePhysics
    ID pg::fixture::kPhysicsId
    ID_HEADER PhysicsIdentity.h
    QUERY pg::fixture::QueryPhysics
    HEADER PhysicsModule.h
)
pangu_tests_enabled(pluginTestsEnabled)
if(pluginTestsEnabled)
    add_subdirectory(Tests)
endif()
