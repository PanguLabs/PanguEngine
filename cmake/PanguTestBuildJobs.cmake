set(PANGU_TEST_BUILD_JOBS 2 CACHE STRING "Nested test build processor budget")
if(NOT PANGU_TEST_BUILD_JOBS MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR "PANGU_TEST_BUILD_JOBS must be a positive integer")
endif()
