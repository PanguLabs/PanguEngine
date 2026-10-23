# iOS device toolchain; use the installed iPhoneOS SDK, not an assumed Xcode SDK.
set(CMAKE_SYSTEM_NAME iOS)
if("$ENV{LLVM_ROOT}" STREQUAL "" OR NOT IS_DIRECTORY "$ENV{LLVM_ROOT}")
    message(FATAL_ERROR "Set LLVM_ROOT to the selected MacPorts LLVM 23 installation prefix.")
endif()
set(CMAKE_C_COMPILER "$ENV{LLVM_ROOT}/bin/clang")
set(CMAKE_CXX_COMPILER "$ENV{LLVM_ROOT}/bin/clang++")
# Apply the SDK's libc++ selection to every C++ target, including third-party sources.
string(FIND "${CMAKE_CXX_FLAGS_INIT}" "-stdlib=libc++" _pangu_has_libcxx_flags)
if(_pangu_has_libcxx_flags EQUAL -1)
    string(APPEND CMAKE_CXX_FLAGS_INIT " -stdlib=libc++")
endif()
unset(_pangu_has_libcxx_flags)
set(CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS "$ENV{LLVM_ROOT}/bin/clang-scan-deps" CACHE FILEPATH "")

if(DEFINED ENV{PANGU_IOS_SDK_PATH} AND NOT "$ENV{PANGU_IOS_SDK_PATH}" STREQUAL "")
    set(_pangu_ios_sdk "$ENV{PANGU_IOS_SDK_PATH}")
else()
    execute_process(
        COMMAND xcrun --sdk iphoneos --show-sdk-path
        OUTPUT_VARIABLE _pangu_ios_sdk
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE _pangu_ios_sdk_result
    )
    if(NOT _pangu_ios_sdk_result EQUAL 0 OR NOT IS_DIRECTORY "${_pangu_ios_sdk}")
        # Standalone SDK fallback for hosts with Command Line Tools but no Xcode.
        set(_pangu_ios_sdk "$ENV{HOME}/Library/Developer/SDKs/iPhoneOS18.2.sdk")
    endif()
    unset(_pangu_ios_sdk_result)
endif()

if(NOT IS_DIRECTORY "${_pangu_ios_sdk}")
    message(FATAL_ERROR "Set PANGU_IOS_SDK_PATH to an installed iPhoneOS device SDK.")
endif()

set(CMAKE_OSX_SYSROOT "${_pangu_ios_sdk}" CACHE PATH "iPhoneOS SDK directory")
set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "iOS device architectures")
set(CMAKE_OSX_DEPLOYMENT_TARGET "18.0" CACHE STRING "iOS minimum OS for current verification")
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
unset(_pangu_ios_sdk)
