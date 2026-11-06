# 测试控制开关精简 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 删除独立 `PANGU_VALIDATE_TOOLCHAIN` 开关，让 Toolchain 测试随 Engine 测试接入，并统一测试嵌套构建预算的默认值与校验。

**Architecture:** 保留 `BUILD_TESTING`、`PANGU_BUILD_ENGINE_TESTS`、`PANGU_BUILD_BENCHMARKS` 的现有职责；前两者决定 Engine Tests 接入，Toolchain 是该测试集的固有部分。正式 Engine full 只检查前两项，跨平台条件和 Project 自有 full 独立性保持原契约。并发预算共享一个最小 CMake 辅助文件，按需加载。

**Tech Stack:** CMake 4.0+、C++23/Clang 现有测试目标、CTest/workflow。

**Spec:** [分层测试规格](../specs/2026-11-05-layered-testing-design.md)及本轮用户明确同意的精简方案；本轮授权覆盖旧规格中正式 full 必须开启三项布尔开关的要求，修订为两项。其余分类、覆盖、平台和隔离契约继续有效。

**Status:** Completed（2026-11-06）；实现 `c40e90d`、规范及验收 `ef75b6b`、最终文档修复 `007c812`。任务双门、整体评审与限定复评通过，全部发现已关闭，本计划临时目录已清理。[验收记录](2026-11-06-simplify-testing-options-validation.md)。分支 `docs/layered-testing` 保留，未合并或推送。

## Global Constraints

- 正确性总开关为 `BUILD_TESTING`；Engine 测试由 `BUILD_TESTING AND PANGU_BUILD_ENGINE_TESTS` 接入。接入后始终配置 Toolchain 测试，交叉编译仍只创建编译探针，不发现或运行目标 GTest。
- 删除 `PANGU_VALIDATE_TOOLCHAIN` 声明、行为分支、full 参数与强制检查；正式 Engine full 仅要求 `BUILD_TESTING=ON`、`PANGU_BUILD_ENGINE_TESTS=ON`。Project 自有 full 不继承 Engine 开关要求。
- 根工程清除自己已废弃的 `PANGU_VALIDATE_TOOLCHAIN` cache 项，不保留别名或新替代开关；旧 `OFF` 缓存不得关闭 Toolchain 或阻止 full，不修改父 Project 的普通变量。
- 保留 Benchmark 独立开关及 v1.10.0 固定依赖，不修改基准输入、计时或性能结果；不用重复测量证明本次接线改动。
- `PANGU_TEST_BUILD_JOBS` 默认 2、正整数、显式用户值优先；默认值与校验集中在 `cmake/PanguTestBuildJobs.cmake`，由实际需要的调用方 include。不因仅 include 测试登记接口而初始化无关配置。
- 收敛根 Tests、BuildTools 测试及 ProjectTestHelpers 中同一预算的重复默认值/校验，保留预算向嵌套构建、CTest PROCESSORS 和环境的传递。
- 不删减原有效断言、真实库/链接/子进程条件；删除旧开关的反例时用新契约的正例替代，保留两个剩余开关的 full 拒绝、Project-only full、空选择、缺程序与源码变化后重建检查。
- Windows/Android 使用仓库 `Build/Windows`、`Build/Android`；Linux 从 `/home/moozen/WindowsGit/PanguEngine` 运行，BuildRoot `$HOME/PanguEngineBuild/Linux`；macOS/iOS 从 `/Users/moozen/WindowsGit/PanguEngine` 运行，BuildRoot `$HOME/PanguEngineBuild/macOS`、`$HOME/PanguEngineBuild/iOS`，通过 `ssh -o BatchMode=yes moozen-macos`，禁止复制源码。
- 单 BuildRoot 同时一个验证会话；配置接线修改期间冻结共享源码后运行正式验收。临时上下文仅放 `.superpowers/sdd/2026-11-06-simplify-testing-options/`，原始证据放宿主 BuildRoot。
- 更新权威 wiki、当前 Tests 命令及规格；历史计划/日志保留当时事实，可加后续修订链接，不将旧三开关验收改写成两开关历史。
- 所有修改的 CMakeLists.txt 保持 `cmake_minimum_required(VERSION 4.0)`；中文注释/提交，无工具署名。

## Review Focus

1. 旧缓存 OFF：重新配置后旧 cache 键消失、Toolchain 目标和对应 full 构建依赖仍存在，full 不再因旧值失败。
2. Engine/Project/交叉组合：两项剩余开关仍拦截无效 Engine full，Project-only full 独立；移动端开启 Engine Tests 自动生成真实编译探针而无 GTest。
3. 共享预算包括脚本模式：默认 2、显式 1/3 保留、0/负数/非整数拒绝，多次 include 不覆盖用户值。
4. 新 helper 在源码镜像/独立宿主测试中可用，重试诊断和嵌套预算传播没有退化。
5. 当前文档/脚本没有继续依赖已删除开关；历史结果明确当时契约，不通过批量替换伪造验收。

