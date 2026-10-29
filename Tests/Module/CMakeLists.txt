cmake_minimum_required(VERSION 4.0)

add_executable(PanguModuleIdentityTests
    ModuleIdentityTests.cpp
)

target_include_directories(PanguModuleIdentityTests PRIVATE
    "${PROJECT_SOURCE_DIR}/Source/Runtime/Module/Private"
)
target_link_libraries(PanguModuleIdentityTests PRIVATE PanguBuildOptions PanguModule)
set_target_properties(PanguModuleIdentityTests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Bin"
)
add_test(NAME ModuleIdentity COMMAND PanguModuleIdentityTests)

add_executable(PanguStaticModuleHostTests StaticModuleHostTests.cpp)
target_link_libraries(PanguStaticModuleHostTests PRIVATE PanguBuildOptions PanguModule PanguFoundation)
set_target_properties(PanguStaticModuleHostTests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
add_test(NAME StaticModuleHost COMMAND PanguStaticModuleHostTests)

add_executable(PanguRegistrationTests RegistrationTests.cpp)
target_link_libraries(PanguRegistrationTests PRIVATE PanguBuildOptions PanguModule)
set_target_properties(PanguRegistrationTests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
add_test(NAME ModuleRegistration COMMAND PanguRegistrationTests)

add_subdirectory(Fixtures)

add_executable(PanguScopeAccessTests ScopeAccessTests.cpp)
target_link_libraries(PanguScopeAccessTests PRIVATE PanguBuildOptions PanguModule)
set_target_properties(PanguScopeAccessTests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
add_test(NAME ScopeAccess COMMAND PanguScopeAccessTests)

add_executable(PanguScopeBuildTests ScopeBuildTests.cpp)
target_link_libraries(PanguScopeBuildTests PRIVATE PanguBuildOptions PanguModule)
set_target_properties(PanguScopeBuildTests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
add_test(NAME ScopeBuild COMMAND PanguScopeBuildTests)

add_executable(PanguScopeLifecycleTests ScopeLifecycleTests.cpp)
target_link_libraries(PanguScopeLifecycleTests PRIVATE PanguBuildOptions PanguModule)
set_target_properties(PanguScopeLifecycleTests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
add_test(NAME ScopeLifecycle COMMAND PanguScopeLifecycleTests)
add_test(NAME ScopeDestructor COMMAND "${CMAKE_COMMAND}"
    "-DPANGU_SCOPE_TEST=$<TARGET_FILE:PanguScopeLifecycleTests>" "-DPANGU_TEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/ScopeDestructorTests.cmake")
