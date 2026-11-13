cmake_minimum_required(VERSION 4.0)

pangu_module_library_type(moduleType)
add_library(PanguFoundation ${moduleType}
    Private/FoundationModule.cpp
    Private/Error.cpp
    Private/FailureReport.cpp
)

add_library(Pangu::Foundation ALIAS PanguFoundation)
pangu_configure_module(PanguFoundation EXPORT_MACRO PG_FOUNDATION_API)
# 保持改造前的扫描策略：只对本目标关闭，不做全局关闭；A/B 验收要求扫描设置一致。
set_property(TARGET PanguFoundation PROPERTY CXX_SCAN_FOR_MODULES OFF)

target_include_directories(PanguFoundation PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/Public"
)

pangu_export_module_query(TARGET PanguFoundation ID pg::module::kFoundationModuleId ID_HEADER FoundationModule.h QUERY pg::module::QueryFoundationModule HEADER FoundationModule.h)

if(WIN32)
    set(panguEmergencySource Private/Platform/Windows/Emergency.cpp)
else()
    set(panguEmergencySource Private/Platform/Posix/Emergency.cpp)
endif()
target_sources(PanguFoundation PRIVATE Private/Fatal.cpp "${panguEmergencySource}")
target_include_directories(PanguFoundation PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Private")
target_compile_definitions(PanguFoundation PUBLIC
    "PG_ENABLE_ASSERTS=$<BOOL:${PANGU_ENABLE_ASSERTS}>"
    "PG_LOG_COMPILED_MIN_LEVEL=${PANGU_LOG_COMPILED_MIN_LEVEL}")
