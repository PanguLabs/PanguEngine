cmake_minimum_required(VERSION 4.0)

include("${PROJECT_SOURCE_DIR}/cmake/PanguTestDependencies.cmake")
pangu_require_googletest(RTTI ON)

add_executable(PanguEcsWorldTests EcsWorldTests.cpp)
target_link_libraries(PanguEcsWorldTests PRIVATE ThirdParty::GoogleTestMain)
add_executable(PanguEcsRegistrationFailureFixture EcsRegistrationFailureFixture.cpp)

foreach(target IN ITEMS PanguEcsWorldTests PanguEcsRegistrationFailureFixture)
    target_link_libraries(
        ${target}
        PRIVATE
            PanguBuildOptions
            Pangu::ECS
    )
    set_target_properties(
        ${target}
        PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY
                "${CMAKE_CURRENT_BINARY_DIR}/Bin"
    )
    pangu_stage_host_runtime(TARGET ${target})
endforeach()

pangu_register_gtest(TARGET PanguEcsWorldTests AREA ecs BOUNDARY integration TIER fast)
pangu_register_test(
    NAME FlecsMissingComponentRegistration
    AREA ecs
    BOUNDARY integration
    TIER precommit
    TYPES
        contract
    REQUIRES
        subprocess
    BUILD_TARGETS
        PanguEcsRegistrationFailureFixture
    COMMAND
        "${CMAKE_COMMAND}"
        "-DFIXTURE=$<TARGET_FILE:PanguEcsRegistrationFailureFixture>"
        -P
        "${CMAKE_CURRENT_SOURCE_DIR}/FlecsMissingComponentRegistration.cmake"
)
