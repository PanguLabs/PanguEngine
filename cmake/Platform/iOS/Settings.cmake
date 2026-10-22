# Device builds only; the SDK and deployment target are selected by the toolchain/preset.
if(NOT CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
    message(FATAL_ERROR "The current iOS toolchain target is arm64.")
endif()

if(NOT IS_DIRECTORY "${CMAKE_OSX_SYSROOT}")
    message(FATAL_ERROR "iOS requires an explicit, installed iPhoneOS SDK.")
endif()
