include_guard(GLOBAL)

function(pangu_prepare_build_tool outputVariable)
    set(source "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../BuildTools/PanguBuildTool")
    if(DEFINED PANGU_BUILD_TOOL_SOURCE_DIR)
        set(source "${PANGU_BUILD_TOOL_SOURCE_DIR}")
    endif()
    cmake_path(ABSOLUTE_PATH source BASE_DIRECTORY "${CMAKE_SOURCE_DIR}" NORMALIZE)
    file(REAL_PATH "${source}" source)
    if(NOT EXISTS "${source}/CMakeLists.txt" OR NOT EXISTS "${source}/Private/Main.cpp" OR
       NOT EXISTS "${source}/Private/BuildLayout.cpp")
        message(FATAL_ERROR "PanguBuildTool source directory is not the expected tool project: ${source}")
    endif()
    get_property(existing GLOBAL PROPERTY PANGU_BUILD_TOOL_EXECUTABLE)
    get_property(existingSource GLOBAL PROPERTY PANGU_BUILD_TOOL_SOURCE)
    if(existing AND EXISTS "${existing}")
        if(NOT existingSource STREQUAL source)
            message(FATAL_ERROR "PanguBuildTool source directory must remain fixed within one build graph")
        endif()
        set("${outputVariable}" "${existing}" PARENT_SCOPE)
        return()
    endif()

    if("$ENV{LLVM_ROOT}" STREQUAL "" OR NOT IS_DIRECTORY "$ENV{LLVM_ROOT}")
        message(FATAL_ERROR "PanguBuildTool host Clang 23 requires LLVM_ROOT on the build host")
    endif()
    if(CMAKE_HOST_WIN32)
        set(suffix ".exe")
    else()
        set(suffix "")
    endif()
    file(TO_CMAKE_PATH "$ENV{LLVM_ROOT}/bin/clang++${suffix}" hostCompiler)
    if(NOT EXISTS "${hostCompiler}")
        message(FATAL_ERROR "PanguBuildTool host Clang compiler missing: ${hostCompiler}")
    endif()

    set(binary "${CMAKE_BINARY_DIR}/BuildTools/PanguBuildTool")
    set(hostToolchainArgs)
    if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
        # 独立工具缓存由本函数拥有；清除旧 libstdc++ 初始化值，重用宿主原生策略。
        list(APPEND hostToolchainArgs
            "-DCMAKE_TOOLCHAIN_FILE:FILEPATH=${CMAKE_CURRENT_FUNCTION_LIST_DIR}/Platform/Linux/Toolchain.cmake"
            -U CMAKE_CXX_FLAGS -U CMAKE_EXE_LINKER_FLAGS)
        if(DEFINED PANGU_LIBCXX_ROOT)
            list(APPEND hostToolchainArgs "-DPANGU_LIBCXX_ROOT:PATH=${PANGU_LIBCXX_ROOT}")
        endif()
    endif()
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -S "${source}" -B "${binary}" -G Ninja
            "-DCMAKE_CXX_COMPILER:FILEPATH=${hostCompiler}"
            "-DCMAKE_BUILD_TYPE:STRING=Release"
            "-DBUILD_TESTING:BOOL=OFF"
            "-DCMAKE_EXPORT_COMPILE_COMMANDS:BOOL=ON"
            ${hostToolchainArgs}
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "PanguBuildTool host configure failed (${status}):\n${output}\n${error}")
    endif()

    execute_process(
        COMMAND "${CMAKE_COMMAND}" --build "${binary}" --target PanguBuildTool
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "PanguBuildTool host build failed (${status}):\n${output}\n${error}")
    endif()
    set(executable "${binary}/PanguBuildTool${suffix}")
    if(NOT EXISTS "${executable}")
        message(FATAL_ERROR "PanguBuildTool binary missing after host build: ${executable}")
    endif()
    message(STATUS "PanguBuildTool host ready: ${executable}")
    set_property(GLOBAL PROPERTY PANGU_BUILD_TOOL_EXECUTABLE "${executable}")
    set_property(GLOBAL PROPERTY PANGU_BUILD_TOOL_SOURCE "${source}")
    set("${outputVariable}" "${executable}" PARENT_SCOPE)
endfunction()
