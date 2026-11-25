cmake_minimum_required(VERSION 4.0)

include("${CMAKE_CURRENT_LIST_DIR}/BuildProject.cmake")
_pangu_build_renderer_project(FirstFrame)
include("${rendererProjectBuild}/FirstFrameArtifacts.cmake")
set(compileDatabase "${rendererProjectBuild}/compile_commands.json")
include("${CMAKE_CURRENT_LIST_DIR}/CheckGraphicsConsumer.cmake")

cmake_path(GET executable PARENT_PATH runtime)
foreach(file IN ITEMS gpu.data gpu.cfg)
    if(NOT EXISTS "${runtime}/${file}")
        message(FATAL_ERROR "Missing graphics resource next to the executable: ${runtime}/${file}")
    endif()
endforeach()
set(deploy "${rendererProjectRunRoot}/Deploy")
file(COPY "${runtime}/" DESTINATION "${deploy}")
cmake_path(GET executable FILENAME executableName)
if(CMAKE_HOST_LINUX)
    find_program(pythonInterpreter python3 NO_CACHE REQUIRED)
    _pangu_run_relocated_unix(
        PanguRenderer
        --unset=DISPLAY
        "${pythonInterpreter}"
        "${CMAKE_CURRENT_LIST_DIR}/ObserveWayland.py"
        "${deploy}/${executableName}"
    )
    if(NOT exitCode STREQUAL "0")
        message(
            FATAL_ERROR
            "Wayland Vulkan presentation failed (exit=${exitCode}): see ${rendererProjectRunRoot}/deployment.log"
        )
    endif()
    string(STRIP "${output}" summary)
    message(STATUS "${summary}")
elseif(CMAKE_HOST_APPLE)
    include("${CMAKE_CURRENT_LIST_DIR}/CheckMetalResourcePath.cmake")
    _pangu_run_relocated_unix(
        PanguRenderer
        /bin/sh
        "${CMAKE_CURRENT_LIST_DIR}/ObserveMac.sh"
        "${deploy}/${executableName}"
        "${PANGU_MAC_WINDOW_SAMPLER}"
    )
    if(NOT exitCode STREQUAL "0")
        message(FATAL_ERROR "AppKit deployment failed: ${output}\n${errors}")
    endif()
    string(STRIP "${output}" output)
    string(
        REGEX MATCH "[^\n]+$"
        summary
        "${output}"
    )
    message(STATUS "${summary}")
else()
    # 正常部署支持当前 GPU，覆盖后的配置使其低于 Forge 可用档位。
    set(office "${rendererProjectRunRoot}/Office")
    file(COPY "${deploy}/" DESTINATION "${office}")
    file(READ "${office}/gpu.cfg" gpuConfig)
    string(
        REPLACE "BEGIN_GPU_SETTINGS;"
        "BEGIN_GPU_SETTINGS;\ngpupresetlevel; GraphicQueueSupported == 1; office;"
        officeConfig
        "${gpuConfig}"
    )
    if(officeConfig STREQUAL gpuConfig)
        message(FATAL_ERROR "The Forge fixture lacks its GPU settings section")
    endif()
    file(WRITE "${office}/gpu.cfg" "${officeConfig}")
    execute_process(
        COMMAND
            "${office}/${executableName}"
        WORKING_DIRECTORY "${office}"
        RESULT_VARIABLE rejected
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error
        TIMEOUT 15
    )
    if(
        NOT rejected STREQUAL "1"
        OR NOT error MATCHES "Pangu Application: error domain=70672e72656e64 code=261"
        OR NOT output MATCHES "Preset: office"
        OR "${output}${error}" MATCHES "first-frame |Adding D3D12 swapchain"
    )
        message(FATAL_ERROR "Office preset must fail during Open (exit=${rejected}):\n${output}${error}")
    endif()
    message(STATUS "Office preset rejected during Open")
    execute_process(
        COMMAND
            powershell.exe -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File
            "${CMAKE_CURRENT_LIST_DIR}/ObserveWin32.ps1" -Executable "${deploy}/${executableName}" -LinkMode
            "${PANGU_LINK_MODE}"
        WORKING_DIRECTORY "${deploy}"
        COMMAND_ERROR_IS_FATAL ANY
    )
endif()
