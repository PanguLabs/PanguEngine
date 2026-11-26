cmake_minimum_required(VERSION 4.0)

foreach(order IN ITEMS LegacyFirst TransformFirst)
    set(build "${testRoot}/${order}")
    execute_process(
        COMMAND
            "${CMAKE_COMMAND}" -S "${CMAKE_CURRENT_LIST_DIR}/ShaderDeployment" -B "${build}" -G Ninja
            "-DengineSource=${engineSource}" "-Dorder=${order}" "-DCMAKE_C_COMPILER=${compiler}"
        COMMAND_ERROR_IS_FATAL ANY
    )
    execute_process(
        COMMAND
            "${CMAKE_COMMAND}" --build "${build}"
        COMMAND_ERROR_IS_FATAL ANY
    )
    if(CMAKE_HOST_WIN32)
        set(platform DIRECT3D12)
    else()
        set(platform VULKAN)
    endif()
    foreach(deploy IN ITEMS Old New Shared)
        set(directory "${build}/${deploy}/${platform}")
        set(expected
            Graph.vert
            Graph.frag
        )
        if(CMAKE_HOST_WIN32)
            list(APPEND expected default.rootsig)
        endif()
        if(NOT deploy STREQUAL "Old")
            list(APPEND expected MeshTransform.vert)
        endif()
        file(GLOB actual RELATIVE "${directory}" "${directory}/*")
        list(SORT expected)
        if(NOT actual STREQUAL expected)
            message(FATAL_ERROR "${directory}: expected ${expected}, found ${actual}")
        endif()
        foreach(name IN LISTS expected)
            file(READ "${directory}/${name}" header LIMIT 4 HEX)
            if(NOT header STREQUAL "4046534c")
                message(FATAL_ERROR "Invalid FSL container: ${directory}/${name}")
            endif()
        endforeach()
    endforeach()
endforeach()
