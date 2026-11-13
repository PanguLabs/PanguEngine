cmake_minimum_required(VERSION 4.0)

set(PANGU_ENGINE_SOURCE_DIR "" CACHE PATH "PanguEngine source checkout")
if(NOT EXISTS "${PANGU_ENGINE_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "PANGU_ENGINE_SOURCE_DIR must reference the PanguEngine source checkout")
endif()

include("${PANGU_ENGINE_SOURCE_DIR}/cmake/ValidateBuildEnvironment.cmake")
project(MyGame LANGUAGES CXX)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

function(CheckMetaHostIsolation)
    if(TARGET LLVMCore OR TARGET clangFrontend OR TARGET PanguMetaFrontend)
        message(FATAL_ERROR "Host Meta SDK targets must not enter the Runtime graph")
    endif()
endfunction()

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
if(PANGU_FIXTURE_PLUGIN_CMAKE_DIR)
    set(PANGU_BUILD_TOOL_SOURCE_DIR "${PANGU_FIXTURE_PLUGIN_CMAKE_DIR}/../BuildTools/PanguBuildTool")
endif()
include("${PANGU_ENGINE_SOURCE_DIR}/cmake/PanguEngine.cmake")
set(PANGU_ENGINE_BUILD_ROOT "${CMAKE_BINARY_DIR}/EngineBuild" CACHE PATH "Isolated fixture Engine artifacts")
pangu_add_engine(SOURCE_DIR "${PANGU_ENGINE_SOURCE_DIR}" ENGINE_BUILD_ROOT "${PANGU_ENGINE_BUILD_ROOT}")
if(NOT CMAKE_PROJECT_NAME STREQUAL "MyGame" OR NOT CMAKE_CXX_COMPILER STREQUAL myGameCompiler)
    message(FATAL_ERROR "Engine subdirectory must not change the top-level project or compiler")
endif()
if(NOT TARGET PanguBuildOptions OR NOT TARGET Pangu::Foundation OR NOT TARGET Pangu::Module)
    message(FATAL_ERROR "Engine core Targets were not added to the MyGame build graph")
endif()

pangu_test_context(OWNER project ENABLED "${BUILD_TESTING}")

option(PANGU_FIXTURE_UNCLASSIFIED_ENGINE_TEST "Inject an Engine test without classification" OFF)
if(PANGU_FIXTURE_UNCLASSIFIED_ENGINE_TEST)
    # 在实际 Engine 子树注入旧式登记，验证漏标不能因归属过滤而被忽略。
    set(unclassifiedCase "${CMAKE_CURRENT_BINARY_DIR}/UnclassifiedEngineTest.cmake")
    file(WRITE "${unclassifiedCase}" "add_test(UnclassifiedEngineProbe \"${CMAKE_COMMAND}\" -E true)\n")
    set_property(DIRECTORY "${PANGU_ENGINE_SOURCE_DIR}" APPEND PROPERTY TEST_INCLUDE_FILES "${unclassifiedCase}")
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
pangu_stage_host_runtime(TARGET MyGameProbe)
pangu_tests_enabled(projectTestsEnabled)
if(projectTestsEnabled)
    pangu_register_test(NAME MyGameProbe COMMAND MyGameProbe
        BOUNDARY integration AREA build TIER precommit BUILD_TARGETS MyGameProbe)
    if("pangu.physics" IN_LIST PANGU_ENABLED_PLUGINS AND "mygame.gameplay" IN_LIST PANGU_ENABLED_PLUGINS)
        include("${PANGU_ENGINE_SOURCE_DIR}/cmake/PanguTestDependencies.cmake")
        pangu_require_googletest(RTTI ON)
        add_executable(MyGamePluginCollaborationTests EXCLUDE_FROM_ALL Tests/PluginCollaborationTests.cpp)
        target_include_directories(MyGamePluginCollaborationTests PRIVATE Source)
        target_link_libraries(MyGamePluginCollaborationTests PRIVATE
            PanguBuildOptions Pangu::Gameplay Pangu::Physics ThirdParty::GoogleTestMain)
        pangu_stage_host_runtime(TARGET MyGamePluginCollaborationTests)
        set(collaborationDiscoveryArgs)
        if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
            # 仅本机共享库消费者显式保留首次启动窗口，不改变测试或外层场景时限。
            list(APPEND collaborationDiscoveryArgs DISCOVERY_TIMEOUT 30)
        endif()
        pangu_register_gtest(TARGET MyGamePluginCollaborationTests BOUNDARY integration AREA build TIER fast
            ${collaborationDiscoveryArgs})
    endif()
endif()

option(PANGU_FIXTURE_BUILD_HOSTS "Build the independent catalog consumer fixtures" OFF)
if(PANGU_FIXTURE_BUILD_HOSTS)
    function(ConfigureConsumer host dependent dependentId)
        target_link_libraries(${host} PRIVATE PanguBuildOptions Pangu::EngineHost)
        target_include_directories(${host} PRIVATE Source)
        if(PANGU_LINK_MODE STREQUAL "Monolithic")
            pangu_meta_host_catalog(TARGET ${host} MODULES Pangu${dependent} PanguPhysics)
            target_link_libraries(${host} PRIVATE Pangu::${dependent} Pangu::Physics)
        else()
            if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
                set_target_properties(${host} PROPERTIES ENABLE_EXPORTS ON)
            endif()
            set(deployDirectory "${CMAKE_BINARY_DIR}/Deploy/${PANGU_LINK_MODE}/${host}")
            set(dependentPath "${deployDirectory}/$<TARGET_FILE_NAME:Pangu${dependent}>")
            set(physicsPath "${deployDirectory}/$<TARGET_FILE_NAME:PanguPhysics>")
            configure_file(Source/ModuleCandidates.h.in "Generated/${host}/ModuleCandidates.h.in" @ONLY)
            file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/Generated/${host}/ModuleCandidates.h"
                INPUT "${CMAKE_CURRENT_BINARY_DIR}/Generated/${host}/ModuleCandidates.h.in")
            target_include_directories(${host} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/Generated/${host}")
            target_compile_definitions(${host} PRIVATE PANGU_FIXTURE_MODULAR)
            set(outputs)
            foreach(module IN ITEMS Physics ${dependent})
                set(output "${deployDirectory}/${CMAKE_SHARED_LIBRARY_PREFIX}Pangu${module}${CMAKE_SHARED_LIBRARY_SUFFIX}")
                add_custom_command(OUTPUT "${output}"
                    COMMAND "${CMAKE_COMMAND}" -E make_directory "${deployDirectory}"
                    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:Pangu${module}>" "${output}"
                    DEPENDS Pangu${module} VERBATIM)
                list(APPEND outputs "${output}")
            endforeach()
            add_custom_target(${host}DeployModules DEPENDS ${outputs})
            add_dependencies(${host} ${host}DeployModules)
            file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/${host}DeployManifest.txt"
                CONTENT "$<TARGET_FILE:Pangu${dependent}>\n${dependentPath}\n$<TARGET_FILE:PanguPhysics>\n${physicsPath}\n")
        endif()
        file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/${host}Consumer.txt"
            CONTENT "mode=${PANGU_LINK_MODE}\nlinks=$<TARGET_PROPERTY:${host},LINK_LIBRARIES>\nfile=$<TARGET_FILE:${host}>\n")
        pangu_stage_host_runtime(TARGET "${host}")
    endfunction()

    if(ANDROID OR CMAKE_SYSTEM_NAME STREQUAL "iOS")
        if(ANDROID)
            add_library(MobileHost SHARED Source/MobileHost.cpp)
            set_target_properties(MobileHost PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
        else()
            add_executable(MobileHost Source/MobileHost.cpp)
        endif()
        ConfigureConsumer(MobileHost Gameplay kGameplayId)
    else()
        add_executable(MyGame Source/GameMain.cpp)
        ConfigureConsumer(MyGame Gameplay kGameplayId)
        add_executable(MyEditor Source/EditorMain.cpp)
        ConfigureConsumer(MyEditor EditorExtras kEditorExtrasId)
    endif()
endif()

if(TARGET PanguSharedProbe)
    get_target_property(sharedProbeType PanguSharedProbe TYPE)
endif()
if(sharedProbeType STREQUAL "SHARED_LIBRARY")
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
cmake_language(DEFER CALL CheckMetaHostIsolation)
