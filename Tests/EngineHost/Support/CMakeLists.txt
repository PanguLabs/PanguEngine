cmake_minimum_required(VERSION 4.0)

add_library(PanguHostTestSupport SHARED HostTestState.cpp)
target_link_libraries(PanguHostTestSupport PUBLIC PanguBuildOptions PanguModule)
target_include_directories(PanguHostTestSupport PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
include(GenerateExportHeader)
generate_export_header(PanguHostTestSupport EXPORT_MACRO_NAME PG_HOST_TEST_SUPPORT_API
    EXPORT_FILE_NAME "${CMAKE_CURRENT_BINARY_DIR}/PanguHostTestSupportExport.h")
target_include_directories(PanguHostTestSupport PUBLIC "${CMAKE_CURRENT_BINARY_DIR}")
set_target_properties(PanguHostTestSupport PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Bin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Bin"
    CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)
