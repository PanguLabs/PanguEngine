cmake_minimum_required(VERSION 4.0)

include("${PROJECT_SOURCE_DIR}/cmake/PanguTestDependencies.cmake")
pangu_require_googletest(RTTI ON)
add_executable(PanguFoundationUnitTests
    ErrorTests.cpp
    FailureReportTests.cpp
    MetaIdentityTests.cpp
    MetaTraitsTests.cpp
)

target_link_libraries(PanguFoundationUnitTests PRIVATE PanguBuildOptions Pangu::Foundation ThirdParty::GoogleTestMain)
set_target_properties(PanguFoundationUnitTests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Bin"
)
pangu_register_gtest(TARGET PanguFoundationUnitTests BOUNDARY unit AREA foundation TIER fast)

# 同一实现直接编入受控分配测试程序，覆盖 DLL 内部不能由 EXE new 替换观测的路径。
add_executable(PanguFailureReportNoAllocationFixture FailureReportTests.cpp
    "${PROJECT_SOURCE_DIR}/Source/Runtime/Foundation/Private/FailureReport.cpp")
target_compile_definitions(PanguFailureReportNoAllocationFixture PRIVATE
    PG_FAILURE_REPORT_ALLOCATION_TEST PanguFoundation_EXPORTS)
target_include_directories(PanguFailureReportNoAllocationFixture PRIVATE
    "$<TARGET_PROPERTY:PanguFoundation,INTERFACE_INCLUDE_DIRECTORIES>")
target_link_libraries(PanguFailureReportNoAllocationFixture PRIVATE PanguBuildOptions)
set_target_properties(PanguFailureReportNoAllocationFixture PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
pangu_register_test(NAME FailureReportNoAllocation
    COMMAND "$<TARGET_FILE:PanguFailureReportNoAllocationFixture>"
    BUILD_TARGETS PanguFailureReportNoAllocationFixture BOUNDARY unit AREA foundation TIER fast TYPES contract)

add_executable(PanguFoundationFatalFixture FatalFixture.cpp)
target_link_libraries(PanguFoundationFatalFixture PRIVATE PanguBuildOptions Pangu::Foundation)
set_target_properties(PanguFoundationFatalFixture PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
pangu_register_test(NAME FoundationFatal
    COMMAND "${CMAKE_COMMAND}"
        "-DPANGU_FATAL_FIXTURE=$<TARGET_FILE:PanguFoundationFatalFixture>"
        "-DPANGU_ASSERTS=${PANGU_ENABLE_ASSERTS}"
        "-DPANGU_REENTRY_FIXTURE=$<TARGET_FILE:PanguFoundationFatalReentryFixture>"
        "-DPANGU_TEST_ROOT=${CMAKE_BINARY_DIR}/Tests/Work/FoundationFatal"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/FatalTests.cmake"
    BUILD_TARGETS PanguFoundationFatalFixture BOUNDARY integration AREA foundation TIER precommit
    TYPES contract REQUIRES subprocess threads TIMEOUT 90)

# 仅在测试对象中重命名原生终止符号，接缝位于独立夹具，不改变产品目标。
if(WIN32)
    set(panguEmergencySource Private/Platform/Windows/Emergency.cpp)
else()
    set(panguEmergencySource Private/Platform/Posix/Emergency.cpp)
endif()
add_library(PanguFatalNativeTestTermination OBJECT
    "${PROJECT_SOURCE_DIR}/Source/Runtime/Foundation/${panguEmergencySource}")
target_compile_definitions(PanguFatalNativeTestTermination PRIVATE EmergencyTerminate=NativeEmergencyTerminate)
target_include_directories(PanguFatalNativeTestTermination PRIVATE
    "${PROJECT_SOURCE_DIR}/Source/Runtime/Foundation/Private")
target_link_libraries(PanguFatalNativeTestTermination PRIVATE PanguBuildOptions)
add_executable(PanguFoundationFatalReentryFixture FatalReentryFixture.cpp
    "${PROJECT_SOURCE_DIR}/Source/Runtime/Foundation/Private/Fatal.cpp"
    $<TARGET_OBJECTS:PanguFatalNativeTestTermination>)
target_compile_definitions(PanguFoundationFatalReentryFixture PRIVATE PanguFoundation_EXPORTS
    "$<TARGET_PROPERTY:PanguFoundation,INTERFACE_COMPILE_DEFINITIONS>")
target_include_directories(PanguFoundationFatalReentryFixture PRIVATE
    "$<TARGET_PROPERTY:PanguFoundation,INTERFACE_INCLUDE_DIRECTORIES>"
    "${PROJECT_SOURCE_DIR}/Source/Runtime/Foundation/Private")
target_link_libraries(PanguFoundationFatalReentryFixture PRIVATE PanguBuildOptions)
set_target_properties(PanguFoundationFatalReentryFixture PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
add_dependencies(PanguFoundationFatalFixture PanguFoundationFatalReentryFixture)
