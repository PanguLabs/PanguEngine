cmake_minimum_required(VERSION 4.0)

pangu_module_library_type(moduleType)
add_library(PanguModule ${moduleType}
    Private/ModuleBuiltin.cpp
    Private/ModuleCodePin.cpp
    Private/ModuleMetaBorrow.cpp
    Private/ModuleMetadataAccess.cpp
    Private/NativeModuleLibrary.cpp
    Private/ModuleIdentity.cpp
    Private/RegistrationContext.cpp
    Private/ScopeAccess.cpp
    Private/ScopeResourceAccess.cpp
    Private/ScopeBuildContext.cpp
    Private/ScopeRuntime.cpp
    Private/ModuleManager.cpp
    Private/ModuleCatalog.cpp
)
if(WIN32)
    target_sources(PanguModule PRIVATE Private/Platform/Windows/NativeModuleLibrary.cpp)
else()
    target_sources(PanguModule PRIVATE Private/Platform/Posix/NativeModuleLibrary.cpp)
    target_link_libraries(PanguModule PRIVATE ${CMAKE_DL_LIBS})
endif()
target_include_directories(PanguModule PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Private")

add_library(Pangu::Module ALIAS PanguModule)
pangu_configure_module(PanguModule EXPORT_MACRO PG_MODULE_API)
target_link_libraries(PanguModule PUBLIC PanguFoundation)
# 保持改造前的扫描策略：只对本目标关闭，不做全局关闭；A/B 验收要求扫描设置一致。
set_property(TARGET PanguModule PROPERTY CXX_SCAN_FOR_MODULES OFF)

target_include_directories(PanguModule PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/Public"
)

pangu_export_module_query(TARGET PanguModule ID pg::module::kModuleRuntimeModuleId ID_HEADER ModuleBuiltin.h QUERY pg::module::QueryModuleRuntime HEADER ModuleBuiltin.h)
target_compile_definitions(PanguModule PRIVATE $<$<STREQUAL:${PANGU_LINK_MODE},Modular>:PG_MODULE_MODULAR=1>)
