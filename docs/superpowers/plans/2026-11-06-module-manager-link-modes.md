# 统一链接模式与 ModuleManager Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 建立统一 Monolithic/Modular 构建、强类型接口与内部 RTTI 校验，以及支持已有 Scope、真实动态加载和安全卸载的 ModuleManager。

**Architecture:** 保留每 Module 一个原生 Library Target，整体模式统一选择 STATIC/SHARED，并补齐必要公共导出。将 StaticModuleHost/ScopeRuntime 演进为拥有稳定记录、代际句柄与原生装载持有的一个协调器；复用注册、工厂、Scope 闸门和逆序清理，常驻执行失败由整体 Shutdown 接管。类型擦除的检查使用 RTTI 值比较，残留访问控制块及其释放实现由宿主常驻 Runtime 提供。

**Tech Stack:** C++23、各平台 Clang、CMake 4.0+、Ninja、CTest、GoogleTest/GoogleMock、Windows 原生 Loader 与 POSIX 动态装载接口。

**Spec:** [已批准设计规格](../specs/2026-11-06-module-manager-link-modes-design.md)，批准提交 `4e641a43cbbfb0d70bc72c2ce2d2d3efe9b6cd10`；执行者同时阅读规格、AGENTS.md、Tests/README.md 和适用 wiki。

**Status:** Completed（2026-11-07）。Task 1–8 实现、逐任务评审、五平台正式矩阵、唯一全分支 Review 与统一修复的限定复评均已完成；I1/M1–M5 全部关闭，无新增问题。14条执行裁决、背景诊断与各轮准确版本已永久归档，本计划临时目录已按授权清理。分支 `docs/module-manager-link-modes`；完整矩阵源码为 `f9349df`，最终修复 `2dd769d` 另有定向验收，不混合计数。各轮证据见[验收记录](2026-11-06-module-manager-link-modes-validation.md)。

## Global Constraints

- 同一 CMake 目标图中的 Pangu Module 全部 STATIC（Monolithic）或全部 SHARED（Modular），不创建同 Module 双 Target，不恢复混合模式或 LinkedStaticModules；普通 ThirdParty、Host、BuildTools、辅助目标按自身用途处理。
- 桌面默认 Modular，Android/iOS 默认 Monolithic；Engine 子目录服从父 Project 的选择，不覆盖父缓存。不使用全局 BUILD_SHARED_LIBS 改变第三方。
- C++23 + Clang、无异常、保留 RTTI；受控 C++ ABI 下允许兼容 STL 跨 Module。公共接口强类型，内部擦除位置比较 std::type_info 值；不用人工持久 TypeId/trait、RTTI 地址或仅有名字/哈希判断类型。
- 一个 Module 一个主要库产物，名称不得包含平台名；Windows import library/PDB 为辅助产物；不引入 BinaryImage、通用 Loader Manager、统一业务 IService/IModule 基类或反射注册表。
- Foundation/Module 补齐持久身份，宿主提供已链接 Query，常驻登记不重复 OS Load，仍显式 Register/Activate；普通卸载拒绝，整体 Shutdown 最后确认常驻逻辑收尾，不释放宿主原生持有。
- 常驻 Register/Activate 的执行失败在同次操作请求整体 Shutdown；前置拒绝不改变状态。保留原始错误，通过 TryFinishShutdown/PollCleanup 推进；常驻句柄只在 TryFinishShutdown 成功时统一失效，不能原地重启 Manager。
- Load 显式候选集合整批接纳，任一失败不交付部分成功句柄；不包含 Register/Activate，不保证回滚 DLL 初始化副作用，不通过全局构造启动业务，不递归发现未知依赖。
- 管理操作和查询由调用方串行协调，重入在副作用前 Busy；业务 Pin/Borrow 支持并发接纳、释放和关闭。Pin 保护代码，Borrow 保护实例，管理器自身装载持有与外部保护分开计数。
- Busy/停止错误/原生释放错误保留资源、依赖、记录与必要库持有；不 ForceUnload、不超时强毁；析构一次同步收尾仍未完成则始终有效地终止进程。
- 最多 256 个受管 Module/Manager、每 Module 64 个依赖、每描述文本 256 字节、每次注册 256 声明、注册域 4096 声明、256 个 Scope、每工厂 64 个 Scope 依赖；常驻和待清理记录计入预算。OOM 沿用无异常终止政策。
- Windows/Android 正式 BuildRoot 为仓库 Build/Windows、Build/Android；Linux 正式源码 `/home/moozen/WindowsGit/PanguEngine`、BuildRoot `$HOME/PanguEngineBuild/Linux`；macOS/iOS 正式源码 `/Users/moozen/WindowsGit/PanguEngine`、BuildRoot `$HOME/PanguEngineBuild/macOS`、`$HOME/PanguEngineBuild/iOS`，通过 `ssh -o BatchMode=yes moozen-macos`，不复制共享源码。
- 同一 BuildRoot 同时一个验证会话；新模式矩阵夹具在宿主 BuildRoot 的独立测试子目录配置，不与外层 CTest/构建递归、不混用平台根。正式配置/构建使用根 preset。
- 正式测试长期保留并按 boundary/area/tier 接线；交叉编译不发现/运行目标 GTest。Android Monolithic 的 `.a` 链入原生宿主 `.so`，iOS 的 `.a` 链入最小 Mach-O executable；两平台 Modular 必须真实 Runtime/业务动态库、Query 导出和最小宿主链接，不能用空 Probe 替代。
- 不实现设备运行、APK/IPA 打包、Hot Reload、完整 EngineHost、ServiceRegistry、PanguMetaTool、调度器或独立二进制插件 Preflight。平台阻塞如实报告，不默默退回 STATIC 冒充 Modular。
- 修改 C++ 使用仓库 clang-format/clang-tidy 和实际编译数据库，所有新/改 CMakeLists.txt 使用 cmake_minimum_required(VERSION 4.0)。提交、注释和文档中文优先，无工具署名。
- 临时上下文统一 `.superpowers/sdd/2026-11-06-module-manager-link-modes/`；原始验收结果写各宿主 BuildRoot。完成 Review 与验收、归档必要结论并确认无运行依赖后，仅清理该临时目录。

## Review Focus

1. 父 Project 显式选择模式、无效枚举及同图中途改值：不覆盖父缓存，不允许混合类型或依赖旧模式产物；Task 1/7 的 ModeMatrix、ModeSwitch 往返测试覆盖。
2. 一批 DLL 中后项失败，前项释放又 Busy/失败：无部分句柄，身份仍被占用，旧业务不受影响，Poll 能继续；Task 5 的 FailedBatchRetainsUnreleasedReservation 覆盖。
3. 最后一次 Borrow/Pin Release 与关闭并发，DLL 卸载后还有 ScopeAccess：不在业务线程卸载、不读悬空 RTTI、不通过 DLL 控制块析构器释放；Task 2/5/6 的发布、并发和子进程回归覆盖。
4. 常驻 Activate 部分 Start 后失败、原报告随后销毁：整体关闭接管，TryFinishUnload 仍拒绝，Busy 保留句柄，原错误跨 Poll，最终统一失效；Task 4/5 的 ResidentFailureRequestsShutdown、ResidentBusyShutdown 覆盖。
5. 真正模块直接导入、原生依赖隐式加载及额外 OS 持有：不把已加载当作已接纳，不承诺释放自身持有必然移除库，可控夹具实际观测卸载；Task 3/5/7 的 NativeHold、ExplicitDependencies、HostImports 回归覆盖。

