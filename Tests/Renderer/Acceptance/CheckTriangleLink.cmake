cmake_minimum_required(VERSION 4.0)

set(target FirstTriangle)
set(method PresentTriangle)
if(linkFixture STREQUAL "IndexedMesh" OR linkFixture STREQUAL "MeshTransform")
    set(target "${linkFixture}")
    set(method PresentIndexedMesh)
endif()
set(PANGU_RENDERER_TEST_TRIANGLE_LINK ON)
include("${CMAKE_CURRENT_LIST_DIR}/BuildProject.cmake")
_pangu_build_renderer_project("${target}")
include("${rendererProjectBuild}/${target}Artifacts.cmake")
set(compileDatabase "${rendererProjectBuild}/compile_commands.json")
include("${CMAKE_CURRENT_LIST_DIR}/CheckGraphicsConsumer.cmake")

execute_process(
    COMMAND
        "${llvmNm}" --demangle "${executable}"
    OUTPUT_VARIABLE symbols
    COMMAND_ERROR_IS_FATAL ANY
)
foreach(symbol IN ITEMS "pg::app::OnFrame" "pg::app::GraphicsContext::${method}" "RendererSession::${method}")
    if(NOT symbols MATCHES "${symbol}[(]")
        message(FATAL_ERROR "Missing ${symbol} in ${executable}")
    endif()
endforeach()
if(linkFixture STREQUAL "IndexedMesh" OR linkFixture STREQUAL "MeshTransform")
    execute_process(
        COMMAND
            "${llvmNm}" --undefined-only --demangle "${callbackObject}"
        OUTPUT_VARIABLE references
        COMMAND_ERROR_IS_FATAL ANY
    )
    set(matrixCall "pg::app::GraphicsContext::PresentIndexedMesh[(][^)\n]*MeshTransform const&[)]")
    if(target STREQUAL "MeshTransform" AND NOT references MATCHES "${matrixCall}")
        message(FATAL_ERROR "${target} callback did not call the matrix overload")
    elseif(target STREQUAL "IndexedMesh" AND references MATCHES "${matrixCall}")
        message(FATAL_ERROR "${target} callback unexpectedly called the matrix overload")
    endif()
endif()
if(target STREQUAL "MeshTransform")
    foreach(owner IN ITEMS "GraphicsContext" "RendererSession")
        if(NOT symbols MATCHES "${owner}::PresentIndexedMesh[(][^)\n]*MeshTransform const&[)]")
            message(FATAL_ERROR "Missing matrix overload for ${owner} in ${executable}")
        endif()
    endforeach()
endif()
message(STATUS "${target} callback and Renderer entry linked in ${executable}")
