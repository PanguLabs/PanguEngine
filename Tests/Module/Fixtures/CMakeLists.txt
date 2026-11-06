cmake_minimum_required(VERSION 4.0)

# 每个夹具 Module 对应一个真实静态库。
foreach(_module IN ITEMS First Second)
    add_library(PanguCatalog${_module} STATIC ${_module}.cpp)
    pangu_configure_module(PanguCatalog${_module})
    target_link_libraries(PanguCatalog${_module} PRIVATE PanguModule)
    # 保持改造前的扫描策略：只对 Catalog 夹具关闭，不做全局关闭；A/B 验收要求扫描设置一致。
    set_property(TARGET PanguCatalog${_module} PROPERTY CXX_SCAN_FOR_MODULES OFF)
endforeach()
target_link_libraries(PanguCatalogFirst PRIVATE PanguCatalogSecond)

# Catalog 装配属于最终可执行程序，通过根引用保留两个独立归档的入口。
configure_file(StaticCatalog.cpp.in "${CMAKE_CURRENT_BINARY_DIR}/StaticCatalog.cpp" @ONLY)
add_executable(PanguStaticCatalogTests
    ../StaticCatalogTests.cpp
    "${CMAKE_CURRENT_BINARY_DIR}/StaticCatalog.cpp")
target_include_directories(PanguStaticCatalogTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(PanguStaticCatalogTests PRIVATE
    PanguBuildOptions PanguModule PanguCatalogFirst PanguCatalogSecond ThirdParty::GoogleTestMain)
target_compile_options(PanguStaticCatalogTests PRIVATE -ffunction-sections -fdata-sections)
if(WIN32)
    target_link_options(PanguStaticCatalogTests PRIVATE "LINKER:/OPT:REF")
elseif(APPLE)
    target_link_options(PanguStaticCatalogTests PRIVATE "LINKER:-dead_strip")
else()
    target_link_options(PanguStaticCatalogTests PRIVATE "LINKER:--gc-sections")
endif()
set_target_properties(PanguStaticCatalogTests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
pangu_register_gtest(TARGET PanguStaticCatalogTests BOUNDARY integration AREA module TIER precommit)
