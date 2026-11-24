cmake_minimum_required(VERSION 4.0)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    message(FATAL_ERROR "WaylandWindow requires the Linux Clang preset.")
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(PanguWayland REQUIRED IMPORTED_TARGET GLOBAL wayland-client)
pkg_check_modules(PanguLibdecor REQUIRED IMPORTED_TARGET GLOBAL libdecor-0>=0.2.2)

add_library(PanguWaylandWindow INTERFACE)
add_library(ThirdParty::WaylandWindow ALIAS PanguWaylandWindow)
target_link_libraries(
    PanguWaylandWindow
    INTERFACE
        PkgConfig::PanguWayland
        PkgConfig::PanguLibdecor
)
