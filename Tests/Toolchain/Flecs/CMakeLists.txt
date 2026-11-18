cmake_minimum_required(VERSION 4.0)

if(NOT TARGET ThirdParty::Flecs)
    message(FATAL_ERROR "FlecsCompileProbe requires ThirdParty::Flecs")
endif()

if(PANGU_LINK_MODE STREQUAL "Monolithic")
    set(expectedType STATIC_LIBRARY)
else()
    set(expectedType SHARED_LIBRARY)
endif()
get_target_property(actualType ThirdParty::Flecs TYPE)
if(NOT actualType STREQUAL expectedType)
    message(FATAL_ERROR "ThirdParty::Flecs is ${actualType}; ${PANGU_LINK_MODE} requires ${expectedType}")
endif()

if(CMAKE_CROSSCOMPILING)
    add_library(FlecsCompileProbe STATIC CompileProbe.cpp)
else()
    add_executable(FlecsCompileProbe CompileProbe.cpp)
endif()
target_compile_features(FlecsCompileProbe PRIVATE cxx_std_23)
target_compile_definitions(FlecsCompileProbe PRIVATE
    PANGU_PROBE_FLECS_STATIC=$<STREQUAL:${expectedType},STATIC_LIBRARY>)
target_link_libraries(FlecsCompileProbe PRIVATE ThirdParty::Flecs)
set_target_properties(FlecsCompileProbe PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Tests/Toolchain"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Tests/Toolchain")
