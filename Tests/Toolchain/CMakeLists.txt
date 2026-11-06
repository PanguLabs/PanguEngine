cmake_minimum_required(VERSION 4.0)

# 真实 ThirdParty 库与 Pangu Module 使用相同平台工具链、不同目标策略。
add_subdirectory(ThirdParty)
add_subdirectory(Module)
pangu_test_build_dependencies(TIER full TARGETS ThirdPartyCompiledProbe PanguToolchainProbe)

get_target_property(_pangu_third_party_type ThirdPartyCompiledProbe TYPE)
get_target_property(_pangu_third_party_links ThirdPartyCompiledProbe LINK_LIBRARIES)
if(NOT _pangu_third_party_type STREQUAL "STATIC_LIBRARY")
    message(FATAL_ERROR "ThirdParty compiled probe must be a real static library.")
endif()
if(_pangu_third_party_links MATCHES "PanguBuildOptions")
    message(FATAL_ERROR "ThirdParty toolchain probe must not inherit Pangu target policy.")
endif()
unset(_pangu_third_party_type)
unset(_pangu_third_party_links)

if(NOT CMAKE_CROSSCOMPILING)
    add_executable(PanguToolchainLinkTests LinkProbe.cpp)
    target_compile_features(PanguToolchainLinkTests PRIVATE cxx_std_23)
    target_link_libraries(PanguToolchainLinkTests PRIVATE ThirdParty::CompiledProbe Pangu::ToolchainProbe)
    set_target_properties(PanguToolchainLinkTests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Bin"
    )
    pangu_register_test(NAME ToolchainLibraryLink COMMAND PanguToolchainLinkTests
        BOUNDARY integration AREA toolchain TIER precommit
        BUILD_TARGETS PanguToolchainLinkTests TYPES contract)
endif()
