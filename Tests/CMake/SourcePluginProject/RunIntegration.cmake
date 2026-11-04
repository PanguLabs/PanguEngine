cmake_minimum_required(VERSION 4.0)

if(NOT DEFINED PANGU_ENGINE_SOURCE_DIR OR NOT DEFINED PANGU_TEST_ROOT)
    message(FATAL_ERROR "SourcePluginProject requires PANGU_ENGINE_SOURCE_DIR and an isolated PANGU_TEST_ROOT")
endif()
set(projectFixture "${PANGU_ENGINE_SOURCE_DIR}/Tests/CMake/SourcePluginProject/MyGame")
if(NOT EXISTS "${projectFixture}/CMakeLists.txt")
    message(FATAL_ERROR "MyGame fixture source is missing: ${projectFixture}")
endif()
file(MAKE_DIRECTORY "${PANGU_TEST_ROOT}")
file(COPY "${projectFixture}" DESTINATION "${PANGU_TEST_ROOT}")
set(source "${PANGU_TEST_ROOT}/MyGame")
execute_process(COMMAND git -C "${PANGU_ENGINE_SOURCE_DIR}" rev-parse HEAD
    RESULT_VARIABLE revResult OUTPUT_VARIABLE engineHead OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT revResult EQUAL 0)
    message(FATAL_ERROR "Cannot resolve current Engine HEAD for MyGame fixture")
endif()
file(WRITE "${source}/Engine.commit" "${engineHead}\n")

if(CMAKE_HOST_WIN32)
    set(compiler "$ENV{LLVM_ROOT}/bin/clang++.exe")
    set(compilerArgs "-DCMAKE_CXX_COMPILER:FILEPATH=${compiler}")
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
    set(compilerArgs "-DCMAKE_TOOLCHAIN_FILE:FILEPATH=${PANGU_ENGINE_SOURCE_DIR}/cmake/Platform/Linux/Toolchain.cmake")
else()
    set(compilerArgs "-DCMAKE_TOOLCHAIN_FILE:FILEPATH=${PANGU_ENGINE_SOURCE_DIR}/cmake/Platform/macOS/Toolchain.cmake")
endif()

function(ConfigureProject name testing engineTests)
    set(binary "${PANGU_TEST_ROOT}/${name}")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -S "${source}" -B "${binary}" -G Ninja
            "${compilerArgs}"
            "-DPANGU_ENGINE_SOURCE_DIR:PATH=${PANGU_ENGINE_SOURCE_DIR}"
            "-DBUILD_TESTING:BOOL=${testing}"
            "-DPANGU_BUILD_ENGINE_TESTS:BOOL=${engineTests}"
            "-DPANGU_ENGINE_ALLOW_LOCAL_OVERRIDE:BOOL=ON"
        RESULT_VARIABLE rc OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "MyGame configure ${name} failed: ${rc}\n${output}\n${error}")
    endif()
    file(READ "${binary}/CMakeCache.txt" cache)
    if(NOT cache MATCHES "CMAKE_PROJECT_NAME:STATIC=MyGame")
        message(FATAL_ERROR "MyGame must own top-level CMake project: ${name}")
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" --build "${binary}" --target MyGameProbe
        RESULT_VARIABLE rc OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "MyGameProbe build ${name} failed: ${rc}\n${output}\n${error}")
    endif()
    execute_process(COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${binary}" -N
        RESULT_VARIABLE rc OUTPUT_VARIABLE listed ERROR_VARIABLE errors)
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "MyGame CTest query failed: ${rc}\n${errors}")
    endif()
    set("${name}_CTEST_LIST" "${listed}" PARENT_SCOPE)
endfunction()

ConfigureProject(Enabled ON OFF)
if(NOT Enabled_CTEST_LIST MATCHES "Total Tests: 1" OR
   Enabled_CTEST_LIST MATCHES "ModuleIdentity|StaticModuleCatalog")
    message(FATAL_ERROR "Project CTest must contain only MyGameProbe by default:\n${Enabled_CTEST_LIST}")
endif()
execute_process(COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${PANGU_TEST_ROOT}/Enabled"
    -R "^MyGameProbe$" --output-on-failure RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "MyGameProbe standalone CTest failed")
endif()

ConfigureProject(Disabled OFF OFF)
if(NOT Disabled_CTEST_LIST MATCHES "Total Tests: 0")
    message(FATAL_ERROR "Project BUILD_TESTING=OFF must register no tests:\n${Disabled_CTEST_LIST}")
endif()

ConfigureProject(EngineOptIn ON ON)
if(NOT EngineOptIn_CTEST_LIST MATCHES "ModuleIdentity")
    message(FATAL_ERROR "Project explicit PANGU_BUILD_ENGINE_TESTS=ON should register Engine tests")
endif()

file(WRITE "${source}/Engine.commit" "invalid-new-lock\n")
execute_process(COMMAND "${CMAKE_COMMAND}" --build "${PANGU_TEST_ROOT}/Enabled" --target MyGameProbe
    RESULT_VARIABLE lockStatus OUTPUT_VARIABLE lockOutput ERROR_VARIABLE lockError)
if(lockStatus EQUAL 0 OR NOT "${lockOutput}\n${lockError}" MATCHES "Engine[.]commit")
    message(FATAL_ERROR "Changing only Engine.commit must trigger lock revalidation: ${lockStatus}\n${lockOutput}\n${lockError}")
endif()
file(WRITE "${source}/Engine.commit" "${engineHead}\n")
message(STATUS "SourcePluginProject: Engine standalone / Project top-level ownership, CTest scopes and lock invalidation passed")
