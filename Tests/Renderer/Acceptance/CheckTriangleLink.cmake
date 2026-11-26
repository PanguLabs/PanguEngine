cmake_minimum_required(VERSION 4.0)

set(target FirstTriangle)
set(method PresentTriangle)
if(linkFixture STREQUAL "IndexedMesh")
    set(target IndexedMesh)
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
message(STATUS "${target} callback and Renderer entry linked in ${executable}")
