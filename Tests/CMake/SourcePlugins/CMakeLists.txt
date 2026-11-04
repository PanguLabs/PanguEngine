cmake_minimum_required(VERSION 4.0)

add_test(NAME SourcePluginBuildTool
    COMMAND "${CMAKE_COMMAND}"
        "-DPANGU_ENGINE_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
        "-DPANGU_TEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/BuildToolConfigure"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/BuildToolConfigureTests.cmake"
)

add_test(NAME SourcePluginBuildToolInvalidation
    COMMAND "${CMAKE_COMMAND}"
        "-DPANGU_ENGINE_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
        "-DPANGU_TEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/BuildToolInvalidation"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/BuildToolInvalidationTests.cmake"
)

add_test(NAME SourcePluginEngineRevision
    COMMAND "${CMAKE_COMMAND}"
        "-DPANGU_SOURCE_ROOT=${PROJECT_SOURCE_DIR}"
        "-DPANGU_TEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/EngineRevision"
        -P "${PROJECT_SOURCE_DIR}/Tests/CMake/SourcePluginProject/EngineRevisionTests.cmake"
)

add_test(NAME SourcePluginProject
    COMMAND "${CMAKE_COMMAND}"
        "-DPANGU_ENGINE_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
        "-DPANGU_TEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/SourcePluginProject"
        -P "${PROJECT_SOURCE_DIR}/Tests/CMake/SourcePluginProject/RunIntegration.cmake"
)

if(WIN32)
    add_test(NAME SourcePluginBuildToolSourceChange
        COMMAND "${CMAKE_COMMAND}"
            "-DPANGU_ENGINE_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
            "-DPANGU_TEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/BuildToolSourceChange"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/BuildToolSourceChangeTests.cmake"
    )
endif()

add_test(NAME SourcePluginEnable
    COMMAND "${CMAKE_COMMAND}"
        "-DPANGU_ENGINE_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
        "-DPANGU_TEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/PluginEnable"
        -P "${PROJECT_SOURCE_DIR}/Tests/CMake/SourcePluginProject/PluginEnableTests.cmake"
)

add_test(NAME SourcePluginStaticCatalog
    COMMAND "${CMAKE_COMMAND}"
        "-DPANGU_ENGINE_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
        "-DPANGU_TEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/StaticCatalog"
        -P "${PROJECT_SOURCE_DIR}/Tests/CMake/SourcePluginProject/StaticCatalogIntegrationTests.cmake"
)

add_test(NAME SourcePluginSharedDeploy
    COMMAND "${CMAKE_COMMAND}"
        "-DPANGU_ENGINE_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
        "-DPANGU_TEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/SharedDeploy"
        -P "${PROJECT_SOURCE_DIR}/Tests/CMake/SourcePluginProject/SharedDeployTests.cmake"
)

add_test(NAME SourcePluginIncremental
    COMMAND "${CMAKE_COMMAND}"
        "-DPANGU_ENGINE_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
        "-DPANGU_TEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/Incremental"
        -P "${PROJECT_SOURCE_DIR}/Tests/CMake/SourcePluginProject/IncrementalBuildTests.cmake"
)
