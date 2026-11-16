cmake_minimum_required(VERSION 4.0)

pangu_module_library_type(moduleType)
add_library(PanguToolchainProbe ${moduleType}
    PolicyProbe.cpp
)
pangu_configure_module(PanguToolchainProbe EXPORT_MACRO PG_TOOLCHAIN_PROBE_API)
set_target_properties(PanguToolchainProbe PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Tests/Toolchain"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Tests/Toolchain"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Tests/Toolchain"
)
