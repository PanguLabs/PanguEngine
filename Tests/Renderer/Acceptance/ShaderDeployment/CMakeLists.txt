cmake_minimum_required(VERSION 4.0)
project(ShaderDeployment LANGUAGES C)
if(NOT CMAKE_C_COMPILER_ID STREQUAL "Clang")
    message(FATAL_ERROR "ShaderDeployment requires Clang")
endif()
include("${engineSource}/cmake/PanguTriangleShaders.cmake")
function(add_fixture name directory)
    pangu_add_triangle_shaders(
        TARGET "${name}"
        BUILD_ROOT "${CMAKE_BINARY_DIR}"
        DEPLOY_DIRECTORY "${CMAKE_BINARY_DIR}/${directory}"
        ${ARGN}
    )
endfunction()
add_fixture(OldShaders Old)
add_fixture(NewShaders New MESH_TRANSFORM_SHADERS)
if(order STREQUAL "LegacyFirst")
    add_fixture(SharedOld Shared)
    add_fixture(SharedNew Shared MESH_TRANSFORM_SHADERS)
else()
    add_fixture(SharedNew Shared MESH_TRANSFORM_SHADERS)
    add_fixture(SharedOld Shared)
endif()
add_custom_target(
    Check
    ALL
    DEPENDS
        OldShaders
        NewShaders
        SharedOld
        SharedNew
)
