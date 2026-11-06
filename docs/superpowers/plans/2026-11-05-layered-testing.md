# 分层测试重构 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将现有正式测试迁入统一的边界分类、累计运行集合和构建/测试入口，保留全部有效覆盖，接入 GoogleTest、GoogleMock、Google Benchmark。

**Architecture:** CTest 调度正确性测试，GoogleTest 按用例发现，Engine/Project 提供显式测试上下文，Plugin 继承来源工程。独立 PanguBuildTool 保留宿主构建图，根验收通过独立测试构建执行并汇总结果；Benchmark 独立开关、显式运行。

**Tech Stack:** C++23、Clang、CMake 4.0+、Ninja、GoogleTest/GoogleMock v1.18.0、Google Benchmark v1.9.5、原生 CTest/Presets。

**Spec:** [已批准的分层测试设计](../specs/2026-11-05-layered-testing-design.md)，实施者必须同时阅读，尤其第 7.5、12、16 节。

**Status:** Completed（2026-11-06）；Task 1–9 已完成并通过任务评审，整分支评审与最终限定复评通过，本计划临时目录已清理。四项非阻断维护项及全部裁决见[验收记录](2026-11-05-layered-testing-validation.md)。

## Global Constraints

- 继续使用 C++23、Clang、CMake 4.0+ 和现有平台工具链；新建或修改的 CMakeLists.txt 使用 `cmake_minimum_required(VERSION 4.0)`。
- 正式依赖固定版本，CMake configure 不联网、不更新 submodule；第三方接口使用 `ThirdParty::`，测试和辅助库不获得 `Pangu::` Module 身份。
- Runtime 测试继承 `PanguBuildOptions`；BuildTools 测试沿用各工具自身策略，不继承 Runtime 的 `PanguBuildOptions`。当前 PanguBuildTool 保持无异常并保留 RTTI。
- 保留全部现有有效场景、编译约束、真实 STATIC 库、生成 Catalog、链接裁剪、竞态触发强度和析构判据，遵守 `Tests/README.md`。
- `fast ⊆ precommit ⊆ full`；fast 60 秒、precommit 5 分钟为测试执行优化目标，构建另记，不是迁移硬门槛。
- 每个 GoogleTest 用例独立登记；首期无动态测试库 Runner、无通用自定义 main、无自动批处理、无 Python/PyTest 新依赖。
- 类别及共用测试机制统一命名 `BuildTools` / `build_tools` / `BUILD_TOOLS`；具体工具保留 PanguBuildTool 等原名。允许同类工具共用测试接线，本次首个实际接入为 PanguBuildTool，不预建不存在的工具工程。
- 只有 `owner.engine` / `owner.project`；Plugin 自带测试跟随来源工程，Project 协作测试跟随 Project；只登记显式启用且所属方允许测试的 Plugin。
- `PANGU_BUILD_BENCHMARKS=OFF` 为默认，独立于 `BUILD_TESTING` 和 `PANGU_BUILD_ENGINE_TESTS`；只开基准不引入 GoogleTest/GoogleMock。
- 工具日常构建为 `<BuildRoot>/BuildTools/PanguBuildTool`，测试专用构建为 `<BuildRoot>/Tests/BuildTools/PanguBuildTool`；不复用两者的 Cache 或产物。
- Windows/Android BuildRoot 为源码下 `Build/Windows` / `Build/Android`；Linux 为 `$HOME/PanguEngineBuild/Linux`，macOS/iOS 为 `$HOME/PanguEngineBuild/macOS` / `$HOME/PanguEngineBuild/iOS`。
- Linux 从 `/home/moozen/WindowsGit/PanguEngine`，macOS 从 `/Users/moozen/WindowsGit/PanguEngine` 读取同一份源码；macOS 使用 `ssh -o BatchMode=yes moozen-macos`，禁止复制 Engine 源码到临时目录构建。
- 同一 BuildRoot 同时只有一个验证会话；会话内允许隔离后的并行，嵌套构建计入处理器预算。
- 三桌面正确性构建和运行全部完成才算迁移完成；移动端保留 Toolchain、LinkSmoke、CrossCompileTests，不宣称设备运行。
- 三类 Benchmark 必须交付，Windows 正式测量至少重复 5 次，Linux/macOS 完成编译、最终链接、运行冒烟；无性能百分比门禁。
- 提交信息和注释中文优先，不添加工具/模型署名尾注。临时上下文位于 `.superpowers/sdd/2026-11-05-layered-testing/`，必要结论归档后按 AGENTS.md 收尾。

## Review Focus

1. **关闭测试、只开基准、父工程已有框架目标**：无隐式依赖/下载，兼容复用或明确冲突。由 Task 1 的 `DependencyIsolation` 和 Task 8 的 `BenchmarkOnly` 钉住。
2. **旧缓存、局部构建、缺失测试程序、空筛选**：不能运行旧产物或空跑后宣称 full。由 Task 7 的 `WorkflowCompleteness` 钉住。
3. **带空格/中文/Emoji 的路径，以及并行或重复运行**：路径不丢失，夹具与 XML 不冲突，直接批量运行也不依赖顺序。由 Task 1 的 `RegistrationIsolation`、Task 4 的 `BuildToolsTests` 钉住。
4. **Project 消费 Engine Plugin、未消费的 EXCLUDE_FROM_ALL 目标**：归属不改变，测试目标显式拉入生产依赖，未启用 Plugin 无测试。由 Task 6 的 `PluginTestOwnership` 钉住。
5. **宿主测试失败/跳过、工具源码镜像与交叉编译**：失败传播，关键跳过阻塞验收，普通 bootstrap 不需要测试源码，也不运行交叉目标。由 Task 4 的 `BuildToolsFailurePropagation` 和 Task 9 的移动端验收钉住。

---

## 文件结构与职责

| 文件/目录 | 操作与唯一职责 |
| --- | --- |
| `Source/ThirdParty/GoogleTest/{CMakeLists.txt,README.md,Repo/}` | 固定上游源码、无异常/RTTI/CRT 适配和六个接口中的四个 GTest/GMock 接口 |
| `Source/ThirdParty/GoogleBenchmark/{CMakeLists.txt,README.md,Repo/}` | 独立基准依赖，不配置上游测试/安装/下载 |
| `cmake/PanguTestDependencies.cmake` | 仅按实际请求接入上述两个依赖，不创建测试或扫描工程 |
| `cmake/PanguTesting.cmake` | 上下文、元数据、发现、原生测试登记、累计构建依赖 |
| `cmake/Testing/ConfigureDiscoveredTests.cmake.in` | CTest 发现后的独立工作目录及属性；使用公开 TEST_LIST/TEST_INCLUDE_FILES 接口 |
| `cmake/PanguBuildToolsTesting.cmake` | BuildTools 类别的共用测试构建准备与 CTest 接线，由调用方显式指定具体工具和测试组 |
| `cmake/Testing/RunBuildToolsTests.cmake` | 运行已经构建的宿主测试并传播结果；不在每个用例里构建工具 |
| `cmake/Testing/ValidateFullTesting.cmake` | 正式 Engine full 的配置前提检查 |
| `Tests/CMake/Testing/` | 接线、开关、上下文、workflow 的正式回归及最小夹具 |
| `Tests/{Foundation,Module,ThirdParty/NlohmannJson}/` | 现有 C++ 行为测试迁移，领域内保留夹具及必要拆分 |
| `Tests/BuildTools/PanguBuildTool/` | 从工具源码目录迁入的唯一测试源码/接线，仍由独立工具工程编译 |
| `Tests/Support/TestWorkspace.h` | 可变文件夹具的显式工作目录辅助，不依赖 Runtime，也不引入通用 main |
| `Tests/Module/Benchmarks/` | 三类实际基准及参数说明 |
| `BuildTools/PanguBuildTool/{CMakeLists.txt,Private/}` | 引入迁移后的测试；在发现结果中保留来源工程供 Plugin 接线使用 |
| `cmake/PanguPlugins.cmake` | 把发现来源转换为 Plugin 子目录的测试上下文，维持 EXCLUDE_FROM_ALL |
| `cmake/Platform/{Windows,Linux,macOS}/CMakePresets.json` | 配套 build/test/workflow 入口，沿用基础 configure preset 和原 BuildRoot |
| `Tests/MIGRATION.md` | 旧场景→新用例/入口、特殊条件、未执行项及替代依据，不保存大量日志 |
| `wiki/foundation/testing.md`、相关 README/索引 | 迁移后长期规范、命令和结果边界 |
| `docs/superpowers/plans/2026-11-05-layered-testing-validation.md` | 本计划验收摘要、版本、平台、指标、提交与遗留项 |

