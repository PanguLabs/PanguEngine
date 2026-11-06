cmake_minimum_required(VERSION 4.0)
include("${CMAKE_CURRENT_LIST_DIR}/TestingTestHelpers.cmake")
set(fixture "${CMAKE_CURRENT_LIST_DIR}/Fixtures/Workflow")
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
    set(platform windows)
    set(platformDirectory Windows)
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
    set(platform macos)
    set(platformDirectory macOS)
else()
    set(platform linux)
    set(platformDirectory Linux)
endif()

# 每轮只重置本回归拥有的夹具，不触碰 Engine 正式测试产物。
set(source "${PANGU_TEST_ROOT}/Workflow Source")
set(binary "${PANGU_TEST_ROOT}/Workflow Build")
file(REMOVE_RECURSE "${source}" "${binary}")
file(MAKE_DIRECTORY "${source}")
configure_file("${fixture}/CMakeLists.txt" "${source}/CMakeLists.txt" COPYONLY)
foreach(tier IN ITEMS Fast Full)
    configure_file("${CMAKE_CURRENT_LIST_DIR}/Fixtures/Registration/SampleTests.cpp"
        "${source}/Sample${tier}.cpp" COPYONLY)
endforeach()
set(fixtureToolchain "")
if(NOT CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
    set(fixtureToolchain "\"toolchainFile\": \"${PANGU_ENGINE_ROOT}/cmake/Platform/${platformDirectory}/Toolchain.cmake\",")
endif()
configure_file("${fixture}/CMakePresets.json.in" "${source}/CMakePresets.json" @ONLY)

TestingRun(EXPECT pass COMMAND "${CMAKE_COMMAND}" -E chdir "${source}"
    "${CMAKE_COMMAND}" --workflow --preset fixture-fast)
if(NOT Testing_OUTPUT MATCHES "100% tests passed" OR Testing_OUTPUT MATCHES "FixtureFull[.]Registration")
    message(FATAL_ERROR "fast must run built cases without accepting unbuilt full placeholders")
endif()
TestingRun(EXPECT fail DIAGNOSTIC "No tests were found|No tests" COMMAND "${CMAKE_COMMAND}" -E chdir "${source}"
    "${CMAKE_CTEST_COMMAND}" --preset fixture-fast -R "^MissingSelection$")
TestingRun(EXPECT fail DIAGNOSTIC "FixtureFull_NOT_BUILT|Unable to find executable|Not Run" COMMAND "${CMAKE_COMMAND}" -E chdir "${source}"
    "${CMAKE_CTEST_COMMAND}" --preset fixture-full)
TestingRun(EXPECT pass COMMAND "${CMAKE_COMMAND}" -E chdir "${source}"
    "${CMAKE_COMMAND}" --workflow --preset fixture-full)

# 修改真实编译输入；若 workflow 只运行旧程序，下面会错误地返回成功。
file(READ "${source}/SampleFast.cpp" sample)
string(REPLACE "EXPECT_EQ(value, \"pass\")" "EXPECT_EQ(value, \"stale\")" stale "${sample}")
if(stale STREQUAL sample)
    message(FATAL_ERROR "Registration fixture assertion changed; update the stale-source mutation")
endif()
file(WRITE "${source}/SampleFast.cpp" "${stale}")
TestingRun(EXPECT fail DIAGNOSTIC "FixtureFast[.]Registration[.]Second.*Failed|Registration.Second.*FAILED"
    COMMAND "${CMAKE_COMMAND}" -E chdir "${source}" "${CMAKE_COMMAND}" --workflow --preset fixture-fast)
file(WRITE "${source}/SampleFast.cpp" "${sample}")

foreach(option IN ITEMS BUILD_TESTING PANGU_BUILD_ENGINE_TESTS)
    TestingRun(EXPECT pass COMMAND "${CMAKE_COMMAND}" -S "${source}" -B "${binary}" "-D${option}=OFF")
    TestingRun(EXPECT fail DIAGNOSTIC "Engine full requires ${option}=ON"
        COMMAND "${CMAKE_COMMAND}" --build "${binary}" --target PanguTestsFull --parallel 1)
    TestingRun(EXPECT pass COMMAND "${CMAKE_COMMAND}" -S "${source}" -B "${binary}" "-D${option}=ON")
endforeach()
TestingRun(EXPECT pass COMMAND "${CMAKE_COMMAND}" -S "${source}" -B "${binary}" -DPANGU_VALIDATE_TOOLCHAIN:BOOL=OFF)
TestingRun(EXPECT pass COMMAND "${CMAKE_COMMAND}" --build "${binary}" --target PanguTestsFull --parallel 1)
TestingRun(EXPECT pass COMMAND "${CMAKE_COMMAND}" -S "${source}" -B "${binary}"
    -DFIXTURE_OWNER=project -DPANGU_BUILD_ENGINE_TESTS=OFF -DPANGU_VALIDATE_TOOLCHAIN:BOOL=OFF)
TestingRun(EXPECT pass COMMAND "${CMAKE_COMMAND}" -E chdir "${source}"
    "${CMAKE_COMMAND}" --workflow --preset fixture-full)
if(NOT Testing_OUTPUT MATCHES "4/4" OR NOT Testing_OUTPUT MATCHES "100% tests passed")
    message(FATAL_ERROR "Project full must actually execute its own four cases")
endif()
file(READ "${binary}/CMakeCache.txt" cache)
foreach(option IN ITEMS PANGU_BUILD_ENGINE_TESTS PANGU_VALIDATE_TOOLCHAIN)
    if(NOT cache MATCHES "${option}:BOOL=OFF")
        message(FATAL_ERROR "Project full changed parent cache ${option}")
    endif()
endforeach()
TestingListTests(BINARY "${binary}" OUTPUT tests)
if(NOT tests MATCHES "owner.project" OR tests MATCHES "owner.engine")
    message(FATAL_ERROR "Project full has incorrect ownership")
endif()
