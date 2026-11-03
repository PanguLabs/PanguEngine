cmake_minimum_required(VERSION 4.0)

add_executable(PanguJsonExtrasTests JsonExtrasTests.cpp)
target_link_libraries(PanguJsonExtrasTests PRIVATE PanguBuildOptions ThirdParty::NlohmannJson)
set_target_properties(PanguJsonExtrasTests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Bin"
)
add_test(NAME JsonExtras COMMAND PanguJsonExtrasTests)

add_test(NAME NlohmannSubmoduleRequired
    COMMAND "${CMAKE_COMMAND}"
        "-DPANGU_SOURCE_ROOT=${PROJECT_SOURCE_DIR}"
        "-DPANGU_TEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/MissingHeaders"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/MissingHeaders.cmake"
)
