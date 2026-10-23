cmake_minimum_required(VERSION 4.0)

# Compile-only validation is also supported by cross builds.
option(PANGU_VALIDATE_TOOLCHAIN "Compile test libraries for Toolchain/ThirdParty policy" ON)
if(PANGU_VALIDATE_TOOLCHAIN)
    add_subdirectory(Toolchain)
endif()

if(CMAKE_CROSSCOMPILING)
    message(STATUS "Pangu CTest executables skipped for cross-compiled targets.")
    return()
endif()

add_test(NAME BuildEnvironment
    COMMAND "${CMAKE_COMMAND}" "-DPANGU_SOURCE_ROOT=${PROJECT_SOURCE_DIR}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/CMake/EnvironmentTests.cmake"
)
add_subdirectory(AbiProfile)
add_subdirectory(Foundation)
add_subdirectory(Module)