本次是同一测试基础设施的连续迁移，采用一份计划；Benchmark 开关独立，不代表省略该任务。不得在计划阶段安装依赖、搬移源码或执行迁移。

## 固定接口与运行约定

### 依赖与元数据接口

以下为本计划确定的 CMake 接口，全部无返回值，除特别注明的输出变量；缺失参数、非法枚举、重复登记或不一致上下文用 `FATAL_ERROR` 诊断：

```cmake
pangu_require_googletest(RTTI ON)
pangu_require_googlebenchmark()
pangu_test_context(OWNER engine|project ENABLED <bool> [PLUGIN_ID <id>])
pangu_tests_enabled(<out-variable>)
pangu_register_gtest(TARGET <existing-executable>
    BOUNDARY unit|integration|system AREA <slug> TIER fast|precommit|full
    [TYPES contract regression] [REQUIRES threads subprocess nested-build]
    [TIMEOUT <seconds>])
pangu_register_test(NAME <unique-name> COMMAND <argv...>
    BOUNDARY unit|integration|system AREA <slug> TIER fast|precommit|full
    [BUILD_TARGETS <targets...>] [TYPES contract regression]
    [REQUIRES threads subprocess nested-build] [PROCESSORS <count>] [TIMEOUT <seconds>])
pangu_test_build_dependencies(TIER fast|precommit|full TARGETS <targets...>)
pangu_add_build_tools_tests(TOOL_NAME <name> SOURCE_DIR <absolute-tool-project>
    GROUP <group-name> BOUNDARY unit|integration|system TIER fast|precommit|full
    BUILD_TARGETS <native-targets...> TEST_REGEX <ctest-regex> [CMAKE_ARGS <args...>])
pangu_require_full_testing(OWNER engine|project)
```

- `pangu_test_context` 只设置当前调用方作用域的 `PANGU_TEST_OWNER`、`PANGU_TEST_ENABLED`、`PANGU_TEST_PLUGIN_ID`，不改父工程开关；函数实现通过明确的调用方输出作用域使子目录继承。
- `pangu_tests_enabled` 输出 `BUILD_TESTING AND PANGU_TEST_ENABLED AND NOT CMAKE_CROSSCOMPILING`；调用方先判断，再声明普通测试目标。独立工具工程将其本地 `BUILD_TESTING` 作为 ENABLED。
- 登记接口消费当前上下文，标签为 `boundary.<value>`、`area.<slug>`、`owner.<value>`、累计 `run.*`、当前 `platform.*` 和可选 `type.*` / `requires.*` / `plugin.<id>`。
- 根运行集合的构建目标为 `PanguTestsFast`、`PanguTestsPrecommit`、`PanguTestsFull`；第一次登记时按需创建，不由仅 include 辅助文件触发。生产目标仍用原生 target 命令声明。
- BuildTools 共用函数按工具/测试组显式登记：同一 TOOL_NAME 的 SOURCE_DIR 与 CMAKE_ARGS 必须一致，复用其测试专用构建；TIER、BOUNDARY、BUILD_TARGETS、TEST_REGEX 属于该组。根调度名为 `BuildTools.<ToolName>.<Group>`，每工具准备目标为 `<ToolName>TestsFast/Precommit/Full`；这些具体目标不机械改成类别复数。
- GTest 的 CTest 名称为 `<target>.<suite>.<case>`，使用 `NO_PRETTY_TYPES`、`NO_PRETTY_VALUES`、`DISCOVERY_MODE PRE_TEST`。默认超时 unit 30 秒、integration 60 秒、system 300 秒，慢工程回归显式 900 秒；ScopeDestructor 子程序超时仍为 15 秒。
- 每目标的 XML 位于 `<BuildRoot>/Tests/Results/<target>/`；发现后用例工作目录为 `<BuildRoot>/Tests/Work/<target>/<SHA256-of-CTest-name>/`。无效或未发现的测试不能被忽略为成功。可变文件夹具显式接收 BuildRoot 内的路径，直接批量运行时同样提供该路径。
- `TestWorkspace.h` 暴露 `pg::test::CreateTestWorkspace(const std::filesystem::path& root, std::string_view caseName) -> std::expected<std::filesystem::path, std::error_code>`，仅创建 root 内的用例目录；拒绝绝对 caseName、`..` 和越界，不根据源码目录猜测输出位置。

### 固定依赖版本与适配选择

| 依赖 | 固定版本/提交 | 接口 |
| --- | --- | --- |
| GoogleTest + GoogleMock | v1.18.0，`063de7e9578f82b369302001269680b4b1553359` | `ThirdParty::GoogleTest`、`GoogleTestMain`、`GoogleMock`、`GoogleMockMain` |
| Google Benchmark | v1.9.5，`192ef10025eb2c4cdd392bc502f0c852196baa48` | `ThirdParty::GoogleBenchmark`、`GoogleBenchmarkMain` |

GoogleTest 采用外层直接列出上游 `gtest-all.cc`、`gtest_main.cc`、`gmock-all.cc`、`gmock_main.cc` 创建四个 STATIC 目标的方式，直接消费固定原始上游源码，`Repo/` 始终保持只读。2026-11-05 用户明确覆盖此前 BuildTools 无 RTTI 及其兼容补丁决定：Pangu 自有 Runtime、BuildTools、测试和基准均保留 RTTI，无异常策略不变；删除仅为无 RTTI 服务的 Patch、BuildRoot 头文件副本及补丁应用/路径检查。这样无需继承上游 CMake 对 C 语言和异常编译参数的初始化；显式连接 `Threads::Threads`，SYSTEM 暴露原始上游头路径。四个目标使用 C++23、`-fno-exceptions`、`-frtti` 和一致的 `GTEST_HAS_EXCEPTIONS=0` / `GTEST_HAS_RTTI=1`；保留 ON 调用接口，OFF 请求明确拒绝。框架与消费方的 RTTI 必须一致。Windows 保持实际 CRT/STL 配置，不把静态库等同于静态 CRT。

普通上游名为 `gtest`、`gtest_main`、`gmock`、`gmock_main`，再建立 ThirdParty ALIAS。每个实际目标记录 `PANGU_TEST_DEPENDENCY_REVISION` 和 `PANGU_TEST_RTTI` 属性。重复请求必须一致；父工程已有同名目标时检查实际来源提交、编译契约及属性，无法证明兼容就诊断冲突，不能只相信一个版本字符串。

Benchmark 使用上游 CMake，目录作用域设置 `BUILD_SHARED_LIBS=OFF`，并设置 `BENCHMARK_ENABLE_TESTING=OFF`、`BENCHMARK_ENABLE_GTEST_TESTS=OFF`、`BENCHMARK_DOWNLOAD_DEPENDENCIES=OFF`、`BENCHMARK_ENABLE_EXCEPTIONS=OFF`、`BENCHMARK_ENABLE_INSTALL=OFF`、`BENCHMARK_ENABLE_WERROR=OFF`、`BENCHMARK_ENABLE_ASSEMBLY_TESTS=OFF`；为实际库补充 C++23 要求。不得通过 CACHE FORCE 改变父工程的 BUILD_TESTING/BUILD_SHARED_LIBS/CRT，不引入 Abseil、RE2、Python 或基准上游测试。

版本于规划时通过官方发布记录核对，尚未在本项目编译。若 Task 1/8 证实存在兼容问题，先归因并记录最小适配；更换固定版本必须更新 gitlink、依赖说明和计划依据，不静默改用浮动 main。

## Task 1: 接入测试框架、登记接口和首个 Foundation 用例

**Files:**
- Create: `Source/ThirdParty/GoogleTest/{CMakeLists.txt,README.md,Repo/}`、`cmake/PanguTestDependencies.cmake`、`cmake/PanguTesting.cmake`、`cmake/Testing/ConfigureDiscoveredTests.cmake.in`。
- Create: `Tests/CMake/Testing/{CMakeLists.txt,TestingTestHelpers.cmake,RegistrationTests.cmake,DependencyIsolationTests.cmake,Fixtures/Registration/CMakeLists.txt,Fixtures/Registration/SampleTests.cpp}`、`Tests/MIGRATION.md`。
- Modify: `.gitmodules`、根 `CMakeLists.txt` 的测试选项/CTest 时序、`Tests/CMakeLists.txt`、`Tests/Foundation/{CMakeLists.txt,ErrorTests.cpp}`。
- Test: `TestingRegistration`、`TestingDependencyIsolation`、`PanguFoundationUnitTests.*`。

