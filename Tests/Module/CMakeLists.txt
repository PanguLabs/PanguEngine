add_executable(PanguModuleIdentityTests
    ModuleIdentityTests.cpp
)

target_include_directories(PanguModuleIdentityTests PRIVATE
    "${PROJECT_SOURCE_DIR}/Source/Runtime/Module/Private"
)
target_link_libraries(PanguModuleIdentityTests PRIVATE PanguModule)
set_target_properties(PanguModuleIdentityTests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Bin"
)
add_test(NAME ModuleIdentity COMMAND PanguModuleIdentityTests)
