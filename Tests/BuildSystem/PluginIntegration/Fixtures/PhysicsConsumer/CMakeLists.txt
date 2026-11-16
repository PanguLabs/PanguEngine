cmake_minimum_required(VERSION 4.0)

include("${PANGU_ENGINE_SOURCE_DIR}/cmake/ValidateBuildEnvironment.cmake")
project(PhysicsConsumer LANGUAGES CXX)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
include(CTest)

# 独立顶层不提供 MyGame/Source，验证 Engine 插件拥有完整公开接口。
list(PREPEND CMAKE_MODULE_PATH "${PANGU_ENGINE_SOURCE_DIR}/cmake")
find_package(PanguEngine REQUIRED MODULE)
pangu_initialize()
include("${PANGU_ENGINE_SOURCE_DIR}/cmake/PanguPlugins.cmake")
pangu_add_enabled_plugins(ENGINE_ROOT "${PANGU_FIXTURE_ENGINE_ROOT}" ENABLED fixture.physics)
