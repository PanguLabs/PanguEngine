# STATIC Scope Lifecycle Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.
> 状态：Completed（STATIC Scope 资源装配与生命周期切片，2026-10-29），已进入 `main` 的 `a224cad`；本轮收尾修复仍在工作区，验收记录见文末。原基线 `c518990`、分支 `feature/module-scope-lifecycle`；下文“未提交”保留实施当时的状态。本轮同样不自动提交或推送。

**Goal:** 将重复工厂、具体 Scope 实例、激活/回滚、Borrow/Busy 和逆序清理接入现有 STATIC Module。

**Architecture:** 声明域保留宿主拥有的稳定工厂节点；运行时协调器持有具体 Scope 资源日志。强类型访问令牌只共享访问控制块，资源仍由 Scope 独占拥有；管理调用串行，已导出的令牌与关闭通过原子闸门线性化。

**Tech Stack:** C++23、Clang、现有 CMake 4.0+ / CTest；统一内部仅移动工厂适配，不依赖 libc++ 尚未提供的 `std::move_only_function`。

**Spec:** [实施设计](../specs/2026-10-28-module-scope-lifecycle-design.md)；长期约束以 `wiki/core/service.md`、`module-api.md` 与 Memory Ownership Contract 为准。

## Global Constraints

- 仅同次构建 STATIC，仍在一个 `PanguModule` Library；没有统一业务 IService、ServiceLocator、BinaryImage 或 SHARED Loader。
- Module/Service 身份不来自 RTTI、地址或序号；临时 Host Domain/Scope generation 仅用于运行时句柄。
- 256 Module、64 Module 依赖、256 字节文本、单次 256 声明、声明域 4096 声明；最多 256 同时在用 Scope、每工厂 64 Scope 依赖。
- 工厂每次生成独立实例；资源先追踪再 Start；创建/激活失败保留原 Error；Busy 保留对象、依赖及工厂，不提前 Destroy。
- 冷路径标准拥有型存储 OOM 终止；调用方保持注册域/Host 生命周期；管理操作外部串行，导出访问令牌支持并发关闭。
- 正式 Presets、共享源码和按宿主隔离 BuildRoot 不变；临时文件放本计划 `.superpowers/sdd/`，不自动提交/推送。

## Review Focus

1. Host 移动、后续注册扩容或 Scope 槽位复用不能使工厂配置、控制块及旧令牌误指向新对象。
2. Factory/Start 第 N 项失败必须只撤销本次资源，Poll Busy 不能丢失未交付 Scope 的清理责任。
3. 依赖先构造、逆序逐个 Stop/Poll/Destroy，内部依赖保护不能造成全图自锁。
4. 闸门关闭与并发 Borrow 获取必须有明确线性化点；旧令牌可活得比 T 久，但不能继续取得 T。
5. 同值 Error 的前置拒绝与执行失败进度、后续 Busy 与原始失败、嵌套报告复用必须正确区分。

### Task 1: Scope 身份、仅移动工厂与访问闸门

**Files:** 新建 `Public/ScopeIdentity.h`、`ScopeFactory.h`、`ScopeAccess.h`、`ModuleDeclaration.h`；扩展 `ModuleError.h`；新建 `Tests/Module/ScopeAccessTests.cpp` 并接线。

**Interfaces:** `pg::service::Scope{kHost,kContext,kWorld}`；`ScopeHandle{uint64_t Domain,uint32_t Slot,uint32_t Generation}`；`ScopeFactory` 保存 noexcept Status(ScopeBuildContext&) callable，移动不复制；`detail::ScopeGate{TryEnter,Leave,Open,Close,Count}`；`ScopeAccess<T>::TryBorrow()->Result<ScopeBorrow<T>>`，Borrow 可移动/Release。控制块包含独立 Module/Scope/Resource 三闸门和 T 指针；实例被销毁前必须关闭 Resource 闸门且 Count==0。