## Task 1: 精简开关、收敛预算并验证

**Files:**
- Modify: 根 `CMakeLists.txt`、`Tests/CMakeLists.txt`、`cmake/PanguTesting.cmake`、`cmake/Testing/ValidateFullTesting.cmake`、`cmake/PanguBuildToolsTesting.cmake`。
- Create: `cmake/PanguTestBuildJobs.cmake`。
- Modify: `Tests/CMake/SourcePluginProject/ProjectTestHelpers.cmake`、`Tests/CMake/Testing/WorkflowCompletenessTests.cmake`、`Tests/CMake/Testing/Fixtures/Workflow/CMakeLists.txt`、`Tests/CMake/Testing/BenchmarkIsolationTests.cmake`，必要时在既有 Testing 回归中补预算契约检查。
- Modify: `wiki/foundation/testing.md`、`Tests/README.md`、`Tests/Toolchain/README.md`、`Tests/Module/Benchmarks/README.md`、`Tests/MIGRATION.md`、批准规格中的现行 full 要求及必要关联规范。
- Create: `docs/superpowers/plans/2026-11-06-simplify-testing-options-validation.md`。

**Interfaces:** `pangu_require_full_testing(OWNER engine|project)` 不变，Engine 的有效配置从三开关改为两开关；`PANGU_TEST_BUILD_JOBS` 名称/默认值/传播不变，通过共享 helper 统一校验；其余测试/基准目标接口不变。

- [x] **Step 1: 写新契约回归并观察红灯。** 先调整 WorkflowCompleteness：两个剩余开关分别 OFF 时仍拒绝；旧开关 OFF 不阻止 Engine fixture full；Project-only full 不强改父缓存；其余原反例保持。使用既有最小 fixture，不递归运行根 full。旧实现应在旧开关 OFF 的新预期处失败，保留真实命令和输出。预算提取为结构调整，不人为制造额外红灯。
- [x] **Step 2: 最小接线修订。** 根工程清理旧 cache；Tests 内 Toolchain 无条件 add_subdirectory；full 参数/validator 只保留两项；移除活跃脚本/fixture 的旧开关依赖。共享预算 helper 按需被三个实际调用方使用，保留脚本缺输入/失败重试语义与默认2/用户覆盖/拒绝非法值。
- [x] **Step 3: 聚焦 green 和提交实现。** Windows 先跑 WorkflowCompleteness、BuildTools/失败传播、ScriptClassification、BenchmarkIsolation 与预算默认/自定义/非法输入检查；证据证明旧 OFF cache 被清除且真实 Toolchain 目标可构建。中文提交实现，使后续正式 Project 回归面对干净工作树；报告列全部实现提交，不用 HEAD~1 缩小评审范围。
- [x] **Step 4: 跨平台验收。** Windows 运行 `windows-full` workflow 一次及必要 SourcePluginProject/镜像回归（full 已含则不重复）；Linux/macOS 在正式 BuildRoot 运行上述受影响 Testing 回归、真实 Toolchain 库/链接检查及恢复后的 fast。Android/iOS 正式 preset 显式开启 `BUILD_TESTING` 和 `PANGU_BUILD_ENGINE_TESTS`，构建两座 Toolchain 编译探针，确认无目标 GTest 发现/运行；本次不重复未改动的移动端部署/设备运行或性能基线。各宿主验证前移除旧 cache 项、结束后恢复正确性 ON/ON、Benchmark OFF、预算2。
- [x] **Step 5: 文档与证据。** 同步现行规范、命令、映射和新验收摘要；历史三开关事实保留并注明后续精简。记录首轮/重跑、精确命令/源码版本/平台条件/结果及未验证范围；仅 CMake/文档变更时执行针对性配置/构建/CTest 与 diff-check，不对未改自有 C++ 重跑 clang 检查。自审后中文提交报告。

## 收尾

- [x] Task 1 独立规格和质量评审。
- [x] 本精简范围最终整体评审及必要修复/限定复评。
- [x] 归档必要裁决、关键提交、证据和遗留项，确认无运行依赖后清理本计划临时目录。

计划自检：五项 Review Focus 分别落在步骤1–5；单任务无跨任务 producer/consumer 冲突；用户已明确同意现行契约修改，执行不等待重复设计审批。