**Interfaces:** Consumes 现有 `PanguBuildOptions`、`Pangu::Foundation`；produces 固定接口节定义的 `pangu_require_googletest(RTTI ON)`、`pangu_test_context`、`pangu_tests_enabled(out)`、`pangu_register_gtest`、`pangu_register_test`、`pangu_test_build_dependencies` 及 `PanguFoundationUnitTests`；Benchmark、宿主调度和正式 full 前提接口分别由 Task 8、4、7 实现。测试辅助文件提供 `TestingRun(EXPECT pass|fail DIAGNOSTIC <regex> COMMAND <argv...>)`、`TestingConfigure(NAME <id> SOURCE <dir> EXPECT pass|fail DIAGNOSTIC <regex> ARGS <argv...>)`、`TestingListTests(BINARY <dir> OUTPUT <variable>)`，最后一个通过 `ctest --show-only=json-v1` 输出 JSON 字符串。Registration fixture 两个用例名为 `Registration.First` / `Registration.Second`；后者读取仅供该夹具的 `PANGU_TEST_FIXTURE_RESULT=pass|fail|skip`，默认 pass，供后续失败传播回归使用。

- [x] **Step 1: 建立旧覆盖基线。** 在对应正式 BuildRoot 完成旧构建/CTest及独立工具三项测试，记录源码版本、命令、失败/跳过；在 MIGRATION 中按现有函数、参数表和脚本断言列映射。已有失败保留归因，不拿历史通过数量充当本次基线。
- [x] **Step 2: 编写新增接线的失败回归。** Registration fixture 提供两个真实 GTest 用例；检查标签累计、逐例发现、工作目录和 XML 隔离、重复登记/非法 Tier/owner 缺失失败。DependencyIsolation 检查仅 include 不产生框架目标、父 BUILD_SHARED_LIBS 不变、缺源码明确拒绝、同图重复请求一致/冲突。

```cmake
# RegistrationTests：对 fixture 列表提取 fast/precommit/full 用例集合后断言。
if(NOT fastNames STREQUAL precommitNames OR NOT fastNames STREQUAL fullNames)
    message(FATAL_ERROR "fast fixture must also appear in precommit and full")
endif()
if(NOT discoveryCount EQUAL 2 OR firstWorkDir STREQUAL secondWorkDir)
    message(FATAL_ERROR "two independently discovered cases need isolated work directories")
endif()
```

- [x] **Step 3: 确认红灯。** 将两个新脚本接入根 Tests 后运行 `cmake --preset windows`、`ctest --preset windows -R '^Testing(Registration|DependencyIsolation)$' --no-tests=error --output-on-failure`；预期因缺少新接口/依赖适配失败，不能以编译器未安装或 fixture 路径错误作为有效红灯。
- [x] **Step 4: 固定依赖并实现接线。** 初始化固定提交的 submodule，按固定接口实现目标级适配、参数校验和累计依赖。把 standalone `include(CTest)` 与 Engine 测试选项初始化移至 Plugin 配置前，嵌入时仍由父级初始化 CTest；测试关闭不配置 GTest。
- [x] **Step 5: 迁移 ErrorTests。** 保留全部 `static_assert`；将错误值、`Result`/`Status` 成功失败、原生错误传播拆为独立 TEST，目标为 `PanguFoundationUnitTests`，unit/foundation/fast。验证精确值 `124`、原始 Error 一致、成功 Status 无错误，不能只检查旧布尔函数。
- [x] **Step 6: 验证交付。** 构建 `PanguFoundationUnitTests` 及 registration fixture 后重跑新增回归与 `ctest --preset windows -R '^PanguFoundationUnitTests[.]' --no-tests=error --output-on-failure`；两次重复及 `--parallel 4` 无状态/XML 冲突。对迁移源码执行 clang-format/clang-tidy，更新映射。
- [x] **Step 7: 提交。** `git add -- .gitmodules Source/ThirdParty/GoogleTest cmake/PanguTestDependencies.cmake cmake/PanguTesting.cmake cmake/Testing/ConfigureDiscoveredTests.cmake.in CMakeLists.txt Tests/CMakeLists.txt Tests/CMake/Testing Tests/Foundation Tests/MIGRATION.md`；`git commit -m "feat: 接入 GoogleTest 与分层测试登记"`。

## Task 2: 迁移 Module 单元测试、JSON，并验证 GoogleMock

**Files:**
- Modify: `Tests/Module/{CMakeLists.txt,ModuleIdentityTests.cpp,ScopeAccessTests.cpp,ScopeBuildTests.cpp}`、`Tests/ThirdParty/NlohmannJson/{CMakeLists.txt,JsonExtrasTests.cpp}`、`Tests/MIGRATION.md`。
- Create: `Tests/Module/{ScopeAccessConcurrencyTests.cpp,ScopeBuildMockTests.cpp}`。
- Test: `PanguModuleUnitTests.*`、`PanguModuleConcurrencyTests.*`、`PanguScopeBuildMockTests.*`、`PanguJsonUnitTests.*`。

**Interfaces:** Consumes `pangu_require_googletest(RTTI ON)`、`pangu_register_gtest(TARGET <target> BOUNDARY unit AREA <slug> TIER fast|precommit [REQUIRES threads])`；produces 上述四个测试程序。`PanguModuleUnitTests` 包含 Identity、普通 ScopeAccess 和 ScopeBuild，为 unit/module/fast；并发目标 unit/module/precommit、requires.threads；Mock 目标 unit/module/fast 链接 GoogleMockMain；JSON 为 unit/thirdparty/fast。

- [x] **Step 1: 钉住新的逐例入口。** 在迁移清单列出原 Identity 非法长度/字符/零值、Scope 三道门与 move-only、所有权/类型错误/粘性错误、JSON 无全局 UDL 与 discarded 断言；用新前缀 `ctest -R ... --no-tests=error` 确认迁移前无此入口。保留 JSON 编译宏、版本和布局断言。
- [x] **Step 2: 转换行为断言。** 使用 ASSERT 验证 expected 是否有值后再访问，用 EXPECT 检查值/错误/计数；文件内支持函数保持局部，公共静态夹具每例恢复。并发例移到新文件，保留原 200 轮和关闭前后关系，线程收集失败，join 后断言。
- [x] **Step 3: 增加真实 seam 的 Mock 用例。** `ScopeBuildMock.InvalidHooksRejectBeforeStage` 使用现有 `detail::ScopeBuildSink`，只提供 RequestStop、不提供 PollStopped，检查 Reject 接收 `kInvalidDeclaration` 且 Stage 不发生；禁止修改生产接口来迎合 Mock。

```cpp
EXPECT_CALL(sink, Stage).Times(0);
EXPECT_CALL(sink, Reject(pg::module::MakeModuleError(pg::module::ModuleError::kInvalidDeclaration)))
    .WillOnce(::testing::ReturnArg<0>());
// 调用带不成对 hooks 的 Own 后：
ASSERT_FALSE(result);
EXPECT_EQ(result.error(), pg::module::MakeModuleError(pg::module::ModuleError::kInvalidDeclaration));
```

该用例验证已有前置拒绝契约。不要错误地断言 ScopeBuildContext 自己锁存错误、因此第二次合法 Own 永远不调用 Stage；当前锁存由实际 Sink 承担，原 Fake 行为仍需保留验证。

- [x] **Step 4: 验证。** `cmake --build --preset windows --target PanguModuleUnitTests PanguModuleConcurrencyTests PanguScopeBuildMockTests PanguJsonUnitTests`；`ctest --preset windows -R '^(PanguModuleUnitTests|PanguModuleConcurrencyTests|PanguScopeBuildMockTests|PanguJsonUnitTests)[.]' --no-tests=error --output-on-failure`。重复运行并用 `--gtest_shuffle --gtest_repeat=3` 直接运行各程序，预期所有原场景通过，无跨例残留。
- [x] **Step 5: 格式与静态检查。** 对上述自有 C++ 用真实根编译数据库执行 clang-format/clang-tidy，更新迁移映射，确认与原触发条件一致。
- [x] **Step 6: 提交。** `git add -- Tests/Module Tests/ThirdParty/NlohmannJson Tests/MIGRATION.md`；`git commit -m "refactor: 迁移模块单元测试并验证依赖交互"`。

