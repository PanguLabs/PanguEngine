cmake_minimum_required(VERSION 4.0)
project(PanguPlatformLinkSmoke LANGUAGES CXX)

if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    message(FATAL_ERROR "PanguPlatformLinkSmoke requires Clang.")
endif()

# 本工程独立于引擎根，不复用 Pangu Toolchain；按 wiki/foundation/apple-toolchain.md 自校验 iOS Clang 策略。
if(CMAKE_SYSTEM_NAME STREQUAL "iOS" AND
    (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 23 OR
     NOT CMAKE_CXX_COMPILER_VERSION VERSION_LESS 24))
    message(FATAL_ERROR "iOS must use MacPorts Clang 23.x.")
endif()

add_executable(PanguPlatformLinkSmoke Main.cpp)
target_compile_features(PanguPlatformLinkSmoke PRIVATE cxx_std_23)
