# 嵌套测试构建第一轮优化实施计划

> 执行方式：当前会话直接实施，参考 superpowers:executing-plans；用户已批准会话中的方案并要求开始。完成后进行一次独立只读评审。

**目标：** 保持全部覆盖，统一嵌套预算并以实测选择 Windows 并行设置。
**规格：** [设计](../specs/2026-11-15-nested-test-builds-design.md)。
**基线：** `f5a5f61`；既有 Windows Full 315/315，2149.78 秒。

## 任务

- [x] 1. 扩展现有基础设施测试，验证 CTest → 子脚本 → 子配置和显式工具测试配置的预算继承、覆盖与非法值诊断；记录 RED。
- [x] 2. 修复预算传递，补齐现有 Project/Module/Log/基础设施辅助脚本的命令计时；定向验证 GREEN，并提交干净的测量源码。
- [x] 3. 在单一 Windows 验证会话内顺序比较三个慢测的 2/4/6 并行，记录耗时和内存；根据证据选择 Windows 内层预算及外层调度，保留现有锁和聚合构建串行。
- [x] 4. 更新测试规范，执行受影响回归和 Windows Full，完成独立分支评审、必要修复及验收记录；归档结论后清理本计划临时工作区。
- [x] 5. 按用户后续授权补充 Linux/macOS 定向验收：保持默认预算 2，构建 full 依赖并运行 TestInfrastructure 七项、Meta 增量、模式切换和 Project 切换，核对 JUnit 后归档。

## 评审重点

- cache / 显式参数优先级、跨进程环境继承与非法值不得被静默归一化。
- `PROCESSORS`、脚本、宿主工具、内层 CTest 的预算必须一致；聚合构建不得放大并发。
- 不改变测试发现、输出捕获、失败诊断、no-op 和恢复断言。
- 并行执行必须保留共享工具锁、独立夹具目录和固定源码版本。
- 性能数据区分平台、配置、冷准备与宿主工具复用；不把单次采样作为稳定性能保证。

## 验收记录

预算与计时实现：`a9bbe78`。现有两项基础设施回归在旧实现失败，修复后预算 4 的基础设施七项通过（37.77 秒）；纯工具上下文初始化调整后的三项定向回归通过（11.09 秒）。另外补充工具附加参数不能放大父预算的回归，旧参数顺序已观察到预算被改为 7 而失败。

### 并行度测量

固定 `a9bbe78`，Windows Release/Modular，override OFF，12 逻辑处理器、31.6 GiB 内存。每项使用新的随机隔离夹具，复用已准备的宿主 Meta 工具；按预算 2 → 4 → 6 顺序运行，外层 CTest 串行。每组合仅一次样本，存在顺序及系统文件缓存影响，不能作为稳定性能保证。

| 测试 | 2 并行 / 秒 | 4 并行 / 秒 | 6 并行 / 秒 |
| --- | ---: | ---: | ---: |
| ModuleLinkModeMatrix | 236.28 | 161.67 | 139.54 |
| ProjectIntegration.PluginTestOwnership | 299.88 | 243.52 | 228.39 |
| PanguMetaProjectSwitch | 270.65 | 175.97 | 145.74 |
| 合计 | 806.81 | 581.16 | 513.67 |

全部九项通过。4 并行合计较 2 并行缩短约 28%；6 并行较 4 并行再缩短约 12%。约每秒采样 CTest 及其后代进程的工作集总和，三项最大观测值分别为 1508.7、1292.4、1648.4 MiB；采样可能漏过瞬时峰值，且共享页会重复计数，不等于独占物理内存。

选择 Windows 内层 4、外层预算 8，允许两项重构建同时调度并为宿主保留处理器余量；6 保留为可覆盖配置。本轮不改变其他平台默认值或缩小资源锁，完整调度收益以最终 Full 实测为准。

### 并发评审与补强

