cmake_minimum_required(VERSION 4.0)
# This fixture configures build-tool integration; it compiles no target itself.
project(PanguBuildToolsConfigureContract LANGUAGES NONE)

include("${PANGU_BUILD_TOOLS_SOURCE_ROOT}/cmake/BuildTools.cmake")
pangu_prepare_build_tools(_first)
add_subdirectory(Consumer)
pangu_prepare_build_tools(_repeated)
if(NOT _first STREQUAL _repeated OR NOT _first STREQUAL _consumerTool)
    message(FATAL_ERROR "Repeated preparation changed the published tool path")
endif()

# Seed another publication root with the same immutable binary. Selecting a
# different root must not reuse a path from the first preparation context.
set(_originalRoot "${PANGU_BUILD_TOOLS_TEST_ROOT}")
get_filename_component(_published "${_first}" DIRECTORY)
get_filename_component(_key "${_published}" NAME)
get_filename_component(_name "${_first}" NAME)
set(PANGU_BUILD_TOOLS_TEST_ROOT "${CMAKE_BINARY_DIR}/OtherBuildTools")
set(_otherPublished "${PANGU_BUILD_TOOLS_TEST_ROOT}/published/${_key}")
file(MAKE_DIRECTORY "${_otherPublished}")
file(COPY_FILE "${_first}" "${_otherPublished}/${_name}" ONLY_IF_DIFFERENT)
file(COPY_FILE "${_published}/ready.sha256" "${_otherPublished}/ready.sha256" ONLY_IF_DIFFERENT)
pangu_prepare_build_tools(_other)
if(NOT _other STREQUAL "${_otherPublished}/${_name}")
    message(FATAL_ERROR "Preparation reused a tool from a different publication root")
endif()

set(PANGU_BUILD_TOOLS_TEST_ROOT "${_originalRoot}")
pangu_prepare_build_tools(_restored)
if(NOT _restored STREQUAL _first)
    message(FATAL_ERROR "Preparation failed to restore the original context")
endif()