## Task 3: 迁移真实 Module 集成与特殊链接/析构回归

**Files:**
- Modify: `Tests/Module/{CMakeLists.txt,StaticModuleHostTests.cpp,RegistrationTests.cpp,ScopeLifecycleTests.cpp,StaticCatalogTests.cpp,ScopeDestructorTests.cmake,Fixtures/CMakeLists.txt,README.md}`、`Tests/MIGRATION.md`。
- Create: `Tests/Module/ScopeDestructorFixture.cpp`、`Tests/Module/Fixtures/ScopeLifecycleFixture.h`；后者只提取生命周期场景和终止夹具共用的测试局部数据/回调，不建立生产新接口。
- Test: `PanguModuleIntegrationTests.*`、`PanguStaticCatalogTests.*`、`ScopeDestructor`。

**Interfaces:** Consumes `pangu_register_gtest(TARGET <target> BOUNDARY integration AREA module TIER fast|precommit)`、`pangu_register_test(NAME <name> COMMAND <argv...> BOUNDARY integration AREA module TIER precommit BUILD_TARGETS <fixture> REQUIRES subprocess)`；produces `PanguModuleIntegrationTests`（integration/module/fast）、原独立 `PanguStaticCatalogTests`（integration/module/precommit）、`PanguScopeDestructorFixture` 与父检查 `ScopeDestructor`（integration/module/precommit、requires.subprocess）。

- [x] **Step 1: 固定迁移前后的验收清单。** 按每个旧布尔场景记录错误值、Progress、OriginalError/CleanupError、调用次数、依赖顺序、句柄代际和析构顺序；用新集成前缀选择确认入口尚不存在。Static Catalog 和 ScopeDestructor 的原入口先保持可运行。
- [x] **Step 2: 转换真实集成用例。** 注册、激活、关闭、失败回滚仍使用真实 Host/RegistrationScope/ScopeAccess；原每个行为函数至少对应一项独立用例，长场景保留时间顺序；相同 Error 值也不能丢失是否进入回调的区别。

```cpp
// 对应原 RollsBackAndPreservesOriginalErrorAndProgress 场景。
EXPECT_EQ(snapshot.Progress, pg::module::ModuleOperationProgress::kCleanupPending);
EXPECT_EQ(snapshot.PrimaryError, kOriginal);
EXPECT_EQ(scope.Declarations().size(), 1U);
EXPECT_EQ(scope.Declarations().front().Owner, kSecond);
```

- [x] **Step 3: 分离确定终止的专用夹具。** 从旧 `--destroy-busy` 分支迁出最小真实场景，父脚本改用 `$<TARGET_FILE:PanguScopeDestructorFixture>`，仍检查退出码 91、`.ready` 存在、Destroy 标记不存在，子进程 15 秒超时。不得用任意非零退出代替。
- [x] **Step 4: 迁移 Catalog 运行断言并保持链接条件。** 仍只通过生成 Catalog 引用两座真实 STATIC 夹具；保留 `-ffunction-sections/-fdata-sections` 以及 `/OPT:REF`、`-dead_strip`、`--gc-sections` 平台分支，禁止 whole-archive 或把夹具源码合并进测试程序。
- [x] **Step 5: 验证。** 构建三个测试/夹具目标，运行 `ctest --preset windows -R '^(PanguModuleIntegrationTests[.]|PanguStaticCatalogTests[.]|ScopeDestructor$)' --no-tests=error --output-on-failure`。检查两座独立归档及最终链接命令；临时在测试夹具移除一个 Catalog 根引用、或改错预期退出码，确认相应回归失败后恢复，不提交故障。
- [x] **Step 6: 提交。** clang-format/clang-tidy 和当前 Module 全域回归通过后，`git add -- Tests/Module Tests/MIGRATION.md`；`git commit -m "refactor: 保留真实生命周期与链接契约迁移集成测试"`。

Task 3 验收：`9e9203a`；独立任务评审符合规格且 Approved。Windows 全量 75/75，三桌面本任务定向各 36/36，乱序三轮与析构故障注入验证通过；Linux/macOS 全量和移动端专项留待 Task 9。

## Task 4: 建立 BuildTools 共用测试接线并迁入 PanguBuildTool 测试

本任务建立 BuildTools 类别共用的测试接线，并以现有 PanguBuildTool 三类正式测试完成实际接入。工具工程和测试组由调用方显式登记，共用实现不写死某个工具的路径、目标或用例名。“宿主”仅描述运行平台；类别及共用机制统一使用 BuildTools，不命名为 HostTools。此处不扩展为新的构建内核，也不为尚不存在的工具创建工程。

平台夹具裁决：Windows 目录链接逃逸使用原生 Junction，Linux/macOS 使用目录符号链接；均须实际确认链接解析目标和拒绝越界，创建失败不得跳过。此等效仅限目录重解析逃逸，不代替文件符号链接及其多级改指验证。通用夹具的源码副本与真实可变输入继续覆盖中文、Emoji 和空格；binary 目录使用 ASCII 带空格路径，不将上游 GTest 对任意 Unicode BuildRoot 的支持扩入本次范围。

**Files:**
- Move: `BuildTools/PanguBuildTool/Tests/PluginManifestTests.cpp`、`PluginDiscoveryTests.cpp`、`PluginCliTests.cmake` → `Tests/BuildTools/PanguBuildTool/` 同名文件。
- Create: `Tests/BuildTools/PanguBuildTool/{CMakeLists.txt,TestWorkspaceTests.cpp}`、`Tests/Support/TestWorkspace.h`、`cmake/PanguBuildToolsTesting.cmake`、`cmake/Testing/RunBuildToolsTests.cmake`、`Tests/CMake/Testing/{BuildToolsTests.cmake,BuildToolsFailurePropagationTests.cmake}`。
- Modify: `BuildTools/PanguBuildTool/CMakeLists.txt`、`Tests/CMakeLists.txt`、`Tests/CMake/Testing/CMakeLists.txt`、`Tests/MIGRATION.md`、`BuildTools/README.md`。
- Test: `TestingBuildToolsTests`、`TestingBuildToolsFailurePropagation`、三个宿主测试族与根调度条目。

**Interfaces:** Consumes `pangu_require_googletest(RTTI ON)`、`pangu_register_gtest(TARGET ... BOUNDARY ... AREA build TIER ...)`、`pangu_register_test(NAME ... COMMAND ... BOUNDARY ... AREA build TIER ... BUILD_TARGETS ...)`、`pangu_test_build_dependencies(TIER ... TARGETS ...)`；produces 固定接口节的 `pangu_add_build_tools_tests(TOOL_NAME ... SOURCE_DIR ... GROUP ... BOUNDARY ... TIER ... BUILD_TARGETS ... TEST_REGEX ... [CMAKE_ARGS ...])`。PanguBuildTool 的实际准备目标为 `PanguBuildToolTestsFast/Precommit/Full`，以及固定接口节的 `pg::test::CreateTestWorkspace(root, caseName)`。独立宿主目标名保留 `PanguPluginManifestTests`、`PanguPluginDiscoveryTests`；根调度名 `BuildTools.PanguBuildTool.PluginManifest`（unit/build/fast）、`BuildTools.PanguBuildTool.PluginDiscovery`（integration/build/precommit）、`BuildTools.PanguBuildTool.PluginCli`（system/build/precommit）。

- [x] **Step 1: 编写失败回归。** BuildToolsTests 检查根入口存在、测试程序实际构建、三类原场景可运行，工具测试不链接 PanguBuildOptions；bootstrap Cache 仍为 OFF，专用测试 Cache 为 ON，地址不相同。另复用最小测试工程，以不同 TOOL_NAME/目标/选择正则登记一组夹具，验证共用接线没有写死 PanguBuildTool，工具间目录和调度名隔离；同一 TOOL_NAME 的工程/配置冲突明确失败，不增加虚构生产工具。FailurePropagation 使用 Task 1 fixture 的 fail/skip 模式、空正则及缺失 fixture 程序，父调用必须非零，且日志含内层用例名。TestWorkspaceTests 加入 Manifest 单元测试程序，断言正常目录位于 root 内，`../outside` 和原生绝对路径被拒绝且未产生越界文件。