---

## 文件职责与实施接缝

| 位置 | 职责与变更方式 |
| --- | --- |
| `cmake/PanguLinkMode.cmake`、`cmake/Platform/Common/Module.cmake` | 一次确定模式、原生目标类型校验和逐 Module export header；不替代 add_library |
| `cmake/ModuleQuery.cpp.in` | 为显式原生 Query 生成一个固定 C-linkage 包装；仅 SHARED 编译，不扫描源码 |
| `Source/Runtime/Foundation/Public/ModuleIdentity.h`、`ModuleDescriptor.h` | 从 Module/Public 移入的纯协议头，命名空间、头文件名和身份语义保持；Foundation Query 可以引用它们而不反向链接 Module 实现 |
| `FoundationModule.h/.cpp`、`ModuleBuiltin.h/.cpp` | 基础 Module 持久身份、定义与原生 Query；实现分别归属自己的唯一 Target |
| `ModuleOperation.h`、`ModuleHandle.h`、`ModuleManager.h/.cpp` | 操作值报告、代际身份、统一外部接口与稳定私有状态；替代 StaticModuleHost，不留转发类 |
| `Private/ModuleRecord.h`、`ModuleCatalog.h/.cpp` | 稳定记录、元数据深复制、依赖绑定及批次暂存；不拥有业务实例 |
| `Private/NativeModuleLibrary.h/.cpp`、`Private/Platform/{Windows,Posix}/NativeModuleLibrary.cpp` | 原生持有、固定 Query 定位和显式可失败释放；仅内部平台接缝 |
| `Public/ModuleCodePin.h`、`Private/ModuleCodePin.cpp`、`Private/ScopeAccess.cpp` | 常驻 Runtime 内的控制块分配、外部计数和最终释放；不在 Pin Release 内调用 OS Unload |
| `Private/ScopeRuntime.h/.cpp`、`Public/ScopeAccess.h`、`ScopeBuildContext.h`、`RegistrationContext.h` | 复用现有 Scope/注册行为，改为稳定增量记录、RTTI 和常驻失败 Shutdown；不无关拆解整个运行时 |
| `Tests/Module/Fixtures/Shared/`、`Tests/Module/Support/` | 真正独立 DLL 的代码/对象/析构和常驻测试观察器，二者寿命分离 |
| `Tests/CMake/ModuleLinkModes/`、`Tests/CMake/SourcePluginProject/` | 类型/模式/Project/部署/交叉目标最终链接与增量行为 |

基础协议头移动不是新的 Library 或兼容转发头：Module 通过现有 PUBLIC Foundation 链接传播包含目录；移除原位置，更新文档路径。Foundation 只实现无业务工厂的原生定义，注册函数接受前置声明的 RegistrationContext 并返回成功，不调用 Module 实现，避免 DLL 链接环。

## 实施接口约定

以下是计划锁定的当前签名，不宣称 Public ABI 永久冻结；步骤中的同名接口必须一致。

- CMake：`pangu_initialize_link_mode()`；`pangu_module_library_type(outVar)` 在调用目录返回 STATIC/SHARED；`pangu_configure_module(targetName EXPORT_MACRO macroName)` 保留既有目标参数，新增可选 export 宏名；`pangu_export_module_query(TARGET target QUERY qualifiedFunction HEADER header)` 只向该 SHARED Target 加包装。
- 原生基础 Query：`const ModuleDefinition* QueryFoundationModule() noexcept`、`const ModuleDefinition* QueryModuleRuntime() noexcept`，位于 `pg::module`；`std::span<const QueryModuleFn> BuiltinResidentQueries() noexcept` 返回前两者的固定集合，描述依赖顺序仍由 Manager 校验。
- 本轮分配 Foundation ModuleId `9f80b25853204dfba45ae9d87c9a1201`、Module Runtime ModuleId `c0e3457b71fb49e39d0c8c0b232ea402`，持久写入源码，不由名称推导；版本均 `{1,0,0}`、协议版本 1，Runtime 声明 Required Foundation `{1,0,0}`。实现用 constexpr 字节常量，不让 Foundation 调用 Module 的运行期解析函数。
- `ModuleHandle { std::uint64_t Domain; std::uint32_t Slot; std::uint64_t Generation; }`，零初始化无效，代际从 1 开始；域唯一、不回绕。`ModuleState` 使用 `kLoaded=0`、`kRegistered=1`、`kActive=2`、`kCleanupPending=3`、`kUnloaded=4`，删除旧 RegistrationFailed 兼容别名。
- `ModuleLoadCandidate = std::variant<StaticModuleCandidate, SharedModuleCandidate>`；前者包含 `QueryModuleFn Query`，后者包含 `ModuleId ExpectedId`、拥有型 `std::filesystem::path Path`；`ModuleLoadResult { std::vector<ModuleHandle> Modules; }` 的句柄按本批依赖优先顺序返回。
- `ModuleSnapshot { OwnedModuleDescriptor Descriptor; std::vector<DependencyBinding> Bindings; ModuleState State; bool Resident; }` 为拥有型诊断；沿用现有 OwnedModuleDescriptor/DependencyBinding 字段。
- `ModuleCleanupOwner { kNone, kCallerUnload, kManagerCleanup, kManagerShutdown }` 加入操作值报告；保留既有 Progress/Module/RelatedModule/Scope/Declaration/PrimaryError/OriginalError/CleanupError，再增加 `ModuleHandle Handle`、`ModuleCleanupOwner CleanupOwner`，各返回路径初始化。没有交付成功句柄的 Load 失败不能泄露可操作的 Handle。
- `Result<ModuleManager> ModuleManager::Create(RegistrationScope&, std::span<const QueryModuleFn> residentQueries, std::span<const OptionalDependencyBinding>, ModuleOperationReport&) noexcept`：接纳并验证常驻集合，至少包含两项基础定义；返回时仍 Loaded，不隐式 Register/Activate。
- `Result<ModuleLoadResult> ModuleManager::Load(std::span<const ModuleLoadCandidate>, std::span<const OptionalDependencyBinding>, ModuleOperationReport&) noexcept`：普通批次接口；空批次拒绝 InvalidDescriptor；已有常驻/清理中身份不可重新占用。
- `Status ModuleManager::{Register,Activate,BeginUnload,TryFinishUnload}(ModuleHandle, ModuleOperationReport&) noexcept`；`Result<ModuleCodePin> ModuleManager::TryPinForBusiness(ModuleHandle) noexcept`。
- `std::optional<ModuleHandle> ModuleManager::Find(ModuleId) const noexcept`；`Result<ModuleSnapshot> ModuleManager::Inspect(ModuleHandle) const noexcept`；`std::vector<ModuleHandle> ModuleManager::Modules() const`、`RegistrationOrder() const` 为串行冷路径拥有型查询，不返回可跨增量修改保留的记录地址。
- `ScopeHandle ModuleManager::HostScope() const noexcept`；`Result<ScopeHandle> CreateScope(pg::service::Scope, ScopeHandle parent, ModuleOperationReport&) noexcept`；`Status {BeginCloseScope,TryFinishCloseScope}(ScopeHandle, ModuleOperationReport&) noexcept`；`Status {PollCleanup,BeginShutdown,TryFinishShutdown}(ModuleOperationReport&) noexcept`。
- Manager 仅移动，私有 State 地址在移动后稳定；保留“移动/销毁不能与操作或回调并发”的现有契约。已关闭 Manager 的 Find/Inspect 只能报告现存诊断或无效句柄，不能复活记录。
- 现有 ModuleError 数值 1–22 保留；追加 `kInvalidHandle=23`、`kResidentModule=24`、`kWrongLoadSource=25`、`kInvalidPath=26`、`kLibraryOpenFailed=27`、`kQuerySymbolMissing=28`、`kLibraryCloseFailed=29`、`kInvalidResidentSet=30`，不从错误码推断是否执行过回调。
- 内部 `NativeModuleLibrary::Open(const std::filesystem::path&) noexcept -> Result<NativeModuleLibrary>` 只取得持有；`ResolveQuery() const noexcept -> Result<QueryModuleFn>` 查固定 `PanguModuleQueryV1`；`Release() noexcept -> Status` 成功才清空持有，失败仍由同一对象拥有。仅移动，未释放析构不得假装成功。
- 原生测试故障只通过私有 per-instance 接缝：`detail::ModuleManagerTestAccess::FailNextNativeClose(ModuleManager&, ModuleHandle) noexcept`，以及尚未交付记录的批次对应内部装载操作；不增加公共 Mock Loader 或普通业务可见的测试参数。测试必须随后用真实原生释放完成清理。
- 内部 ScopeRuntime 接缝：`AttachModules(std::span<ModuleRecord* const>) noexcept -> void`（接纳预算/元数据已由 Manager 校验）、`DetachModule(ModuleHandle) noexcept -> void`、`TryFinishResources(ModuleHandle, ModuleOperationReport&) noexcept -> Status`、`RequestShutdown() noexcept -> void`。资源清理成功只设置记录的 ResourcesDrained，不使公开状态 Unloaded；Manager 再等待外部 Pin、完成原生释放并确认句柄失效。整体关闭逐记录沿依赖逆序驱动该接缝，不能先一口气销毁所有 Provider 再尝试业务库释放。
- 新故障报告测试入口 `detail::ModuleManagerTestAccess::FailNextRollbackClose(ModuleManager&) noexcept`：仅使下一次尚未交付批次的一个真实原生释放失败一次，随后恢复真实实现；与 FailNextNativeClose 均由私有测试头访问、每 Manager 隔离。

