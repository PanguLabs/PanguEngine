# MinimalHost 测试迁移实施计划

> 执行：当前会话顺序完成；不自动提交。

**目标：** 取消 Samples 开关，将现有 MinimalHost 验证纳入正式 EngineHost 测试。

**架构：** 保留真实进程和服务目标，迁入 Tests 子树；使用现有测试开关和 precommit/full 集合。

**规格：** [测试迁移设计](../specs/2026-11-16-minimal-host-test-design.md)。

- [x] 1. 更新现有脚本测试与测试开关隔离断言，确认迁移前失败。
- [x] 2. 迁移五个文件，移除根选项与脚本 samples 操作；同步 Project 和 ModuleRegression 消费。
- [x] 3. 更新当前文档及历史替代说明；验证脚本、双模式、测试开关和 Project 接线。
- [x] 4. 独立评审、定向验证与 Diff 检查，记录范围与限制。

## 验收记录

2026-11-16，基于 `5f8dbbe` 的未提交工作区；Windows / Clang 23.1.5 / CMake 4.4.3。

- RED：脚本回归因仍输出旧 Samples 参数失败；BenchmarkIsolation 在 Engine 测试开启后因缺少 MinimalHost 目标与 Smoke 失败。
- `cmake --preset windows -U PANGU_BUILD_SAMPLES` 与 `cmake --build --preset windows --target PanguMinimalHost` 通过，根缓存中的旧选项已清除。
- Windows 定向 CTest 6/6 通过（65.19 秒）：MinimalHostSmoke、LocalBuildScripts、BenchmarkIsolation、TestClassification、WorkflowCompleteness、ModuleRegression。覆盖 Modular 宿主、测试 ON/OFF 切换、precommit/full 分类及 Monolithic 最小宿主运行。
- MyGame Base 在开发 override ON 下通过，保留 Enabled/Disabled、Game/Editor 服务隔离及 Engine.commit 失效校验；日志 `Build/Windows/Tests/Work/MinimalHostMyGameBase.log`。
- 独立嵌入式 MyGame 显式启用 Engine 测试，定向构建 PanguMinimalHost 并运行 MinimalHostSmoke，1/1 通过；owner.engine、precommit/full 标签与 EngineBuild/Bin 产物位置正确。日志 `Build/Windows/Tests/Work/MinimalHostEmbedded.log`。
- Windows PowerShell、WSL Ubuntu Bash、macOS Bash 的正式 BuildScriptsTests 均通过；Linux/macOS 使用共享源码与各自本地 BuildRoot，未复制 Engine 源码。
- 三份 C++ 文件与原 Samples 路径的 Git 对象内容相同；新路径的 clang-format dry-run 和基于真实 Windows 编译数据库的 clang-tidy 通过。
- 独立评审无代码/覆盖实质问题，指出的当前构建文档仍写旧 Samples 恢复步骤已修正；历史验收加替代说明，保留历史事实。
- `git diff --check` 通过。根构建保留 Release / Modular / Engine 测试 ON，未提交或推送。

本次未重跑全量矩阵、Linux/macOS Runtime 或移动最终链接；不将脚本回归外推为这些平台的引擎验收。正式测试产物保留在 BuildRoot，无计划临时目录待清理。
