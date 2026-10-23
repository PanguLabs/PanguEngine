cmake_minimum_required(VERSION 4.0)

add_executable(PanguAbiProfileTests
    AbiProfileTests.cpp
    JsonForwardTests.cpp
)

target_link_libraries(PanguAbiProfileTests PRIVATE Pangu::BuildOptions PanguAbiProfileCore)
target_include_directories(PanguAbiProfileTests PRIVATE
    "${PROJECT_SOURCE_DIR}/Tools/AbiProfile/Private"
)
set_target_properties(PanguAbiProfileTests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Bin"
)
add_test(NAME AbiProfileVectors COMMAND PanguAbiProfileTests
    "${PROJECT_SOURCE_DIR}/wiki/foundation")
add_test(NAME AbiProfileCli
    COMMAND "${CMAKE_COMMAND}"
        "-DPANGU_ABI_PROFILE_TOOL=$<TARGET_FILE:PanguAbiProfileTool>"
        "-DPANGU_TEST_SCHEMA=${PROJECT_SOURCE_DIR}/wiki/foundation/abi-profile-v1.schema.json"
        "-DPANGU_TEST_VECTORS=${PROJECT_SOURCE_DIR}/wiki/foundation/abi-profile-v1.vectors.json"
        "-DPANGU_TEST_OUTPUT=${CMAKE_CURRENT_BINARY_DIR}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/CliTests.cmake"
)

add_executable(PanguJsonExtrasTests JsonExtrasTests.cpp)
target_link_libraries(PanguJsonExtrasTests PRIVATE Pangu::BuildOptions Pangu::NlohmannJson)
set_target_properties(PanguJsonExtrasTests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
add_test(NAME JsonExtras COMMAND PanguJsonExtrasTests)

add_executable(PanguAbiProfileIoTests IoTests.cpp)
target_link_libraries(PanguAbiProfileIoTests PRIVATE Pangu::BuildOptions PanguAbiProfileCore)
target_include_directories(PanguAbiProfileIoTests PRIVATE "${PROJECT_SOURCE_DIR}/Tools/AbiProfile/Private")
set_target_properties(PanguAbiProfileIoTests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
add_test(NAME AbiProfileIo COMMAND PanguAbiProfileIoTests "${CMAKE_CURRENT_BINARY_DIR}")
