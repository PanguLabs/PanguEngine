# GoogleTest / GoogleMock

- 上游：<https://github.com/google/googletest>
- 固定版本：v1.18.0，提交 `063de7e9578f82b369302001269680b4b1553359`。
- 上游许可证：BSD-3-Clause，见 `Repo/LICENSE`。
- 初始化：`git submodule update --init Source/ThirdParty/GoogleTest/Repo`；CMake 配置不下载或更新依赖。

通过 `cmake/PanguTestDependencies.cmake` 的 `pangu_require_googletest(RTTI ON)` 按需接入。
外层适配直接编译上游四个源文件，提供 `ThirdParty::GoogleTest`、`ThirdParty::GoogleTestMain`、
`ThirdParty::GoogleMock`、`ThirdParty::GoogleMockMain` 四个 STATIC 库接口；不修改 `Repo/` 或父工程缓存。
目标采用 C++23、无异常、显式 Threads 依赖，并启用 RTTI（`-frtti`、`GTEST_HAS_RTTI=1`）；Windows CRT/STL 由实际工具链决定。
同一构建图的重复请求必须具有相同来源、固定提交及编译契约。

2026-11-05 用户明确保留全项目 RTTI，覆盖此前工具无 RTTI 决定。OFF 请求明确拒绝。
直接使用固定原始 `Repo/`，不再准备补丁、头文件副本或前置 include。
`TestingDependencyIsolation` 验证实际 RTTI 编译/链接/运行、重复配置、OFF 拒绝，以及来源和编译契约冲突。