- [x] 先写不可复制/可移动、空令牌、关闭后拒绝、已接纳 Borrow 阻止销毁、并发获取/关闭与控制块长于实例的测试，观察缺失 API 的 RED。
- [x] 实现上述原语，不为访问令牌拥有 T 或调用业务 Destroy。
- [x] 构建并执行 `ctest --preset windows -R '^ScopeAccess$' --output-on-failure`，通过；使用仓库 format/tidy。

### Task 2: 稳定工厂声明与资源构造上下文

**Files:** 新建 `Public/ScopeBuildContext.h`、`Private/ScopeBuildContext.cpp`；扩展 `RegistrationContext.h/.cpp`；调整 DeclarationId 引用；扩展正式注册/资源测试。

**Interfaces:** `ScopeDependency{DeclarationId Id, pg::service::Scope Scope}`；`ScopeHooks<T>{Status(*Start)(T&)noexcept,void(*RequestStop)(T&)noexcept,Status(*PollStopped)(T&)noexcept}`；`ScopeBuildContext::{Owner,Scope,Own<T>,Dependency<T>}`。内部 `detail::ScopeBuildSink` 接收 `unique_ptr<ErasedScopeObject>` 并返回共享控制块；依赖返回带类型核验信息的受控借用；Reject 为粘性失败。`DeclareFactory(DeclarationId,Scope,ScopeFactory,span<const ScopeDependency>,string_view)->Status` 保存稳定节点；现有 Declare 保留。

- [x] 仅移动捕获配置、重复实例、坏 Hook/未声明或类型不符依赖的核心测试先观察缺失 API 的 RED；空函数指针工厂另行行为 RED→GREEN；零或多次 Own 后补验收通过，未另计独立 RED。
- [x] 实现模板资源适配及稳定节点，保证 Deleter 在原创建方执行；工厂期间不 Start。
- [x] 定向 `ScopeAccess|ModuleRegistration` 回归通过，确认旧注册事务及粘性失败不退化。

### Task 3: STATIC Scope 协调器与 Module 生命周期

**Files:** 新建 `Private/ScopeRuntime.h/.cpp`；扩展 `StaticModuleHost.h/.cpp`、现有 Module CMake；新建 `Tests/Module/ScopeLifecycleTests.cpp`。

**Interfaces:** Host 保留 Create/Register/Modules/RegistrationOrder，增加 `HostScope()->ScopeHandle`、`CreateScope(Scope,ScopeHandle,ModuleOperationReport&)->Result<ScopeHandle>`、`Activate/Load/BeginUnload/TryFinishUnload(ModuleId,report)->Status`、`BeginCloseScope/TryFinishCloseScope(ScopeHandle,report)->Status`、`PollCleanup/BeginShutdown/TryFinishShutdown(report)->Status`。报告增加 Scope/Declaration、OriginalError/CleanupError，始终值拥有；Module 状态扩展 Active/CleanupPending/Unloaded。管理操作拒绝同 Host 重入，移动 Host 使用稳定内部存储。

工厂跨 Module 依赖在 Activate 前解析为本 Host 的固定 Provider，参与保守合并图无环检查及 Provider 的卸载保护；注册可以先收集声明，Activate 前置拒绝保持 Registered。Scope DAG 按具体 Owner 解析祖先，不隐式找到其他 Context。纯元数据不生成实例。失败 Context 不交付成功句柄，Host 的 PollCleanup 继续推进；重新 Load 分配新的访问代际，不复活旧令牌。

- [x] 先写 A Ready/B 失败不影响 A/C 后续成功、注册一次工厂多次、Start 部分失败+Busy、激活跨 Scope 回滚、外部 Borrow、依赖逆序、错误所有权、旧句柄/外来 Host、重入和 Host 移动，观察缺失 API 的 RED；局部声明环及评审问题另有行为 RED→GREEN。
- [x] 实现状态机及所有预算/前置条件，逐个 Stop/Poll/Destroy；父级在子级完成前不能消失。
- [x] 补齐 Module Load/Unload/Shutdown 和析构误用子进程；kRegistrationFailed 为源码兼容别名，失败报告明确 CleanupPending。
- [x] 执行上述定向验证，并纳入最终完整 Windows CTest，全部通过。

