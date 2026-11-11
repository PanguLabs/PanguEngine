cmake_minimum_required(VERSION 4.0)

function(pangu_configure_meta_fixture_host target)
    target_link_libraries(${target} PRIVATE PanguBuildOptions PanguEngineHost PanguHostTestSupport)
    target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/Fixtures")
    pangu_meta_consume(TARGET ${target} MODULE PanguMetaFixture HEADERS MetaActor.h EMIT both)
    pangu_meta_consume(TARGET ${target} MODULE PanguMetaServiceFixture HEADERS MetaService.h EMIT both)
    pangu_meta_host_catalog(TARGET ${target} MODULES PanguMetaFixture PanguMetaServiceFixture)
    set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
    if(PANGU_LINK_MODE STREQUAL "Modular")
        target_compile_definitions(${target} PRIVATE PG_TEST_MODULAR=1)
        add_dependencies(${target} PanguMetaFixture PanguMetaServiceFixture)
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            set_target_properties(${target} PROPERTIES ENABLE_EXPORTS ON)
        endif()
    else()
        target_link_libraries(${target} PRIVATE PanguMetaFixture PanguMetaServiceFixture)
    endif()
endfunction()
add_executable(PanguMetaIntegrationTests MetaIntegrationTests.cpp MetaUnloadTests.cpp)
pangu_configure_meta_fixture_host(PanguMetaIntegrationTests)
target_link_libraries(PanguMetaIntegrationTests PRIVATE ThirdParty::GoogleTestMain)
pangu_register_gtest(TARGET PanguMetaIntegrationTests BOUNDARY integration AREA meta TIER precommit)

add_executable(PanguMetaLifetimeFixture MetaLifetimeFixture.cpp)
pangu_configure_meta_fixture_host(PanguMetaLifetimeFixture)
pangu_register_test(NAME MetaLifetime BOUNDARY integration AREA meta TIER precommit REQUIRES subprocess
    BUILD_TARGETS PanguMetaLifetimeFixture
    COMMAND "${CMAKE_COMMAND}" "-DFIXTURE=$<TARGET_FILE:PanguMetaLifetimeFixture>"
        "-DMARKER_ROOT=${CMAKE_CURRENT_BINARY_DIR}/LifetimeMarkers"
        "-DMODULAR=$<STREQUAL:${PANGU_LINK_MODE},Modular>" -P "${CMAKE_CURRENT_SOURCE_DIR}/MetaLifetimeTests.cmake")

option(PANGU_BUILD_META_BENCHMARKS "Build generated metadata access benchmarks" OFF)
if(PANGU_BUILD_META_BENCHMARKS)
    pangu_require_googlebenchmark()
    add_executable(PanguMetaAccessBenchmarks Benchmarks/MetaAccessBenchmarks.cpp)
    pangu_configure_meta_fixture_host(PanguMetaAccessBenchmarks)
    target_link_libraries(PanguMetaAccessBenchmarks PRIVATE ThirdParty::GoogleBenchmarkMain)
endif()