Task 1/2/3 可在现有协调器上独立验证。Task 4 的核心迁移以 Monolithic 既有完整 Module 回归为第一门，Modular 同时通过原生批次/常驻基础测试；Task 5/6 再完成全部动态寿命与 Scope 场景。任何过渡期模式专用测试必须如实登记并有可运行的另一模式夹具，不能静默跳过旧覆盖或把阶段门当作整计划完成。

### Task 1: 统一链接模式、公共导出与协议头归属

**Files:**
- Create: `cmake/PanguLinkMode.cmake`、`cmake/ModuleQuery.cpp.in`、`Tests/CMake/ModuleLinkModes/CMakeLists.txt`、`ModeMatrixTests.cmake`、`ModeSwitchTests.cmake`、`Fixtures/CMakeLists.txt`（后三者均位于该测试目录）。
- Move: `Source/Runtime/Module/Public/ModuleIdentity.h`、`ModuleDescriptor.h` → `Source/Runtime/Foundation/Public/` 同名文件。
- Modify: 根 `CMakeLists.txt`、`cmake/Platform/Common/Module.cmake`、`Source/Runtime/{Foundation,Module}/CMakeLists.txt`、`Source/Runtime/Foundation/Public/Error.h`、`Source/Runtime/Module/Private/ModuleIdentityParse.h`、`Source/Runtime/Module/Public/{RegistrationContext,ScopeBuildContext,StaticModuleHost}.h` 的必要导出。
- Modify: `Tests/CMakeLists.txt`、`Tests/Toolchain/Module/CMakeLists.txt` 及对应 Probe 声明、`Tests/Module/Fixtures/{CMakeLists.txt,FixtureModules.h,First.cpp,Second.cpp}`；`Tests/CMake/SourcePluginProject/FixtureEnginePlugins/Physics/` 与 `Tests/CMake/SourcePluginProject/MyGame/Plugins/{Gameplay,EditorExtras,SharedProbe}/` 各自 CMakeLists.txt/Query 公共声明，及生成这些 Module 的现有 Scale/构建夹具。

**Interfaces:** Consumes 既有 pangu_configure_module；Produces 上述三项模式函数和 Query 包装函数，生成 `<Target>Export.h`（PUBLIC 构建包含目录）及 `PG_FOUNDATION_API`、`PG_MODULE_API`，其它目标显式选择自身宏名。模式值在首个初始化点确定，同图再次初始化若值不同即拒绝。

- [x] **Step 1: 写 ModeMatrix 和 ModeSwitch 失败回归。** 检查默认值、父 Project 显式选择、非法 `modular`/空值、Modular+STATIC/Monolithic+SHARED 错配、中途改模式；ThirdParty 保持 STATIC。Fixture 配置中断言：
  ```cmake
  get_target_property(actualType PanguFoundation TYPE)
  if(NOT actualType STREQUAL "SHARED_LIBRARY")
      message(FATAL_ERROR "Expected Modular Foundation SHARED_LIBRARY")
  endif()
  ```
  正例分别构建 Foundation/Module/两项真实夹具及跨库 MakeError/ParseModuleId 调用，负例必须在配置期以 `does not match PANGU_LINK_MODE` 或 `Invalid PANGU_LINK_MODE` 拒绝。
- [x] **Step 2: 观察红灯。** 登记测试后运行 `cmake --preset windows`、`ctest --preset windows-full -R '^ModuleLinkMode(Matrix|Switch)$' --no-tests=error`；旧实现应因仍 STATIC、父选择无效或错误配置未拒绝而失败，不以缺环境/缺依赖当红灯。
- [x] **Step 3: 实现模式与导出。** 根工程在 project()/目标平台选择后、Runtime add_subdirectory 前初始化模式，按目标 CMAKE_SYSTEM_NAME 选择默认值；使用原生 add_library，调用 pangu_module_library_type 得到类型。helper 验证真实目标、生成显式 export header，源头声明必要非模板函数及跨库类型可见性，不启用自动导出全部符号。移动纯协议头而不增加库；包装仅 SHARED 编译，STATIC 不产生多个同名固定符号。同步全部既有 pangu_configure_module 调用方，不让夹具偷偷固定 STATIC。
- [x] **Step 4: 聚焦 green。** 运行上述两测试、`cmake --build --preset windows-precommit`、`ctest --preset windows-precommit -R 'ToolchainLibraryLink|ModuleIdentity|StaticCatalog' --no-tests=error`；同树 Modular→Monolithic→Modular 后检查类型、链接、测试清单及第二次 Ninja 无工作。跨库测试必须调用导出函数，不能只生成库。
- [x] **Step 5: 检查和提交。** 对修改自有 C++ 格式化/tidy，`git diff --check`，提交 `build: 统一 Module 链接模式与公共导出`。此阶段不声称生产 Loader 已实现。

### Task 2: 内部 RTTI 校验与常驻访问控制块

**Files:** Modify `Source/Runtime/Module/Public/{ScopeAccess,ScopeBuildContext,ScopeFactory}.h`、`Source/Runtime/Module/Private/{ScopeBuildContext,ScopeRuntime}.cpp`；Create `Source/Runtime/Module/Private/ScopeAccess.cpp`、`Tests/Module/ScopeTypeTests.cpp`、`Tests/Module/Support/{CMakeLists.txt,ScopeTestSupport.h,ScopeTestSupport.cpp}`；Modify `Tests/Module/CMakeLists.txt`、`Tests/Module/{ScopeAccessTests,ScopeAccessConcurrencyTests,ScopeBuildTests,ScopeBuildMockTests}.cpp`。