```cmake
if(bootstrapTesting OR NOT hostTesting OR hostBinary STREQUAL bootstrapBinary)
    message(FATAL_ERROR "bootstrap and host tests must use separate configurations")
endif()
if(innerFailureParentExit EQUAL 0 OR emptySelectionParentExit EQUAL 0 OR skippedParentExit EQUAL 0)
    message(FATAL_ERROR "host failure, empty selection and critical skip must fail the parent")
endif()
```

- [x] **Step 2: 确认红灯。** 接入脚本后运行 `ctest --preset windows -R '^TestingBuildTools' --no-tests=error --output-on-failure`；预期缺少新调度/独立测试接线而失败。
- [x] **Step 3: 搬移并迁移三项测试。** 独立工具 `if(BUILD_TESTING)` 内才引入依赖辅助文件及根测试子目录，显式传递 Engine 根和测试 binary directory。Manifest 使用 GoogleTest 参数表保留所有合法/非法 TOML、ID、SemVer、诊断路径；Discovery 保留真实文件系统、排序、双根与逃逸；CLI 保留 stdout/stderr、退出码、JSON、中文/Emoji 路径。所有可变数据通过显式工作目录创建；缺符号链接能力的关键用例必须暴露为未验收/失败，不悄悄通过。
- [x] **Step 4: 实现 BuildTools 共用构建准备并登记具体工具。** 共用函数使用调用方传入的工具工程、测试目标及配置参数，专用目录为 `<BuildRoot>/Tests/BuildTools/<ToolName>`。PanguBuildTool 调用方显式登记 PluginManifest、PluginDiscovery、PluginCli 三组及其边界/层级、原生目标和选择正则；其目录仍严格为 `<BuildRoot>/Tests/BuildTools/PanguBuildTool`，原生日常目录仍为 `<BuildRoot>/BuildTools/PanguBuildTool`。用测试专用 custom target 显式配置宿主工具链、构建工具和测试；不挂普通 ALL，不复用 bootstrap Cache，不在根 configure 构建测试。将各准备目标加入对应 `PanguTests*` 依赖。
- [x] **Step 5: 实现根调度。** RunBuildToolsTests 接收 `PANGU_BUILD_TOOLS_TEST_BINARY_DIR`、`PANGU_BUILD_TOOLS_TEST_REGEX`、`PANGU_BUILD_TOOLS_TEST_RESULT_DIR`、`PANGU_BUILD_TOOLS_TEST_JOBS`，只运行已构建程序。使用内层 `ctest --no-tests=error --output-on-failure --output-junit <独立文件>`，根三条目分别选择两个程序前缀及 CLI 名，保证每轮每例只执行一次；保留原始 XML/日志，内层失败及关键 skipped 状态传播为父失败。
- [x] **Step 6: 验证直接与根入口。** 先构建 `PanguTestsPrecommit`，运行 `ctest --preset windows -R '^BuildTools[.]PanguBuildTool[.]' --no-tests=error --output-on-failure`；再直接 `ctest --test-dir Build/Windows/Tests/BuildTools/PanguBuildTool --no-tests=error --output-on-failure`。两个入口各自完整，但一次正式 full 不能同时重复调度两套。测试根使用带空格及中文的副本路径，原生路径结果不丢失。
- [x] **Step 7: 验证 bootstrap 镜像边界。** 运行现有 SourcePluginBuildToolSourceChange、SourcePluginIncremental：它们只复制工具输入且关闭工具测试时，不要求镜像中存在根 Tests、GoogleTest 或新辅助文件。正常构建/无变化原生 Ninja 不新增宿主测试扫描。
- [x] **Step 8: 提交。** 使用宿主测试编译数据库执行 C++ 检查；`git add -A -- BuildTools/PanguBuildTool/Tests BuildTools/PanguBuildTool/CMakeLists.txt BuildTools/README.md Tests/BuildTools Tests/Support Tests/CMakeLists.txt Tests/CMake/Testing Tests/MIGRATION.md cmake/PanguBuildToolsTesting.cmake cmake/Testing/RunBuildToolsTests.cmake`；`git commit -m "refactor: 集中构建工具测试并接入宿主验收"`。

Task 4 验收：主体 `8cc8470`，RTTI 政策修订 `4de6ca2`；两项独立评审均符合规格且 Approved。用户已明确项目不禁用 RTTI，旧无 RTTI 补丁及其生成链已移除。最终 Windows 根全量 80/80；三桌面框架定向各 4/4、独立宿主各 40/40，实际编译数据库不再引用禁用 RTTI 选项或补丁路径。最终跨平台全量与移动端专项仍由 Task 9 验收。

## Task 5: 统一脚本与 Toolchain 分类，完善隔离和显式跳过

**Files:**
- Modify: `Tests/CMakeLists.txt`、`Tests/Toolchain/CMakeLists.txt`、`Tests/CMake/SourcePlugins/{CMakeLists.txt,BuildToolConfigureTests.cmake,BuildToolInvalidationTests.cmake,BuildToolSourceChangeTests.cmake}`、`Tests/CMake/SourcePluginProject/{ProjectTestHelpers.cmake,RunIntegration.cmake}`。
- Create: `Tests/CMake/Testing/ScriptClassificationTests.cmake`；modify 其 CMakeLists、`Tests/MIGRATION.md`。
- Test: `TestingScriptClassification`、全部现有 SourcePlugin*、BuildEnvironment、ToolchainLibraryLink。

**Interfaces:** Consumes `pangu_register_test(NAME <name> COMMAND <argv...> BOUNDARY ... AREA ... TIER ... [BUILD_TARGETS ...] [PROCESSORS <count>])`、`pangu_test_build_dependencies(TIER full TARGETS <targets...>)`；produces 对所有原生/脚本测试的完整元数据。Toolchain 编译目标 `ThirdPartyCompiledProbe`、`PanguToolchainProbe` 加入 full 构建依赖；不改变最小链接程序的生产依赖。

- [x] **Step 1: 写分类与隔离回归。** ScriptClassification 从 CTest JSON 验证每项原有脚本拥有 boundary/area/owner/run.full，BuildEnvironment 和 ToolchainLibraryLink 进入 precommit，SourcePlugin* 为 full。两项并行脚本不能共享可变目录；失败后再次执行能重新生成输入。

```cmake
if(NOT "run.full" IN_LIST labels OR NOT "owner.engine" IN_LIST labels)
    message(FATAL_ERROR "an existing Engine regression is missing from full ownership")
endif()
if(firstMutableRoot STREQUAL secondMutableRoot)
    message(FATAL_ERROR "parallel scripts must not share mutable fixture roots")
endif()
```

- [x] **Step 2: 确认红灯。** `ctest --preset windows -R '^TestingScriptClassification$' --no-tests=error --output-on-failure` 当前应报未分类的原测试，不用固定总用例数代替检查。
- [x] **Step 3: 替换登记接线。** 保留现有脚本行为，只用统一登记接口表达命令、目标依赖、PROCESSORS、TIMEOUT。按测试名分配根下 Tests/Work 的工作根；已有随机夹具继续使用。嵌套构建从 `PANGU_TEST_BUILD_JOBS` 消费并行度，外层 PROCESSORS 与其一致，替代固定 `--parallel 8` 的倍增行为。
- [x] **Step 4: 拆出可见能力场景。** BuildToolInvalidation 和 Discovery 的符号链接分支作为可独立定位的正式场景；缺少必要权限的关键验收明确失败/未完成。不得继续只打印 skipped 后成功，也不能删除 Junction、多级文件改指等原条件。
- [x] **Step 5: 验证。** 构建 Toolchain 原目标及根正式测试，执行新分类检查和全部原脚本；对 `RunIntegration` 的 EngineOptIn 显式构建需发现的测试目标后再查 JSON，改掉旧测试名/固定总数假设。原 CLI、插件增量/失败修复/部署判据保持通过。
- [x] **Step 6: 提交。** `git add -- Tests/CMakeLists.txt Tests/Toolchain/CMakeLists.txt Tests/CMake/SourcePlugins Tests/CMake/SourcePluginProject/ProjectTestHelpers.cmake Tests/CMake/SourcePluginProject/RunIntegration.cmake Tests/CMake/Testing Tests/MIGRATION.md`；`git commit -m "feat: 统一系统回归分类与测试资源隔离"`。

