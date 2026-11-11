cmake_minimum_required(VERSION 4.0)

if(TARGET PanguMinimalHost)
    set(minimalHostArguments)
    if(PANGU_LINK_MODE STREQUAL "Modular")
        list(APPEND minimalHostArguments "$<TARGET_FILE:PanguMinimalServices>")
    endif()
    pangu_register_test(NAME MinimalHostSmoke COMMAND PanguMinimalHost ${minimalHostArguments}
        BOUNDARY system AREA engine-host TIER precommit BUILD_TARGETS PanguMinimalHost PanguMinimalServices)
endif()

include("${PROJECT_SOURCE_DIR}/cmake/PanguTestDependencies.cmake")
pangu_require_googletest(RTTI ON)
add_subdirectory(Support)
add_subdirectory(Fixtures)
add_executable(PanguEngineHostTests EngineHostStartupTests.cpp EngineHostFailureTests.cpp EngineHostShutdownTests.cpp
    EngineHostScopeTests.cpp EngineHostServiceTests.cpp EngineHostLifetimeTests.cpp EngineHostMetadataTests.cpp)
target_link_libraries(PanguEngineHostTests PRIVATE PanguBuildOptions PanguEngineHost ThirdParty::GoogleTestMain)
target_link_libraries(PanguEngineHostTests PRIVATE PanguHostTestSupport PanguManualMetaFixture)
target_include_directories(PanguEngineHostTests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Fixtures" "${CMAKE_CURRENT_BINARY_DIR}/Fixtures"
    "${PROJECT_SOURCE_DIR}/Source/Runtime/EngineHost/Private"
    "${PROJECT_SOURCE_DIR}/Source/Runtime/Module/Private")
add_dependencies(PanguEngineHostTests PanguHostServiceFixture PanguHostConsumerFixture)
if(NOT PANGU_LINK_MODE STREQUAL "Modular")
    target_link_libraries(PanguEngineHostTests PRIVATE PanguHostServiceFixture PanguHostConsumerFixture)
endif()
set_target_properties(PanguEngineHostTests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
if(PANGU_LINK_MODE STREQUAL "Modular")
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set_target_properties(PanguEngineHostTests PROPERTIES ENABLE_EXPORTS ON)
    endif()
    target_compile_definitions(PanguEngineHostTests PRIVATE PG_TEST_MODULAR=1)
    target_include_directories(PanguEngineHostTests PRIVATE
        "${PROJECT_SOURCE_DIR}/Tests/Module/Fixtures/Shared"
        "${CMAKE_CURRENT_BINARY_DIR}/../Module/Fixtures/Shared")
    target_link_libraries(PanguEngineHostTests PRIVATE PanguScopeTestSupport)
    add_dependencies(PanguEngineHostTests PanguSharedProvider)
endif()
pangu_register_gtest(TARGET PanguEngineHostTests BOUNDARY integration AREA engine-host TIER fast)

add_executable(PanguEngineHostDestructorFixture EngineHostDestructorFixture.cpp)
target_link_libraries(PanguEngineHostDestructorFixture PRIVATE PanguBuildOptions PanguEngineHost PanguHostTestSupport)
target_include_directories(PanguEngineHostDestructorFixture PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Fixtures" "${CMAKE_CURRENT_BINARY_DIR}/Fixtures"
    "${PROJECT_SOURCE_DIR}/Source/Runtime/EngineHost/Private"
    "${PROJECT_SOURCE_DIR}/Source/Runtime/Module/Private")
target_compile_definitions(PanguEngineHostDestructorFixture PRIVATE $<$<STREQUAL:${PANGU_LINK_MODE},Modular>:PG_TEST_MODULAR=1>)
add_dependencies(PanguEngineHostDestructorFixture PanguHostServiceFixture)
if(NOT PANGU_LINK_MODE STREQUAL "Modular")
    target_link_libraries(PanguEngineHostDestructorFixture PRIVATE PanguHostServiceFixture)
endif()
set_target_properties(PanguEngineHostDestructorFixture PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
pangu_register_test(NAME EngineHostDestructor BOUNDARY integration AREA engine-host TIER precommit
    REQUIRES subprocess BUILD_TARGETS PanguEngineHostDestructorFixture
    COMMAND "${CMAKE_COMMAND}" "-DFIXTURE=$<TARGET_FILE:PanguEngineHostDestructorFixture>"
        "-DMARKER_ROOT=${CMAKE_CURRENT_BINARY_DIR}/DestructorMarkers"
        "-DMODULAR=$<STREQUAL:${PANGU_LINK_MODE},Modular>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/EngineHostDestructorTests.cmake")

add_executable(PanguEngineHostConcurrencyTests EngineHostConcurrencyTests.cpp)
target_link_libraries(PanguEngineHostConcurrencyTests PRIVATE
    PanguBuildOptions PanguEngineHost PanguHostTestSupport ThirdParty::GoogleTestMain)
target_include_directories(PanguEngineHostConcurrencyTests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Fixtures" "${CMAKE_CURRENT_BINARY_DIR}/Fixtures")
target_compile_definitions(PanguEngineHostConcurrencyTests PRIVATE
    $<$<STREQUAL:${PANGU_LINK_MODE},Modular>:PG_TEST_MODULAR=1>)
add_dependencies(PanguEngineHostConcurrencyTests PanguHostServiceFixture)
if(NOT PANGU_LINK_MODE STREQUAL "Modular")
    target_link_libraries(PanguEngineHostConcurrencyTests PRIVATE PanguHostServiceFixture)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set_target_properties(PanguEngineHostConcurrencyTests PROPERTIES ENABLE_EXPORTS ON)
endif()
set_target_properties(PanguEngineHostConcurrencyTests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
pangu_register_gtest(TARGET PanguEngineHostConcurrencyTests BOUNDARY integration AREA engine-host TIER precommit
    REQUIRES threads)

if(PANGU_LINK_MODE STREQUAL "Modular")
    add_executable(PanguEngineHostLifetimeFixture EngineHostLifetimeFixture.cpp)
    target_link_libraries(PanguEngineHostLifetimeFixture PRIVATE PanguBuildOptions PanguEngineHost PanguHostTestSupport)
    target_include_directories(PanguEngineHostLifetimeFixture PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/Fixtures" "${CMAKE_CURRENT_BINARY_DIR}/Fixtures")
    target_compile_definitions(PanguEngineHostLifetimeFixture PRIVATE PG_TEST_MODULAR=1)
    add_dependencies(PanguEngineHostLifetimeFixture PanguHostServiceFixture)
    set_target_properties(PanguEngineHostLifetimeFixture PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set_target_properties(PanguEngineHostLifetimeFixture PROPERTIES ENABLE_EXPORTS ON)
    endif()
    pangu_register_test(NAME EngineHostLifetime BOUNDARY integration AREA engine-host TIER precommit
        REQUIRES subprocess BUILD_TARGETS PanguEngineHostLifetimeFixture
        COMMAND "${CMAKE_COMMAND}" "-DFIXTURE=$<TARGET_FILE:PanguEngineHostLifetimeFixture>"
            "-DMARKER_ROOT=${CMAKE_CURRENT_BINARY_DIR}/LifetimeMarkers"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/EngineHostLifetimeTests.cmake")
endif()
