cmake_minimum_required(VERSION 4.0)
project(PanguSharedModuleFixture LANGUAGES CXX)
include(CTest)
set(PANGU_LINK_MODE Modular CACHE STRING "Pangu Module linkage" FORCE)
set(PANGU_BUILD_ENGINE_TESTS ON CACHE BOOL "Build engine tests" FORCE)
# 独立入口与 Engine 内部 Shared 目标分目录，避免同一源码目录形成目录属性遍历环。
list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/../../../../cmake")
find_package(PanguEngine REQUIRED MODULE)
pangu_initialize()
