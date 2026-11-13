cmake_minimum_required(VERSION 4.0)

function(pangu_source_plugin_test name script)
    pangu_register_test(NAME "${name}"
        COMMAND "${CMAKE_COMMAND}" -E env "CMAKE_BUILD_PARALLEL_LEVEL=${PANGU_TEST_BUILD_JOBS}"
            "${CMAKE_COMMAND}"
            "-DPANGU_ENGINE_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
            "-DPANGU_SOURCE_ROOT=${PROJECT_SOURCE_DIR}"
            "-DPANGU_TEST_ROOT=${CMAKE_BINARY_DIR}/Tests/Work/${name}"
            "-DPANGU_TEST_BUILD_JOBS=${PANGU_TEST_BUILD_JOBS}"
            "-DPANGU_ENGINE_ALLOW_LOCAL_OVERRIDE=${PANGU_ENGINE_ALLOW_LOCAL_OVERRIDE}"
            "-DPANGU_META_SDK_CONFIG=${PANGU_META_SDK_CONFIG}"
            "-DPANGU_META_HOST_BUILD_ROOT=${CMAKE_BINARY_DIR}/Tests"
            ${ARGN} -P "${script}"
        BOUNDARY system AREA plugins TIER full TYPES regression
        REQUIRES subprocess nested-build PROCESSORS "${PANGU_TEST_BUILD_JOBS}" TIMEOUT 900)
    set_tests_properties("${name}" PROPERTIES RESOURCE_LOCK PanguMetaHostBuild)
endfunction()

pangu_source_plugin_test(SourcePluginBuildTool "${CMAKE_CURRENT_SOURCE_DIR}/BuildToolConfigureTests.cmake")
foreach(scenario IN ITEMS Invalidation FileLinks DirectoryLinks)
    pangu_source_plugin_test("SourcePluginBuildTool${scenario}"
        "${CMAKE_CURRENT_SOURCE_DIR}/BuildToolInvalidationTests.cmake"
        "-DPANGU_INVALIDATION_SCENARIO=${scenario}")
endforeach()
if(WIN32)
    pangu_source_plugin_test(SourcePluginBuildToolSourceChange "${CMAKE_CURRENT_SOURCE_DIR}/BuildToolSourceChangeTests.cmake")
endif()
set(projectTests "${PROJECT_SOURCE_DIR}/Tests/CMake/SourcePluginProject")
foreach(pair IN ITEMS "EngineRevision|EngineRevisionTests" "Project|RunIntegration" "Enable|PluginEnableTests"
        "StaticCatalog|StaticCatalogIntegrationTests" "SharedDeploy|SharedDeployTests" "Incremental|IncrementalBuildTests"
        "TestOwnership|PluginTestOwnershipTests" "Collaboration|PluginCollaborationTests")
    string(REPLACE "|" ";" pair "${pair}")
    list(GET pair 0 name)
    list(GET pair 1 script)
    set(stageArgs)
    if(name STREQUAL "Project")
        list(APPEND stageArgs -DPANGU_PROJECT_TEST_STAGE=Base)
    elseif(name STREQUAL "TestOwnership" AND CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        list(APPEND stageArgs -DPANGU_OWNERSHIP_STAGE=Base)
    endif()
    pangu_source_plugin_test("SourcePlugin${name}" "${projectTests}/${script}.cmake" ${stageArgs})
endforeach()
pangu_source_plugin_test(SourcePluginEmbeddedEngine "${projectTests}/RunIntegration.cmake"
    -DPANGU_PROJECT_TEST_STAGE=Engine)
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    foreach(stage IN ITEMS Project Engine Provider)
        pangu_source_plugin_test("SourcePluginTestOwnership${stage}"
            "${projectTests}/PluginTestOwnershipTests.cmake" "-DPANGU_OWNERSHIP_STAGE=${stage}")
    endforeach()
    # 完整冷场景实测1183.60秒；仅构建准备留约26%余量，不改变业务或发现期限。
    set_tests_properties(SourcePluginTestOwnershipEngine PROPERTIES TIMEOUT 1500)
endif()
