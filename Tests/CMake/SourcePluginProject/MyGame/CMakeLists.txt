cmake_minimum_required(VERSION 4.0)

set(PANGU_ENGINE_SOURCE_DIR "" CACHE PATH "PanguEngine source checkout")
if(NOT EXISTS "${PANGU_ENGINE_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "PANGU_ENGINE_SOURCE_DIR must reference the PanguEngine source checkout")
endif()

include("${PANGU_ENGINE_SOURCE_DIR}/cmake/ValidateBuildEnvironment.cmake")
project(MyGame LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(CTest)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/Engine.commit" PANGU_ENGINE_EXPECTED_COMMIT)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Engine.commit")
string(STRIP "${PANGU_ENGINE_EXPECTED_COMMIT}" PANGU_ENGINE_EXPECTED_COMMIT)
option(PANGU_ENGINE_ALLOW_LOCAL_OVERRIDE "Explicitly bypass Engine Git lock for local development" OFF)
include("${PANGU_ENGINE_SOURCE_DIR}/cmake/PanguEngineRevision.cmake")
pangu_validate_engine_revision(
    SOURCE_DIR "${PANGU_ENGINE_SOURCE_DIR}"
    EXPECTED_COMMIT "${PANGU_ENGINE_EXPECTED_COMMIT}"
    ALLOW_LOCAL_OVERRIDE "${PANGU_ENGINE_ALLOW_LOCAL_OVERRIDE}"
)

set(myGameCompiler "${CMAKE_CXX_COMPILER}")
add_subdirectory("${PANGU_ENGINE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/Engine")
if(NOT CMAKE_PROJECT_NAME STREQUAL "MyGame" OR NOT CMAKE_CXX_COMPILER STREQUAL myGameCompiler)
    message(FATAL_ERROR "Engine subdirectory must not change the top-level project or compiler")
endif()
if(NOT TARGET PanguBuildOptions OR NOT TARGET Pangu::Foundation OR NOT TARGET Pangu::Module)
    message(FATAL_ERROR "Engine core Targets were not added to the MyGame build graph")
endif()

set(PANGU_FIXTURE_PLUGIN_CMAKE_DIR "${PANGU_ENGINE_SOURCE_DIR}/cmake" CACHE PATH "Isolated mutable build-tool test wiring")
include("${PANGU_FIXTURE_PLUGIN_CMAKE_DIR}/PanguPlugins.cmake")
set(PANGU_FIXTURE_ENGINE_ROOT "${PANGU_ENGINE_SOURCE_DIR}" CACHE PATH "Isolated Engine plugin fixture root")
set(PANGU_ENABLED_PLUGINS "" CACHE STRING "Enabled source Plugin IDs")
file(WRITE "${CMAKE_BINARY_DIR}/PluginVisits.txt" "")
pangu_add_enabled_plugins(
    ENGINE_ROOT "${PANGU_FIXTURE_ENGINE_ROOT}"
    PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}"
    ENABLED ${PANGU_ENABLED_PLUGINS}
)

add_executable(MyGameProbe Source/Main.cpp)
target_link_libraries(MyGameProbe PRIVATE PanguBuildOptions Pangu::Foundation Pangu::Module)
target_link_libraries(MyGameProbe PRIVATE ${PANGU_FIXTURE_PROBE_DEPENDENCIES})
if(BUILD_TESTING)
    add_test(NAME MyGameProbe COMMAND MyGameProbe)
endif()

option(PANGU_FIXTURE_BUILD_HOSTS "Build the independent catalog consumer fixtures" OFF)
if(PANGU_FIXTURE_BUILD_HOSTS)
    add_executable(MyGame Source/GameMain.cpp Source/GameCatalog.cpp)
    target_link_libraries(MyGame PRIVATE PanguBuildOptions Pangu::Gameplay Pangu::Physics Pangu::Module)
    add_executable(MyEditor Source/EditorMain.cpp Source/EditorCatalog.cpp)
    target_link_libraries(MyEditor PRIVATE PanguBuildOptions Pangu::EditorExtras Pangu::Physics Pangu::Module)
endif()

if(TARGET PanguSharedProbe)
    set(deployDirectory "${CMAKE_BINARY_DIR}/Deploy")
    set(deployFile "${deployDirectory}/${CMAKE_SHARED_LIBRARY_PREFIX}PanguSharedProbe${CMAKE_SHARED_LIBRARY_SUFFIX}")
    file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/SharedDeployManifest.txt"
        CONTENT "$<TARGET_FILE:PanguSharedProbe>\n${deployFile}\n")
    add_custom_command(OUTPUT "${deployFile}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${deployDirectory}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:PanguSharedProbe>" "${deployFile}"
        DEPENDS PanguSharedProbe
        VERBATIM)
    add_custom_target(MyGameDeployShared DEPENDS "${deployFile}")
endif()
