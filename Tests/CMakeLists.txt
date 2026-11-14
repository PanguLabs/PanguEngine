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
pangu_register_test(NAME LocalBuildScripts
    COMMAND "${CMAKE_COMMAND}" "-DPANGU_SOURCE_ROOT=${PROJECT_SOURCE_DIR}"
        "-DPANGU_TEST_ROOT=${CMAKE_BINARY_DIR}/Tests/LocalBuildScripts"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/Scripts/BuildScriptsTests.cmake"
    BOUNDARY system AREA toolchain TIER precommit TYPES regression REQUIRES subprocess
)
add_subdirectory(ThirdParty/NlohmannJson)
add_subdirectory(Foundation)
add_subdirectory(Log)
add_subdirectory(Meta/Fixtures)
add_subdirectory(Module)
add_subdirectory(EngineHost)
add_subdirectory(Meta)
add_subdirectory(CMake/SourcePlugins)
add_subdirectory(CMake/Testing)
add_subdirectory(CMake/ModuleLinkModes)
add_subdirectory(CMake/PanguMeta)

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

pangu_add_build_tools_tests(TOOL_NAME PanguMetaTool
    SOURCE_DIR "${PROJECT_SOURCE_DIR}/BuildTools/PanguMetaTool"
    GROUP MetaClang BOUNDARY integration TIER precommit
    BUILD_TARGETS PanguMetaTool PanguMetaClangTests PanguMetaDependencySmoke TEST_REGEX "^MetaClang[.]")
foreach(group IN ITEMS Model Request Context Ownership Attr CodeGen Publish Cli)
    set(target "PanguMeta${group}Tests")
    set(expression "^Meta${group}[.]")
    set(rootGroup "Meta${group}")
    set(boundary integration)
    if(group STREQUAL "Model")
        set(expression "^MetaIR[.]")
        set(rootGroup MetaIR)
        set(boundary unit)
    elseif(group STREQUAL "Ownership")
        set(target PanguMetaContextTests)
        set(boundary unit)
    elseif(group STREQUAL "Cli")
        set(target PanguMetaTool)
    elseif(group STREQUAL "Request" OR group STREQUAL "CodeGen")
        set(boundary unit)
    endif()
    pangu_add_build_tools_tests(TOOL_NAME PanguMetaTool
        SOURCE_DIR "${PROJECT_SOURCE_DIR}/BuildTools/PanguMetaTool"
        GROUP "${rootGroup}" BOUNDARY "${boundary}" TIER precommit
        BUILD_TARGETS "${target}" TEST_REGEX "${expression}")
endforeach()
add_subdirectory(CMake/LogConfig)
