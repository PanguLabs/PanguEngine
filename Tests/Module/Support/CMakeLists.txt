cmake_minimum_required(VERSION 4.0)

if(PANGU_LINK_MODE STREQUAL "Modular")
    set(supportType SHARED)
else()
    set(supportType STATIC)
endif()
add_library(PanguScopeTestSupport ${supportType} ScopeTestSupport.cpp)
target_link_libraries(PanguScopeTestSupport PUBLIC PanguBuildOptions PanguModule)
target_include_directories(PanguScopeTestSupport PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
include(GenerateExportHeader)
generate_export_header(PanguScopeTestSupport EXPORT_MACRO_NAME PG_SCOPE_TEST_SUPPORT_API
    EXPORT_FILE_NAME "${CMAKE_CURRENT_BINARY_DIR}/PanguScopeTestSupportExport.h")
target_include_directories(PanguScopeTestSupport PUBLIC "${CMAKE_CURRENT_BINARY_DIR}")
if(supportType STREQUAL "STATIC")
    target_compile_definitions(PanguScopeTestSupport PUBLIC PANGUSCOPETESTSUPPORT_STATIC_DEFINE)
endif()
set_target_properties(PanguScopeTestSupport PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Bin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Bin"
    CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)
