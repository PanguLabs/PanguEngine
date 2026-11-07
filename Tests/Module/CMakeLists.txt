cmake_minimum_required(VERSION 4.0)

include("${PROJECT_SOURCE_DIR}/cmake/PanguTestDependencies.cmake")
pangu_require_googletest(RTTI ON)
add_subdirectory(Support)

add_executable(PanguModuleUnitTests
    ModuleIdentityTests.cpp
    ScopeAccessTests.cpp
    ScopeBuildTests.cpp
    ScopeTypeTests.cpp
)

target_include_directories(PanguModuleUnitTests PRIVATE
    "${PROJECT_SOURCE_DIR}/Source/Runtime/Module/Private"
)
target_link_libraries(PanguModuleUnitTests PRIVATE PanguBuildOptions PanguModule PanguScopeTestSupport ThirdParty::GoogleTestMain)
set_target_properties(PanguModuleUnitTests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Bin"
)
pangu_register_gtest(TARGET PanguModuleUnitTests BOUNDARY unit AREA module TIER fast)

add_executable(PanguModuleConcurrencyTests ScopeAccessConcurrencyTests.cpp)
target_link_libraries(PanguModuleConcurrencyTests PRIVATE PanguBuildOptions PanguModule PanguScopeTestSupport ThirdParty::GoogleTestMain)
set_target_properties(PanguModuleConcurrencyTests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
pangu_register_gtest(TARGET PanguModuleConcurrencyTests BOUNDARY unit AREA module TIER precommit REQUIRES threads)

add_executable(PanguModuleUnloadTests ModuleCodePinTests.cpp)
target_include_directories(PanguModuleUnloadTests PRIVATE "${PROJECT_SOURCE_DIR}/Source/Runtime/Module/Private")
target_link_libraries(PanguModuleUnloadTests PRIVATE PanguBuildOptions PanguModule PanguScopeTestSupport ThirdParty::GoogleTestMain)
set_target_properties(PanguModuleUnloadTests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND PANGU_LINK_MODE STREQUAL "Modular")
    # 常驻 Factory 在 EXE 内参与公共类型 RTTI；ELF 还须将其公开符号放入动态符号表。
    set_target_properties(PanguModuleUnloadTests PROPERTIES ENABLE_EXPORTS ON)
endif()
pangu_register_gtest(TARGET PanguModuleUnloadTests BOUNDARY integration AREA module TIER precommit REQUIRES threads subprocess)

add_executable(PanguScopeBuildMockTests ScopeBuildMockTests.cpp)
target_link_libraries(PanguScopeBuildMockTests PRIVATE PanguBuildOptions PanguModule ThirdParty::GoogleMockMain)
set_target_properties(PanguScopeBuildMockTests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
pangu_register_gtest(TARGET PanguScopeBuildMockTests BOUNDARY unit AREA module TIER fast)

add_executable(PanguModuleIntegrationTests
    ModuleManagerTests.cpp
    ResidentModuleTests.cpp
    BatchLoadTests.cpp
)
if(PANGU_LINK_MODE STREQUAL "Monolithic")
    target_sources(PanguModuleIntegrationTests PRIVATE StaticModuleHostTests.cpp RegistrationTests.cpp ScopeLifecycleTests.cpp)
else()
    target_compile_definitions(PanguModuleIntegrationTests PRIVATE PG_TEST_MODULAR=1)
endif()
target_link_libraries(PanguModuleIntegrationTests PRIVATE
    PanguBuildOptions PanguModule PanguScopeTestSupport ThirdParty::GoogleTestMain)
set_target_properties(PanguModuleIntegrationTests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
pangu_register_gtest(TARGET PanguModuleIntegrationTests BOUNDARY integration AREA module TIER fast)

if(PANGU_LINK_MODE STREQUAL "Modular")
    add_subdirectory(Fixtures/Shared)
    target_include_directories(PanguModuleIntegrationTests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/Fixtures/Shared" "${CMAKE_CURRENT_BINARY_DIR}/Fixtures/Shared")
    add_dependencies(PanguModuleIntegrationTests PanguSharedProvider PanguSharedConsumer PanguMissingQuery PanguNullQuery)
    target_sources(PanguModuleIntegrationTests PRIVATE SharedScopeLifecycleTests.cpp)
    add_dependencies(PanguModuleIntegrationTests PanguScopeProvider PanguScopeConsumer)
    add_executable(PanguModuleLifetimeFixture ModuleLifetimeFixture.cpp)
    target_include_directories(PanguModuleLifetimeFixture PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/Fixtures/Shared" "${CMAKE_CURRENT_BINARY_DIR}/Fixtures/Shared")
    target_link_libraries(PanguModuleLifetimeFixture PRIVATE PanguBuildOptions PanguModule PanguScopeTestSupport)
    set_target_properties(PanguModuleLifetimeFixture PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
    add_dependencies(PanguModuleLifetimeFixture PanguScopeProvider)
    pangu_register_test(NAME ModuleLifetime COMMAND "${CMAKE_COMMAND}"
        "-DPANGU_MODULE_LIFETIME_TEST=$<TARGET_FILE:PanguModuleLifetimeFixture>"
        "-DPANGU_TEST_ROOT=${CMAKE_BINARY_DIR}/Tests/Work/ModuleLifetime"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/ModuleLifetimeTests.cmake"
        BOUNDARY integration AREA module TIER precommit
        BUILD_TARGETS PanguModuleLifetimeFixture REQUIRES subprocess)
    target_sources(PanguModuleUnloadTests PRIVATE ModuleUnloadTests.cpp ModuleUnloadConcurrencyTests.cpp)
    target_include_directories(PanguModuleUnloadTests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/Fixtures/Shared" "${CMAKE_CURRENT_BINARY_DIR}/Fixtures/Shared")
    add_dependencies(PanguModuleUnloadTests PanguSharedProvider PanguSharedConsumer PanguMissingQuery PanguNullQuery)
else()
    add_subdirectory(Fixtures)
endif()

if(PANGU_LINK_MODE STREQUAL "Monolithic")
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
endif()
