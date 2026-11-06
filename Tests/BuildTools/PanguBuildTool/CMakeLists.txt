cmake_minimum_required(VERSION 4.0)

pangu_require_googletest(RTTI ON)
if(TARGET PanguBuildOptions)
    message(FATAL_ERROR "BuildTools tests must not import the Runtime PanguBuildOptions policy")
endif()
add_executable(PanguPluginManifestTests PluginManifestTests.cpp TestWorkspaceTests.cpp)
target_link_libraries(PanguPluginManifestTests PRIVATE PanguPluginManifest ThirdParty::GoogleTestMain)
add_executable(PanguPluginDiscoveryTests PluginDiscoveryTests.cpp)
target_link_libraries(PanguPluginDiscoveryTests PRIVATE PanguPluginDiscovery ThirdParty::GoogleTestMain)
foreach(target IN ITEMS PanguPluginManifestTests PanguPluginDiscoveryTests)
    target_include_directories("${target}" PRIVATE "${PANGU_ENGINE_ROOT}/Tests/Support")
    target_compile_options("${target}" PRIVATE -fno-exceptions)
endforeach()
pangu_register_gtest(TARGET PanguPluginManifestTests BOUNDARY unit AREA build TIER fast)
pangu_register_gtest(TARGET PanguPluginDiscoveryTests BOUNDARY integration AREA build TIER precommit)
pangu_register_test(NAME PluginCli COMMAND "${CMAKE_COMMAND}"
    "-DPANGU_TOOL_EXE=$<TARGET_FILE:PanguBuildTool>"
    "-DPANGU_TEST_ROOT=${CMAKE_BINARY_DIR}/Tests/Work/PluginCli/路径 空格"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/PluginCliTests.cmake"
    BOUNDARY system AREA build TIER precommit BUILD_TARGETS PanguBuildTool REQUIRES subprocess)
