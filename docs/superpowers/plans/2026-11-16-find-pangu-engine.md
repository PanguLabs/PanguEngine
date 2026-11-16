# PanguEngine find_package 接入实施计划

> 执行方式：当前会话按 superpowers:executing-plans 顺序实施；不自动提交或升级模型。

**目标：** 以 FindPanguEngine 和 pangu_initialize 统一完整 Engine 的外部源码接入。

**架构：** 查找与初始化分开；初始化保留一个 CMake/Ninja 图、现有 BuildRoot 归属和父 Project 所有权。

**技术：** CMake 4.0+、Ninja；现有 Clang/C++23 Runtime 无源码变更。

**规格：** [接入设计](../specs/2026-11-16-find-pangu-engine-design.md)。

## 约束与评审重点

- 包查找不得准备宿主工具或添加 Runtime 目标；重复查找必须安全。
- QUIET/REQUIRED 遵循 CMake 语义；明确诊断错误源码路径与相对 BuildRoot。
- 保留 Project 的工具链、CTest、Engine Git 锁及两种链接模式。
- 保留 Engine/Project 产物分根；路径包含空格时可用。
- 单模块隔离夹具与 Engine 内部目标不扩大成完整 Engine 接入。

## 任务

- [x] 1. 新增 `Tests/BuildSystem/ProjectIntegration/FindPackageTests.cmake` 并接入正式 CTest；运行并观察入口缺失的 RED。
- [x] 2. 新增 `cmake/FindPanguEngine.cmake`，提供 `pangu_initialize()`，替换旧入口并使包发现测试通过。
- [x] 3. 迁移 MyGame、Meta Project、ModuleLinkModes 两个夹具、LogConfig 和 Shared 夹具；同步当前 wiki 与测试说明。
- [x] 4. 运行包发现、真实构建/运行、产物归属与链接模式定向验证；评审 diff，记录范围与限制。

补充：独立 Shared 配置发现重复源码目录导致目录属性遍历递归。将独立入口移至 `SharedProject/`，保留内部 Shared 目标目录和产物路径，新增 `ModuleSharedProject` 正式回归验证配置、构建和原生测试运行。

## 验收记录

2026-11-16，Windows / Clang 23.1.5 / CMake 4.4.3；工作区未提交，根配置保留 Release / Modular / Samples ON。

- RED：新增包发现脚本首次运行因缺少 `FindPanguEngine.cmake` 失败；补齐后 5 个发现/失败语义场景通过。
- `cmake --preset windows` 通过。沙箱限制会阻止现有 C++ BuildLayout 规范化路径，实际构建与 CTest 在获准的沙箱外执行。
- 第一批定向 CTest：FindPackage、ModuleLinkModeMatrix、ModuleLinkModeSwitch、LogAssertConfig、PanguMetaBuildLayout、PanguMetaProjectSwitch，6/6 通过（367.15 秒）。覆盖双模式消费、无变化增量、配置隔离与 Project 切换。
- 单独 `ModuleRegression` 通过（77.93 秒）；MyGame `ProjectIntegrationTests.cmake` 的 Base 场景通过，使用 `PANGU_ENGINE_ALLOW_LOCAL_OVERRIDE=ON`，验证 Enabled/Disabled、Game/Editor/MinimalHost 服务隔离和 Engine.commit 失效重校验。手动场景日志在 `Build/Windows/Tests/Work/FindPackageMyGame.log`；样例产生的日志已归入同一测试工作区。
- Shared 原独立入口配置复现目录属性递归溢出；分离 `SharedProject` 后，`ModuleSharedProject` 通过（34.93 秒），内层 13/13 原生模块用例通过。WorkflowCompleteness 同批通过。
- 分类检查首次拒绝新 FindPackage 的 precommit 登记；改用现有 ProjectIntegration helper，统一 full 分层与处理器预算。最终 FindPackage 与 TestClassification 复验 2/2 通过（6.00 秒）。以上分批结果不合并宣称全量矩阵通过。
- 独立只读评审及 Shared 补充评审均无阻断性/重要发现；`git diff --check` 通过。

本次无 C++ 源码变更，未额外运行 clang-format/clang-tidy；未重跑全量、Linux/macOS 或 Android/iOS 验收。未提交或推送。正式测试产物保留于 BuildRoot，无额外计划临时目录待清理。