Task 5 验收：主体 `b0029d4`，嵌入分类修复 `490d052`；初评的问题已通过限定复评。主体 Windows 通过用户授权的管理员测试进程完整执行 83/83，Linux/macOS 定向各 21/21；修复后 standalone/真实嵌入分类三桌面各 2/2，并验证合法通过、Engine 漏标拒绝及恢复通过，Windows 全新夹具补验通过。原非管理员权限失败与先前会话并发观察保留为历史；当前未修改系统设置，最终完整矩阵仍由 Task 9 验收。

## Task 6: 落实 Project / Plugin 测试上下文与真实消费者

**Files:**
- Modify: `BuildTools/PanguBuildTool/Private/{PluginDiscovery.h,PluginDiscovery.cpp,PluginJson.cpp}`、`cmake/PanguPlugins.cmake`、根 `CMakeLists.txt`。
- Modify: `Tests/BuildTools/PanguBuildTool/{PluginDiscoveryTests.cpp,PluginCliTests.cmake}`、`Tests/CMake/SourcePluginProject/MyGame/CMakeLists.txt`、`Tests/CMake/SourcePluginProject/FixtureEnginePlugins/Physics/CMakeLists.txt`、`Tests/CMake/SourcePluginProject/MyGame/Plugins/Gameplay/CMakeLists.txt`。
- Create: `Tests/CMake/SourcePluginProject/PluginTestOwnershipTests.cmake`、两个 Plugin 夹具下 `Tests/{CMakeLists.txt,PluginBehaviorTests.cpp}`，以及 MyGame 的 `Tests/PluginCollaborationTests.cpp`。
- Modify: `Tests/CMake/SourcePlugins/CMakeLists.txt`、`Tests/MIGRATION.md`。
- Test: `SourcePluginTestOwnership`、独立宿主发现/CLI用例、三个明确归属的夹具测试目标。

**Interfaces:** Consumes `pangu_test_context(OWNER engine|project ENABLED <bool> PLUGIN_ID <id>)`、`pangu_tests_enabled(<out>)`、`pangu_register_gtest(TARGET ... BOUNDARY unit|integration AREA build TIER fast)` 与真实 Plugin 双根发现；produces 工具私有 `enum class PluginOrigin : std::uint8_t { kEngine, kProject }`、`PluginEntry::Origin`，JSON 新字段 `origin` 值为 `engine` / `project`。`DiscoverPlugins(...)`、Plugin Manifest、`pangu_add_enabled_plugins(...)` 的现有调用接口保持不变。夹具目标名为 `PanguPhysicsPluginTests`、`MyGameGameplayPluginTests`、`MyGamePluginCollaborationTests`，均为 fast/area.build，前两者为 unit，协作为 integration。

- [x] **Step 1: 写来源与开关矩阵回归。** 工具发现保留原扫描来源，即使两个根有同名目录或经规范化路径调用也不改变归属。Project fixture 测试：未启用 Plugin 无条目；Engine 开关 OFF 时仅 Project 自带与协作测试；Engine 开关 ON 时三类均存在；全局 BUILD_TESTING OFF 时均无正确性测试。

```cmake
if(engineTestsOff AND enginePluginTestFound)
    message(FATAL_ERROR "Engine Plugin tests must follow the Engine switch")
endif()
if(projectTestsOn AND (NOT projectPluginTestFound OR NOT collaborationTestFound))
    message(FATAL_ERROR "Project-owned tests must not depend on enabling Engine tests")
endif()
```

- [x] **Step 2: 确认红灯。** 通过 `SourcePluginTestOwnership` 的新脚本运行矩阵；当前无来源字段/测试上下文，应因具体归属断言失败。所有 fixture 输出均在已有 ProjectTestHelpers 生成的隔离目录。
- [x] **Step 3: 实现明确来源传递。** DiscoverPlugins 在扫描 Engine/Project 时直接标记 Origin，JSON 输出该字段；CMake 校验枚举，进入每个 Plugin 子目录前调用上下文函数，Engine ENABLED 使用 Engine 开关，Project ENABLED 使用所属 Project 的测试策略，PLUGIN_ID 为已校验 ID。MyGame 在加入 Engine 后、配置 Plugin 前显式设置 owner.project 上下文，子目录归属不泄漏回父级。不能仅按调用栈或源路径字符串前缀猜归属。
- [x] **Step 4: 添加最小实际测试消费者。** Plugin CMake 在 `pangu_tests_enabled()` 为真时显式加入 Tests；夹具测试目标链接相应真实 Physics/Gameplay Module。`PhysicsPlugin.DefinitionMatchesModule` 调用 `pg::fixture::QueryPhysics()`，断言非空及 Descriptor.Id 等于 kPhysicsId；`GameplayPlugin.DependsOnPhysics` 调用 QueryGameplay，断言自身 ID 及唯一依赖 kPhysicsId。仅在 Physics、Gameplay 均已显式启用时创建 `ProjectPluginCollaboration.RegistersProviderFirst`，使用两个真实 Query 创建 Host、验证顺序并实际注册，owner.project。原 MyGameProbe 同步改用 `pangu_register_test` 登记为 integration/build/precommit、BUILD_TARGETS MyGameProbe；零 Plugin 的原 Project 测试仍可运行。只复用现有 FixtureModules.h，不新增 Plugin 开关、CTest 初始化、独立 owner 或自动测试扫描。
- [x] **Step 5: 验证 EXCLUDE_FROM_ALL。** 普通 MyGameProbe 构建不因此构建未消费测试；显式请求 PanguTestsFast/Precommit/Full 时应拉入相应夹具生产 Module 和测试。检查符号/实际行为以及 CTest JSON 的 owner/plugin 标签，而非只检查目标名字存在。
- [x] **Step 6: 验证回归并提交。** 重跑 SourcePluginEnable、SourcePluginStaticCatalog、SourcePluginIncremental、SourcePluginProject、SourcePluginTestOwnership 和工具 CLI/Discovery，保持普通 no-op 行为；对工具 C++ 用宿主编译数据库检查。`git add -- BuildTools/PanguBuildTool/Private cmake/PanguPlugins.cmake CMakeLists.txt Tests/BuildTools/PanguBuildTool Tests/CMake/SourcePluginProject Tests/CMake/SourcePlugins/CMakeLists.txt Tests/MIGRATION.md`；`git commit -m "feat: 按所属工程接入插件测试上下文"`。

Task 6 验收：`b950165`，独立规格/质量评审通过。Windows 主体完整 84/84；末次预算修正后 ownership 1/1，Linux/macOS 受影响工具各 12/12，分轮记录不合成最终源码一次全量。其它桌面完整 ownership 和最终冻结源码矩阵由 Task 9 验收。用户已开启 Windows Developer Mode，非管理员文件链接正式回归 1/1 通过，后续无需因该权限请求 UAC。生产工具既有静态诊断保留为收尾评审事项。

## Task 7: 配套 Presets、正式 full 前提及失败传播

**Files:**
- Modify: `cmake/Platform/{Windows,Linux,macOS}/CMakePresets.json`、`cmake/PanguTesting.cmake`、`Tests/CMakeLists.txt`、`Tests/CMake/Testing/CMakeLists.txt`。
- Create: `cmake/Testing/ValidateFullTesting.cmake`、`Tests/CMake/Testing/WorkflowCompletenessTests.cmake`、`Tests/CMake/Testing/Fixtures/Workflow/{CMakeLists.txt,CMakePresets.json.in}`。
- Modify: `Tests/README.md`、`Tests/Toolchain/README.md`、`Tests/MIGRATION.md`。
- Test: `TestingWorkflowCompleteness`、三运行集合及其构建依赖。

**Interfaces:** Consumes `PanguTestsFast/Precommit/Full`、`PanguBuildToolTestsFast/Precommit/Full`、`TestingConfigure(...)`、`TestingRun(...)`、`TestingListTests(BINARY ... OUTPUT ...)`；produces `<platform>-fast/precommit/full` build、test、workflow preset、`pangu_require_full_testing(OWNER engine|project)` 及 `PanguValidateEngineFullTesting` 构建前提目标。基础 configure preset 名称与 BuildRoot 保持不变；每个 workflow 的 build/test 都引用起始 configure preset。前提函数仅在 OWNER engine 时把 Engine 三开关检查接入 full 构建，Project 自有集合不继承这些开关。

- [x] **Step 1: 写完整性失败回归。** WorkflowCompleteness 使用仅 include 实际辅助接线的最小 Workflow fixture，不能把包含本回归的完整 Engine 测试集再次嵌套运行。检查五类条件：空筛选非零；无 fixture 测试程序/发现失败非零；修改 fixture 源码后 workflow 重建并运行新结果；`PANGU_VALIDATE_TOOLCHAIN=OFF` 时正式 Engine full 构建拒绝；owner.project 仅运行自己的 full 不被强制开启 Engine 测试。局部 fast 构建不把未构建的 full 专用 GTest 占位条目计为通过。

