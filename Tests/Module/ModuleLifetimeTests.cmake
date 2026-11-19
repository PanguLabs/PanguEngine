cmake_minimum_required(VERSION 4.0)
file(MAKE_DIRECTORY "${PANGU_TEST_ROOT}")
string(RANDOM LENGTH 16 ALPHABET 0123456789abcdef runId)
foreach(mode IN ITEMS public private busy)
    set(marker "${PANGU_TEST_ROOT}/${mode}-${runId}")
    execute_process(
        COMMAND
            "${PANGU_MODULE_LIFETIME_TEST}" "${mode}" "${marker}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr
        TIMEOUT 15
    )
    if(mode STREQUAL "busy")
        if(CMAKE_HOST_WIN32)
            set(expectedResult "Exit code 0xc0000409")
        else()
            set(expectedResult "Subprocess killed")
        endif()
        if(
            NOT "${result}"
                STREQUAL
                "${expectedResult}"
            OR NOT EXISTS
                "${marker}.ready"
            OR EXISTS
                "${marker}.unloaded"
            OR NOT EXISTS
                "${marker}.before-destruction"
        )
            message(FATAL_ERROR "未完成的 Manager 析构必须终止且不得提前 NativeUnload: ${result}\n${stdout}\n${stderr}")
        endif()
        file(READ "${marker}.before-destruction" unloadsBeforeTermination)
        if(NOT "${unloadsBeforeTermination}" STREQUAL "0")
            message(FATAL_ERROR "进入析构前不应发生 NativeUnload: ${unloadsBeforeTermination}")
        endif()
    elseif(NOT "${result}" STREQUAL "0" OR NOT EXISTS "${marker}.unloaded" OR NOT EXISTS "${marker}.released")
        message(FATAL_ERROR "${mode} 访问凭据必须在真实 NativeUnload 后继续安全借用/复制/最终释放: ${result}\n${stdout}\n${stderr}")
    endif()
endforeach()
