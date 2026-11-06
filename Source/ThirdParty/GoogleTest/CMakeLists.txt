cmake_minimum_required(VERSION 4.0)

if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    message(FATAL_ERROR "Pangu GoogleTest adapter requires Clang")
endif()
if(NOT PANGU_GOOGLETEST_RTTI STREQUAL "ON")
    message(FATAL_ERROR "Use pangu_require_googletest(RTTI ON); RTTI OFF is not supported")
endif()
find_package(Threads REQUIRED)
set(names gtest gtest_main gmock gmock_main)
set(sources googletest/src/gtest-all.cc googletest/src/gtest_main.cc
    googlemock/src/gmock-all.cc googlemock/src/gmock_main.cc)
set(aliases GoogleTest GoogleTestMain GoogleMock GoogleMockMain)
foreach(index RANGE 0 3)
    list(GET names ${index} name)
    list(GET sources ${index} source)
    list(GET aliases ${index} alias)
    add_library("${name}" STATIC "${CMAKE_CURRENT_SOURCE_DIR}/Repo/${source}")
    add_library("ThirdParty::${alias}" ALIAS "${name}")
    target_compile_features("${name}" PUBLIC cxx_std_23)
    target_compile_options("${name}" PRIVATE -fno-exceptions)
    target_compile_definitions("${name}" PUBLIC GTEST_HAS_EXCEPTIONS=0)
    target_compile_options("${name}" PRIVATE -frtti)
    target_compile_definitions("${name}" PUBLIC GTEST_HAS_RTTI=1)
    target_include_directories("${name}" SYSTEM PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/Repo/googletest/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/Repo/googlemock/include")
    target_include_directories("${name}" PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/Repo/googletest"
        "${CMAKE_CURRENT_SOURCE_DIR}/Repo/googlemock")
    target_link_libraries("${name}" PUBLIC Threads::Threads)
    set_target_properties("${name}" PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
        PANGU_TEST_DEPENDENCY_REVISION "063de7e9578f82b369302001269680b4b1553359"
        PANGU_TEST_RTTI "${PANGU_GOOGLETEST_RTTI}")
endforeach()
target_link_libraries(gtest_main PUBLIC gtest)
target_link_libraries(gmock PUBLIC gtest)
target_link_libraries(gmock_main PUBLIC gmock)