```cmake
# staleSourceWorkflowExit：把隔离 fixture 的测试改为失败后运行 workflow。
if(emptySelectionExit EQUAL 0 OR missingProgramExit EQUAL 0 OR
   staleSourceWorkflowExit EQUAL 0 OR disabledToolchainFullExit EQUAL 0)
    message(FATAL_ERROR "incomplete or stale validation must not pass")
endif()
if(NOT projectOnlyFullExit EQUAL 0)
    message(FATAL_ERROR "Project full must not require enabling Engine tests")
endif()
```

- [x] **Step 2: 确认红灯。** 在临时 fixture 的正式测试工作目录运行新检查，预期因缺 preset/full guard 失败。不要在验收中删除其它任务的真实测试程序来模拟缺失；只操作本用例生成的 fixture。
- [x] **Step 3: 实现配套入口。** build presets 分别请求 PanguTestsFast/Precommit/Full；test fast/precommit 筛选精确 run 标签，full 不遗漏任何普通正确性条目，现有 `<platform>` test preset 保持与 full 一致。全部入口设置 `noTestsAction:error`；workflow 顺序 configure→build→test。
- [x] **Step 4: 落实正式 full 前提。** 实现前提函数，使 Engine full 聚合构建依赖校验目标，通过配置期生成的参数文件检查 BUILD_TESTING、PANGU_BUILD_ENGINE_TESTS、PANGU_VALIDATE_TOOLCHAIN 均 ON；任一不满足给出明确错误。Project 自有集合不接上 Engine 专属 guard。不强制重写父工程 cache。
- [x] **Step 5: 验证与记录。** `cmake --list-presets=all`、`cmake --workflow --preset windows-fast`、`windows-precommit`、`windows-full` 均按预期执行；对三个集合查询 JSON 并验证包含关系、领域和宿主工具条目。确认 full 构建确实构建 Toolchain STATIC 探针及最小最终链接程序。
- [x] **Step 6: 更新命令契约。** README 推荐 workflow；直接 test preset 需要先完成同集合 build preset。基础 `<platform>` build preset 保持普通 ALL 行为，不因此给普通 Ninja 加常驻测试准备命令；不要再把基础 ALL 的完成当成独立宿主测试已准备好。
- [x] **Step 7: 提交。** `git add -- cmake/Platform/Windows/CMakePresets.json cmake/Platform/Linux/CMakePresets.json cmake/Platform/macOS/CMakePresets.json cmake/PanguTesting.cmake cmake/Testing/ValidateFullTesting.cmake Tests/CMakeLists.txt Tests/CMake/Testing Tests/README.md Tests/Toolchain/README.md Tests/MIGRATION.md`；`git commit -m "feat: 提供分层测试构建与运行 workflow"`。

Task 7 验收：`89c6cee`，独立规格/质量评审均通过，无新增问题。Windows fast/precommit/full workflow 分别 59/59、66/66、85/85（2.35/3.34/311.02 秒），集合包含关系与真实 Toolchain 依赖已核验；随后跨平台 fixture 工具链路径修正，经三宿主必要定向复验通过。完整运行先于该夹具修订，当前源码三桌面完整 workflow、Plugin 矩阵与移动端专项仍由 Task 9 验收。

## Task 8: 独立 Benchmark 与三类真实测量

**Files:**
- Create: `Source/ThirdParty/GoogleBenchmark/{CMakeLists.txt,README.md,Repo/}`、`Tests/Module/Benchmarks/{CMakeLists.txt,ModuleIdentityBenchmarks.cpp,ScopeAccessBenchmarks.cpp,StaticModuleHostBenchmarks.cpp,README.md}`、`Tests/CMake/Testing/BenchmarkIsolationTests.cmake`。
- Modify: `.gitmodules`、根 `CMakeLists.txt`、`cmake/PanguTestDependencies.cmake`、`Tests/CMake/Testing/CMakeLists.txt`。
- Test: `TestingBenchmarkIsolation`、`PanguModuleBenchmarks` 的运行冒烟与输出校验。

**Interfaces:** Consumes 独立基准开关和现有真实 Module API；produces `pangu_require_googlebenchmark()`、`PanguModuleBenchmarks`，输出位于 `<BuildRoot>/Bin/`，不登记为正确性 CTest 用例。

- [x] **Step 1: 写开关矩阵回归。** `BUILD_TESTING=OFF`、`PANGU_BUILD_ENGINE_TESTS=OFF`、`PANGU_BUILD_BENCHMARKS=ON` 时，仅请求基准能构建运行，无 gtest/gmock 目标；全部关闭时不要求 Benchmark Repo 初始化；单独开测试不配置 Benchmark；打开基准也不增加普通正确性 CTest 条目。

```cmake
# 对 Benchmark-only 配置实际查询生成目标及 CTest JSON。
if(NOT benchmarkTargetFound OR gtestTargetFound OR gmockTargetFound OR NOT correctTestCount EQUAL 0)
    message(FATAL_ERROR "benchmark-only must not pull correctness tests or GoogleTest/GoogleMock")
endif()
```

- [x] **Step 2: 确认红灯。** `TestingBenchmarkIsolation` 在独立 fixture 中先失败于缺少开关/目标。缺依赖的负例通过 fixture 的缺失源码输入完成，不能移动或删除真实 submodule。
- [x] **Step 3: 接入固定依赖与根入口。** Benchmark 接线位于正确性测试 gate 外，仍限原生桌面；只显式开启时配置上游并创建基准目标，选项按固定适配选择设置。
- [x] **Step 4: 实现三个计时契约。** ModuleIdentity：解析固定合法 32 字符 ID 和格式化，分别登记；沿用 `ModuleIdentityParse.h`，私有 include 只加到基准目标，不提升生产 Public API。ScopeAccess：实际 control/gates 在计时外准备，循环内只执行 TryBorrow、读值和 Release；StaticModuleHost：`CatalogCreate/1`、`/8`、`/32`，计时外准备稳定 Query/Descriptor/线性依赖，每轮包含 Create 和对应 Host 销毁，明确名称为含创建/销毁成本。
- [x] **Step 5: 保证测量有效。** 使用 `DoNotOptimize`/必要的 `ClobberMemory`，输入与输出实际消费；操作失败调用 SkipWithError 并让验收报告失败，不把失败路径当高性能。Benchmark-only 运行检查 JSON 中确有上述基准、无 error_occurred、含参数/单位/迭代和环境信息。
- [x] **Step 6: 验证独立构建与冒烟。** `cmake --preset windows -DBUILD_TESTING=OFF -DPANGU_BUILD_ENGINE_TESTS=OFF -DPANGU_BUILD_BENCHMARKS=ON`；`cmake --build --preset windows --target PanguModuleBenchmarks`；运行 `Build/Windows/Bin/PanguModuleBenchmarks.exe --benchmark_dry_run --benchmark_out=<本轮结果路径> --benchmark_out_format=json`。目录预先创建，结果仅证明冒烟；随后显式恢复正确性开关为 ON、基准为 OFF，再重跑 windows-fast，防止 cache 混用。
- [x] **Step 7: 提交。** 对基准源码用实际编译数据库执行 clang-format/clang-tidy；`git add -- .gitmodules Source/ThirdParty/GoogleBenchmark Tests/Module/Benchmarks Tests/CMake/Testing/BenchmarkIsolationTests.cmake Tests/CMake/Testing/CMakeLists.txt CMakeLists.txt cmake/PanguTestDependencies.cmake`；`git commit -m "feat: 接入独立性能基准与模块测量"`。

Task 8 验收：`1077643`，独立规格/质量评审通过。固定 Google Benchmark v1.9.5 与完整提交，benchmark-only 构建及六场景 JSON dry run、隔离矩阵与同树开关往返、恢复后的 windows-fast 59/59 均通过；实际编译数据库格式/静态检查完成。JSON 字段粒度和上游无异常 regex/format 提示记录为非阻断观察；Linux/macOS 编译链接冒烟、Windows 至少五次正式样本与最终完整矩阵由 Task 9 验收，dry run 不作为性能基线。

## Task 9: 三桌面、移动端专项、长期文档与收尾

