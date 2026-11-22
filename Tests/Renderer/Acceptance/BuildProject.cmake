include_guard(GLOBAL)

include("${PANGU_ENGINE_SOURCE_DIR}/cmake/PanguTestBuildJobs.cmake")

if(NOT PANGU_ENGINE_ALLOW_LOCAL_OVERRIDE)
    set(PANGU_ENGINE_ALLOW_LOCAL_OVERRIDE OFF)
endif()

function(_pangu_build_renderer_project target)
    set(graphics OFF)
    if(target STREQUAL "FirstFrame")
        set(graphics ON)
    endif()
    string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef runId)
    set(runRoot "${PANGU_TEST_ROOT}/${runId}")
    file(MAKE_DIRECTORY "${runRoot}")
    file(COPY "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../Project" DESTINATION "${runRoot}")
    set(projectSource "${runRoot}/Project")
    set(projectBuild "${runRoot}/Build")

    execute_process(
        COMMAND
            git --no-optional-locks -C "${PANGU_ENGINE_SOURCE_DIR}" rev-parse --verify HEAD
        RESULT_VARIABLE revisionResult
        OUTPUT_VARIABLE engineHead
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(NOT revisionResult STREQUAL "0")
        message(FATAL_ERROR "Cannot read Engine HEAD (${revisionResult})")
    endif()
    file(WRITE "${projectSource}/Engine.commit" "${engineHead}\n")
    message(STATUS "Renderer Project Engine lock: ${engineHead}; local override=${PANGU_ENGINE_ALLOW_LOCAL_OVERRIDE}")

    set(platformArgs)
    if(CMAKE_HOST_APPLE OR CMAKE_HOST_LINUX)
        list(APPEND platformArgs "-DCMAKE_TOOLCHAIN_FILE:FILEPATH=${CMAKE_TOOLCHAIN_FILE}")
    endif()
    if(graphics)
        list(APPEND platformArgs "-DCMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE:PATH=${projectBuild}/ReleaseBin")
    endif()
    execute_process(
        COMMAND
            "${CMAKE_COMMAND}" -S "${projectSource}" -B "${projectBuild}" -G Ninja
            "-DCMAKE_C_COMPILER:FILEPATH=${CMAKE_C_COMPILER}" "-DCMAKE_CXX_COMPILER:FILEPATH=${CMAKE_CXX_COMPILER}"
            -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DBUILD_TESTING=OFF
            "-DPANGU_ENGINE_SOURCE_DIR:PATH=${PANGU_ENGINE_SOURCE_DIR}"
            "-DPANGU_ENGINE_ALLOW_LOCAL_OVERRIDE:BOOL=${PANGU_ENGINE_ALLOW_LOCAL_OVERRIDE}"
            "-DPANGU_LINK_MODE:STRING=${PANGU_LINK_MODE}" "-DPANGU_RENDERER_TEST_GRAPHICS:BOOL=${graphics}"
            ${platformArgs}
        COMMAND_ERROR_IS_FATAL ANY
    )
    execute_process(
        COMMAND
            "${CMAKE_COMMAND}" --build "${projectBuild}" --parallel "${PANGU_TEST_BUILD_JOBS}" --target "${target}"
        COMMAND_ERROR_IS_FATAL ANY
    )
    set(rendererProjectBuild "${projectBuild}" PARENT_SCOPE)
    set(rendererProjectRunRoot "${runRoot}" PARENT_SCOPE)
endfunction()

# 每次验收有独立的 Build 目录。测试子进程退出后恢复目录，保留可复用的诊断产物。
function(_pangu_run_relocated_unix requiredModule)
    if(CMAKE_HOST_APPLE)
        set(loaderEnv
            --unset=DYLD_LIBRARY_PATH
            --unset=DYLD_FALLBACK_LIBRARY_PATH
            DYLD_PRINT_LIBRARIES=1
        )
        set(loadPrefix "dyld\\[[0-9]+\\]:")
    else()
        set(loaderEnv
            --unset=LD_LIBRARY_PATH
            LD_DEBUG=libs
        )
        set(loadPrefix "calling init:")
    endif()
    set(offline "${rendererProjectRunRoot}/OfflineBuild")
    file(RENAME "${rendererProjectBuild}" "${offline}")
    execute_process(
        COMMAND
            "${CMAKE_COMMAND}" -E env ${loaderEnv} ${ARGN}
        WORKING_DIRECTORY "${deploy}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr
        TIMEOUT 75
    )
    file(RENAME "${offline}" "${rendererProjectBuild}")
    file(WRITE "${rendererProjectRunRoot}/deployment.log" "${stdout}\n${stderr}")
    if(PANGU_LINK_MODE STREQUAL "Modular")
        file(REAL_PATH "${deploy}" deployPath)
        string(
            REGEX MATCHALL "${loadPrefix}[^\r\n]*/libPangu[^/\r\n]*"
            libraries
            "${stdout}\n${stderr}"
        )
        if(NOT libraries MATCHES "lib${requiredModule}[.]")
            message(FATAL_ERROR "Missing ${requiredModule} in ${rendererProjectRunRoot}/deployment.log")
        endif()
        foreach(line IN LISTS libraries)
            string(
                FIND "${line}"
                "${deployPath}/libPangu"
                localPath
            )
            if(localPath EQUAL -1)
                message(FATAL_ERROR "Pangu library loaded outside deployment: ${line}")
            endif()
        endforeach()
    endif()
    set(exitCode "${result}" PARENT_SCOPE)
    set(output "${stdout}" PARENT_SCOPE)
    set(errors "${stderr}" PARENT_SCOPE)
endfunction()