独立只读评审核对 `f5a5f61..9928cae`，未报告确定缺陷。随后针对同一工具多个组共用 CTest 运行树补充真实并发夹具，旧实现实际发生 PRE_TEST 发现竞态：GoogleTest 读取不完整 JSON，一组失败。为同一工具的组增加工具级资源锁，不同工具保持独立调度；既有 `PanguMetaHostBuild` 和宿主工具文件锁不变。工具组运行环境同时携带选定的预算变量。

并发夹具与分类回归已通过（5.71 秒）。

### 首轮 Full 失败与修复

`fda2c01` 上执行正式 Windows Full，`ModuleLinkModeSwitch`、`PanguMetaProjectSwitch` 和 `ModuleLinkModeMatrix` 的 no-op 断言失败；`HostModuleConsumption` 已通过，其余尚未完成时主动停止该轮，不能计作完整通过。日志：`Build/windows-full-nested-builds-20261115-125528.log`。

根因是不同原生图进入共享宿主引导时，`BuildMetaTool.cmake` 即使 Ninja 报告无工作仍无条件刷新工具 exe 时间，导致其他图重复生成。扩展 `PanguMetaIncremental`，直接验证第二次无变化引导保留工具时间；旧实现稳定失败（39.40 秒），修复后完整原增量场景通过（53.97 秒）。修复在既有文件锁内按登记输入与引导脚本的新旧决定是否推进时间，保留实际输入变化的失效处理；另补未被编译的登记头变化后只失效一次的检查。

### 最终 Windows 验收

修复提交 `dcb82201e34127c0aa9612bbf0f2e24700ac4910`，被测 tree 为 `64ca90876ea6fa45d156e45e18a11daece53b745`。干净源码、override OFF，Windows Release/Modular，BUILD_TESTING 与 Engine 测试均开启；执行 `cmake --workflow --preset windows-full`，配置、full 依赖构建和 CTest 全部通过，workflow 退出码 0。

| 指标 | 原基线 | 最终 Full |
| --- | ---: | ---: |
| 根 CTest | 315/315 通过 | 315/315 通过 |
| CTest 实际耗时 / 秒 | 2149.78 | 1117.73 |
| CTest 实际耗时 | 35 分 49.78 秒 | 18 分 37.73 秒 |

CTest 本轮缩短 48.01%；workflow 总耗时 1128.50 秒。对照基线日志逐项核对，315 个用例名称完全一致，30 项嵌套测试的运行集合保留。新并发设置可能延长个别测试的独立耗时，整体收益按实际总时间衡量；Label Time 的 `sec*proc` 随 PROCESSORS 加权，不能用于两轮壁钟耗时比较。此为同一宿主上的一次完整对照，不是跨机器或多次统计性能保证。

本轮完整通过包括此前失败的三个 no-op 场景、工具组共享发现目录的正式回归、Meta 无变化引导及未编译登记头的一次失效回归。首次 Full 失败与本次重跑分别保留；没有把首次失败写为首轮通过。

关键提交：`a9bbe78`（预算与计时）、`9928cae`（Windows 调度与预算防放大）、`fda2c01`（工具组 CTest 隔离）、`dcb8220`（共享 Meta 无变化时间戳）。独立只读评审范围为 `f5a5f61..9928cae`，后续并发修复由 RED→GREEN 和最终 Full 验证；没有进行第二次独立评审。

最终日志：`Build/windows-full-nested-builds-20261115-130615.log`。九项测量的 JSON、分项日志/JUnit、采样方法及执行账本归档至本机 `Build/Windows/Tests/Results/NestedBuildOptimization-dcb8220/`；正式输入与回归均已版本化，临时采样脚本不是功能验收依赖。确认无运行任务依赖后清理本计划 `.superpowers/sdd/2026-11-15-nested-test-builds/`，最终归档提交仅更新本设计和计划文档。

### 边界与后续

