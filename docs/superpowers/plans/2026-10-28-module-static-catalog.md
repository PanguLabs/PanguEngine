# Module STATIC Catalog Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.
> 状态：Completed（STATIC 描述、Catalog 与同步声明事务范围）。代码已进入 `main` 的 `c518990`；原分支 `feature/module-static-catalog`，基线 `4392b2e`。Android 并行采集遗留已由[后续独立修复](2026-10-28-abi-output-concurrency.md)关闭。下文“未提交”和分支约束保留实施当时的状态；最新核对见文末。

**Goal:** 正式实现 STATIC 描述数据、显式依赖 Catalog 与同步声明注册闭环。

**Architecture:** 扩展现有 `PanguModule`，Host 持有描述快照和固定依赖；具体 Scope 持有声明。Catalog 装配源加入最终目标，两个真实 STATIC 夹具检验链接裁剪。

**Tech Stack:** C++23、Clang、CMake 4.0+、CTest。

**Spec:** [批准范围与实现规格](../specs/2026-10-28-module-static-catalog.md)，权威行为来源 `wiki/core/module-api.md`。

## Global Constraints

- 一个 Module 一个实际 STATIC/SHARED 库；本切片仅 STATIC，不新增 EngineHost/Service/Loader 架构层。
- C++23、禁用异常、公开失败返回 `pg::Result/Status`；OOM 不承诺可恢复。
- 限制 256 Module、每个 64 依赖、诊断字符串 256 字节；全零身份无效，版本精确匹配且 `0.0.0` 合法。
- Windows/WSL/macOS 使用同一源码和根 Presets，BuildRoot 分宿主隔离。
- 临时记录存 `.superpowers/sdd/2026-10-28-module-static-catalog/`；不操作其他构建状态，不提交或推送。

## Review Focus

1. 源描述数据销毁或修改不得改变 Host 快照（Task 1 生命周期回归）。
2. Optional 已存在也不能隐式绑定；非法显式选择不能静默忽略（Task 1 表驱动负测）。
3. 注册失败后已有 Scope 内容不变，回调忽略 Declare 错误也不得提交（Task 2 粘性失败回归）。
4. 前置拒绝和回调返回相同 Error 时，报告进度及状态仍能区分（Task 2 状态回归）。
5. 跨 Host/同 Scope 重入、裁剪链接或错误 Catalog 顺序不得破坏事务与依赖（Task 2/3 集成测试）。

### Task 1: 描述与 Catalog 验证

**Files:** 新增 `Public/ModuleDescriptor.h`、`Public/ModuleError.h`、`Public/StaticModuleHost.h`、`Private/StaticModuleHost.cpp`；修改 Module 与 Tests/Module CMake；新增 `Tests/Module/StaticModuleHostTests.cpp`。

**Interfaces:** `ModuleDefinition` 包含 Descriptor、RegisterModule、LinkedStaticModules；`StaticModuleHost::Create(const StaticModuleCatalog&, RegistrationScope&, span<const OptionalDependencyBinding>, ModuleOperationReport&) -> Result<StaticModuleHost>`；Host 暴露只读 `Modules()` 与依赖优先 `RegistrationOrder()`。

- [x] 写正式用例并通过目标构建观察缺失头/API 的 RED，记录结果。
- [x] 实现深复制、有效性和限制校验、显式 Optional 解析及拓扑排序；查询只执行一次。
- [x] 运行 `ctest --preset windows -R '^(ModuleIdentity|StaticModuleHost)$' --output-on-failure`，期望全通过。

### Task 2: 同 Scope 声明事务

**Files:** 新增 `Public/RegistrationContext.h`、`Private/RegistrationContext.cpp`；扩展 Task 1 的 Host、测试和接线。

**Interfaces:** `RegistrationScope` 持有已发布 `RegisteredDeclaration`；`RegistrationContext::Declare(DeclarationId, string_view) -> Status`；`StaticModuleHost::Register(ModuleId, ModuleOperationReport&) -> Status` 使用 Task 1 固定的依赖与身份，报告区分 unchanged/registered/rolled-back。

- [x] 增加事务冲突、原错误、Ignored Declare、一次注册、依赖顺序、两个 Scope、重入用例，观察 RED。
- [x] 实现只暂存再提交、粘性错误、前置检查和失败终态；所有回调在同 Scope 注册守卫内串行执行。
- [x] 运行上述定向 CTest，期望全通过；不新增服务生命周期能力。

### Task 3: 真实 STATIC 集成与验收

**Files:** 新增 `Tests/Module/Fixtures/` 的两个 Module、Catalog 模板及 `StaticCatalogTests.cpp`；扩展 CMake 和相关 wiki 实现状态。

**Interfaces:** 夹具 Query 返回 Task 1 的 ModuleDefinition，回调通过 Task 2 提交声明；最终目标的生成 Catalog 直接引用 Query。

