cmake_minimum_required(VERSION 4.0)
project(RuntimeDeployment LANGUAGES C)
if(NOT CMAKE_C_COMPILER_ID STREQUAL "Clang")
    message(FATAL_ERROR "RuntimeDeployment requires the target Clang compiler")
endif()
include("${PANGU_SOURCE_ROOT}/cmake/PanguRuntimeDeployment.cmake")
set(PANGU_LINK_MODE Modular)
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/leaf.c" CONTENT "int leaf(void) { return 42; }\n")
file(
    CONFIGURE
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/bridge.c"
    CONTENT "int leaf(void); int bridge(void) { return leaf(); }\n"
)
file(
    CONFIGURE
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/main.c"
    CONTENT "int bridge(void); int main(void) { return bridge() != 42; }\n"
)
option(USE_SHARED_WIDGET "Link the fixture's shared Widget" ON)
add_library(Bridge SHARED "${CMAKE_CURRENT_BINARY_DIR}/bridge.c")
if(USE_SHARED_WIDGET)
    add_library(Widget SHARED "${CMAKE_CURRENT_BINARY_DIR}/leaf.c")
    target_link_libraries(Bridge PRIVATE Widget)
else()
    target_sources(Bridge PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/leaf.c")
endif()
if(APPLE AND USE_SHARED_WIDGET)
    file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/sample.txt" CONTENT "bundle resource\n")
    set_target_properties(
        Widget
        PROPERTIES
            FRAMEWORK
                TRUE
            RESOURCE
                "${CMAKE_CURRENT_BINARY_DIR}/sample.txt"
    )
    target_sources(Widget PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/sample.txt")
endif()
add_library(Unrelated SHARED "${CMAKE_CURRENT_BINARY_DIR}/leaf.c")
set_target_properties(
    Unrelated
    PROPERTIES
        BUILD_RPATH
            "/preferred"
)
add_executable(App "${CMAKE_CURRENT_BINARY_DIR}/main.c")
target_link_libraries(App PRIVATE Bridge)
set_target_properties(
    App
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY
            "${CMAKE_CURRENT_BINARY_DIR}/Bin"
)
pangu_stage_host_runtime(TARGET App APPLICATION)
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/Unrelated.path" CONTENT "$<TARGET_FILE:Unrelated>")