### Task 4: 真实 STATIC 接线、文档与平台验收

**Files:** 扩展 `Tests/Module/Fixtures` / `StaticCatalogTests.cpp` 与 README；同步 `wiki/core/service.md`、`module-api.md` 实现状态及本计划。

- [x] 真实 STATIC 查询入口注册可重复工厂，在最终宿主执行多 Scope 装配/关闭，保持裁剪根引用及无全局业务构造。
- [x] Windows 正式全量构建及完整 CTest；Linux/macOS 正式 Preset 构建与生命周期定向回归；Android/iOS 正式交叉编译及宿主 ABI 采集，分开记录限制。
- [x] 受影响 C++ 使用真实数据库 clang-tidy、clang-format；Public 头独立编译；diff/文档链接检查。
- [x] 独立全分支评审，处理有效问题并记录 RED/GREEN、验收及遗留；未提交、推送或清理其他工作区。

## 最终验收（2026-10-29）

源码版本为 `c518990` 加本分支未提交实现；最终代码停止修改后执行下表，之后仅归档文档。复用同一共享源码，各宿主使用正式 Preset 与隔离 BuildRoot。

| 平台/检查 | 本轮结果 |
| --- | --- |
| Windows x64 Release | `cmake --preset windows`、全量构建通过；`ctest --preset windows --parallel 2 --output-on-failure` **45/45 PASS**，470.67 秒，无失败重跑 |
| WSL Ubuntu Linux x64 | 正式 `linux` 全量构建；`ctest --preset linux -R Scope` **4/4**、`-R Module` **4/4** 通过，非完整 Linux CTest |
| macOS x64 | 固定 PATH，正式 `macos` 全量构建；Scope **4/4**（1.26 秒）、Module **4/4**（2.79 秒）通过，非完整 macOS CTest |
| Android ARM64 | 正式 `android-arm64 --parallel 8` 配置/编译/归档及两个 Runtime Module 的宿主 ABI Collection 通过，五个 Module TU 均实际编译；没有设备执行 |
| iOS ARM64 | 正式 `ios-arm64 --parallel 2` 配置/编译/归档及宿主 ABI Collection 通过，五个 Module TU 均实际编译；没有设备执行 |
| 格式/静态/头文件 | 21 个受影响 C++ 文件 clang-format dry-run 通过；11 个实际 Windows 编译数据库 TU 执行完整仓库 clang-tidy，退出码均 0；8 个公开头逐个独立语法编译通过；差异及 21 个本地 Markdown 链接检查通过 |

### 独立检查与修复

- 设计由独立 Builder Check 检查，结论 `nothing important left`。首位代码评审因模型额度中断，不能视为完整通过；替换上下文完成了全分支评审，得到 **4 Important、1 Minor，无 Critical**。作者对文档借用失效项按实际悬空风险一并修复。
- 前一不完整评审提示的 Module 清理顺序/Scope 原错，以及作者确认的空函数指针/共享声明域工厂析构重入，四组用例行为 RED。修复传递 Module 依赖阻挡、原错保留、空工厂检测及共享域撤销守卫后，ScopeBuild/ScopeLifecycle/ScopeDestructor **3/3** 通过。
- 完整评审指出：Register 失败进度与 CleanupPending 不一致；CleanupError 混入原错；未 Active 的无关声明环阻断新 Scope；失败 Scope 回收后 Shutdown 再次丢原错。新增注册 3 项断言和生命周期 5 组用例全部 RED；修复后定向及最终完整矩阵通过。没有把作者修复后的 TDD 验证写成另一轮独立复审批准。
- 初始仅元数据阶段的“Host 析构后仍保留注册项”被本轮生命周期撤销取代：原字符串独立复制断言保留在 Host 存活期间，新增 Shutdown 后声明为空的断言，没有删除该覆盖。
- 实际 libc++ 缺少 move_only_function 的预检促使采用统一内部仅移动工厂；未修改或降级工具链。Apple 初次裸编译缺少 Sysroot 的错误不作为可用性结论，最终以正式 Preset 的实际 SDK 参数验收。

