# iOS device toolchain; use the installed iPhoneOS SDK, not an assumed Xcode SDK.
set(CMAKE_SYSTEM_NAME iOS)
if("$ENV{LLVM_ROOT}" STREQUAL "" OR NOT IS_DIRECTORY "$ENV{LLVM_ROOT}")
    message(FATAL_ERROR "Set LLVM_ROOT to the selected MacPorts LLVM 23 installation prefix.")
endif()
set(CMAKE_C_COMPILER "$ENV{LLVM_ROOT}/bin/clang")
set(CMAKE_CXX_COMPILER "$ENV{LLVM_ROOT}/bin/clang++")
# Apply the SDK's libc++ selection to every C++ target, including third-party sources.
string(
    FIND "${CMAKE_CXX_FLAGS_INIT}"
    "-stdlib=libc++"
    panguLibcxxFlagIndex
)
if(panguLibcxxFlagIndex EQUAL -1)
    string(APPEND CMAKE_CXX_FLAGS_INIT " -stdlib=libc++")
endif()
unset(panguLibcxxFlagIndex)
set(CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS "$ENV{LLVM_ROOT}/bin/clang-scan-deps" CACHE FILEPATH "")

block(SCOPE_FOR VARIABLES)
    if(DEFINED ENV{PANGU_IOS_SDK_PATH} AND NOT "$ENV{PANGU_IOS_SDK_PATH}" STREQUAL "")
        set(iosSdkPath "$ENV{PANGU_IOS_SDK_PATH}")
    else()
        execute_process(
            COMMAND
                xcrun --sdk iphoneos --show-sdk-path
            OUTPUT_VARIABLE iosSdkPath
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
            RESULT_VARIABLE iosSdkResult
        )
        if(NOT iosSdkResult EQUAL 0 OR NOT IS_DIRECTORY "${iosSdkPath}")
            # Standalone SDK fallback for hosts with Command Line Tools but no Xcode.
            set(iosSdkPath "$ENV{HOME}/Library/Developer/SDKs/iPhoneOS18.2.sdk")
        endif()
    endif()

    if(NOT IS_DIRECTORY "${iosSdkPath}")
        message(FATAL_ERROR "Set PANGU_IOS_SDK_PATH to an installed iPhoneOS device SDK.")
    endif()

    set(CMAKE_OSX_SYSROOT "${iosSdkPath}" CACHE PATH "iPhoneOS SDK directory")
endblock()
set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "iOS device architectures")
set(CMAKE_OSX_DEPLOYMENT_TARGET "18.0" CACHE STRING "iOS minimum OS for current verification")
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