**Interfaces:** Produces `detail::MakeScopeGate() noexcept -> std::shared_ptr<ScopeGate>`、`detail::MakeScopeObjectControl(std::shared_ptr<ScopeGate> module, std::shared_ptr<ScopeGate> scope, void* object) noexcept -> std::shared_ptr<ScopeObjectControl>`，分配和共享控制块最终释放均在 PanguModule 实现。资源适配器 `ErasedScopeObject::Type() const noexcept -> const std::type_info&`；`ScopeDependencyObject` 的类型引用只在资源代码驻留期间有效，不在 ScopeAccess 控制块保存 RTTI。

测试支持接口：`test::ScopeTestObserver& test::Observer() noexcept` 返回宿主驻留的观察器，提供 `Reset() noexcept`、`Record(Event) noexcept`、`RegisterCount() const noexcept -> std::size_t`、`NativeUnloadCount() const noexcept -> std::size_t`、`Events() const -> std::vector<Event>`。Event 的枚举为 `kNativeLoad/kRegister/kStart/kStop/kDestroy/kFactoryDestroy/kNativeUnload`；`test::ProbeState` 包含 SceneName 字符串及 Values 向量，生命周期钩子记录事件。后续任务只扩充同一观察器，不在各 DLL 建立各自的静态观察状态。

- [x] **Step 1: 写类型与寿命断言。** ScopeTypeTests 覆盖同 T 成功、不同 T 返回 kTypeMismatch、合法只读访问、不能移除 const、非多态非平凡含 string/vector 的类型、`unique_ptr<Base>` 擦除目标为 Base 而非动态 Derived。测试支持库提供线程安全事件计数及拥有型 ProbeState/ScopeAccess 槽位；它是宿主链接的普通辅助 Target，Modular 时共享单份观察状态，不具有 ModuleId/Query。断言片段：
  ```cpp
  EXPECT_EQ(wrongType.error(), MakeModuleError(ModuleError::kTypeMismatch));
  EXPECT_EQ(borrow->SceneName, "World A");
  EXPECT_EQ(borrow->Values.size(), 3U);
  ```
- [x] **Step 2: 红灯。** `cmake --build --preset windows-precommit`、`ctest --preset windows-precommit -R 'ScopeType|ScopeAccess|ScopeBuild' --no-tests=error`；新增非模板分配接口或 RTTI 形状尚不存在时编译失败为预期，不把现有正例删掉来制造失败。
- [x] **Step 3: 实现接缝。** Own<T> 由实际擦除 T 生成 typeid(T)，Dependency<T> 在代码/实例受保护时先比较值再转换；移除 ScopeTypeToken 和地址匹配。模块、Scope、对象控制块均通过常驻非模板函数创建；移除残留控制块里的 Type 指针。Make<T> 的类型证明来自框架 Own<T> 路径，不开放任意 void*/类型信息组合。ScopeAccess/Borrow 的公开强类型行为与原子闸门保持。
- [x] **Step 4: green。** 同一测试选择通过；使用真实编译数据库检查导出和类型声明。并发回归覆盖获取成功必须发生在关闭前、失败分支回退计数、Release 可跨线程；记录控制块创建/最终释放入口归属 Runtime。跨 DLL 真正卸载后的证明由 Task 6 完成，不能将本任务静态/驻留夹具结果冒充该证据。
- [x] **Step 5: 提交。** `refactor: 使用 RTTI 校验并集中访问控制块寿命`。

### Task 3: 可失败的原生装载持有与真实 Query 夹具

**Files:** Create `Source/Runtime/Module/Private/{NativeModuleLibrary.h,NativeModuleLibrary.cpp}`、`Source/Runtime/Module/Private/Platform/Windows/NativeModuleLibrary.cpp`、`Source/Runtime/Module/Private/Platform/Posix/NativeModuleLibrary.cpp`、`Tests/Module/NativeModuleLibraryTests.cpp`、`Tests/Module/Fixtures/Shared/{CMakeLists.txt,Provider.cpp,Consumer.cpp,MissingQuery.cpp,NullQuery.cpp,SharedFixtureContract.h}`；Modify `Source/Runtime/Module/CMakeLists.txt`、`Tests/Module/CMakeLists.txt` 和 `Tests/Module/Support/{ScopeTestSupport.h,ScopeTestSupport.cpp}`。

**Interfaces:** Produces NativeModuleLibrary 的 Open/ResolveQuery/Release 约定及一个固定 Query 名。Provider/Consumer 各是独立真实 SHARED Module，在 SharedFixtureContract.h 分别定义 `kSharedProviderId`、`kSharedConsumerId`，正常 Query 返回定义、原生注册回调；无全局构造业务注册。观察器沿用 Task 2 的 Event，不另建日志。错误及空定义夹具是明确的协议负例。

由 CMake file(GENERATE) 生成测试私有 SharedFixturePaths.h，提供 Provider/Consumer/MissingQuery/NullQuery 的目标文件路径，避免依赖 CTest 工作目录。Test EXE 显式 add_dependencies 到所消费的夹具库，或将它们加入相同 tier 的 pangu_test_build_dependencies；只有 `$<TARGET_FILE:...>` 文本而没有构建依赖不算接线完成。

- [x] **Step 1: 写原生集成测试。** 新目标 PanguNativeModuleLibraryTests 登记为 integration/area.module/precommit；NativeModuleLibrary.OpenMissing、MissingQuery、NativeHold、UnicodeAbsolutePath 验证错误分类、Query 不初始化业务、额外 OS 持有使本对象 Release 不等于物理卸载，以及最后实际持有释放后 NativeUnload 恰好一次。NullDefinition 只验证成功取得的 Query 可返回空，拒绝接纳由 Task 4 的 Manager 测试负责。测试工作目录的中文含空格绝对路径应正常装载，直接相对路径返回 kInvalidPath；错误路径仍显式释放已取得的库。
  ```cpp
  EXPECT_EQ(observer.RegisterCount(), 0U);
  EXPECT_EQ(observer.NativeUnloadCount(), 0U); // 额外真实持有尚在。
  // 最后一个真实持有释放之后：
  EXPECT_EQ(observer.NativeUnloadCount(), 1U);
  ```
- [x] **Step 2: 红灯。** Modular 配置，`cmake --build --preset windows-precommit`、`ctest --preset windows-precommit -R '^PanguNativeModuleLibraryTests\.NativeModuleLibrary\.' --no-tests=error`；旧代码缺少原生持有接口。使用真实登记器的 Target 前缀，不写会选空的套件锚点。
- [x] **Step 3: 实现平台适配。** 只接受绝对路径，不擅改全局 DLL 搜索目录/进程环境；Windows 使用原生加载、符号查询和释放，POSIX 使用立即解析、全局符号作用域及对应关闭（Task 8 两桌面真实 RTTI 回归后以 RTLD_NOW|RTLD_GLOBAL 替代原 LOCAL 选择，保持最小公共导出/私有隐藏，并承担公共符号名称冲突/插入风险），依平台通过 CMAKE_DL_LIBS 连接。原生错误转为拥有型 Error/有界报告；函数指针转换只封装在平台接缝，Query 调用期间保持持有。Open 不做会丢失清理责任的后续校验，缺 Query 时仍由现存对象持有库；Release 失败不清空 handle。
- [x] **Step 4: green 与导出检查。** 同选择通过；`llvm-readobj --coff-exports Build/Windows/Bin/PanguSharedProvider.dll` 检查固定符号及必要公共符号；POSIX 在 Task 8 复验。Test EXE 不直接链接可卸载 Provider/Consumer，观察器及 Runtime 由宿主链接保持驻留。Monolithic 不登记原生加载运行测试，以独立 Modular 夹具保留覆盖。
- [x] **Step 5: 提交。** `feat: 增加受管原生库持有与 Query 接口`。

