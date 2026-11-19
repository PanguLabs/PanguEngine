cmake_minimum_required(VERSION 4.0)

function(_pangu_register_source_plugin_test name script)
    pangu_register_test(
        NAME "${name}"
        COMMAND
            "${CMAKE_COMMAND}"
            -E
            env
            "CMAKE_BUILD_PARALLEL_LEVEL=${PANGU_TEST_BUILD_JOBS}"
            "${CMAKE_COMMAND}"
            "-DPANGU_ENGINE_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
            "-DPANGU_SOURCE_ROOT=${PROJECT_SOURCE_DIR}"
            "-DPANGU_TEST_ROOT=${CMAKE_BINARY_DIR}/Tests/Work/${name}"
            "-DPANGU_TEST_BUILD_JOBS=${PANGU_TEST_BUILD_JOBS}"
            "-DPANGU_ENGINE_ALLOW_LOCAL_OVERRIDE=${PANGU_ENGINE_ALLOW_LOCAL_OVERRIDE}"
            "-DPANGU_META_HOST_BUILD_ROOT=${CMAKE_BINARY_DIR}/Tests"
            ${ARGN}
            -P
            "${script}"
        BOUNDARY system
        AREA plugins
        TIER full
        TYPES
            regression
        REQUIRES
            subprocess
            nested-build
        PROCESSORS "${PANGU_TEST_BUILD_JOBS}"
        TIMEOUT 900
    )
    set_tests_properties(
        "${name}"
        PROPERTIES
            RESOURCE_LOCK
                PanguMetaHostBuild
    )
endfunction()

_pangu_register_source_plugin_test(
    PluginIntegration.BuildToolConfigure
    "${CMAKE_CURRENT_SOURCE_DIR}/BuildToolConfigureTests.cmake"
)
foreach(scenario IN ITEMS FileLinks DirectoryLinks)
    _pangu_register_source_plugin_test(
        "PluginIntegration.Discovery${scenario}"
        "${CMAKE_CURRENT_SOURCE_DIR}/PluginDiscoveryInvalidationTests.cmake"
        "-DPANGU_INVALIDATION_SCENARIO=${scenario}"
    )
endforeach()
set(projectTests "${PROJECT_SOURCE_DIR}/Tests/BuildSystem/ProjectIntegration")
_pangu_register_source_plugin_test(ProjectIntegration.FindPackage "${projectTests}/FindPackageTests.cmake")
foreach(
    pair
    IN
    ITEMS
        "EngineRevision|EngineRevisionTests"
        "Base|ProjectIntegrationTests"
        "PluginEnable|PluginEnableTests"
        "HostModuleConsumption|HostModuleConsumptionTests"
        "SharedDeploy|SharedDeployTests"
        "IncrementalBuild|IncrementalBuildTests"
        "PluginTestOwnership|PluginTestOwnershipTests"
        "PluginCollaboration|PluginCollaborationTests"
)
    string(
        REPLACE "|"
        ";"
        pair
        "${pair}"
    )
    list(GET pair 0 name)
    list(GET pair 1 script)
    set(stageArgs)
    if(name STREQUAL "Base")
        list(APPEND stageArgs -DPANGU_PROJECT_TEST_STAGE=Base)
    elseif(name STREQUAL "PluginTestOwnership" AND CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        list(APPEND stageArgs -DPANGU_OWNERSHIP_STAGE=Base)
    endif()
    _pangu_register_source_plugin_test("ProjectIntegration.${name}" "${projectTests}/${script}.cmake" ${stageArgs})
endforeach()
_pangu_register_source_plugin_test(
    ProjectIntegration.EcsWorld
    "${projectTests}/EcsWorldTests.cmake"
    "-DPANGU_LINK_MODE=${PANGU_LINK_MODE}"
)
_pangu_register_source_plugin_test(
    ProjectIntegration.EmbeddedEngine
    "${projectTests}/ProjectIntegrationTests.cmake"
    -DPANGU_PROJECT_TEST_STAGE=Engine
)
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    # 全量冷构建及三轮分类检查实测903.98秒；仅为准备过程留余量，业务与发现期限不变。
    set_tests_properties(
        ProjectIntegration.EmbeddedEngine
        PROPERTIES
            TIMEOUT
                1200
    )
    foreach(stage IN ITEMS Project Engine Provider)
        _pangu_register_source_plugin_test(
            "ProjectIntegration.PluginTestOwnership.${stage}"
            "${projectTests}/PluginTestOwnershipTests.cmake"
            "-DPANGU_OWNERSHIP_STAGE=${stage}"
        )
    endforeach()
    # 完整冷场景实测1183.60秒；仅构建准备留约26%余量，不改变业务或发现期限。
    set_tests_properties(
        ProjectIntegration.PluginTestOwnership.Engine
        PROPERTIES
            TIMEOUT
                1500
    )
endif()