### 保留项与范围

- clang-tidy 保留 **7 条非阻断提示**：私有 Definition/RecipeOrder 可 const 两项、反向循环一项、私有参数命名两项、单元素聚合初始化尾逗号两项。未扩大检查禁用范围；不把退出 0 描述为无诊断。后续可按需整理这些私有接口/单元素布局。
- 仅同次构建 STATIC 的资源生命周期；API/布局仍 Draft，Module/资源投影合并门槛保守，后续更精细阶段分析可放宽；尚未实现完整业务 Service Registry、外部候选发现、SHARED CodePin/Loader、最终 EXE ABI、TaskScheduler/GPU 退休。
- 管理操作需外部串行，导出的强类型 Borrow/关闭有原子线性化保护；业务 T 的数据同步仍由自身负责。Borrow/工作 Busy 时须保持 Host/声明域存活并显式推进 Shutdown；违规析构使用始终有效终止，不提前释放对象。冷路径 OOM 不承诺可恢复。
- Shutdown 只锁存开始时尚未完成的失败并保存到结束，不自动导入已完成历史错误；历史诊断由调用方保留此前值快照。声明撤销会使旧 Declarations 借用失效。
- Windows 既有文件/目录 symlink 负测受账户权限限制，正式日志可有 unavailable/SKIP；本轮没有扩大账户权限。Linux/macOS 未执行完整 CTest，移动端未执行设备程序。
- 保留工作区和本计划 `.superpowers/sdd/`，未提交/推送；正式 Tests 不依赖临时目录。其他平台或任务缓存未清理。

## 收尾复核与修复（2026-10-29）

### 源码与独立复核

- 原切片已包含在 `main` 的 `a224cad`；本节记录其上的未提交收尾修复，不把原阶段的“未提交”或历史分支当作当前状态。本轮不执行 commit、push、add 或 reset，保留用户已有暂存状态。
- 独立 Reviewer 只读检查 `4392b2e..a224cad`、三项计划、冻结规范、最终实现及正式测试，发现 **1 Important、1 Minor，无 Critical**。
- **Important：失败事务工厂析构覆盖报告。** `Register` 先写最终失败报告，局部事务随后析构，暂存工厂捕获的析构回调可重入同声明域并共用报告。内层虽正确返回 Busy，却覆盖外层 CleanupPending、原 Error 和 Module 身份。修复在保存原错、记录注册失败后，保持注册域与 Runtime 守卫有效，先清空 `PendingRecipes`，再写最终报告。
- 正式 `FailedRegistrationRetiresFactoriesBeforeReporting` 覆盖回调直接返回自定义原错及忽略粘性声明冲突两条路径；联合检查析构重入 Busy、最终报告原错/进度、双方 Module 状态、未发布声明，以及守卫退出后另一 Host 可正常注册。修复前 `ctest --preset windows -R '^ScopeLifecycle$'` **行为 RED**，唯一失败名为该新增用例；修复后 `ModuleRegistration|ScopeLifecycle|ScopeDestructor` **3/3 GREEN**。
- **Minor：传递依赖缺少正式回归。** 原两节点 Busy 用例扩展为直接依赖与 `A → 无资源 Bridge → Provider` 两种配置，确认 Busy 时 Provider 未 Stop/Destroy，完成后销毁顺序为依赖方先、Provider 后。既有闭包实现无须修改；不把新增覆盖写成生产缺陷的 RED/GREEN。
- 同一独立 Reviewer 对修复及测试再次只读复核，结论 **Spec Pass / Quality Pass**，无剩余 Critical、Important 或 Minor。此前七条非阻断 tidy 建议仍按原范围保留。