### Task 4: 统一 Manager、稳定记录、常驻身份和原子批次

**Files:** Create `Source/Runtime/Foundation/{Public/FoundationModule.h,Private/FoundationModule.cpp}`、`Source/Runtime/Module/{Public/ModuleBuiltin.h,Private/ModuleBuiltin.cpp,Public/ModuleHandle.h,Public/ModuleOperation.h,Public/ModuleManager.h,Private/ModuleManager.cpp,Private/ModuleRecord.h,Private/ModuleCatalog.h,Private/ModuleCatalog.cpp}`；Modify `Source/Runtime/Module/Public/{ModuleError,RegistrationContext}.h`、`Source/Runtime/Module/Private/{RegistrationContext,ScopeRuntime}.cpp`、`Source/Runtime/Module/Private/ScopeRuntime.h`；Remove `Source/Runtime/Module/Public/StaticModuleHost.h`、`Source/Runtime/Module/Private/StaticModuleHost.cpp`（以迁移替代，不保留兼容外观）。
- Modify/Test: `Tests/Module/{StaticModuleHostTests,RegistrationTests,ScopeLifecycleTests,StaticCatalogTests,ScopeDestructorFixture}.cpp`、`Tests/Module/Fixtures/{ScopeLifecycleFixture.h,StaticCatalog.cpp.in}`、`Tests/CMake/SourcePluginProject/MyGame/Source/HostCheck.h`、对应 Game/Editor main/catalog 与 `Tests/Module/Benchmarks/StaticModuleHostBenchmarks.cpp`；Create `Tests/Module/{ModuleManagerTests,ResidentModuleTests,BatchLoadTests}.cpp`；更新相关 CMake、README 和测试迁移清单。
- Create: `Tests/CMake/ModuleLinkModes/ModuleRegressionTests.cmake`、`Tests/CMake/ModuleLinkModes/RegressionFixture/CMakeLists.txt`，作为无递归的另一模式正式回归入口。

**Interfaces:** Produces 前述 Manager/Handle/Report/Snapshot/LoadResult 查询与操作接口及内部 ScopeRuntime 接缝，ModuleCodePin/TryPinForBusiness 的声明和实现由 Task 5 交付。私有 State 使用 256 个固定槽位，每槽位拥有稳定 `unique_ptr<ModuleRecord>`，ScopeRuntime 引用稳定节点。Roots 由 Create 一次接纳，普通 Load 不能覆盖。RegressionFixture 是独立顶层 CMake 图，显式启用 Engine Tests、选择 Monolithic 并传入 `PANGU_TEST_MODULE_MATRIX_CHILD=ON`，只构建/运行 area.module；该内部标志只禁止子图再次登记 ModuleRegression，不关闭其它有效测试。

- [x] **Step 1: 写身份/批次/常驻失败红灯。** ModuleManagerTests 覆盖域、代际、移动后旧 Scope 可用、元数据快照脱离原 DLL、两常驻记录消耗预算，常驻加 254 个可卸载项达到 256，下一项拒绝。BatchLoadTests 覆盖重复/零 ID、协议、空入口、精确版本、Required/Optional、合并图环、空批次、模式来源错误和增量扩容；复用原 Metadata/Registration 断言，不减少字段检查。ResidentModuleTests 覆盖两基础定义依赖、Loaded 不自动注册/激活和单独卸载拒绝。
  ```cpp
  EXPECT_EQ(manager.Modules().size(), 256U);
  EXPECT_EQ(overBudget.error(), MakeModuleError(ModuleError::kLimitExceeded));
  EXPECT_EQ(residentUnload.error(), MakeModuleError(ModuleError::kResidentModule));
  EXPECT_NE(oldHandle.Generation, reloadedHandle.Generation);
  ```
- [x] **Step 2: 红灯。** 先以 `cmake --preset windows -DPANGU_LINK_MODE=Monolithic`、build windows-precommit，运行 `ctest --preset windows-precommit -R 'ModuleManager|ResidentModule|BatchLoad|Registration|ScopeLifecycle' --no-tests=error`；新接口缺失为预期。模式检查/Native 测试继续在各自独立配置运行，不在同图混合。
- [x] **Step 3: 实现稳定接纳与根启动。** 消费 Task 1 已移动的纯头、赋予基础固定 ID，Foundation 不调用 Module 实现。先验证预算/已知身份并预留槽位，取得持有、Query、深复制、固定依赖后整批提交；返回本批依赖优先句柄，回滚保留未能释放的内部记录。旧 Register/Activate/Scope 核心改用 Handle→稳定记录解析，资源清理与记录最终确认分开，旧 API 调用和基准一起迁移，不让公共接口裸借用记录地址。
- [x] **Step 4: 实现常驻例外协议。** 自定义常驻测试 Query 加在两基础项后：Register 执行失败自动请求 Shutdown，回报 CleanupOwner=kManagerShutdown；前置错误保持 Running。BeginShutdown 不通过回调重入自身，TryFinishUnload 始终拒绝常驻；Poll 不独立确认根退出，TryFinishShutdown 成功统一失效。CreateScope 及已有 Scope 的新 Borrow 在 Shutdown 后关闭，诊断快照及原错误仍可读取；新业务 Pin 的对应断言由 Task 5 完成。
- [x] **Step 5: 迁移回归并 green。** 上述选择及既有 Mono Module 集全部通过；Modular 跑真实 DLL 的基础批次/常驻/描述校验。现有大量同二进制伪 Query 场景先作为明确的 Monolithic 回归保留，通过 `Tests/CMake/ModuleLinkModes/ModuleRegressionTests.cmake` 的独立子配置在 Modular full/precommit 中构建并运行，子配置显式关闭该矩阵登记，不能递归；Task 6 补齐真实 Modular Scope 对应覆盖。独立 Project 的已链接夹具在本阶段以显式常驻 Query 集合启动并更新数量/顺序断言，Task 7 再迁移可卸载消费路径；不把 SHARED 伪装成 STATIC 候选。
- [x] **Step 6: 格式/tidy 和提交。** `refactor: 统一 ModuleManager 与常驻身份接纳`。阶段记录指出尚待 Task 5/6 的完整动态 Pin/卸载验收，不宣称整个规格完成。

### Task 5: CodePin、失败清理和真正安全卸载

**Files:** Create `Source/Runtime/Module/{Public/ModuleCodePin.h,Private/ModuleCodePin.cpp,Private/ModuleTestAccess.h}`、`Tests/Module/{ModuleCodePinTests,ModuleUnloadTests,ModuleUnloadConcurrencyTests}.cpp`；Modify `Source/Runtime/Module/Private/{ModuleManager,ModuleCatalog,ScopeRuntime,NativeModuleLibrary}.cpp`、`Source/Runtime/Module/Private/ModuleRecord.h`、`Tests/Module/{ResidentModuleTests,BatchLoadTests}.cpp`、`Tests/Module/Fixtures/Shared/{Provider,Consumer}.cpp` 和 `Tests/Module/Support/{ScopeTestSupport.h,ScopeTestSupport.cpp}`。