Task 9 验收记录提交为 `3aa2f10596b8619b8d3d74c6335027ab928af63b`，最终文档修复为 `b77ace3fb8e8cf0d06abeb5b2f4f116552891639`；限定复评确认 M1/M2 已修复、无新增问题，M3–M6 作为非阻断维护项保留。必要结论已归档，并确认三宿主无残余验证进程后清理本计划临时目录。详见[验收记录](2026-11-05-layered-testing-validation.md)。

**Files:**
- Create: `wiki/foundation/testing.md`、`docs/superpowers/plans/2026-11-05-layered-testing-validation.md`。
- Modify: `wiki/README.md`、`wiki/foundation/{build-system.md,third-party.md}`、`Tests/{README.md,MIGRATION.md,Module/README.md,Toolchain/README.md}`、`BuildTools/README.md`、本计划状态/复选框。
- Test: 三桌面 workflow、独立宿主工具 CTest、三类基准、Android/iOS 现有编译/最终链接/部署入口。

**Interfaces:** Consumes `<platform>-fast/precommit/full` workflow、`PanguModuleBenchmarks`、`BuildRoot/Tests/BuildTools/PanguBuildTool` 的独立 CTest，以及 CrossCompileTests 的 `PANGU_ENGINE_SOURCE_DIR` / `PANGU_TEST_ROOT` / `PANGU_TARGET_PRESET` 参数；produces 可复核验收摘要和长期规范，确认后才标记整项迁移完成。

- [x] **Step 1: 核对场景映射。** MIGRATION 每项有来源、目标测试/入口、关键断言、层级、平台/资源条件、实际结果；三项独立工具测试不得缺席。任何替代都要核对触发模式和失败路径，不用数量/覆盖率代替等效证明。
- [x] **Step 2: Windows 正式验收。** 显式恢复 `BUILD_TESTING=ON`、`PANGU_BUILD_ENGINE_TESTS=ON`、`PANGU_VALIDATE_TOOLCHAIN=ON` 后依次运行三个 workflow；独立宿主 CTest 单独验证运行能力，正式 full 汇总不重复计数。记录首轮、重跑、关键跳过、直接批量/乱序及并发结果。
- [x] **Step 3: Linux/macOS 正式验收。** 在约定共享源码根及各自本地 BuildRoot，运行相应 `<platform>-fast/precommit/full` workflow；通过 WSL Ubuntu 和 `ssh -o BatchMode=yes moozen-macos` 调度。三桌面结果必须属于当前源码状态；任一必要场景未验证则保持计划未完成。
- [x] **Step 4: 移动端编译/链接专项。** Windows 执行 `cmake --preset android-arm64 -DBUILD_TESTING=ON` 与相应 build preset；macOS 执行 `ios-arm64`，仅保留 Toolchain 编译入口，无交叉 GTest 发现/运行。另按 Toolchain README 执行 LinkSmoke 最终链接；显式运行 `CrossCompileTests.cmake`，传入绝对 Engine 根、该宿主 BuildRoot 内独立 `PANGU_TEST_ROOT` 和 `PANGU_TARGET_PRESET=android-arm64|ios-arm64`，检查 Game/Editor 最终链接、SHARED 部署和宿主工具运行。保留 ELF/Mach-O/目标 SDK 检查结果，不宣称设备运行。
- [x] **Step 5: 完成性能交付。** Windows 为三类基准运行 `--benchmark_repetitions=5 --benchmark_out_format=json`，归档到 `Build/Windows/BenchmarkResults/<run>/`，保留版本/完整提交、编译器/STL、优化配置、CPU、负载、参数、原始样本、中位数和离散程度；Linux/macOS 各自编译、最终链接和 dry run 冒烟，不以其结果比较 Windows 性能。记录 fast/precommit 的构建与测试分项耗时，超预算只列优化事项。
- [x] **Step 6: 执行静态检查与离线检查。** 自有 C++ 使用仓库 clang-format；Runtime/基准用根编译数据库，工具测试用专用宿主数据库执行 clang-tidy，不检查上游 Repo。确认测试/基准全关、Benchmark-only、嵌入 Engine 默认关、Plugin 开关矩阵、无联网 configure 及普通 Ninja no-op。局部豁免必须最小范围且说明原因。
- [x] **Step 7: 同步长期文档。** testing.md 负责分类/运行契约；Tests README 负责命令和维护；BuildTools README 说明两套构建目录与独立测试。验收文档仅记录必要结论、命令、版本、结果、关键提交和未完成项，不粘贴大量日志。
- [x] **Step 8: 整分支评审与收尾。** 按执行方式要求完成整分支评审，修复有依据的问题并针对性复验；检查无运行任务依赖后归档必要结论，再清理本计划对应临时目录。保留正式 Tests；无跨平台残留缺口且验收完成后，更新计划状态和实际提交。
- [x] **Step 9: 提交。** `git add -- wiki/foundation/testing.md wiki/README.md wiki/foundation/build-system.md wiki/foundation/third-party.md Tests/README.md Tests/MIGRATION.md Tests/Module/README.md Tests/Toolchain/README.md BuildTools/README.md docs/superpowers/plans/2026-11-05-layered-testing.md docs/superpowers/plans/2026-11-05-layered-testing-validation.md`；`git commit -m "docs: 完成分层测试规范与跨平台验收记录"`。

## 执行约定与自检

- 执行时按对应技能确认工作区隔离；实施前检查工作区状态和各 BuildRoot 无其它会话占用，不在规划阶段创建工作树或改变执行方法。
- 上述迁移任务以旧行为作为回归基线；新增接线先写能识别缺失能力的红灯，再实现。机械搬移不另写镜像测试，也不为追求红灯故意破坏生产逻辑。
- 每个任务检查目标应独立可验收；未到 Task 7 时通过当前基础 preset 加 `--target` / `-R` 定向验证，不提前使用尚未创建的新 workflow。
- 私有 C++ 测试辅助函数按仓库命名/所有权规则实现；计划代码片段是待实现测试的关键断言，不声称本轮已编译或通过 clang-tidy。
- 工作区有真实生产缺陷时按 spec Q16 单独提交最小修复及回归证据；冻结契约变化或明显扩大范围须重开设计讨论。

| Spec 覆盖 | 任务 |
| --- | --- |
| 分类、累计标签、逐用例发现、离线依赖 | 1、5、7 |
| 现有 C++ 行为、Mock、并发、回滚、终止和真实链接 | 2、3 |
| 独立工具三项测试集中、宿主构建、失败汇总 | 4 |
| Engine/Project 归属、Plugin 显式启用及消费者、嵌入 | 6 |
| 配套 workflow、编译契约、full 前提和空跑拒绝 | 5、7 |
| 独立基准、三类测量、Windows 基线 | 8、9 |
| 三桌面、移动端现有专项、文档和生命周期 | 9 |

计划编写自检：逐节覆盖 spec；固定接口在后续任务保持同名；五项 Review Focus 均有归属任务；实现细节以接口/判据表达。实施进度仅在对应任务通过评审后勾选，不把文档检查算成产品或平台验收。

## 版本与官方依据

- [GoogleTest v1.18.0](https://github.com/google/googletest/releases/tag/v1.18.0)、[固定提交](https://github.com/google/googletest/commit/063de7e9578f82b369302001269680b4b1553359)。直接列出源码属于 Pangu 第三方规范允许的目标适配方式，实施时复核许可证和 TU include/link 条件。
- [Google Benchmark v1.9.5](https://github.com/google/benchmark/releases/tag/v1.9.5)、[固定提交](https://github.com/google/benchmark/commit/192ef10025eb2c4cdd392bc502f0c852196baa48)、[版本对应的 CMake 选项](https://github.com/google/benchmark/blob/v1.9.5/CMakeLists.txt)。
- [CMake 4.0 GoogleTest](https://cmake.org/cmake/help/v4.0/module/GoogleTest.html)、[TEST_INCLUDE_FILES](https://cmake.org/cmake/help/v4.0/prop_dir/TEST_INCLUDE_FILES.html)、[Presets/workflow](https://cmake.org/cmake/help/v4.0/manual/cmake-presets.7.html)。使用公开接口，避免依赖某一安装版本的私有实现。

## 执行交接

执行方式为 Subagent-driven；Task 1–9、整分支评审、限定复评及计划临时目录清理均完成。分支 `docs/layered-testing` 保留，未合并或推送；后续维护项与证据索引见验收记录。
