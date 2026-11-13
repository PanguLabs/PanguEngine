cmake_minimum_required(VERSION 4.0)

if(PANGU_ASSERT_CONFIG_CHILD)
    return()
endif()
set(argumentsFile "${CMAKE_CURRENT_BINARY_DIR}/ParentConfig.cmake")
file(WRITE "${argumentsFile}" "set(parentArgs)\n")
get_cmake_property(cacheNames CACHE_VARIABLES)
foreach(name IN LISTS cacheNames)
    if(name MATCHES "^CMAKE_(TOOLCHAIN_FILE|CXX_COMPILER|CXX_COMPILER_CLANG_SCAN_DEPS|MAKE_PROGRAM|SYSROOT|OSX_.*|ANDROID_.*|SYSTEM_NAME|SYSTEM_VERSION|SYSTEM_PROCESSOR|CXX_FLAGS.*|EXE_LINKER_FLAGS.*|SHARED_LINKER_FLAGS.*)$"
        OR name MATCHES "^PANGU_(META_SDK_CONFIG|LINK_MODE|LIBCXX_ROOT)$"
        OR name MATCHES "^ANDROID_(ABI|PLATFORM|NDK|STL)$")
        get_property(type CACHE "${name}" PROPERTY TYPE)
        get_property(value CACHE "${name}" PROPERTY VALUE)
        file(APPEND "${argumentsFile}" "list(APPEND parentArgs [==[-D${name}:${type}=${value}]==])\n")
    endif()
endforeach()
pangu_register_test(NAME LogAssertConfig
    COMMAND "${CMAKE_COMMAND}" "-DPANGU_SOURCE_ROOT=${PROJECT_SOURCE_DIR}"
        "-DPANGU_PARENT_CONFIG=${argumentsFile}"
        "-DPANGU_PARENT_CONTEXT=${CMAKE_BINARY_DIR}/Generated/BuildContext/CurrentContext.h"
        "-DPANGU_TEST_ROOT=${CMAKE_BINARY_DIR}/Tests/Work/LogAssertConfig"
        "-DPANGU_TEST_BUILD_JOBS=${PANGU_TEST_BUILD_JOBS}"
        "-DPANGU_META_HOST_BUILD_ROOT=${CMAKE_BINARY_DIR}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/AssertConfigTests.cmake"
    BOUNDARY system AREA foundation TIER precommit TYPES contract
    REQUIRES subprocess nested-build PROCESSORS "${PANGU_TEST_BUILD_JOBS}" TIMEOUT 600)

set_tests_properties(LogAssertConfig PROPERTIES RESOURCE_LOCK PanguMetaHostBuild)
