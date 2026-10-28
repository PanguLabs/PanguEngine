cmake_minimum_required(VERSION 4.0)

foreach(_required IN ITEMS PANGU_SOURCE PANGU_BINARY PANGU_COMPILER PANGU_TOOLS_ROOT)
    if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
        message(FATAL_ERROR "Missing ${_required}")
    endif()
endforeach()

# Trace executed commands, not source text or elapsed time. Five calls across
# two directories and two publication roots should prepare exactly twice.
# Repeating configure also proves that the memo is never persisted in Cache.
foreach(_run RANGE 1 2)
    set(_trace "${PANGU_BINARY}/BuildToolsConfigure/trace-${_run}.jsonl")
    file(MAKE_DIRECTORY "${PANGU_BINARY}/BuildToolsConfigure")
    execute_process(COMMAND "${CMAKE_COMMAND}"
        --trace-source=${PANGU_SOURCE}/cmake/BuildTools.cmake
        --trace-format=json-v1 --trace-redirect=${_trace}
        -S "${PANGU_SOURCE}/Tests/AbiProfile/Fixtures/BuildToolsConfigure"
        -B "${PANGU_BINARY}/BuildToolsConfigure" -G Ninja
        "-DPANGU_BUILD_TOOLS_SOURCE_ROOT=${PANGU_SOURCE}"
        "-DPANGU_BUILD_TOOLS_TEST_ROOT=${PANGU_TOOLS_ROOT}"
        "-DCMAKE_CXX_COMPILER=${PANGU_COMPILER}"
        RESULT_VARIABLE _result OUTPUT_VARIABLE _output ERROR_VARIABLE _error)
    if(NOT _result EQUAL 0)
        message(FATAL_ERROR "Build tool configure fixture failed: ${_output}\n${_error}")
    endif()

    set(_scans 0)
    set(_versions 0)
    set(_locks 0)
    file(STRINGS "${_trace}" _events)
    foreach(_event IN LISTS _events)
        string(JSON _command ERROR_VARIABLE _jsonError GET "${_event}" cmd)
        if(_jsonError)
            continue()
        endif()
        string(JSON _operation ERROR_VARIABLE _jsonError GET "${_event}" args 0)
        if(_jsonError)
            continue()
        endif()
        if(_command STREQUAL "file" AND _operation STREQUAL "GLOB_RECURSE")
            math(EXPR _scans "${_scans} + 1")
        elseif(_command STREQUAL "file" AND _operation STREQUAL "LOCK")
            math(EXPR _locks "${_locks} + 1")
        elseif(_command STREQUAL "execute_process" AND _operation STREQUAL "COMMAND")
            string(JSON _argument GET "${_event}" args 2)
            if(_argument STREQUAL "--version")
                math(EXPR _versions "${_versions} + 1")
            endif()
        endif()
    endforeach()
    if(NOT _scans EQUAL 2 OR NOT _versions EQUAL 2 OR NOT _locks EQUAL 2)
        message(FATAL_ERROR
            "Repeated preparation in configure ${_run}: scans=${_scans}, compiler checks=${_versions}, locks=${_locks}; expected 2 each")
    endif()
endforeach()
