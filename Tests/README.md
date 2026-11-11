# PanguEngine 测试维护规范

`Tests/` 保存长期维护、纳入 Git 的正式测试。采用**长期保留、定期维护、按需淘汰**的策略，Plan 完成不触发测试清理。目录生命周期总规则见 [AGENTS.md](../AGENTS.md)，构建与平台路径见[构建系统](../wiki/foundation/build-system.md)。

本文件规定测试维护与验收记录要求；具体功能契约以对应 `wiki/` 规范为准。下文区分维护要求与当前构建入口的实现状态，不将测试规划表述为已有平台覆盖。

测试分类、标签和运行集合的权威契约见[测试分类与运行契约](../wiki/foundation/testing.md)。本文件维护实际命令与测试生命周期。

Meta 首版的强类型访问、描述 Pin、真实卸载及 ARM64 移动最终链接入口见 [Meta 测试](Meta/README.md)，三桌面双模式与移动双模式证据见[Meta 正式验收](../docs/superpowers/plans/2026-11-09-pangumeta-tool-validation.md)。根 Full 包含 `BuildTools.PanguMetaTool` 的九个互斥组（Sdk/IR/Request/Context/Ownership/Attr/CodeGen/Publish/Cli），其内层用例不与根调度数量累加。移动仅编译、最终链接和宿主产物检查。

## 保留范围与测试准入

正式测试包括以下类型；类型可以重叠，目录按功能领域组织，无需为每种类型复制测试或建立独立目录。

| 类型 | 验证范围 |
| --- | --- |
| 单元测试 | API、算法、数据结构、错误表达与传播 |
| 集成测试 | Module、Service、构建系统及跨模块协作 |
| 回归测试 | 已确认缺陷的触发条件与修复后的正确行为 |
| 契约测试 | ABI、生命周期、所有权等明确约定的行为；标明对应规范及冻结范围 |
| 平台测试 | 编译器、标准库、SDK、文件系统和原生平台行为差异 |

新增或从临时探针迁入正式测试时，应具备：

- 明确的验证对象、预期结果和失败判据；断言能够发现目标问题，而非仅检查程序能够运行。
- 可维护的源码、必要夹具与输入；适用平台、SDK/工具链等前置条件能够从测试或说明中确定。
- 正式 CMake 测试接线；编译型测试通过构建验收，可运行测试通过 CTest 验收，不依赖 `.probe/` 或 `.superpowers/sdd/` 中的文件。
- 对应缺陷或契约的必要说明。回归测试保留原触发模式；可复现时记录修复前失败、修复后通过的结果，不要求为文档或机械修改新增测试。

测试不以 API 已冻结为准入前提；验证未冻结 API 时应明确当前适用行为，随接口演进维护。已有测试中的 `Probe`、`Fixture` 命名表达验证角色，不决定其生命周期；例如 [Toolchain](Toolchain/README.md) 属于正式测试。

## 测试源码与运行产物

`Tests/` 保留测试源码、辅助脚本、夹具定义、必要输入和预期结果。需要版本化的二进制样本应有明确用途和来源；与运行时生成的产物区分。

运行生成的日志、对象、归档、临时工程及缓存写入对应宿主的 BuildRoot；普通测试工作目录使用该构建树的测试子目录。普通测试不生成 AbiProfile 收据或兼容证明。可变夹具应在测试工作目录内生成或从只读输入复制，不直接修改共享源码中的正式文件。

各平台使用根目录 Preset 和隔离的 BuildRoot，具体路径以[构建系统](../wiki/foundation/build-system.md)为准。清理测试产物前确认没有任务使用；清理产物不删除正式测试源码，也不操作其他任务或平台的构建状态。

并发执行的测试应隔离输出、锁和环境；确需共享资源时，在接线中明确依赖或互斥。测试应能在约定前置条件满足后重新生成所需状态，不依赖上一次运行残留。

## 删除、合并与替代

仅在以下情况考虑删除或合并测试：

1. 对应功能已正式移除。
2. 测试验证的行为契约已废弃。
3. 多项测试验证的条件与断言完全重复，可以合并且不降低有效覆盖。
4. 已有正式测试等效覆盖原测试，包括从临时探针迁入的场景。
5. 测试绑定的内部实现已不存在，且经评估已无独立的有效行为需要验证。

内部实现变化时，优先将断言调整到仍有效的行为边界。测试失败、运行缓慢、偶发失败或维护成本高，本身均不足以构成删除理由。

删除或替代时，在变更说明中记录原测试验证什么、淘汰理由、替代测试或已废弃契约，以及针对性的验证结果。替代依据应核对原触发条件、失败路径、平台差异和关键断言；“新测试更完整”、源码行覆盖率相同或测试数量变化，都不能单独证明等效覆盖。正常构建成功不能替代并发清理竞态或失败回滚回归。

## 运行分层与偶发失败

运行分层用于控制反馈时间，保留同一套有效测试：

| 运行范围 | 用途 | 结果边界 |
| --- | --- | --- |
| 定向验证 | 修改后检查受影响的行为和平台 | 明确测试选择及遗漏范围 |
| 快速验证 | 提供日常高频反馈，优先选择稳定且依赖少的测试 | 不能替代相关集成、契约和平台验证 |
| 完整验证 | 执行该配置下注册的完整 CTest 集，并完成需要的构建验收 | 只代表实际执行的配置与平台 |

桌面平台推荐从仓库根目录运行 workflow，分别覆盖累计的 fast、precommit 和 full 集合：

