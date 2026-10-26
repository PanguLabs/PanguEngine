cmake_minimum_required(VERSION 4.0)
foreach(_name IN ITEMS PANGU_SOURCE PANGU_BINARY PANGU_COMPILER)
    if(NOT DEFINED ${_name})
        message(FATAL_ERROR "Missing ${_name}")
    endif()
endforeach()
foreach(_kind IN ITEMS module static)
    foreach(_mode IN ITEMS global target)
        set(_build "${PANGU_BINARY}/LauncherContract/${_kind}-${_mode}")
        execute_process(COMMAND "${CMAKE_COMMAND}"
            -S "${PANGU_SOURCE}/Tests/AbiProfile/Fixtures/Launcher" -B "${_build}" -G Ninja
            "-DPANGU_ABI_SOURCE_ROOT=${PANGU_SOURCE}" "-DCMAKE_CXX_COMPILER=${PANGU_COMPILER}"
            -DCMAKE_BUILD_TYPE=Release "-DPANGU_LAUNCHER_KIND=${_kind}" "-DPANGU_LAUNCHER_MODE=${_mode}"
            RESULT_VARIABLE _result OUTPUT_VARIABLE _output ERROR_VARIABLE _error)
        if(_result EQUAL 0)
            message(FATAL_ERROR "ABI registration silently replaced the ${_mode} ${_kind} launcher")
        endif()
        if(NOT "${_output}${_error}" MATCHES "pre-existing CXX_COMPILER_LAUNCHER")
            message(FATAL_ERROR "Registration failed for the wrong reason: ${_output}${_error}")
        endif()
    endforeach()
endforeach()