**Interfaces:** Produces 仅移动 ModuleCodePin（默认无效、移动、显式 Release、析构均 noexcept）及 TryPinForBusiness；Pin 控制和最终释放在 PanguModule。每记录的原生持有仍由 Manager 唯一负责；外部 Pin 与 Scope Borrow/清理内部权限分开，最后 Pin Release 只递减计数，不调用原生释放。私有 per-instance 释放故障接缝供失败回归，不能成为公共配置。

- [x] **Step 1: 写 Pin/批次延迟清理红灯。** ModuleCodePin 仅 Active 成功、Loaded/Registered/Closed/旧代际拒绝；关闭与获取竞争仅允许“关闭前已接纳”或“拒绝”两种结果，并覆盖并发 Load/槽位回收时旧句柄不能取得新记录 Pin。FailedBatchRetainsUnreleasedReservation 用合法前项+后项缺符号，由 FailNextRollbackClose 使前项原生释放失败一次，检查整批无结果、占位/容量仍在，旧 Active Module 正常，Poll 后能以新句柄重载。
  ```cpp
  EXPECT_EQ(report.CleanupOwner, ModuleCleanupOwner::kManagerCleanup);
  EXPECT_FALSE(loadResult.has_value());
  EXPECT_EQ(duplicate.error(), MakeModuleError(ModuleError::kDuplicateModule));
  EXPECT_EQ(observer.NativeUnloadCount(), 0U);
  ```
- [x] **Step 2: 红灯。** Modular 下 build windows-precommit，`ctest --preset windows-precommit -R 'ModuleCodePin|ModuleUnload|FailedBatch|ResidentBusyShutdown' --no-tests=error`；观察 Pin 未实现或提前清空原生持有的明确失败。
- [x] **Step 3: 实现顺序和失败状态。** BeginUnload 原子关业务门；按现有有效依赖逆序 Stop/Poll/Destroy，销毁 Factory/捕获/RTTI 借用后再尝试原生释放。TryFinishResources 成功后仍保持 CleanupPending，继续等待外部 Pin/原生释放；不能等待清理自身持有归零才开始 Destroy。原生释放失败保留记录、身份、原错误和可重复清理；成功 TryFinishUnload 确认失效，重新 Load 使用新代际。原生额外引用存在时按用户确认的“释放自身持有”定义返回成功。
- [x] **Step 4: 实现并发句柄解析。** State 内使用一个 AdmissionMutex，保护槽位 generation/控制块的发布与回收；TryPinForBusiness 在锁内验证句柄并取得常驻控制块引用，离锁后通过同一业务闸门接纳。回收前关闭旧闸门，避免旧控制块指向新代际；Load/Shutdown 对可被 Pin 线程读取的状态使用同一同步。不在锁内调用注册、工厂、Stop/Poll/析构或原生 Loader；ScopeAccess/Borrow 继续仅通过已有控制块操作。
- [x] **Step 5: 真实异步和常驻 Busy green。** 使用部分 Start 后失败、Stop 需多次 Poll、内部依赖 Borrow 与外部 Pin/Borrow 的不同组合；ResidentBusyShutdown 在原报告销毁后继续读取原始错误，普通卸载拒绝、Root 句柄仍诊断有效，整体 Finish 成功才统一失效。重复停止/清理只执行一次，业务线程释放最后 Pin 后 NativeUnload 计数仍为零，随后管理线程 Finish 才发生卸载。测试目标为 requires.threads/subprocess 的 precommit。
- [x] **Step 6: 提交。** `feat: 完成 CodePin 与失败可继续的安全卸载`。

### Task 6: 已有 Scope 的动态装配与卸载后访问回归

**Files:** Create `Tests/Module/{SharedScopeLifecycleTests.cpp,ModuleLifetimeFixture.cpp,ModuleLifetimeTests.cmake}`、`Tests/Module/Fixtures/Shared/{ScopeProvider.cpp,ScopeConsumer.cpp}`；Modify `Tests/Module/Fixtures/Shared/SharedFixtureContract.h`、`Tests/Module/Support/{ScopeTestSupport.h,ScopeTestSupport.cpp}`、`Tests/Module/{CMakeLists.txt,ScopeLifecycleTests.cpp,ScopeAccessConcurrencyTests.cpp,ScopeDestructorTests.cmake}`、`Source/Runtime/Module/Private/{ScopeRuntime,ScopeAccess,ModuleManager}.cpp`（仅对新回归暴露的缺口修复）。

**Interfaces:** Consumes 全部 Manager、Pin、Scope 公共接口；扩充同一测试观察器 `SaveAccess(ScopeHandle, ScopeAccess<ProbeState>) noexcept -> void`、`Access(ScopeHandle) const -> ScopeAccess<ProbeState>`，按具体 Scope 区分对象；其代码/数据由宿主链接的辅助库持有，Provider/Consumer 的对象/Factory/析构真实位于各自 DLL。公共 ProbeState 含 string、vector 及计数状态，不以 C POD 桥代替 C++ 访问。

另提供测试专用的 `ScopeAccess<ProviderPrivateState>` 槽位：公共测试契约只前置声明 ProviderPrivateState，完整非平凡定义与其 RTTI 仅在 ScopeProvider.cpp 中；观察器以 `SavePrivateAccess(ScopeAccess<ProviderPrivateState>) noexcept`/`PrivateAccess() const -> ScopeAccess<ProviderPrivateState>` 传递凭据，宿主不构造该类型、不调用其方法或 typeid。这样卸载回归不仅覆盖 RTTI 可能已在常驻库中的公共 ProbeState，也覆盖确实属于业务 DLL 的类型信息。

- [x] **Step 1: 写真实 DLL Scope 场景。** SharedScopeLifecycle.ExistingOwners 装载前创建 Context A/World A，激活后各实例独立，再创建 B；LocalRollback 的 B 失败不影响 A/C；ActivationRollback 在多个已有 Scope 中途失败则本 Module 统一回滚。TypedDependency 验证跨 DLL 同 T 的 RTTI 值匹配、错误 T 返回 kTypeMismatch、合法 STL 访问；Assertions 不要求 type_info 地址不同或相同。
  ```cpp
  EXPECT_EQ(observer.RegisterCount(), 1U);
  EXPECT_NE(contextAObject, contextBObject);
  EXPECT_EQ(originalScopeValue, "World A");
  EXPECT_EQ(wrongType.error(), MakeModuleError(ModuleError::kTypeMismatch));
  ```
