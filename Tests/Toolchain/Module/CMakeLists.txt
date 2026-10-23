cmake_minimum_required(VERSION 4.0)

add_library(PanguToolchainProbe STATIC
    PolicyProbe.cpp
)
add_library(Pangu::ToolchainProbe ALIAS PanguToolchainProbe)
pangu_configure_module(PanguToolchainProbe)
set_target_properties(PanguToolchainProbe PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Probe"
)
