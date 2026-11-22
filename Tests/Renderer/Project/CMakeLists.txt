cmake_minimum_required(VERSION 4.0)

include("${PANGU_ENGINE_SOURCE_DIR}/cmake/ValidateBuildEnvironment.cmake")
project(
    RendererProject
    LANGUAGES
        C
        CXX
)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/Engine.commit" engineCommit)
set_property(
    DIRECTORY
    APPEND
    PROPERTY
        CMAKE_CONFIGURE_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/Engine.commit"
)
string(STRIP "${engineCommit}" engineCommit)
option(PANGU_ENGINE_ALLOW_LOCAL_OVERRIDE "Explicitly bypass Engine Git lock for local development" OFF)
include("${PANGU_ENGINE_SOURCE_DIR}/cmake/PanguEngineRevision.cmake")
pangu_validate_engine_revision(
    SOURCE_DIR "${PANGU_ENGINE_SOURCE_DIR}"
    EXPECTED_COMMIT "${engineCommit}"
    ALLOW_LOCAL_OVERRIDE "${PANGU_ENGINE_ALLOW_LOCAL_OVERRIDE}"
)

list(PREPEND CMAKE_MODULE_PATH "${PANGU_ENGINE_SOURCE_DIR}/cmake")
find_package(PanguEngine REQUIRED MODULE)
set(PANGU_ENGINE_BUILD_ROOT "${CMAKE_BINARY_DIR}/EngineBuild" CACHE PATH "Isolated fixture Engine artifacts")
pangu_initialize()

pangu_add_application(TARGET CheckContext KIND HEADLESS SOURCES CheckContext.cpp)
option(PANGU_RENDERER_TEST_GRAPHICS "Build the graphical consumer fixture" OFF)
if(PANGU_RENDERER_TEST_GRAPHICS)
    pangu_add_application(TARGET FirstFrame KIND GRAPHICS SOURCES FirstFrame.cpp)
    file(
        GENERATE OUTPUT "${CMAKE_BINARY_DIR}/FirstFrameArtifacts.cmake"
        CONTENT "set(executable [==[$<TARGET_FILE:FirstFrame>]==])\n"
    )
endif()
file(
    GENERATE OUTPUT "${CMAKE_BINARY_DIR}/CheckContextArtifacts.cmake"
    CONTENT
        "set(executable [==[$<TARGET_FILE:CheckContext>]==])\nset(runtimeLibraries [==[$<TARGET_RUNTIME_DLLS:CheckContext>]==])\n"
)
