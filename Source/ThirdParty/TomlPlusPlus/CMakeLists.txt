cmake_minimum_required(VERSION 4.0)

set(tomlPlusPlusIncludeDir "${CMAKE_CURRENT_SOURCE_DIR}/Repo/include")
if(NOT EXISTS "${tomlPlusPlusIncludeDir}/toml++/toml.h")
    message(FATAL_ERROR
        "Pangu toml++ v3.4.0 Git submodule is missing or incomplete. "
        "From the PanguEngine root, run: git submodule update --init --recursive.")
endif()

add_library(ThirdPartyTomlPlusPlus INTERFACE)
add_library(ThirdParty::TomlPlusPlus ALIAS ThirdPartyTomlPlusPlus)
target_include_directories(ThirdPartyTomlPlusPlus SYSTEM INTERFACE "${tomlPlusPlusIncludeDir}")
target_compile_definitions(ThirdPartyTomlPlusPlus INTERFACE TOML_EXCEPTIONS=0)
