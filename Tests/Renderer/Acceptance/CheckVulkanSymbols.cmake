cmake_minimum_required(VERSION 4.0)

execute_process(
    COMMAND
        "${llvmNm}" --dynamic --defined-only --demangle --format=posix "${rendererModule}"
    OUTPUT_VARIABLE symbols
    COMMAND_ERROR_IS_FATAL ANY
)
if(
    symbols
        MATCHES
        "(^|\n)(initRenderer|initRendererContext|exitRenderer|volkInitialize)[ \t]"
)
    message(FATAL_ERROR "Renderer exposes a private Forge Vulkan entry: ${CMAKE_MATCH_2}")
endif()
if(NOT symbols MATCHES "(^|\n)pg::renderer::QueryRendererModule\\(")
    message(FATAL_ERROR "Renderer Module query export is missing")
endif()
message(STATUS "Renderer Module query is exported; Forge Vulkan entries are private")
