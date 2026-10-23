add_executable(PanguFoundationErrorTests
    ErrorTests.cpp
)

target_link_libraries(PanguFoundationErrorTests PRIVATE PanguBuildOptions Pangu::Foundation)
set_target_properties(PanguFoundationErrorTests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Bin"
)
add_test(NAME FoundationError COMMAND PanguFoundationErrorTests)
