cmake_minimum_required(VERSION 4.0)

add_library(PanguModule STATIC
    Private/ModuleIdentity.cpp
    Private/RegistrationContext.cpp
    Private/ScopeBuildContext.cpp
    Private/ScopeRuntime.cpp
    Private/StaticModuleHost.cpp
)

add_library(Pangu::Module ALIAS PanguModule)
pangu_configure_module(PanguModule)
target_link_libraries(PanguModule PUBLIC PanguFoundation)
# 保持改造前的扫描策略：只对本目标关闭，不做全局关闭；A/B 验收要求扫描设置一致。
set_property(TARGET PanguModule PROPERTY CXX_SCAN_FOR_MODULES OFF)

target_include_directories(PanguModule PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/Public"
)
