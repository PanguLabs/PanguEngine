cmake_minimum_required(VERSION 4.0)
if(
    NOT "$ENV{PANGU_TEST_BUILD_JOBS}"
        STREQUAL
        EXPECT_BUDGET
    OR NOT "$ENV{CMAKE_BUILD_PARALLEL_LEVEL}"
        STREQUAL
        EXPECT_BUDGET
)
    message(FATAL_ERROR "CTest did not restore its reserved nested processor budget")
endif()
execute_process(
    COMMAND
        "${CMAKE_COMMAND}" -S "${BUDGET_FIXTURE}" -B "${BUDGET_CHILD}" -G Ninja
        "-DPANGU_ENGINE_ROOT=${PANGU_ENGINE_ROOT}" -DBUDGET_LEAF=ON
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
)
if(NOT result STREQUAL "0")
    message(FATAL_ERROR "Nested budget configure failed: ${output}\n${error}")
endif()
file(READ "${BUDGET_CHILD}/effective-budget.txt" actual)
if(NOT actual STREQUAL EXPECT_BUDGET)
    message(FATAL_ERROR "Nested configure used ${actual} processors instead of reserved ${EXPECT_BUDGET}")
endif()
