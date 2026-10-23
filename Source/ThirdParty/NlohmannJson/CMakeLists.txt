cmake_minimum_required(VERSION 4.0)

# Platform/toolchain validation is performed by the PanguEngine root project.
if(NOT TARGET Pangu::BuildOptions)
    message(FATAL_ERROR "NlohmannJson must be configured through the PanguEngine root project.")
endif()

set(nlohmannJsonIncludeDir "${CMAKE_CURRENT_SOURCE_DIR}/Repo/single_include")
foreach(header IN ITEMS json.hpp json_fwd.hpp)
    if(NOT EXISTS "${nlohmannJsonIncludeDir}/nlohmann/${header}")
        message(FATAL_ERROR
            "Pangu NlohmannJson submodule is missing or incomplete (${header}). "
            "From the repository root, run: git submodule update --init --recursive"
        )
    endif()
endforeach()

# Use our target/configuration instead of configuring upstream tests and options.
# This header-only dependency is compiled in its consumers' toolchain context.
add_library(PanguNlohmannJson INTERFACE)
add_library(Pangu::NlohmannJson ALIAS PanguNlohmannJson)
target_include_directories(PanguNlohmannJson SYSTEM INTERFACE
    "${nlohmannJsonIncludeDir}"
)

# Keep literal operators in nlohmann::literals, without global namespace imports.
target_compile_definitions(PanguNlohmannJson INTERFACE JSON_USE_GLOBAL_UDLS=0)
