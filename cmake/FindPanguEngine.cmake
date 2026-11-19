# 源码包只负责发现；由 Project 在 project() 后显式初始化同一目标图。
include(FindPackageHandleStandardArgs)
cmake_path(GET CMAKE_CURRENT_LIST_DIR PARENT_PATH panguEngineSourceDir)
file(REAL_PATH "${panguEngineSourceDir}" panguEngineSourceDir)
set(panguEngineSourceValid TRUE)
if(PANGU_ENGINE_SOURCE_DIR)
    file(REAL_PATH "${PANGU_ENGINE_SOURCE_DIR}" panguEngineRequestedSourceDir)
    set(panguEngineSelectedSourceDir "${panguEngineSourceDir}")
    if(CMAKE_HOST_WIN32)
        string(TOLOWER "${panguEngineRequestedSourceDir}" panguEngineRequestedSourceDir)
        string(TOLOWER "${panguEngineSelectedSourceDir}" panguEngineSelectedSourceDir)
    endif()
    if(NOT panguEngineRequestedSourceDir STREQUAL panguEngineSelectedSourceDir)
        set(panguEngineSourceValid FALSE)
    endif()
else()
    set(PANGU_ENGINE_SOURCE_DIR "${panguEngineSourceDir}")
endif()
foreach(
    panguEngineRequiredFile
    IN
    ITEMS
        CMakeLists.txt
        cmake/PanguBuildLayout.cmake
        cmake/PanguRuntimeDeployment.cmake
        Source/Runtime/Foundation/CMakeLists.txt
        Source/Runtime/EngineHost/CMakeLists.txt
)
    if(NOT EXISTS "${panguEngineSourceDir}/${panguEngineRequiredFile}")
        set(panguEngineSourceValid FALSE)
    endif()
endforeach()
find_package_handle_standard_args(
    PanguEngine
    REQUIRED_VARS
        PANGU_ENGINE_SOURCE_DIR
        panguEngineSourceValid
    HANDLE_COMPONENTS
    REASON_FAILURE_MESSAGE
        "CMAKE_MODULE_PATH must select a complete Engine source tree matching PANGU_ENGINE_SOURCE_DIR."
)
if(NOT PanguEngine_FOUND)
    return()
endif()
set(PANGU_ENGINE_SOURCE_DIR "${panguEngineSourceDir}")

function(pangu_initialize)
    if(ARGC GREATER 0)
        message(FATAL_ERROR "pangu_initialize takes no arguments; set PANGU_ENGINE_BUILD_ROOT before calling it")
    endif()
    if(NOT PANGU_ENGINE_BUILD_ROOT)
        set(PANGU_ENGINE_BUILD_ROOT "${CMAKE_BINARY_DIR}" CACHE PATH "Shared Engine artifact root" FORCE)
    endif()
    if(NOT IS_ABSOLUTE "${PANGU_ENGINE_BUILD_ROOT}")
        message(FATAL_ERROR "Pangu Engine build root must be absolute")
    endif()

    # 全局语言列表也包含其他目录启用的语言，编译器信息须在调用方可见。
    get_property(enabledLanguages GLOBAL PROPERTY ENABLED_LANGUAGES)
    if(
        NOT "C"
            IN_LIST
            enabledLanguages
        OR NOT "CXX"
            IN_LIST
            enabledLanguages
        OR NOT CMAKE_C_COMPILER_ID
        OR NOT CMAKE_CXX_COMPILER_ID
    )
        message(
            FATAL_ERROR
            "pangu_initialize requires Project languages C and CXX. Use project(... LANGUAGES C CXX) in the top-level CMakeLists.txt."
        )
    endif()

    cmake_path(GET CMAKE_CURRENT_FUNCTION_LIST_DIR PARENT_PATH panguEngineSourceDir)
    file(REAL_PATH "${panguEngineSourceDir}" panguEngineSourceDir)
    include("${panguEngineSourceDir}/cmake/PanguBuildLayout.cmake")
    include("${panguEngineSourceDir}/cmake/PanguRuntimeDeployment.cmake")
    set(PANGU_ENGINE_SOURCE_DIR "${panguEngineSourceDir}" PARENT_SCOPE)
    set(PANGU_SOURCE_OWNER project PARENT_SCOPE)
    add_subdirectory("${panguEngineSourceDir}" "${PANGU_ENGINE_BUILD_ROOT}/Targets/Engine")
endfunction()
