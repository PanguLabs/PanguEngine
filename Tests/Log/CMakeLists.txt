cmake_minimum_required(VERSION 4.0)

add_subdirectory(Fixtures)

include("${PROJECT_SOURCE_DIR}/cmake/PanguTestDependencies.cmake")
pangu_require_googletest(RTTI ON)
add_executable(PanguLogUnitTests ConfigTests.cpp FormatTests.cpp WriterTests.cpp QueueTests.cpp)
target_sources(PanguLogUnitTests PRIVATE InitializationTests.cpp LifecycleTests.cpp DrainTests.cpp)
target_compile_definitions(PanguLogUnitTests PRIVATE PG_LOG_INIT_TEST_ROOT="${CMAKE_CURRENT_BINARY_DIR}/InitializationCases")
target_sources(PanguLogUnitTests PRIVATE "${PROJECT_SOURCE_DIR}/Source/Runtime/Log/Private/SpdlogBackend.cpp")
target_include_directories(PanguLogUnitTests PRIVATE "${PROJECT_SOURCE_DIR}/Source/Runtime/Log/Private")
target_link_libraries(PanguLogUnitTests PRIVATE PanguBuildOptions Pangu::Log ThirdParty::Spdlog ThirdParty::GoogleTestMain)
set_target_properties(PanguLogUnitTests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
pangu_register_gtest(TARGET PanguLogUnitTests BOUNDARY unit AREA log TIER fast TYPES contract)

add_executable(PanguLogConcurrencyTests WriterConcurrencyTests.cpp DrainConcurrencyTests.cpp)
target_include_directories(PanguLogConcurrencyTests PRIVATE "${PROJECT_SOURCE_DIR}/Source/Runtime/Log/Private")
target_link_libraries(PanguLogConcurrencyTests PRIVATE PanguBuildOptions Pangu::Log ThirdParty::GoogleTestMain)
set_target_properties(PanguLogConcurrencyTests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
pangu_register_gtest(TARGET PanguLogConcurrencyTests BOUNDARY integration AREA log TIER precommit TYPES contract REQUIRES threads)

file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/FormatCompileSettings.cmake" CONTENT
"set(FORMAT_COMPILER [==[${CMAKE_CXX_COMPILER}]==])
set(FORMAT_FLAGS [==[${CMAKE_CXX_FLAGS}]==])
set(FORMAT_SYSROOT [==[${CMAKE_OSX_SYSROOT}]==])
set(FORMAT_ARCH [==[${CMAKE_OSX_ARCHITECTURES}]==])
set(FORMAT_INCLUDES [==[$<TARGET_PROPERTY:PanguLog,INTERFACE_INCLUDE_DIRECTORIES>;${PROJECT_SOURCE_DIR}/Source/ThirdParty/Fmt/Repo/include]==])
")
pangu_register_test(NAME LogFormatCompile
    COMMAND "${CMAKE_COMMAND}"
        "-DFORMAT_SETTINGS=${CMAKE_CURRENT_BINARY_DIR}/FormatCompileSettings.cmake"
        "-DFORMAT_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/Fixtures/FormatCompile"
        "-DFORMAT_BINARY=${CMAKE_CURRENT_BINARY_DIR}/FormatCompile"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/FormatCompileTests.cmake"
    BUILD_TARGETS PanguLogUnitTests BOUNDARY unit AREA log TIER fast TYPES contract)
add_executable(PanguLogBackendTests BackendTests.cpp)
include("${CMAKE_CURRENT_SOURCE_DIR}/NoAllocationFixture.cmake")
pangu_add_log_allocation_fixture("${PROJECT_SOURCE_DIR}" "${CMAKE_BINARY_DIR}")
target_sources(PanguLogBackendTests PRIVATE "${PROJECT_SOURCE_DIR}/Source/Runtime/Log/Private/SpdlogBackend.cpp")
target_include_directories(PanguLogBackendTests PRIVATE "${PROJECT_SOURCE_DIR}/Source/Runtime/Log/Private"
    "${PROJECT_SOURCE_DIR}/Source/Runtime/Log/Public")
target_link_libraries(PanguLogBackendTests PRIVATE PanguBuildOptions Pangu::Foundation ThirdParty::Spdlog)
set_target_properties(PanguLogBackendTests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
target_link_libraries(PanguLogBackendTests PRIVATE ThirdParty::GoogleTestMain)

pangu_register_gtest(TARGET PanguLogBackendTests BOUNDARY unit AREA log TIER precommit TYPES contract)
pangu_register_test(NAME LogNoAllocation
    COMMAND "${CMAKE_COMMAND}" "-DFIXTURE=$<TARGET_FILE:PanguLogNoAllocationFixture>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/RunNoAllocation.cmake"
    BUILD_TARGETS PanguLogNoAllocationFixture BOUNDARY integration AREA log TIER precommit TYPES contract REQUIRES subprocess)

add_executable(PanguLogFileTests FileStoreTests.cpp OutputTests.cpp)
add_executable(PanguLogFileStoreFixture FileStoreFixture.cpp)
foreach(target IN ITEMS PanguLogFileTests PanguLogFileStoreFixture)
    target_include_directories(${target} PRIVATE "${PROJECT_SOURCE_DIR}/Source/Runtime/Log/Private")
    target_link_libraries(${target} PRIVATE PanguBuildOptions Pangu::Log ThirdParty::Spdlog)
    set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
endforeach()
target_sources(PanguLogFileTests PRIVATE "${PROJECT_SOURCE_DIR}/Source/Runtime/Log/Private/SpdlogBackend.cpp")
target_link_libraries(PanguLogFileTests PRIVATE ThirdParty::GoogleTestMain)
target_compile_definitions(PanguLogFileTests PRIVATE PG_LOG_FILE_TEST_ROOT="${CMAKE_CURRENT_BINARY_DIR}/FileStoreCases")
pangu_register_gtest(TARGET PanguLogFileTests BOUNDARY integration AREA log TIER precommit TYPES contract REQUIRES subprocess)
pangu_register_test(NAME LogFileStoreProcesses
    COMMAND "${CMAKE_COMMAND}" "-DFIXTURE=$<TARGET_FILE:PanguLogFileStoreFixture>"
        "-DWORK_ROOT=${CMAKE_CURRENT_BINARY_DIR}/FileStoreProcesses"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/RunFileStore.cmake"
    BUILD_TARGETS PanguLogFileStoreFixture BOUNDARY integration AREA log TIER precommit TYPES contract REQUIRES subprocess)

add_executable(PanguLogInitializationFixture InitializationFixture.cpp)
target_compile_definitions(PanguLogInitializationFixture PRIVATE PG_LOG_INIT_TEST_ROOT="${CMAKE_CURRENT_BINARY_DIR}/InitializationCases")
target_include_directories(PanguLogInitializationFixture PRIVATE "${PROJECT_SOURCE_DIR}/Source/Runtime/Log/Private")
target_link_libraries(PanguLogInitializationFixture PRIVATE PanguBuildOptions Pangu::Log)
set_target_properties(PanguLogInitializationFixture PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
pangu_register_test(NAME LogInitializationFailures
    COMMAND "${CMAKE_COMMAND}" "-DFIXTURE=$<TARGET_FILE:PanguLogInitializationFixture>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/RunInitialization.cmake"
    BUILD_TARGETS PanguLogInitializationFixture BOUNDARY integration AREA log TIER precommit TYPES contract REQUIRES subprocess threads)

add_executable(PanguLogDrainFixture DrainFixture.cpp)
target_include_directories(PanguLogDrainFixture PRIVATE "${PROJECT_SOURCE_DIR}/Source/Runtime/Log/Private")
target_link_libraries(PanguLogDrainFixture PRIVATE PanguBuildOptions Pangu::Log)
set_target_properties(PanguLogDrainFixture PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
pangu_register_test(NAME LogDrainProcesses
    COMMAND "${CMAKE_COMMAND}" "-DFIXTURE=$<TARGET_FILE:PanguLogDrainFixture>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/DrainTests.cmake"
    BUILD_TARGETS PanguLogDrainFixture BOUNDARY integration AREA log TIER precommit TYPES contract REQUIRES subprocess threads)

add_executable(PanguLogLifetimeFixture LifetimeFixture.cpp)
target_compile_definitions(PanguLogLifetimeFixture PRIVATE PG_LOG_NATIVE_MARKER="${CMAKE_CURRENT_BINARY_DIR}/Lifetime/native-unloaded")
target_link_libraries(PanguLogLifetimeFixture PRIVATE PanguBuildOptions Pangu::EngineHost ThirdParty::GoogleTestMain)
target_include_directories(PanguLogLifetimeFixture PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/Fixtures"
    "${PROJECT_SOURCE_DIR}/Source/Runtime/EngineHost/Private" "${PROJECT_SOURCE_DIR}/Source/Runtime/Log/Private")
target_compile_definitions(PanguLogLifetimeFixture PRIVATE $<$<STREQUAL:${PANGU_LINK_MODE},Modular>:PG_TEST_MODULAR=1>)
add_dependencies(PanguLogLifetimeFixture PanguLogProducer)
if(NOT PANGU_LINK_MODE STREQUAL "Modular")
    target_link_libraries(PanguLogLifetimeFixture PRIVATE PanguLogProducer)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set_target_properties(PanguLogLifetimeFixture PROPERTIES ENABLE_EXPORTS ON)
endif()
set_target_properties(PanguLogLifetimeFixture PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
pangu_register_test(NAME LogLifetime
    COMMAND "${CMAKE_COMMAND}" "-DFIXTURE=$<TARGET_FILE:PanguLogLifetimeFixture>"
        "-DWORK_ROOT=${CMAKE_CURRENT_BINARY_DIR}/Lifetime" -P "${CMAKE_CURRENT_SOURCE_DIR}/LifetimeTests.cmake"
    BUILD_TARGETS PanguLogLifetimeFixture BOUNDARY integration AREA log TIER precommit TYPES contract REQUIRES subprocess threads)

add_executable(PanguLogRuntimeNoAllocationFixture RuntimeNoAllocationFixture.cpp)
target_link_libraries(PanguLogRuntimeNoAllocationFixture PRIVATE PanguBuildOptions Pangu::Log)
target_include_directories(PanguLogRuntimeNoAllocationFixture PRIVATE "${PROJECT_SOURCE_DIR}/Source/Runtime/Log/Private")
target_compile_definitions(PanguLogRuntimeNoAllocationFixture PRIVATE PG_LOG_RUNTIME_ROOT="${CMAKE_CURRENT_BINARY_DIR}/RuntimeNoAllocation")
set_target_properties(PanguLogRuntimeNoAllocationFixture PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
pangu_register_test(NAME LogRuntimeNoAllocation
    COMMAND "${CMAKE_COMMAND}" "-DFIXTURE=$<TARGET_FILE:PanguLogRuntimeNoAllocationFixture>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/RuntimeNoAllocationTests.cmake"
    BUILD_TARGETS PanguLogRuntimeNoAllocationFixture BOUNDARY integration AREA log TIER precommit TYPES contract REQUIRES subprocess threads)
