cmake_minimum_required(VERSION 4.0)

execute_process(
    COMMAND
        /usr/bin/nm -gjU "${rendererModule}"
    OUTPUT_VARIABLE exports
    COMMAND_ERROR_IS_FATAL ANY
)
if(NOT exports MATCHES "QueryRendererModule")
    message(FATAL_ERROR "Renderer module Query missing in Mach-O exports")
endif()
if(
    exports
        MATCHES
        "(^|\n)_(initRenderer|exitRenderer|initQueue|cmdBindRenderTargets|queuePresent|queueSubmit)(\n|$)"
)
    message(FATAL_ERROR "Forge entry leaked into Mach-O Renderer exports: ${CMAKE_MATCH_0}")
endif()