- [x] **Step 2: 写卸载后凭据子进程红灯并运行。** ModuleLifetime 子进程分别持有公共 ProbeState 与 DLL 私有 ProviderPrivateState 的旧 ScopeAccess、卸载真实 DLL，验证 NativeUnload 后继续 TryBorrow/复制/最终释放控制块，断言失败访问和正常退出；私有类型的完整定义不进入 Host/观察器，不能靠常驻 RTTI 掩盖悬空引用。另一个子进程让 Manager 析构时有未完成工作，断言非零退出且不能记录提前 NativeUnload。运行 `ctest --preset windows-precommit -R 'SharedScopeLifecycle|ModuleLifetime|ScopeDestructor' --no-tests=error`，缺口必须在正确场景失败。
- [x] **Step 3: 修复实际缺口。** 保留 Module/Scope 两级发布闸门；活跃模块新增 Scope 不重跑 Register，初次 Activate 失败与新增 Scope 失败清理范围不同。消除残留控制块中的业务 DLL RTTI/销毁器依赖、确认相应 shared_ptr 控制块来自常驻非模板实现；不用复制 type_info 名称/哈希规避寿命问题。
- [x] **Step 4: green 并核对覆盖迁移。** 上述选择通过，模块卸载前顺序观测包含 Stop/Poll/Destroy/FactoryDestroy/NativeUnload，且每项次数正确。为旧 Mono 生命周期/析构/注册测试逐项记录其保留或真实 Modular 对应测试；旧有效覆盖继续可在独立 Mono 子配置运行，不以测试数或仅链接成功宣称等效。运行两模式的 Module 测试集合，修复后不额外反复跑未受影响测试。
- [x] **Step 5: 提交。** `test: 覆盖动态 Scope 装配与卸载后访问寿命`。

### Task 7: 独立 Project 消费、部署及移动端最终链接

**Files:** Modify `Tests/CMake/SourcePluginProject/{ProjectTestHelpers,RunIntegration,StaticCatalogIntegrationTests,SharedDeployTests,IncrementalBuildTests,CrossCompileTests,PluginTestOwnershipTests,PluginScaleTests}.cmake`、`MyGame/CMakeLists.txt`、`MyGame/Source/{HostCheck,FixtureModules}.h`、`{GameMain,EditorMain,GameCatalog,EditorCatalog}.cpp`、真实 Physics/Gameplay/EditorExtras/SharedProbe Module 源码与 CMake；Create `Tests/CMake/SourcePluginProject/MyGame/Source/{ModuleCandidates.h.in,MobileHost.cpp}`；Modify 五平台 `cmake/Platform/*/CMakePresets.json`、ModuleLinkModes 测试，及必要 `cmake/PanguPlugins.cmake` 的消费者接线。

**Interfaces:** 既有 `ConfigureFixture(name enabled expected diagnostic)` 签名保持，通过其已有 ARGN 传 `-DPANGU_LINK_MODE=...`；CrossCompileTests 使用 `PANGU_TARGET_PRESET`（android-arm64/ios-arm64）和 `PANGU_LINK_MODE`，无额外移动运行参数。生成 ModuleCandidates 仅包含该消费者明确选择的 ID/部署路径；Monolithic 生成显式 Query Catalog，Modular Host 仅链接常驻 Runtime、显式依赖并部署业务库，再传候选给 Manager。

- [x] **Step 1: 写 Project 双模式断言。** 扩展已有 SourcePluginProject/StaticCatalog/SharedDeploy/Incremental 测试：同一 Project 两次独立配置，Mono 检查静态 Catalog 根引用及裁剪，Modular 检查 MyGame/MyEditor 消费集合独立、无未消费 Plugin 编译、业务 DLL 不直接导入 Host、显式部署和运行激活。增量检查第二次 Ninja 无工作、未消费 Plugin 修改不重编无关 Module、模式往返没有旧测试/旧部署产物被消费；保留已有源插件身份与路径负例。在 Modular 的 MyGame 配置断言片段：
  ```cmake
  get_target_property(hostLinks MyGame LINK_LIBRARIES)
  if("Pangu::Gameplay" IN_LIST hostLinks)
      message(FATAL_ERROR "Modular Host must not import Gameplay")
  endif()
  ```
  运行用例还须验证部署 DLL 经 Manager 注册/激活；只检查链接列表不算满足本步骤。
- [x] **Step 2: 写移动真实链接断言。** Android Mono 最终目标为原生宿主 SHARED（无 ModuleId），iOS 为最小 executable；两者实际使用 Foundation MakeError、Manager 和一个业务定义，不能只有空 main。Modular 编译并链接真实 Runtime/业务库及 Query；记录 ELF/AArch64 或 Mach-O/ARM64、iOS platform 2，以及真实动态依赖/固定 Query 导出。
- [x] **Step 3: 观察消费者 red。** 执行新增桌面 Project 模式测试；在当前可用移动宿主执行新交叉脚本，其旧 SharedProbe/LinkSmoke 应因没有真实 Runtime/业务 Query 或宿主引用而失败。未执行的平台不制造红灯记录；错误必须来自新契约断言。
- [x] **Step 4: 实现消费者路径。** 为独立 Host 生成它自己的 Catalog/候选装配源，不建全局 Plugin Catalog或第二个依赖 DAG。宿主加载顺序由运行期已声明依赖决定；OS 隐式加载依赖不能代替 Manager 接纳。基础 Root 显式 Register/Activate，业务再按句柄顺序注册/激活；关闭先业务后 Root。交叉配置不发现 GTest、不执行设备程序，不改系统签名/SDK 设置。
- [x] **Step 5: 桌面聚焦 green。** `cmake --build --preset windows-full`、`ctest --preset windows-full -R '^(SourcePlugin(Project|StaticCatalog|SharedDeploy|Incremental|TestOwnership|Enable|EngineRevision)|ModuleLinkMode(Matrix|Switch)|ToolchainLibraryLink)$' --no-tests=error`；这些是现有登记器的真实名称，不能仅以 SourcePluginProject 子串漏掉其它 Project 脚本。正式 Project 锁定当前 Engine 提交且干净树，必要联调只用已有显式 local override 并记录。移动实际平台命令归入 Task 8，Task 7 可先在当前可用宿主验证受影响平台，不能以未执行的其它平台当完成证据。
- [x] **Step 6: 提交。** `feat: 接入双模式 Project 与移动端真实模块链接`。

### Task 8: 权威规范同步、跨平台验收与收尾

**Files:** Create `wiki/decisions/0015-unified-link-modes-module-manager.md`、`docs/superpowers/plans/2026-11-06-module-manager-link-modes-validation.md`；Modify `wiki/{README,architecture}.md`、`wiki/foundation/{build-system,testing,abi}.md`、`wiki/core/{plugin-module,module-api,module-registration-lifetime,engine-host,memory-management}.md`、`wiki/decisions/{README,0005-one-module-one-library,0011-module-api-behavior-contract}.md`、`Tests/{README,MIGRATION}.md`、`Tests/Module/{README,Benchmarks/README}.md`、`Tests/Toolchain/README.md`、本计划状态与覆盖映射。其它历史规格仅添加必要替代链接，保留历史结果。

**Interfaces:** Consumes Task 1–7 全部交付；Produces 可复现命令、模式/目标/宿主/源码版本及证据索引。ADR-0015 明确替代旧混合类型、同库 Catalog 范围和常驻失败例外，保留未替代的身份/所有权契约；不把新的 Public C++ 布局直接标为 Frozen。

