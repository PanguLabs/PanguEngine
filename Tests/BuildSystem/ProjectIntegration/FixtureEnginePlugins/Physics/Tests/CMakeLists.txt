cmake_minimum_required(VERSION 4.0)
include("${PANGU_ENGINE_SOURCE_DIR}/cmake/PanguTestDependencies.cmake")
pangu_require_googletest(RTTI ON)
add_executable(PanguPhysicsPluginTests PluginBehaviorTests.cpp)
# 独立部署目录避免与不消费 Physics 的宿主合并 DLL 依赖。
set_target_properties(PanguPhysicsPluginTests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(PanguPhysicsPluginTests PRIVATE PanguBuildOptions Pangu::Physics ThirdParty::GoogleTestMain)
pangu_stage_host_runtime(TARGET PanguPhysicsPluginTests)
pangu_register_gtest(TARGET PanguPhysicsPluginTests BOUNDARY unit AREA build TIER fast)
