cmake_minimum_required(VERSION 4.0)

function(_pangu_run_fatal_scenario scenario expectedOutput fatal)
    set(marker "${PANGU_TEST_ROOT}/${scenario}.cleanup")
    file(REMOVE "${marker}")
    execute_process(
        COMMAND
            "${PANGU_FATAL_FIXTURE}" "${scenario}" "${marker}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error
        TIMEOUT 8
    )
    string(
        REPLACE "\r\n"
        "\n"
        output
        "${output}"
    )
    if(result MATCHES "timeout|Timeout" OR result STREQUAL "Process terminated due to timeout")
        message(FATAL_ERROR "${scenario}: child timed out: ${result}")
    endif()
    if(fatal)
        if(CMAKE_HOST_WIN32)
            set(expectedExit "Exit code 0xc0000409")
        else()
            set(expectedExit "Subprocess killed")
        endif()
        if(NOT result STREQUAL expectedExit OR EXISTS "${marker}")
            message(FATAL_ERROR "${scenario}: Fatal returned or ran cleanup: ${result}")
        endif()
    elseif(NOT result STREQUAL "0")
        message(FATAL_ERROR "${scenario}: expected success, got ${result}: ${error}")
    endif()
    if(NOT output STREQUAL expectedOutput OR NOT error STREQUAL "")
        message(FATAL_ERROR "${scenario}: unexpected evaluation/output: '${output}' stderr '${error}'")
    endif()
    message(STATUS "${scenario}: exit=${result}")
endfunction()

file(MAKE_DIRECTORY "${PANGU_TEST_ROOT}")
if(PANGU_ASSERTS)
    _pangu_run_fatal_scenario(Evaluation "condition\ncondition\nreturned\n" FALSE)
    _pangu_run_fatal_scenario(AssertFailure "condition\nmessage\n" TRUE)
else()
    _pangu_run_fatal_scenario(Evaluation "condition\nreturned\n" FALSE)
    _pangu_run_fatal_scenario(AssertFailure "returned\n" FALSE)
endif()
_pangu_run_fatal_scenario(CheckFailure "condition\nmessage\n" TRUE)
foreach(scenario IN ITEMS NoLogInstance ConcurrentFatal BlockedOutput)
    _pangu_run_fatal_scenario("${scenario}" "" TRUE)
endforeach()
if(PANGU_REENTRY_FIXTURE)
    set(PANGU_FATAL_FIXTURE "${PANGU_REENTRY_FIXTURE}")
    foreach(
        scenario
        IN
        ITEMS
            SnapshotContent
            SnapshotTruncated
            SnapshotExactCapacity
            SnapshotWithoutNative
            SnapshotUnknown
    )
        _pangu_run_fatal_scenario("${scenario}" "snapshot verified\n" TRUE)
    endforeach()
    foreach(scenario IN ITEMS RecursiveFatal FirstFatalBlocked)
        # 该夹具仅接收场景参数；额外的清理标记路径由 Run 传入并忽略。
        _pangu_run_fatal_scenario("${scenario}" "second entry\n" TRUE)
    endforeach()
endif()
