cmake_minimum_required(VERSION 4.0)

include("${CMAKE_CURRENT_LIST_DIR}/BuildProject.cmake")
_pangu_build_renderer_project(CheckContext)
include("${rendererProjectBuild}/CheckContextArtifacts.cmake")

set(deploy "${rendererProjectRunRoot}/Deploy")
if(CMAKE_HOST_LINUX OR CMAKE_HOST_APPLE)
    cmake_path(GET executable PARENT_PATH runtime)
    cmake_path(GET executable FILENAME executableName)
    file(COPY "${runtime}/" DESTINATION "${deploy}")
    file(GLOB rendererLibraries "${deploy}/libPanguRenderer*")
    _pangu_run_relocated_unix(PanguEngineHost "${deploy}/${executableName}")
    if(rendererLibraries OR NOT exitCode STREQUAL "0" OR NOT output MATCHES "(^|\n)context-closed\r?(\n|$)")
        message(FATAL_ERROR "Relocated headless application failed: ${output}\n${errors}")
    endif()
    message(STATUS "Relocated ${executableName}: context-closed")
    return()
endif()
file(MAKE_DIRECTORY "${deploy}")
file(
    COPY
        "${executable}"
        ${runtimeLibraries}
    DESTINATION "${deploy}"
)
cmake_path(GET executable FILENAME executableName)

set(rendererImports)
foreach(artifact IN ITEMS "${executable}" ${runtimeLibraries})
    cmake_path(GET artifact FILENAME name)
    execute_process(
        COMMAND
            "$ENV{LLVM_ROOT}/bin/llvm-readobj.exe" --coff-imports "${deploy}/${name}"
        OUTPUT_VARIABLE imports
        COMMAND_ERROR_IS_FATAL ANY
    )
    string(TOLOWER "${imports}" imports)
    string(
        REGEX MATCHALL "[ \t]name: [^\r\n]*(renderer|theforge)[^\r\n]*"
        matches
        "${imports}"
    )
    list(APPEND rendererImports ${matches})
endforeach()

execute_process(
    COMMAND
        "${CMAKE_COMMAND}" -E env "--unset=PATH" "${deploy}/${executableName}"
    WORKING_DIRECTORY "${deploy}"
    RESULT_VARIABLE exitCode
    OUTPUT_VARIABLE output
    ERROR_VARIABLE errors
)
string(
    REGEX MATCH "(^|\n)context-closed\r?(\n|$)"
    sawContextClosed
    "${output}"
)
if(NOT exitCode STREQUAL "0" OR NOT sawContextClosed OR rendererImports)
    message(
        FATAL_ERROR
        "Headless tool failed: exit=${exitCode}, Renderer imports=${rendererImports}\n${output}${errors}"
    )
endif()
message(STATUS "Relocated ${executableName}: exit=${exitCode}, context-closed, Renderer imports=${rendererImports}")
