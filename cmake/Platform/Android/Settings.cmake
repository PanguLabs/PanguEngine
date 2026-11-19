# Use the selected Android NDK's CMake toolchain; never require desktop Clang 23.
if(NOT ANDROID OR NOT CMAKE_ANDROID_NDK)
    message(FATAL_ERROR "Android requires the official NDK CMake toolchain file.")
endif()

if("$ENV{ANDROID_HOME}" STREQUAL "" OR NOT IS_DIRECTORY "$ENV{ANDROID_HOME}")
    message(FATAL_ERROR "Set ANDROID_HOME to the Android SDK installation directory.")
endif()
if("$ENV{ANDROID_NDK_HOME}" STREQUAL "" OR NOT IS_DIRECTORY "$ENV{ANDROID_NDK_HOME}")
    message(FATAL_ERROR "Set ANDROID_NDK_HOME to the Android NDK installation directory.")
endif()

block(SCOPE_FOR VARIABLES)
    get_filename_component(ndkRoot "${CMAKE_ANDROID_NDK}" REALPATH)
    get_filename_component(ndkEnvRoot "$ENV{ANDROID_NDK_HOME}" REALPATH)
    if(NOT ndkRoot STREQUAL ndkEnvRoot)
        message(FATAL_ERROR "Android must use the NDK selected by ANDROID_NDK_HOME.")
    endif()
    get_filename_component(compilerPath "${CMAKE_CXX_COMPILER}" REALPATH)
    cmake_path(
        IS_PREFIX
        ndkRoot
        "${compilerPath}"
        NORMALIZE
        isNdkClang
    )
    if(NOT isNdkClang)
        message(FATAL_ERROR "Android Clang must come from the selected NDK.")
    endif()
endblock()
