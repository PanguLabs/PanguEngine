cmake_minimum_required(VERSION 4.0)

include(cmake/ValidateBuildEnvironment.cmake)
project(PanguEngine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(cmake/Platform/Common/BuildOptions.cmake)
include(cmake/PanguPlatform.cmake)
include(cmake/Platform/Common/Module.cmake)
set(PANGU_ABI_SOURCE_ROOT "${PROJECT_SOURCE_DIR}")
include(cmake/AbiProfile/AbiProfile.cmake)

add_subdirectory(Source/ThirdParty/NlohmannJson)
add_subdirectory(Source/Runtime/Foundation)
add_subdirectory(Source/Runtime/Module)

# AbiProfile is a host-side tool; mobile cross builds do not execute it.
if(NOT CMAKE_CROSSCOMPILING)
    add_subdirectory(BuildTools/AbiProfile)
endif()

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(Tests)
endif()

# Include real fixture Modules when testing, after all library hooks have registered.
get_property(_panguAbiModules GLOBAL PROPERTY PANGU_ABI_MODULE_TARGETS)
pangu_finalize_abi_profiles(TARGETS ${_panguAbiModules} PanguBuildOptions)
