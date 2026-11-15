cmake_minimum_required(VERSION 4.0)
project(NestedTestBudget LANGUAGES NONE)
include("${PANGU_ENGINE_ROOT}/cmake/PanguTestBuildJobs.cmake")
file(WRITE "${CMAKE_BINARY_DIR}/effective-budget.txt" "${PANGU_TEST_BUILD_JOBS}")
if(BUDGET_LEAF)
    return()
endif()

include(CTest)
include("${PANGU_ENGINE_ROOT}/cmake/PanguTesting.cmake")
pangu_test_context(OWNER engine ENABLED ON)
pangu_register_test(NAME BudgetInheritance
    COMMAND "${CMAKE_COMMAND}" "-DPANGU_ENGINE_ROOT=${PANGU_ENGINE_ROOT}"
        "-DBUDGET_FIXTURE=${CMAKE_CURRENT_SOURCE_DIR}" "-DBUDGET_CHILD=${CMAKE_BINARY_DIR}/Child"
        "-DEXPECT_BUDGET=${PANGU_TEST_BUILD_JOBS}" -P "${CMAKE_CURRENT_SOURCE_DIR}/CheckBudget.cmake"
    BOUNDARY system AREA testing TIER full REQUIRES subprocess nested-build
    PROCESSORS "${PANGU_TEST_BUILD_JOBS}")
