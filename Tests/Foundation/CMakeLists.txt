cmake_minimum_required(VERSION 4.0)

include("${PROJECT_SOURCE_DIR}/cmake/PanguTestDependencies.cmake")
pangu_require_googletest(RTTI ON)
add_executable(PanguFoundationUnitTests
    ErrorTests.cpp
)

target_link_libraries(PanguFoundationUnitTests PRIVATE PanguBuildOptions Pangu::Foundation ThirdParty::GoogleTestMain)
set_target_properties(PanguFoundationUnitTests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Bin"
)
pangu_register_gtest(TARGET PanguFoundationUnitTests BOUNDARY unit AREA foundation TIER fast)