- 按用户要求在现有 checkout 的新分支实施，验证期间独占该 checkout；未另建 worktree，分支保留供后续整合。
- Windows 默认 4/8 为宿主保留处理器余量；单项串行采样中 6 比 4 约快 12%，可手动覆盖，默认选择兼顾两项重构建调度。
- 第一轮保留全部重复场景和既有共享锁；矩阵精简、夹具范围收缩与缩小锁域留到第二轮，现有重复准备成本仍存在。
- Windows 优化阶段未重新运行其他平台；随后补充的 Linux/macOS 定向证据见下节。Android/iOS 仍未重新运行，不将桌面结果扩展为移动平台验收。

### 后续 Linux/macOS 定向验收

用户授权后，在干净的 `bceef9cc25460bf56bf4192f0ad432d9d6c28191` 上执行。共享实现与已通过 Windows Full 的 `dcb8220` 相同，期间未修改 tracked 文件或 HEAD。两端均 Release/Modular、BUILD_TESTING=ON、PANGU_BUILD_ENGINE_TESTS=ON、override OFF，保持原内层预算 2；定向 CTest 显式使用 `--parallel 2`，不改变平台 preset 的默认并行配置。

| 宿主 | 工具链 | 正式 BuildRoot 文件系统 | 定向结果 | CTest 实际耗时 |
| --- | --- | --- | --- | --- |
| WSL Ubuntu | CMake 4.4.2 / Clang 23.1.0 | ext4 | 10/10 通过 | 309.01 秒 |
| macOS | CMake 4.4.3 / MacPorts Clang 23.1.2 | APFS | 10/10 通过 | 1072.29 秒 |

Linux 从 `/home/moozen/WindowsGit/PanguEngine` 的 CIFS 挂载构建，BuildRoot 为 `/home/moozen/PanguEngineBuild/Linux`，LLVM_ROOT 为 `/opt/llvm-23.1.0`。macOS 通过 `ssh -o BatchMode=yes moozen-macos`，从 `/Users/moozen/WindowsGit/PanguEngine` 直接读取同一份共享源码，BuildRoot 为 `/Users/moozen/PanguEngineBuild/macOS`，LLVM_ROOT 为 `/opt/local/libexec/llvm-23`；未复制 Engine 源码另行构建。macOS 宿主为 2 逻辑处理器、8 GiB 内存。

两端依次执行以下正式入口，其中 `platform` 分别为 `linux`、`macos`，`result_root` 位于各自 BuildRoot 的 `Tests/Results`：

```sh
cmake --preset "$platform" -DBUILD_TESTING=ON -DPANGU_BUILD_ENGINE_TESTS=ON \
    -DPANGU_TEST_BUILD_JOBS=2 -DPANGU_ENGINE_ALLOW_LOCAL_OVERRIDE=OFF
cmake --build --preset "${platform}-full"
ctest --preset "${platform}-full" \
    -R '^(TestInfrastructure[.]|PanguMetaIncremental$|ModuleLinkModeSwitch$|PanguMetaProjectSwitch$)' \
    --parallel 2 --output-on-failure --output-junit "$result_root/targeted.xml"
```

配置、full 依赖构建及十项测试在两端均退出 0。七项基础设施测试覆盖 Registration、DependencyIsolation、BenchmarkIsolation、BuildToolsTestIntegration、BuildToolsFailurePropagation、TestClassification、WorkflowCompleteness。额外三项包含无变化共享引导保留时间戳、登记头变化只失效一次、Monolithic/Modular 往返以及 Debug/Release 与 Project 切换恢复。分别解析两份 JUnit，核对选中的十个名称及失败、错误、跳过均为零；这是定向运行，不是 Linux/macOS Full。

原始配置/构建/选择/CTest 日志与 `targeted.xml` 位于各自主机：

- Linux：`/home/moozen/PanguEngineBuild/Linux/Tests/Results/NestedBuildOptimization-20261115T091836/`。
- macOS：`/Users/moozen/PanguEngineBuild/macOS/Tests/Results/NestedBuildOptimization-20261115T091845/`。

验证后核对两端源码仍为被测提交、工作区干净，根缓存保持 Modular 与预算 2。没有实现代码修改，沿用 Windows Full 结果；本次仅提交验收文档。必要执行结论与辅助脚本归档后，清理重新建立的本计划临时工作区。
