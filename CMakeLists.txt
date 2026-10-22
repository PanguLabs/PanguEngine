cmake_minimum_required(VERSION 4.0)

project(PanguEngine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(cmake/Platform/Common/BuildOptions.cmake)
include(cmake/PanguPlatform.cmake)
include(cmake/Platform/Common/Module.cmake)

add_subdirectory(Source/Runtime/Module)

include(CTest)
if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
    add_subdirectory(Tests/Module)
elseif(BUILD_TESTING AND CMAKE_CROSSCOMPILING)
    message(STATUS "Pangu CTest executables skipped for cross-compiled targets.")
endif()
