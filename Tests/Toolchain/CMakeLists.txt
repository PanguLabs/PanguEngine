cmake_minimum_required(VERSION 4.0)

# Real C++ ThirdParty target and an actual Pangu Module target.
# Both use the selected platform compiler/STL, but only the Pangu Module
# receives PanguBuildOptions (warnings/no-exceptions).
add_subdirectory(ThirdParty)
add_subdirectory(Module)

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

# Compiler evidence audit operates on the exact generated TU commands, not guessed flags.
add_custom_target(PanguAbiCompileAudit ALL
    COMMAND "${CMAKE_COMMAND}"
        "-DPANGU_COMPILE_COMMANDS=${CMAKE_BINARY_DIR}/compile_commands.json"
        "-DPANGU_EVIDENCE_OUTPUT=${CMAKE_BINARY_DIR}/Abi/compile-evidence.json"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/AuditCompilerMacros.cmake"
    DEPENDS PanguFoundation PanguModule ThirdPartyCompiledProbe PanguToolchainProbe
    VERBATIM
)
if(NOT CMAKE_CROSSCOMPILING)
    # Link two distinct real compiled library targets and exercise C++23 ABI symbols.
    add_executable(PanguToolchainLinkTests LinkProbe.cpp)
    target_compile_features(PanguToolchainLinkTests PRIVATE cxx_std_23)
    target_link_libraries(PanguToolchainLinkTests PRIVATE ThirdParty::CompiledProbe Pangu::ToolchainProbe)
    set_target_properties(PanguToolchainLinkTests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
    add_test(NAME ToolchainLibraryLink COMMAND PanguToolchainLinkTests)
    foreach(mode IN ITEMS RTTI EXCEPTIONS)
        add_test(NAME "AbiCompileAuditReject${mode}"
            COMMAND "${CMAKE_COMMAND}"
                "-DPANGU_COMPILE_COMMANDS=${CMAKE_BINARY_DIR}/compile_commands.json"
                "-DPANGU_AUDIT_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/AuditCompilerMacros.cmake"
                "-DPANGU_EVIDENCE_OUTPUT=${CMAKE_BINARY_DIR}/Abi/negative-${mode}.json"
                "-DPANGU_NEGATIVE_MODE=${mode}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/NegativeAudit.cmake"
        )
    endforeach()
    foreach(rtti IN ITEMS ON OFF)
        if(rtti STREQUAL "ON")
            set(rttiOption -frtti)
        else()
            set(rttiOption -fno-rtti)
        endif()
        add_test(NAME "AbiCompileAuditRTTIBaseline${rtti}"
            COMMAND "${CMAKE_COMMAND}"
                "-DPANGU_COMPILE_COMMANDS=${CMAKE_BINARY_DIR}/compile_commands.json"
                "-DPANGU_AUDIT_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/AuditCompilerMacros.cmake"
                "-DPANGU_TEST_DIRECTORY=${CMAKE_CURRENT_BINARY_DIR}/RTTI-${rtti}"
                "-DPANGU_RTTI_OPTION=${rttiOption}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/RttiBaselineAudit.cmake"
        )
    endforeach()
    add_test(NAME AbiCompileAuditMissingDatabase
        COMMAND "${CMAKE_COMMAND}"
            "-DPANGU_AUDIT_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/AuditCompilerMacros.cmake"
            "-DPANGU_TEST_DIRECTORY=${CMAKE_CURRENT_BINARY_DIR}/MissingDatabase"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/MissingDatabaseAudit.cmake"
    )
    add_test(NAME AbiCompileAudit
        COMMAND "${CMAKE_COMMAND}"
            "-DPANGU_COMPILE_COMMANDS=${CMAKE_BINARY_DIR}/compile_commands.json"
            "-DPANGU_EVIDENCE_OUTPUT=${CMAKE_BINARY_DIR}/Abi/compile-evidence.json"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/AuditCompilerMacros.cmake"
    )
endif()
