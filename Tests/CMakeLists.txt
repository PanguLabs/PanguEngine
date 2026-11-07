cmake_minimum_required(VERSION 4.0)

include("${PROJECT_SOURCE_DIR}/cmake/PanguTestBuildJobs.cmake")

# 交叉编译也保留仅编译的 Toolchain 验证。
add_subdirectory(Toolchain)

if(CMAKE_CROSSCOMPILING)
    message(STATUS "Pangu CTest executables skipped for cross-compiled targets.")
    return()
endif()

pangu_register_test(NAME BuildEnvironment
    COMMAND "${CMAKE_COMMAND}" "-DPANGU_SOURCE_ROOT=${PROJECT_SOURCE_DIR}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/CMake/EnvironmentTests.cmake"
    BOUNDARY system AREA toolchain TIER precommit TYPES regression REQUIRES subprocess
)
add_subdirectory(ThirdParty/NlohmannJson)
add_subdirectory(Foundation)
add_subdirectory(Module)
add_subdirectory(EngineHost)
add_subdirectory(CMake/SourcePlugins)
add_subdirectory(CMake/Testing)
add_subdirectory(CMake/ModuleLinkModes)

include("${PROJECT_SOURCE_DIR}/cmake/PanguBuildToolsTesting.cmake")
pangu_add_build_tools_tests(TOOL_NAME PanguBuildTool
    SOURCE_DIR "${PROJECT_SOURCE_DIR}/BuildTools/PanguBuildTool"
    GROUP PluginManifest BOUNDARY unit TIER fast
    BUILD_TARGETS PanguPluginManifestTests TEST_REGEX "^PanguPluginManifestTests[.]")
pangu_add_build_tools_tests(TOOL_NAME PanguBuildTool
    SOURCE_DIR "${PROJECT_SOURCE_DIR}/BuildTools/PanguBuildTool"
    GROUP PluginDiscovery BOUNDARY integration TIER precommit
    BUILD_TARGETS PanguPluginDiscoveryTests TEST_REGEX "^PanguPluginDiscoveryTests[.]")
pangu_add_build_tools_tests(TOOL_NAME PanguBuildTool
    SOURCE_DIR "${PROJECT_SOURCE_DIR}/BuildTools/PanguBuildTool"
    GROUP PluginCli BOUNDARY system TIER precommit
    BUILD_TARGETS PanguBuildTool TEST_REGEX "^PluginCli$")
