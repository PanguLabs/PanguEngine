cmake_minimum_required(VERSION 4.0)
get_filename_component(panguInspectorDirectory "${CMAKE_CXX_COMPILER}" DIRECTORY)
set(panguInspector "${panguInspectorDirectory}/llvm-readobj${CMAKE_EXECUTABLE_SUFFIX}")

foreach(case IN ITEMS BuildLayout ProjectSwitch)
    pangu_register_test(NAME "PanguMeta${case}"
        COMMAND "${CMAKE_COMMAND}"
            "-DPANGU_ENGINE_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
            "-DPANGU_TEST_ROOT=${CMAKE_BINARY_DIR}/Tests/Work/PanguMeta${case}"
            "-DPANGU_TEST_BUILD_JOBS=${PANGU_TEST_BUILD_JOBS}"
            "-DPANGU_TEST_INSPECTOR=${panguInspector}"
            "-DPANGU_META_SDK_CONFIG=${PANGU_META_SDK_CONFIG}"
            "-DPANGU_META_HOST_BUILD_ROOT=${CMAKE_BINARY_DIR}/Tests"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/${case}Tests.cmake"
        BOUNDARY system AREA build TIER full TYPES regression
        REQUIRES subprocess nested-build PROCESSORS "${PANGU_TEST_BUILD_JOBS}" TIMEOUT 900)
endforeach()

if(PANGU_META_SDK_CONFIG)
    foreach(case IN ITEMS Registration Incremental FailureRecovery)
        pangu_register_test(NAME "PanguMeta${case}"
            COMMAND "${CMAKE_COMMAND}"
                "-DPANGU_ENGINE_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
                "-DPANGU_TEST_ROOT=${CMAKE_BINARY_DIR}/Tests/Work/PanguMeta${case}"
                "-DPANGU_META_SDK_CONFIG=${PANGU_META_SDK_CONFIG}"
                "-DPANGU_META_HOST_BUILD_ROOT=${CMAKE_BINARY_DIR}/Tests"
                "-DPANGU_TEST_BUILD_JOBS=${PANGU_TEST_BUILD_JOBS}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/${case}Tests.cmake"
            BOUNDARY system AREA build TIER full TYPES regression
            REQUIRES subprocess nested-build PROCESSORS "${PANGU_TEST_BUILD_JOBS}" TIMEOUT 900)
        set_tests_properties("PanguMeta${case}" PROPERTIES RESOURCE_LOCK PanguMetaHostBuild)
    endforeach()
endif()
