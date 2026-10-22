include_guard(GLOBAL)

if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    message(FATAL_ERROR "PanguEngine requires Clang; found '${CMAKE_CXX_COMPILER_ID}'.")
endif()

# Android uses the selected NDK Clang instead of LLVM 23.
if(NOT ANDROID AND (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 23.0
        OR NOT CMAKE_CXX_COMPILER_VERSION VERSION_LESS 24.0))
    message(FATAL_ERROR "PanguEngine requires Clang 23.x except on Android.")
endif()

# Desktop and iOS compilers must come from the explicitly selected LLVM installation.
if(NOT ANDROID)
    if("$ENV{LLVM_ROOT}" STREQUAL "" OR NOT IS_DIRECTORY "$ENV{LLVM_ROOT}")
        message(FATAL_ERROR "Set LLVM_ROOT to the selected LLVM 23 installation prefix.")
    endif()

    get_filename_component(_pangu_llvm_root "$ENV{LLVM_ROOT}" REALPATH)
    get_filename_component(_pangu_compiler_real "${CMAKE_CXX_COMPILER}" REALPATH)
    cmake_path(IS_PREFIX _pangu_llvm_root "${_pangu_compiler_real}" NORMALIZE _pangu_is_llvm_clang)
    if(NOT _pangu_is_llvm_clang)
        message(FATAL_ERROR "Clang must come from the installation selected by LLVM_ROOT.")
    endif()
    unset(_pangu_llvm_root)
    unset(_pangu_compiler_real)
    unset(_pangu_is_llvm_clang)
endif()

if(ANDROID)
    set(PANGU_TARGET_PLATFORM "Android")
elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(PANGU_TARGET_PLATFORM "iOS")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(PANGU_TARGET_PLATFORM "Windows")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(PANGU_TARGET_PLATFORM "Linux")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(PANGU_TARGET_PLATFORM "macOS")
else()
    message(FATAL_ERROR "PanguEngine does not support target system '${CMAKE_SYSTEM_NAME}'.")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/Platform/${PANGU_TARGET_PLATFORM}/Settings.cmake")

message(STATUS "Pangu target: ${PANGU_TARGET_PLATFORM}; Clang ${CMAKE_CXX_COMPILER_VERSION}; C++23")