- [x] 建立逆序 Catalog 与裁剪链接测试，先以空 Catalog 观察行为 RED，再接入生成根引用，期望注册两个 Module 及声明。
- [x] 正式 Module 库配置/构建与 Windows 完整 CTest；Linux/macOS 正式 Preset 构建及定向 CTest、Android/iOS Runtime 编译尽可能执行，分平台记录限制。
- [x] 使用仓库 clang-format 和实际 compile_commands 运行针对性 clang-tidy，检查 diff。
- [x] 执行独立全分支评审、处理有效问题；同步文档及本计划验收状态。无需提交或合并。

## 验收摘要（2026-10-28）

验收源码为 `4392b2e` 上本分支未提交实现；没有新提交或推送。保留既有暂存区，不执行 add/reset。C++ 与构建接线完成最终修改后执行下表；后续仅更新文档验收状态。

| 平台/检查 | 本轮实际结果 |
| --- | --- |
| Windows x64 Release | 正式 `windows` Preset 配置、全量构建通过；`ctest --preset windows --parallel 2 --output-on-failure` **40/40 PASS**，446.55 秒，无失败重跑 |
| WSL Ubuntu Linux x64 | 正式 `linux` Preset 全量构建；`ctest --preset linux -R Module --output-on-failure` **4/4 PASS**，未重跑完整 Linux 测试集 |
| macOS x64 | 正式 `macos` Preset 全量构建；`ctest --preset macos -R Module --output-on-failure` **4/4 PASS**，未重跑完整 macOS 测试集 |
| Android ARM64 | 正式 Preset 首轮并行编译在 ABI 工具 `canonical.json` 检查遇到 `output_symlink` / Windows system error 5；保留失败，不计为首轮通过。清除本轮中断后旧 Module 归档，以 `cmake --build --preset android-arm64 --parallel 1` 完成编译、归档及两个 Runtime Module 的宿主 ABI 采集 |
| iOS ARM64 | 正式 `ios-arm64` Preset 配置、构建、归档及两个 Runtime Module 的宿主 ABI 采集通过 |
| 静态检查 | 13 个新增 C++ 文件的 clang-format dry-run；7 个实际 Windows 编译数据库 TU 的 clang-tidy；4 个新 Public Header 逐个独立编译；`git diff HEAD --check` 和 50 个本地 Markdown 链接检查通过 |

### 独立评审与 RED/GREEN

- 元数据/入口/依赖测试先因缺失正式头文件/API 编译失败；事务测试先因缺失 Register/Declare 链接失败，实现后定向通过。
- 真实两个 STATIC 夹具先以空 Catalog 构建成功但 CTest 行为失败，加入直接 Query 根引用后通过；最终 EXE 启用平台链接裁剪，不使用 whole-archive。
- 独立只读全分支评审发现 1 项 Important，无 Critical/Minor：嵌套注册共用报告会污染外层结果。新增同 Scope、跨 Scope 和 Query 报告别名回归后，两组测试均按预期失败；修复所有最终出口完整赋值报告后，纳入上述最终完整 Windows 测试及两桌面定向验收全部通过。未将作者修复后的验证描述为另一次独立复审。

### 遗留与边界

- 本阶段 Android 首轮并行失败发生在既有 ABI 工具的输出路径检查，当时没有修改 BuildTools 或放宽安全校验。串行结果不作为并行修复证据；该问题随后单独复现并修复，正式回归、根因和两轮并行重编译验收见[后续修复计划](2026-10-28-abi-output-concurrency.md)，不混入本阶段表格。
- Android/iOS 没有设备执行；Linux/macOS 没有本轮完整 CTest。最终 EXE ABI 一致性、Profile 嵌入、外部 Loader、Service 工厂/激活、CodePin 与排空仍属后续切片。
- 本切片以每个 StaticModuleHost 为身份域，以具体 RegistrationScope 协调声明发布；跨 Host 的共同身份域留给完整 ModuleManager。调用方负责外部串行化和 Scope/Host/回调的生命周期，OOM 沿用无异常终止政策。
- 共享源码直接在原目录使用新分支，未复制到 Mac 或新建 worktree。临时执行记录位于对应 `.superpowers/sdd/`；因代码未提交且存在上述遗留，保留该目录，不依赖它运行正式 Tests。

## 收尾核对（2026-10-29）

- 已核对 `main` 的 `c518990` 包含本切片及并行输出修复，`a224cad` 在其上实现 STATIC Scope 生命周期。旧执行记录中的“未提交”、Service 工厂/激活尚未实现及 Android 并行遗留均只描述当时阶段；当前 Scope 能力见[后续计划](2026-10-28-module-scope-lifecycle.md)。
- 独立只读复核覆盖 `4392b2e..a224cad` 及本轮收尾修复。Catalog/回调体/Query 报告别名的原问题已关闭；复核发现的失败注册事务工厂析构报告别名已修复，并经同一独立 Reviewer 复审，最终 **Spec Pass / Quality Pass**。该新增问题的 RED/GREEN、验收与临时目录处理统一归档在后续 Scope 计划，避免重复拼接测试矩阵。
- 正式规格、计划、源码及 Tests 长期保留；临时 SDD 的历史分支/日志路径不作为正式验收依赖。
- 本计划临时 SDD 已在本轮完整验收及运行依赖核对后清理，具体范围和校验见上述 Scope 计划的“记录与临时目录”。
