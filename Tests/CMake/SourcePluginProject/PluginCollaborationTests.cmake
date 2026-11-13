cmake_minimum_required(VERSION 4.0)
include("${CMAKE_CURRENT_LIST_DIR}/ProjectTestHelpers.cmake")

# 独立构建并实际执行协作 C++ 用例，不依赖其他 Project 测试的构建副作用。
ConfigureFixture(Collaboration "pangu.physics;mygame.gameplay" PASS ""
    -DBUILD_TESTING=ON -DPANGU_BUILD_ENGINE_TESTS=OFF -DPANGU_LINK_MODE=Monolithic)
BuildFixture(MyGamePluginCollaborationTests)
ProjectCommand(PASS "100% tests passed" "${CMAKE_CTEST_COMMAND}" --test-dir "${binary}"
    -R "^MyGamePluginCollaborationTests[.]" --no-tests=error --output-on-failure)
message(STATUS "${commandOutput}")
