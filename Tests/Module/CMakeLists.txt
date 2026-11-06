cmake_minimum_required(VERSION 4.0)

include("${PROJECT_SOURCE_DIR}/cmake/PanguTestDependencies.cmake")
pangu_require_googletest(RTTI ON)

add_executable(PanguModuleUnitTests
    ModuleIdentityTests.cpp
    ScopeAccessTests.cpp
    ScopeBuildTests.cpp
)

target_include_directories(PanguModuleUnitTests PRIVATE
    "${PROJECT_SOURCE_DIR}/Source/Runtime/Module/Private"
)
target_link_libraries(PanguModuleUnitTests PRIVATE PanguBuildOptions PanguModule ThirdParty::GoogleTestMain)
set_target_properties(PanguModuleUnitTests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Bin"
)
pangu_register_gtest(TARGET PanguModuleUnitTests BOUNDARY unit AREA module TIER fast)

add_executable(PanguModuleConcurrencyTests ScopeAccessConcurrencyTests.cpp)
target_link_libraries(PanguModuleConcurrencyTests PRIVATE PanguBuildOptions PanguModule ThirdParty::GoogleTestMain)
set_target_properties(PanguModuleConcurrencyTests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
pangu_register_gtest(TARGET PanguModuleConcurrencyTests BOUNDARY unit AREA module TIER precommit REQUIRES threads)

add_executable(PanguScopeBuildMockTests ScopeBuildMockTests.cpp)
target_link_libraries(PanguScopeBuildMockTests PRIVATE PanguBuildOptions PanguModule ThirdParty::GoogleMockMain)
set_target_properties(PanguScopeBuildMockTests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
pangu_register_gtest(TARGET PanguScopeBuildMockTests BOUNDARY unit AREA module TIER fast)

add_executable(PanguModuleIntegrationTests
    StaticModuleHostTests.cpp
    RegistrationTests.cpp
    ScopeLifecycleTests.cpp
)
target_link_libraries(PanguModuleIntegrationTests PRIVATE
    PanguBuildOptions PanguModule ThirdParty::GoogleTestMain)
set_target_properties(PanguModuleIntegrationTests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
pangu_register_gtest(TARGET PanguModuleIntegrationTests BOUNDARY integration AREA module TIER fast)

add_subdirectory(Fixtures)

add_executable(PanguScopeDestructorFixture ScopeDestructorFixture.cpp)
target_link_libraries(PanguScopeDestructorFixture PRIVATE PanguBuildOptions PanguModule)
set_target_properties(PanguScopeDestructorFixture PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
string(SHA256 _destructor_work "ScopeDestructor")
pangu_register_test(NAME ScopeDestructor COMMAND "${CMAKE_COMMAND}"
    "-DPANGU_SCOPE_TEST=$<TARGET_FILE:PanguScopeDestructorFixture>"
    "-DPANGU_TEST_ROOT=${CMAKE_BINARY_DIR}/Tests/Work/${_destructor_work}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/ScopeDestructorTests.cmake"
    BOUNDARY integration AREA module TIER precommit
    BUILD_TARGETS PanguScopeDestructorFixture REQUIRES subprocess)