- [x] **Step 1: 同步现行规范与测试映射。** 使用真实实现接口替换旧 StaticModuleHost 现行说明，记录两种模式、根启动/失败 Shutdown、RTTI/残留控制块、成功释放自身 OS 持有语义及移动端边界。回查所有仍是现行说明的 LinkedStaticModules/混合模式/静态 Probe/旧头路径，历史文档加链接不改历史事实；Tests/MIGRATION 逐项列旧覆盖、新位置及等效关系。
- [x] **Step 2: 提交现行规范。** 自查替代关系和命令后提交 `docs: 同步统一链接模式与 ModuleManager 现行规范`，确认工作树干净再启动正式矩阵；不要让未提交的规范变更使 Project 的 Engine 版本锁验证面对脏树。
- [x] **Step 3: Windows 正式验收。** 干净树上运行 `cmake --preset windows -DPANGU_LINK_MODE=Modular -DBUILD_TESTING=ON -DPANGU_BUILD_ENGINE_TESTS=ON -DPANGU_BUILD_BENCHMARKS=OFF -DPANGU_TEST_BUILD_JOBS=2`，再 `cmake --workflow --preset windows-full`。随后同一根显式配置 Monolithic、build windows-precommit、`ctest --preset windows-precommit -L '^area\.module$' --no-tests=error`；运行 ModeMatrix/ModeSwitch 和受影响 Project 子集；恢复 Modular 并验证 fast。保存首轮/重跑、失败原因及真实卸载事件，不拼接不同版本成绩。
- [x] **Step 4: Linux/macOS 正式验收。** Linux 在约定 CIFS 源路径分别 `cmake --preset linux -DPANGU_LINK_MODE=Modular -DBUILD_TESTING=ON -DPANGU_BUILD_ENGINE_TESTS=ON -DPANGU_BUILD_BENCHMARKS=OFF -DPANGU_TEST_BUILD_JOBS=2`、`cmake --build --preset linux-full`、`ctest --preset linux-full -L '^area\.(module|toolchain|plugins)$' --no-tests=error`，Monolithic 重复这组受影响集合，再运行 `ctest --preset linux-full -R '^ModuleLinkMode(Matrix|Switch)$' --no-tests=error`；macOS 通过 BatchMode SSH 在共享源码路径用 macos 对应命令。Windows 用 llvm-readobj，Linux 用 `llvm-readobj --dyn-symbols --needed-libs`，macOS 用 `nm -gU`/`otool -L` 核对真实 Query/依赖；每宿主恢复默认 Modular，不能用 Windows 结论替代另两者。
- [x] **Step 5: Android 两模式验收。** Windows 在仓库根依次执行以下 PowerShell；检查 `.a`/`.so` 架构、Query 导出、真实 Runtime 动态依赖和最终宿主链接，任何模式失败必须记录未完成。最后显式恢复 android-arm64 的 Monolithic 缓存，不运行目标 GTest。
  ```powershell
  foreach ($linkMode in @('Monolithic', 'Modular')) {
      cmake --preset android-arm64 "-DPANGU_LINK_MODE=$linkMode" -DBUILD_TESTING=ON -DPANGU_BUILD_ENGINE_TESTS=ON
      if ($LASTEXITCODE -ne 0) { throw "Android configure failed: $linkMode" }
      cmake --build --preset android-arm64
      if ($LASTEXITCODE -ne 0) { throw "Android build failed: $linkMode" }
      cmake -DPANGU_ENGINE_SOURCE_DIR=D:/Git/PanguEngine "-DPANGU_TEST_ROOT=D:/Git/PanguEngine/Build/Android/Tests/ModuleLinkModes/$linkMode" -DPANGU_TARGET_PRESET=android-arm64 "-DPANGU_LINK_MODE=$linkMode" -P Tests/CMake/SourcePluginProject/CrossCompileTests.cmake
      if ($LASTEXITCODE -ne 0) { throw "Android Project link failed: $linkMode" }
  }
  ```
- [x] **Step 6: iOS 两模式验收。** 通过 BatchMode SSH 在共享源码根执行以下 shell，检查 Mach-O/ARM64、iOS platform 2、Query 和真实依赖；最后恢复 ios-arm64 的 Monolithic。链接不能代替设备 RTTI/装卸执行，SDK 阻塞如实记录。
  ```sh
  set -eu
  for link_mode in Monolithic Modular; do
      cmake --preset ios-arm64 -DPANGU_LINK_MODE="$link_mode" -DBUILD_TESTING=ON -DPANGU_BUILD_ENGINE_TESTS=ON
      cmake --build --preset ios-arm64
      cmake -DPANGU_ENGINE_SOURCE_DIR=/Users/moozen/WindowsGit/PanguEngine "-DPANGU_TEST_ROOT=$HOME/PanguEngineBuild/iOS/Tests/ModuleLinkModes/$link_mode" -DPANGU_TARGET_PRESET=ios-arm64 -DPANGU_LINK_MODE="$link_mode" -P Tests/CMake/SourcePluginProject/CrossCompileTests.cmake
  done
  ```
- [x] **Step 7: 汇总 C++ 检查和基准迁移。** 在相关目标编译数据库上完成 clang-format/clang-tidy，记录已有与新增诊断。Benchmark 显式单独开启时，两模式构建并 dry run 现有六场景；保留业务输入及操作目的，说明常驻根新增的必要成本和接口差异，不通过移出计时改善结果，也不将新数字直接混入旧基线。本轮不重采无关性能基线、不建立跨宿主性能门禁，完成后 Benchmark OFF。
- [x] **Step 8: 整体 Review。** 唯一全分支评审为 Critical 0 / Important 1 / Minor 5；统一修复后限定复评 I1/M1–M5 全部 ADDRESSED，未新增问题。 执行一次全分支 Review 并修复有依据发现；修复涉及哪一契约就定向重验，不能以文档检查代替生命周期回归。
- [x] **Step 9: 归档并提交。** f934正式矩阵、最终定向修复、14条裁决及原因/代价、全部评审与复评结论已永久归档；控制器完成最终确认。 验收报告记录实现提交、平台/模式矩阵、精确命令、原始结果路径、未验证/阻塞范围和 Review 结论；更新本计划所有任务状态，提交 `docs: 归档 ModuleManager 双模式验收与规范`。
- [x] **Step 10: 清理临时上下文。** 2026-11-07已确认无运行依赖并完成必要结论归档；解析校验精确目标且非ReparsePoint后，仅删除本计划目录，正式Tests、其它计划目录与各平台BuildRoot保留。 确认没有运行任务依赖后仅删除本计划临时目录，保留正式 Tests、其他任务目录及各平台 BuildRoot；不得在未完成或验收失败时清理执行上下文。

## 自审与需求覆盖

本计划由主规划者对照批准规格自审，不以额外代理评审代替此步骤：

| 规格范围 | 落点 |
| --- | --- |
| §1–4：模式、默认值、单目标、导出、基础身份、移动宿主形态 | Task 1、3、4、7；Task 8 正式矩阵 |
| §5：批次、固定依赖、稳定记录、代际、Scope、常驻失败 Shutdown | Task 4；Task 5 的异步/原生失败；Task 6 的实际 Scope |
| §6：RTTI、常驻控制块、Pin/Borrow、逆序清理与 OS 成功含义 | Task 2、3、5、6 |
| §7：原始错误、有界报告、预算、并发、无异常与增量构建 | Task 1、4、5、7 |
| §8：ADR 替代、旧接口/历史结果、无 Meta 依赖 | Task 1/4 代码迁移；Task 8 规范与归档 |
| §9：类型/模式/Project/失败/卸载/移动验收 | 各任务同名测试；Task 8 执行完整受影响矩阵 |
| §10：具体接口与实现者选择 | 本计划“实施接口约定”，私有容器/原子细节在不改变后置条件下实现 |

自审结果：任务之间的函数名和类型以上述接口约定为唯一来源；五项 Review Focus 均绑定正式测试；没有独立 Meta/Registry/Hot Reload 子项目。构建模式和 Runtime 彼此耦合，适合一份顺序计划，不拆成不能单独验收的多份规格。代码片段只表达必要断言，未把整个实现抄入计划。用户已明确选择SDD；实施、正式验收、最终复评及本计划临时目录清理均已完成。
