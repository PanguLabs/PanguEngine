cmake_minimum_required(VERSION 4.0)
project(PanguAbiProfileBootstrap LANGUAGES CXX)

if(NOT DEFINED PANGU_BUILD_TOOLS_SOURCE_ROOT OR NOT IS_DIRECTORY "${PANGU_BUILD_TOOLS_SOURCE_ROOT}")
    message(FATAL_ERROR "PANGU_BUILD_TOOLS_SOURCE_ROOT must reference the existing engine sources.")
endif()
if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR
   CMAKE_CXX_COMPILER_VERSION VERSION_LESS 23.0 OR
   NOT CMAKE_CXX_COMPILER_VERSION VERSION_LESS 24.0)
    message(FATAL_ERROR "ABI build tool requires native Clang 23.x.")
endif()
if(CMAKE_CROSSCOMPILING)
    message(FATAL_ERROR "ABI build tool must never be cross-compiled.")
endif()
if(NOT CMAKE_BUILD_TYPE STREQUAL "Release")
    message(FATAL_ERROR "ABI build tool must use independent Release configuration.")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include("${PANGU_BUILD_TOOLS_SOURCE_ROOT}/cmake/Platform/Common/BuildOptions.cmake")
include("${PANGU_BUILD_TOOLS_SOURCE_ROOT}/cmake/Platform/Common/Module.cmake")
add_subdirectory("${PANGU_BUILD_TOOLS_SOURCE_ROOT}/Source/ThirdParty/NlohmannJson"
                 "${CMAKE_CURRENT_BINARY_DIR}/ThirdParty/NlohmannJson")
add_subdirectory("${PANGU_BUILD_TOOLS_SOURCE_ROOT}/Source/Runtime/Foundation"
                 "${CMAKE_CURRENT_BINARY_DIR}/Foundation")
add_subdirectory("${PANGU_BUILD_TOOLS_SOURCE_ROOT}/BuildTools/AbiProfile"
                 "${CMAKE_CURRENT_BINARY_DIR}/AbiProfile")
