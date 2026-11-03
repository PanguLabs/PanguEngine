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

# Assembly belongs to the final executable; no second Module library or whole-archive.
configure_file(StaticCatalog.cpp.in "${CMAKE_CURRENT_BINARY_DIR}/StaticCatalog.cpp" @ONLY)
add_executable(PanguStaticCatalogTests
    ../StaticCatalogTests.cpp
    "${CMAKE_CURRENT_BINARY_DIR}/StaticCatalog.cpp")
target_include_directories(PanguStaticCatalogTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(PanguStaticCatalogTests PRIVATE
    PanguBuildOptions PanguModule PanguCatalogFirst PanguCatalogSecond)
target_compile_options(PanguStaticCatalogTests PRIVATE -ffunction-sections -fdata-sections)
if(WIN32)
    target_link_options(PanguStaticCatalogTests PRIVATE "LINKER:/OPT:REF")
elseif(APPLE)
    target_link_options(PanguStaticCatalogTests PRIVATE "LINKER:-dead_strip")
else()
    target_link_options(PanguStaticCatalogTests PRIVATE "LINKER:--gc-sections")
endif()
set_target_properties(PanguStaticCatalogTests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
add_test(NAME StaticModuleCatalog COMMAND PanguStaticCatalogTests)
