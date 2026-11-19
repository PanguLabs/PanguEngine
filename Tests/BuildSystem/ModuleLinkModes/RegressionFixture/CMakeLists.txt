cmake_minimum_required(VERSION 4.0)
project(
    PanguModuleRegression
    LANGUAGES
        C
        CXX
)
include(CTest)
set(PANGU_LINK_MODE Monolithic CACHE STRING "Module regression linkage" FORCE)
set(PANGU_BUILD_ENGINE_TESTS ON CACHE BOOL "Build Engine tests" FORCE)
set(PANGU_TEST_MODULE_MATRIX_CHILD ON CACHE BOOL "Prevent recursive ModuleRegression registration" FORCE)
list(PREPEND CMAKE_MODULE_PATH "${PANGU_SOURCE_ROOT}/cmake")
find_package(PanguEngine REQUIRED MODULE)
pangu_initialize()
