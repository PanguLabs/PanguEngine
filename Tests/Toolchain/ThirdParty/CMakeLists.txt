cmake_minimum_required(VERSION 4.0)

add_library(ThirdPartyCompiledProbe STATIC
    CompiledProbe.cpp
)
add_library(ThirdParty::CompiledProbe ALIAS ThirdPartyCompiledProbe)
target_compile_features(ThirdPartyCompiledProbe PUBLIC cxx_std_23)
# This target intentionally does NOT link PanguBuildOptions or call pangu_configure_module().
set_target_properties(ThirdPartyCompiledProbe PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Probe"
)