```sh
cmake --workflow --preset windows-fast
cmake --workflow --preset windows-precommit
cmake --workflow --preset windows-full
```

Linux/macOS 使用对应的 `linux` / `macos` 前缀和约定源码挂载路径。workflow 依次配置、构建所选集合及运行测试；空选择报错。正式 Engine full 要求 `BUILD_TESTING`、`PANGU_BUILD_ENGINE_TESTS` 均为 ON；接入 Engine 测试后始终配置 Toolchain。Project 自有 full 不继承 Engine 的开关前提。

直接运行 test preset 前，必须先完成对应的 build preset，例如：

```sh
cmake --preset windows
cmake --build --preset windows-precommit
ctest --preset windows-precommit
```

基础 `windows` build preset 仍是普通 ALL 构建，不保证独立宿主工具测试已准备；基础 test preset 与 full 选择一致。定向筛选可在完成相应构建后附加 CTest 的 `-R` 参数。

以下示例先构建 precommit 集合，再用多个 `-L` 取交集；单个 `-L` 中的 `|` 取标签并集。两者都限定在已构建的 precommit 集合内：

```powershell
cmake --preset windows
cmake --build --preset windows-precommit
ctest --preset windows-precommit -L '^area\.module$' -L '^boundary\.integration$'
ctest --preset windows-precommit -L '^area\.(module|foundation)$'
```

Module 基准的三桌面配置、采样与恢复命令见 [Benchmark 说明](Module/Benchmarks/README.md)。

慢测试应评估隔离、重复准备、进程启动和运行条件，按需要调整执行频率。偶发失败应调查同步、资源竞争、时限及环境依赖，优先构造能够稳定识别问题的回归。

必要时可暂时隔离不稳定测试，但应记录原因、适用平台、修复事项和恢复条件；验收明确列出被隔离或未执行的覆盖。重跑结果与首轮结果分别记录，不把重跑通过写成首轮完整通过，也不以隐藏失败或无限放宽时限代替修复。

## 平台验证与验收记录

验收应区分目标平台与执行宿主，并注明实际完成的层次：

- **编译验证**：目标源码能够通过所选编译器及配置检查。
- **链接验证**：目标归档或最终产物完成相应链接检查；仅生成 STATIC 归档不等同于最终应用链接成功。
- **宿主检查**：必要时由宿主工具检查目标文件或 SDK，不代表目标程序已运行；普通 Tests 不运行自制 AbiProfile。
- **运行验证**：明确在桌面宿主、模拟器或真实设备上执行的测试及结果。

当前 Android/iOS Preset 默认关闭 `BUILD_TESTING`；显式开启时保留 Toolchain 编译验证，交叉目标不创建需要在设备上运行的测试程序。移动端最终链接冒烟另行显式执行，不计作设备运行验收，详见 [Toolchain 测试说明](Toolchain/README.md)。

记录测试命令、源码版本或工作区状态、配置、实际结果，以及跳过、隔离、重跑和未验证范围。编译型测试即使没有对应的 CTest 条目，也应记录构建结果。不同提交或轮次的通过数量分别保留，不拼接成当前版本的完整矩阵通过结论。

## 双模式 ModuleManager 验收

正式矩阵以[当前验收记录](../docs/superpowers/plans/2026-11-06-module-manager-link-modes-validation.md)为准。桌面默认 Modular；Monolithic 的旧有效 Module 覆盖保留，Modular 的 ModuleRegression 在独立子图自动执行，子标记只防递归。ModuleLinkModeMatrix/Switch 验证父模式、目标类型、非法值/中途改值和往返；真实 DLL 的 RTTI/Pin/Scope/物理卸载不能由静态归档替代。同根模式切换须等上一会话结束，最终 format/tidy/build/CTest 也串行。

Android/iOS 两模式除正式根编译，还须显式运行 SourcePluginProject/CrossCompileTests.cmake，检查真实 Runtime/业务 Query、动态依赖、目标架构和最小宿主最终链接；不运行交叉目标 GTest，不宣称设备 RTTI/动态卸载、打包或签名。两模式六场景 benchmark 单独显式开启/dry run，完成恢复 OFF，新增常驻根成本保留在原计时区间，不混旧基线。

## EngineHost 生命周期验收

正式用例位于 `Tests/EngineHost`，覆盖 Startup、Failure、Shutdown、Scope、ServiceAccess、Concurrency 和 Modular 真实物理卸载；`Tests/Module/ScopeResourceAccessTests.cpp` 覆盖底层精确查询。样例开启时另运行 MinimalHostSmoke，独立 Project 保留消费集合/部署/增量/双模式测试。Modular 的 ModuleRegression 运行旧 Module Mono 和新增 EngineHost Mono，不能仅以根 Modular 用例替代。

失败阶段测试配置真实可观测业务候选，确认常驻失败不继续业务 Query；并发测试检查每次竞态 TryBorrow 的成功值或失败错误，若竞态成功则保留 Borrow 至关闭后，确认其独立阻止完成，不要求调度必然产生成功。规格补强对应 `NonBusyStopFailurePreservesStartupFailure` 与 `QueriedAccessSurvivesPhysicalUnload`，分别验证 E1/E2 分离和新具名令牌跨真实卸载。实际三桌面双模式和两移动双模式证据统一记录于[EngineHost 验收](../docs/superpowers/plans/2026-11-07-engine-host-lifecycle-validation.md)，移动仅最终链接/宿主检查，不运行交叉 GTest。
