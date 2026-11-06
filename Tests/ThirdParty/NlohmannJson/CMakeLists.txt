cmake_minimum_required(VERSION 4.0)

include("${PROJECT_SOURCE_DIR}/cmake/PanguTestDependencies.cmake")
pangu_require_googletest(RTTI ON)
add_executable(PanguJsonUnitTests JsonExtrasTests.cpp)
target_link_libraries(PanguJsonUnitTests PRIVATE PanguBuildOptions ThirdParty::NlohmannJson ThirdParty::GoogleTestMain)
set_target_properties(PanguJsonUnitTests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Bin"
)
pangu_register_gtest(TARGET PanguJsonUnitTests BOUNDARY unit AREA thirdparty TIER fast)

pangu_register_test(NAME NlohmannSubmoduleRequired
    COMMAND "${CMAKE_COMMAND}"
        "-DPANGU_SOURCE_ROOT=${PROJECT_SOURCE_DIR}"
        "-DPANGU_TEST_ROOT=${CMAKE_BINARY_DIR}/Tests/Work/NlohmannSubmoduleRequired"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/MissingHeaders.cmake"
    BOUNDARY system AREA thirdparty TIER full TYPES regression REQUIRES subprocess
)