### 本轮验证

源码为 `a224cad` 加本节修复；所有宿主读取同一共享源码，BuildRoot 按正式 Preset 隔离。

| 检查 | 本轮实际结果 |
| --- | --- |
| Windows x64 Release | 正式全量构建通过；完整 `ctest --preset windows --parallel 2 --output-on-failure` **45/45 PASS**，473.81 秒，退出码 0，无失败重跑 |
| WSL Ubuntu Linux x64 | 正式 `linux` 全量构建及 Module / Scope 定向 **8/8 PASS**；最终测试格式整理后另重建并复验 ScopeLifecycle **1/1 PASS**；未执行完整 Linux CTest |
| macOS x64 | 正式 `macos` 全量构建及 Module / Scope 定向 **8/8 PASS**（4.87 秒）；固定 CMake/MacPorts PATH，未执行完整 macOS CTest |
| C++ 质量 | 两个受影响 C++ 文件 clang-format dry-run 通过；真实 Windows 编译数据库 clang-tidy 退出码 0。测试中一条既有单元素 trailing-comma 提示保留，没有新增用户代码诊断或扩大豁免 |
| 文档检查 | 6 份受影响 Markdown 的 28 个本地链接均可解析；`git diff HEAD --ignore-submodules=all --check` 通过 |
| 范围限制 | 本轮未重建 Android/iOS，也未执行设备程序；原移动端验收保留其前一阶段范围。Linux/macOS 全量构建有既有 WindowsCollector 非宿主辅助函数 unused 警告，构建退出 0，不声称无诊断 |

首次在受限沙箱执行 9 项定向测试，7 项 Module/Scope 通过，两项 ABI 文件系统测试在普通夹具路径立即 native 5；同命令移至沙箱外后两项均通过，详见[并发修复收尾记录](2026-10-28-abi-output-concurrency.md)。这不是生产校验放宽，也不作为首轮全绿。

### 记录与临时目录

- 三项正式计划已关联 `c518990` / `a224cad`，旧分支、未提交和遗留叙述保留为历史；wiki 将 2026-10-28 Descriptor/Catalog 明确标为历史切片，并与当前 Scope 实现边界对齐。
- 完整验收通过、必要结论归档后，检查 Windows、WSL Ubuntu 与 macOS 进程，未发现运行中的构建/测试或 SDD 目录使用者；生产源码、构建接线和正式测试不引用这些临时产物。
- 按用户授权删除 `.superpowers/sdd/2026-10-28-module-static-catalog/`、`.superpowers/sdd/2026-10-28-abi-output-concurrency/`、`.superpowers/sdd/2026-10-28-module-scope-lifecycle/`，合计 **3 个目录、81 个文件、621,611 字节**。删除前核对绝对路径及无 Reparse Point，删除后确认三个路径均不存在；`.superpowers/sdd/.gitignore` 和 Git index 的 SHA-256 均保持不变。
- 上述目录中的详细命令、阶段日志、头文件/标准库探针与 ledger 已是历史路径；本计划保留独立复核发现、修复、RED/GREEN 和平台验收，不依赖已删除文件读取结论。正式规格、计划、源码、Tests、`.probe/` 和各平台 BuildRoot 均保留。
- `Build/Windows/Testing/Temporary/LastTestsFailed.log` 仍残留 08:53 的 RED 用例 `43:ScopeLifecycle`；更晚的完整 `LastTest.log` 确认 **45 个 Test Passed、0 个 Test Failed** 并正常结束，CTest 进程退出 0。旧清单不表示本轮完整验收失败；构建日志原样保留。
