cmake_minimum_required(VERSION 4.0)
file(MAKE_DIRECTORY "${WORK_ROOT}")
file(MAKE_DIRECTORY "${WORK_ROOT}/contention" "${WORK_ROOT}/crash")
file(REMOVE "${WORK_ROOT}/contention/ready" "${WORK_ROOT}/contention/release")
# execute_process 的多个 COMMAND 同时启动；夹具使用固定标记进行有界握手。
execute_process(COMMAND "${FIXTURE}" holder "${WORK_ROOT}/contention"
    COMMAND "${FIXTURE}" contender "${WORK_ROOT}/contention"
    RESULTS_VARIABLE contention TIMEOUT 10)
if(NOT contention STREQUAL "0;0")
    message(FATAL_ERROR "跨进程非等待租约失败: ${contention}")
endif()
execute_process(COMMAND "${FIXTURE}" crash "${WORK_ROOT}/crash" RESULT_VARIABLE crashed TIMEOUT 10)
if(NOT crashed EQUAL 42)
    message(FATAL_ERROR "异常退出夹具未到达持有 lease 的退出点: ${crashed}")
endif()
execute_process(COMMAND "${FIXTURE}" recover "${WORK_ROOT}/crash" RESULT_VARIABLE reopened TIMEOUT 10)
if(NOT reopened EQUAL 0)
    message(FATAL_ERROR "进程退出后 lease 未回收: ${reopened}")
endif()
