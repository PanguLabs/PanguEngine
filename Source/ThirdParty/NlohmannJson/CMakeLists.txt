cmake_minimum_required(VERSION 4.0)

# 平台/工具链由根工程或显式验证过的独立宿主工程负责。
if(NOT TARGET PanguBuildOptions AND NOT PANGU_VALIDATED_STANDALONE_HOST)
    message(FATAL_ERROR "NlohmannJson requires the PanguEngine root or a validated standalone host project.")
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
add_library(ThirdPartyNlohmannJson INTERFACE)
add_library(ThirdParty::NlohmannJson ALIAS ThirdPartyNlohmannJson)
target_include_directories(ThirdPartyNlohmannJson SYSTEM INTERFACE
    "${nlohmannJsonIncludeDir}"
)

# Keep literal operators in nlohmann::literals, without global namespace imports.
target_compile_definitions(ThirdPartyNlohmannJson INTERFACE JSON_USE_GLOBAL_UDLS=0)
