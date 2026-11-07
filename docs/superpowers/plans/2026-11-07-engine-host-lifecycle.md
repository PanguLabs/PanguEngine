# EngineHost 最小启动与关闭闭环 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 交付 Application 拥有的 EngineHost，贯通业务 Module 的 Concrete Service 创建、精确作用域访问、失败恢复和安全关闭。

**Architecture:** EngineHost 是独立第三常驻 Module，拥有稳定的声明域和 Manager；底层 Module Runtime 增加只读的已声明资源查询，复用既有 ScopeAccess 控制块与三道 Gate。业务 Module 通过自身具名强类型入口消费宿主桥接，EngineHost 不反向依赖业务类型；首版固定启动集合，不实现通用参数注入。

**Tech Stack:** C++23、Clang、RTTI、无 C++ Exception、CMake 4.0+、Ninja、CTest、GoogleTest，Windows / Linux / macOS 原生加载及 Android / iOS 最终链接。

**Spec:** [EngineHost 最小启动与关闭闭环](../specs/2026-11-07-engine-host-lifecycle-design.md)，规划输入为 `c2336d6`，含两项规格评审验收补强。执行者同时阅读 AGENTS.md、Tests/README.md 和适用 wiki。

**Status:** 实现、正式矩阵及最终复审已完成，三项分支评审发现全部关闭。用户选择 SDD；Task 1–7 已通过独立双项评审。固定共享源码分支 `docs/engine-host-lifecycle`，未创建或搬迁工作区。

## Global Constraints

- 三种 Service 模式沿用既有规范：Concrete 首版贯通；完整 Contract Provider 选择与 Registry Contribution 后续。Service 不限定 `XXXManager`、共同基类或虚方法；Module 可以完全没有 Service。
- Application 拥有 EngineHost；EngineHost 不可复制、不可移动，构造不 Query / Load / Register；声明域活得比 Manager 更久。同进程多个独立宿主纳入正式验收，管理操作与查询由调用方串行协调。
- 一次宿主一次同步 Start：常驻接纳、注册、激活，再接纳一个业务批次、注册、激活。启动 Busy 也是失败；失败整体关闭，Closed 不重启；业务空批次跳过 Load。
- Ready 后业务 Module 集合固定；Context / World 创建失败局部回滚。只在 Ready 接纳新服务查询；查询精确指定 EngineHost、ScopeHandle、Owner、DeclarationId、Kind 和类型，不自动搜祖先或选择 Provider。
- ScopeAccess 只持有常驻控制块；ScopeBorrow 保护实例及代码。关闭拒绝新 Borrow，已有 Borrow / Pin、任意停止错误、原生释放失败均保留必要所有者；无 ForceUnload、超时强毁或后台清理线程。
- 内部类型擦除比较 RTTI 值；不比较 RTTI 地址，不使用仅名称/hash 或人工类型身份。DLL 类型信息不进入残留令牌；最终控制块释放仍在常驻 Runtime。
- 不引入 ConfigureInstance、附加 Register 回调、通用构造参数注入、全局 ServiceLocator 或业务令牌输出 Map；不复制 Manager 的资源 DAG / 清理器。
- 一个 Pangu Module 一个真实 STATIC/SHARED Target，同图统一 Monolithic/Modular；新增产物名称不得包含平台名；`BuiltinResidentQueries()` 保持原有两项。
- C++23 + Clang、无异常、保留 RTTI；标准容器 OOM 沿用终止政策。新增/修改 CMakeLists.txt 为 `cmake_minimum_required(VERSION 4.0)`，编译器校验沿用目标平台策略；格式/静态检查使用根配置及实际编译数据库。
- 正式源码共享，BuildRoot 按宿主隔离：Windows/Android 为 `Build/Windows` / `Build/Android`；Linux 为 `$HOME/PanguEngineBuild/Linux`，源码 `/home/moozen/WindowsGit/PanguEngine`；macOS/iOS 为 `$HOME/PanguEngineBuild/macOS` / `iOS`，源码 `/Users/moozen/WindowsGit/PanguEngine`。Linux 使用 WSL Ubuntu，Mac 使用 `ssh -o BatchMode=yes moozen-macos`；不复制引擎源码，不混用构建树。
- 正式 Tests 长期保留；同 BuildRoot 的 configure/build/test 串行。移动端仅编译/最终链接与宿主产物检查，不运行交叉目标 GTest，不声称设备、打包或签名通过。
- 提交、注释、文档中文，无工具署名尾注。临时文件仅放本计划 `.superpowers/sdd/2026-11-07-engine-host-lifecycle/`，正式验收不依赖它；完成评审和验收后先归档必要结论再精确清理。

## Review Focus

1. 纯元数据声明、同类型不同声明及无 Service Module：不能按类型误选或伪造访问；Task 1 / 4 的 `MetadataAndIdentityAreExact` 固定行为。
2. 失败 Scope 槽位复用与另一个宿主使用相同 DeclarationId：旧句柄/令牌不能访问新对象，其他 Scope 仍可用；Task 4 的 `FailedScopeReuseDoesNotAlias` 固定行为。
3. Query / Factory / 原生释放回调重入且复用同一报告：返回 Busy，外层状态和最终报告不被污染；Task 3 / 4 的 `SameReportReentryKeepsOuterResult` 固定行为。
4. 启动错误 E1 后停止轮询返回非 Busy E2：仍 Closing、必要资源存活，启动首错与当前清理错误分开，恢复后可完成；Task 3 的 `NonBusyStopFailurePreservesStartupFailure` 固定行为。
5. 新桥接生成的令牌残留在业务库真实物理卸载之后：复制、失败 TryBorrow 和最终释放全部安全；Task 5 的 `QueriedAccessSurvivesPhysicalUnload` 固定行为，不能使用旧 Observer 令牌替代。

---

## 文件职责与跨任务接口

| 位置 | 职责 |
| --- | --- |
| `Module/Public/ScopeResourceAccess.h`，`Module/Private/ScopeResourceAccess.cpp` | 内部只读查询及经校验的类型包装；路径均相对 `Source/Runtime/` |
| `Module/Public/{ScopeAccess,ModuleManager}.h`、`Module/Private/ScopeRuntime.{h,cpp}` | 最小 friend 接线、活资源类型缓存与现有 Runtime 中的资源定位，不拆解原清理器 |
| `EngineHost/Public/{EngineHost,EngineHostModule,HostStartupConfig,HostOperation,HostError,HostServiceAccess}.h` | 宿主、身份、输入、值报告、错误域、业务 Module 可调用的受限模板桥接 |
| `EngineHost/Private/{EngineHost,EngineHostModule,HostServiceAccess}.cpp`、`EngineHostState.h`、`EngineHostTestAccess.h` | 稳定所有权、状态机、非模板桥接、私有逐实例测试访问器 |
| `Tests/Module/ScopeResourceAccessTests.cpp`、`Tests/Module/Fixtures/ResourceAccess/` | 只依赖 Module 的基础查询契约，两模式运行 |
| `Tests/EngineHost/` | 宿主公开入口、失败清理、Scope、真实 DLL 及子进程终止；夹具/支持文件随职责放在子目录 |
| `Samples/MinimalHost/` | 普通 Concrete 服务与显式启动/Scope/借用/关闭示例，两模式使用相同入口协议 |
| `Tests/CMake/SourcePluginProject/` | 独立 Project 的 Game/Editor/MobileHost 消费迁移及增量/部署/最终链接 |

下列接口在计划内锁定；每个任务的 Interfaces 指向这里的同名签名，不自行换名或增设兼容转发层。

### A. Module 的只读资源查询（Task 1）

`pg::module::detail::ScopeResourceQuery { ScopeHandle Scope{}; ModuleId Owner{}; DeclarationId Declaration{}; pg::service::Scope Kind{pg::service::Scope::kHost}; }`。`ScopeResourceAccess` 为仅含静态方法的内部访问器，在 ScopeAccess 和 ModuleManager 中增加对应 friend：

```cpp
template <class T>
    requires(std::is_object_v<T> && !std::is_const_v<T> && !std::is_array_v<T> && std::is_nothrow_destructible_v<T>)
[[nodiscard]] static pg::Result<ScopeAccess<T>> Acquire(const ModuleManager& manager,
                                                       const ScopeResourceQuery& query) noexcept;
```

该模板调用 private 的 `PG_MODULE_API static Result<std::shared_ptr<ScopeObjectControl>> Resolve(const ModuleManager&, const ScopeResourceQuery&, const std::type_info&) noexcept`，校验成功才调用 ScopeAccess 的私有构造；没有公开的 unchecked/adopt 入口。Runtime 新增 `Result<std::shared_ptr<ScopeObjectControl>> ScopeRuntime::AcquireScopeResource(const ScopeResourceQuery&, const std::type_info&) const noexcept`。

错误顺序固定：Manager 正在操作 → `kBusy`；Manager 无效/已关闭/整体关闭 → `kInvalidState`；无效 Kind 或与实际 Scope Kind 不符 → `kInvalidScope`；Domain/槽位/generation 错误 → `kScopeNotFound`；Scope 未交付或 Closing → `kNotReady`；无对应活资源、Owner 不符或纯元数据声明 → `kNotFound`；Module 非 Active/资源非 Ready → `kNotReady`；RTTI 值不符 → `kTypeMismatch`。不追加或重编号既有 ModuleError。

### B. 宿主公开协议（Task 2–4）

- 命名空间 `pg::engine`；真实 Target `PanguEngineHost`，别名 `Pangu::EngineHost`，导出宏 `PG_ENGINE_HOST_API`，生成头 `PanguEngineHostExport.h`。
- `kEngineHostModuleId` 分配为 `8b07b8da6c9647469f8a31608f7e2193`，版本 `{1,0,0}` / 协议 1，Required Module Runtime `{1,0,0}`；`const module::ModuleDefinition* QueryEngineHostModule() noexcept`。
- `kHostErrorDomain = pg::ErrorDomainId::Constant<0x70672e686f7374ULL>()`；`enum class HostError : std::uint8_t { kInvalidState=1, kBusy=2 };`；`constexpr pg::Error MakeHostError(HostError) noexcept`。
- `HostState` 为 `kCold=0, kStarting=1, kReady=2, kClosing=3, kClosed=4`；`HostOperationStage` 为 `kNone=0, kResidentAdmission=1, kResidentRegistration=2, kResidentActivation=3, kBusinessAdmission=4, kBusinessRegistration=5, kBusinessActivation=6, kScopeOperation=7, kShutdown=8`。
- `HostStartupConfig` 四个借用字段：`span<const module::QueryModuleFn> AdditionalResidentQueries`、`span<const module::OptionalDependencyBinding> ResidentBindings`、`span<const module::ModuleLoadCandidate> Modules`、`span<const module::OptionalDependencyBinding> ModuleBindings`，均默认空。
- `HostStartupFailure { HostOperationStage Stage; pg::Error Cause; optional<module::ModuleOperationReport> ModuleReport; }`；只以真实错误构造，不假定 Error 有默认成功值。
- `HostOperationReport { HostState State{HostState::kCold}; HostOperationStage Stage{HostOperationStage::kNone}; optional<pg::Error> PrimaryError; optional<module::ModuleOperationReport> ModuleReport; optional<HostStartupFailure> StartupFailure; }`，拥有型固定字段，无堆上报告链。
- EngineHost 公开 `EngineHost()`、`~EngineHost()`；删除全部复制/移动操作。`GetState() const noexcept -> HostState`、`HostScope() const noexcept -> module::ScopeHandle`、`InspectModules() const noexcept -> Result<vector<module::ModuleSnapshot>>`、`InspectDeclarations() const noexcept -> Result<vector<module::RegisteredDeclaration>>`。后者复制注册域的已发布声明，用于保留 Project 现有诊断断言，不返回原 span。
- `Status Start(const HostStartupConfig&, HostOperationReport&) noexcept`；`Status BeginShutdown(HostOperationReport&) noexcept`；`Status TryFinishShutdown(HostOperationReport&) noexcept`。
- `Result<module::ScopeHandle> CreateContext(HostOperationReport&) noexcept`；`Result<module::ScopeHandle> CreateWorld(module::ScopeHandle context, HostOperationReport&) noexcept`；`Status BeginCloseScope(module::ScopeHandle, HostOperationReport&) noexcept`；`Status TryFinishCloseScope(module::ScopeHandle, HostOperationReport&) noexcept`；`Status PollCleanup(HostOperationReport&) noexcept`。
- `GetState` / `HostScope` 为无副作用观察；Cold/Closed 的 HostScope 无效。两项 Inspect 在管理操作内返回 Host Busy，无 Manager 时返回空集合；Ready/Closing/Closed 均可读取仍存续的诊断，成功关闭后集合为空。

私有 State 按声明域、optional Manager 的顺序存放，反向析构保证声明域最后销毁；另存 HostState、Operating 和 optional StartupFailure。报告只使用局部临时值，外部代码返回后整体发布。HostScope 不可单独关闭。

### C. 宿主桥接与业务入口（Task 4–6）

`pg::engine::detail::HostResourceAccess` 是 EngineHost 的 friend，公开与 A 相同 T 约束的静态 `Result<module::ScopeAccess<T>> Acquire(const EngineHost&, const module::detail::ScopeResourceQuery&) noexcept`。private 非模板 `PG_ENGINE_HOST_API static Result<const module::ModuleManager*> CheckedManager(const EngineHost&) noexcept` 只供该模板使用：Operating 优先返回 Host Busy，非 Ready 返回 Host InvalidState；不对调用方公开 Manager 指针。

模板通过 CheckedManager 后调用 A 的 Acquire，不回调业务代码、不缓存管理器地址。具名 Module 入口是头内薄包装，只固定自己的 Owner/声明/Kind/T，并接收 `const EngineHost&` 和 ScopeHandle；应用不手工传身份。精确访问目标 Scope，需祖先服务时调用方明确传祖先句柄。

### D. 夹具与示例身份

ModuleId：基础查询夹具 `d1000000000000000000000000000001`、Host 服务夹具 `d1000000000000000000000000000002`、最小示例服务 `d1000000000000000000000000000003`。使用显式 constexpr 字节，不能运行时生成。

声明前缀分别为 `d2` / `d3` / `d4`，其余字节为零、最后一字节取 `01` Host、`02` Context、`03` World、`04` 同类型第二 Context 实例、`05` 元数据。三组不得在同一声明域冲突。两类夹具各在自己的 namespace 提供 `kModuleId`、`kHostDeclaration`、`kContextDeclaration`、`kWorldDeclaration`、`kSecondContextDeclaration`、`kMetadataDeclaration` 常量。服务对象为普通结构体，不以 Manager 命名；同一种类型的不同声明通过 Value=1/2/3/4 区分。涉及跨 DSO 精确 RTTI 的这些公开非多态类型在 Windows 不标 class dllimport，在 POSIX 显式 default visibility；访问器保持头内定义。

## 实施任务

### Task 1: 补齐 Module 的精确只读资源访问

**Files:**
- Create: `Source/Runtime/Module/Public/ScopeResourceAccess.h`、`Source/Runtime/Module/Private/ScopeResourceAccess.cpp`。
- Modify: `Source/Runtime/Module/Public/{ScopeAccess,ModuleManager}.h`、`Private/ScopeRuntime.{h,cpp}`、`Source/Runtime/Module/CMakeLists.txt`。
- Create: `Tests/Module/ScopeResourceAccessTests.cpp`、`Tests/Module/Fixtures/ResourceAccess/{CMakeLists.txt,ResourceAccessFixture.h,ResourceAccessFixture.cpp}`；Modify `Tests/Module/CMakeLists.txt`。

**Interfaces:** Consumes 既有 ScopeHandle/DeclarationId、ScopeAccess 控制块、ScopeRuntime。Produces A；真实测试 Module `PanguScopeResourceFixture` 和 Query `pg::module::test::QueryResourceAccessFixture() noexcept`，身份/声明用 D，普通 `ScopeValue { int Value{}; }`。

- [x] **Step 1: 写新查询的失败测试。** 加入既有 PanguModuleIntegrationTests。`ScopeResourceAccess.MetadataAndIdentityAreExact` 断言 Host/Context/World 对应 Value=1/2/3，同类型第二 Context 声明 Value=4；元数据/错 Owner 为 kNotFound，错 T 为 kTypeMismatch，错 Kind 为 kInvalidScope，错 Domain/代际为 kScopeNotFound，不回退祖先。`ScopeResourceAccess.RejectsUnreadyAndClosing` 覆盖未交付、未 Active 和 Closing，不多调用工厂。夹具 Mono 链接静态 Query；Modular 只引用生成的绝对路径和公开类型头，add_dependencies 而不链接业务 DLL。

  ```cpp
  const auto access = pg::module::detail::ScopeResourceAccess::Acquire<ScopeValue>(*Manager, query);
  ASSERT_TRUE(access);
  auto borrow = access->TryBorrow();
  ASSERT_TRUE(borrow);
  EXPECT_EQ((*borrow)->Value, 2);
  ```

  测试类 `ScopeResourceAccessTest` 持有 RegistrationScope 和 optional Manager，先基础根 Register/Activate，再 Load/Register/Activate 夹具，最后显式 Shutdown；任何失败路径同样完成 Manager 清理。
- [x] **Step 2: 运行 RED。** `cmake --preset windows`；`cmake --build Build/Windows --target PanguModuleIntegrationTests`。预期新头/Acquire 尚不存在导致对应编译诊断；环境预检、未知目标或缺依赖不算 RED。
- [x] **Step 3: 实现 A。** Resolve 拒绝 Operating/关闭状态，再调用 Runtime 查询。Stage 时在 Resource 元数据缓存 `const std::type_info* TypeInfo{}`，取值发生于受保护的 Own 装配内；Destroy 清理对象时清空，不加入 ScopeObjectControl。新查询仅比较缓存 RTTI，不调用 ErasedScopeObject 的虚 Type/Get，不调用工厂，不改变注册或资源集合。模板校验成功才包装原控制块。Linux Modular 的 PanguModuleIntegrationTests 因新增 EXE 参与的 RTTI 查询显式设置 ENABLE_EXPORTS，保持库的最小导出与隐藏策略。
- [x] **Step 4: 运行 GREEN。** 构建上述目标，`ctest --preset windows-precommit -R 'ScopeResourceAccess[.]' --no-tests=error --output-on-failure`；模式不同的正式根由 Task 7 全验收，此处不宣称两模式通过。clang-format/实际 DB tidy 检查改动 TU。
- [x] **Step 5: 提交。** `git diff --check`；仅暂存本任务文件，提交 `feat: 支持作用域资源的精确只读访问`。

### Task 2: 交付 EngineHost Module 与基本启动闭环

**Files:**
- Create: `Source/Runtime/EngineHost/CMakeLists.txt`、`Public/{EngineHost,EngineHostModule,HostStartupConfig,HostOperation,HostError}.h`、`Private/{EngineHost,EngineHostModule}.cpp`、`Private/EngineHostState.h`。
- Modify: 根 `CMakeLists.txt`、`Tests/CMakeLists.txt`。
- Create: `Tests/EngineHost/{CMakeLists.txt,EngineHostStartupTests.cpp}`。

**Interfaces:** Consumes 既有 Manager Create/RegistrationOrder/Load/Register/Activate/BeginShutdown/TryFinishShutdown。Produces B 的身份/值类型、全部观察与启动/关闭入口；Scope 方法由 Task 4 定义，不建立假的返回成功实现。

- [x] **Step 1: 写启动与所有权测试。** `EngineHostStartup.ColdConstructionHasNoModuleSideEffects` 断言 Cold、无效 HostScope、快照空及不可复制/移动；`EmptyBusinessStartsThreeResidents` 断言三个常驻 Active、Resident=true、BuiltinResidentQueries 仍为2，并能 Begin/Finish 到 Closed；`ServiceLessBusinessParticipatesNormally` 验证仅元数据的合法业务 Module 为第四记录，无工厂、无需服务也 Ready。静态断言写入测试 TU：

  ```cpp
  static_assert(!std::is_copy_constructible_v<pg::engine::EngineHost>);
  static_assert(!std::is_move_constructible_v<pg::engine::EngineHost>);
  TEST(EngineHostStartup, EmptyBusinessStartsThreeResidents)
  {
      pg::engine::EngineHost host;
      pg::engine::HostOperationReport report;
      ASSERT_TRUE(host.Start({}, report));
      EXPECT_EQ(host.GetState(), pg::engine::HostState::kReady);
      ASSERT_TRUE(host.BeginShutdown(report));
      ASSERT_TRUE(host.TryFinishShutdown(report));
      EXPECT_EQ(host.GetState(), pg::engine::HostState::kClosed);
  }
  ```

  新目标 `PanguEngineHostTests` 链接 PanguEngineHost / GoogleTestMain，area.engine-host、boundary.integration、tier.fast；从 Tests 的交叉编译 return 之后接入。
- [x] **Step 2: 运行 RED。** 配置并构建 PanguEngineHostTests，确认缺少 EngineHost 接口或断言失败，不将测试登记失败算作契约 RED。
- [x] **Step 3: 实现 Module 与 B 的基本流程。** 根在 Module 后添加 EngineHost；CMake 用现有模式/导出/Query helper，PUBLIC 链接 PanguModule，固定身份、空注册和 Required Module Runtime。实现稳定 State、三根组合、有界数量检查、分阶段依赖顺序、空业务跳过、基本失败清理和 Cold/Ready/Closed 规则；任一失败不得泄露 Manager 或发布 Ready。本任务只代表基本闭环，失败报告跨轮持久性由 Task 3 验收。
- [x] **Step 4: 运行 GREEN。** `cmake --build Build/Windows --target PanguEngineHostTests`；`ctest --preset windows-fast -R 'PanguEngineHostTests[.]EngineHostStartup[.]' --no-tests=error --output-on-failure`。检查生成 PanguEngineHostExport.h、单一目标类型、Query 包装只在 Modular；不得开启自动导出全部符号。
- [x] **Step 5: 提交。** format/tidy/diff 检查后提交 `feat: 建立 EngineHost 常驻模块与启动闭环`。

### Task 3: 完成失败报告、可重试关闭和有限析构

**Files:**
- Modify: `Source/Runtime/EngineHost/Private/{EngineHost.cpp,EngineHostState.h}`、`Public/{EngineHost,HostOperation}.h`、`Tests/EngineHost/CMakeLists.txt`。
- Create: `Source/Runtime/EngineHost/Private/EngineHostTestAccess.h`；`Tests/EngineHost/{EngineHostFailureTests.cpp,EngineHostShutdownTests.cpp,EngineHostDestructorFixture.cpp,EngineHostDestructorTests.cmake}`；`Tests/EngineHost/Fixtures/{CMakeLists.txt,HostServiceModule.h,HostServiceModule.cpp}`；`Tests/EngineHost/Support/{CMakeLists.txt,HostTestState.h,HostTestState.cpp}`。

**Interfaces:** Consumes B。Produces 完整 Start/Shutdown/报告行为；私有 `detail::EngineHostTestAccess::Manager(EngineHost&) noexcept -> module::ModuleManager*`，函数以 PG_ENGINE_HOST_API 导出、仅在 Private 测试头声明，只供测试继续使用现有 ModuleManagerTestAccess 的单次原生释放故障。夹具 `PanguHostServiceFixture` 采用 D 身份，通过 `pg::engine::test::QueryHostServiceFixture() noexcept` 接纳。

测试支持库 `PanguHostTestSupport` 是普通 SHARED 辅助目标（不调用 pangu_configure_module、无 Pangu:: 别名/ModuleId），沿用 `Tests/Module/Support/CMakeLists.txt` 的导出/输出目录模式，供 EXE 与夹具共享故障控制和计数，不负责服务查询、不保存业务令牌。`HostTestState` 提供 `void Reset() noexcept`、`optional<pg::Error> FailRegister/FailStart/PollError`、初值0的 `size_t FactoryCount/DestroyCount/NativeUnloadCount`，以及 `vector<HostTestEventRecord> Events`；`HostTestEvent` 枚举为 kFactory、kStart、kStop、kDestroy、kDeleter、kFactoryDestroy、kNativeUnload，`HostTestEventRecord { HostTestEvent Event; module::ScopeHandle Scope; module::ModuleId Owner; }`。支持库导出 `shared_ptr<HostTestState> CurrentHostTestState() noexcept` 和 `void SetHostTestState(shared_ptr<HostTestState>) noexcept`，仅供串行测试准备/恢复；注册时捕获一份 shared_ptr，之后不读取被别的 Host 替换的当前状态。该观测设施不参与产品服务查找或配置接口。夹具 Descriptor Required EngineHost 1.0.0；其自身模式与外层一致。

- [x] **Step 1: 写失败及跨轮报告测试。** `EngineHostFailure.ResidentAndBusinessStagesStopAtFirstError` 参数化常驻/业务接纳、Register、Activate 六阶段，检查 Stage、E1、后续阶段未执行、Closed/Closing；`StartupBusyIsFailure` 用服务 Start 返回 Module kBusy，断言不能恢复启动。`Shutdown.NonBusyStopFailurePreservesStartupFailure` 使用测试错误域 `0x741`、E1 code=101、E2 code=102，保持 PollError=E2 直至手动恢复：

  ```cpp
  ASSERT_TRUE(report.StartupFailure);
  EXPECT_EQ(report.StartupFailure->Cause, pg::MakeError(pg::ErrorDomainId::Constant<0x741>(), 101));
  EXPECT_EQ(host.GetState(), pg::engine::HostState::kClosing);
  EXPECT_EQ(report.PrimaryError, pg::MakeError(pg::ErrorDomainId::Constant<0x741>(), 102));
  EXPECT_EQ(state.DestroyCount, 0);
  ```

  上述 PrimaryError=E2 断言属于后续失败的 TryFinishShutdown；Start 当次返回/PrimaryError 必须是 E1。旧 report 销毁后重复，最后清除 PollError，断言 Closed 且 StartupFailure 仍 E1。增加 `NativeCloseFailureRetainsManager`、`SameReportReentryKeepsOuterResult`、重复 Start/Begin/Finish、不重新 Query、超限/错误候选清理；组合额外常驻项上限为253，254项返回 Module LimitExceeded 且不执行 Query。
- [x] **Step 2: 运行 RED。** 构建 PanguEngineHostTests，`ctest --preset windows-precommit -R 'PanguEngineHostTests[.](EngineHostFailure|Shutdown)[.]' --no-tests=error --output-on-failure`。至少验证新增跨轮首错/当前错断言的真实失败；已由 Task 2 自然满足的分支如实记录，不人为删代码制造红灯。
- [x] **Step 3: 实现持久错误与关闭推进。** 首错拥有型快照保存在 State，Start 失败主动 Begin + 至多一次 Finish；后续清理返回当前错误，首错不覆盖。所有外部调用完成后整体发布局部报告，重入只返回 Host Busy。Begin 的成功不等于完成，Closed 仅由 Manager Finish 成功或无 Manager 的终止路径确定；任意停止/释放错误保留 State。
- [x] **Step 4: 写并验证析构子进程。** `EngineHostDestructorFixture` 支持参数 `clean|busy` 和一个 BuildRoot marker 路径：clean 显式/兜底一次完成，退出0；busy 保持真实外部 Pin，terminate handler 在进程退出 DLL 回调前保存 NativeUnloadCount=0，并 `_Exit(91)`。准备失败退出2–12；CMake 驱动必须精确检查0/91及 marker，不把任意非零当成功。析构在 Operating 时终止；未完成时立即终止，不能先让 Manager 析构再隐式第二次清理。
- [x] **Step 5: 运行 GREEN 并提交。** PanguEngineHostTests fast + `EngineHostDestructor` precommit/subprocess 均通过，format/tidy，提交 `feat: 完善 EngineHost 失败报告与可重试关闭`。

### Task 4: 接入 Scope、宿主桥接和业务具名服务入口

**Files:**
- Create: `Source/Runtime/EngineHost/Public/HostServiceAccess.h`、`Private/HostServiceAccess.cpp`、`Tests/EngineHost/{EngineHostScopeTests.cpp,EngineHostServiceTests.cpp}`。
- Modify: `EngineHost/Public/EngineHost.h`、`Private/EngineHost.cpp`、EngineHost/Tests 的 CMakeLists；`Tests/EngineHost/Fixtures/{HostServiceModule.h,HostServiceModule.cpp}`。

**Interfaces:** Consumes A / B。Produces B 中所有 Scope 方法、C 的 HostResourceAccess，以及夹具头内 `Result<module::ScopeAccess<HostServiceValue>> AcquireContextValue(const EngineHost&, module::ScopeHandle) noexcept` / `AcquireWorldValue(...) noexcept`；`HostServiceValue { int Value{}; }`，Context=2、World=3，普通类型无 Manager 基类。身份为 D 的 Host 服务组；两个 accessors 不导入业务 Query 或非内联业务函数。

- [x] **Step 1: 写 Scope 与强类型访问测试。** `EngineHostService.TwoHostsAndScopesStayIndependent` 创建两个 Host、各两个 Context、Context 下两个 World，使用具名入口验证 Value 及独立对象地址；只在 Borrow 存活时比较地址。`MetadataAndIdentityAreExact`、`NoImplicitAncestorLookup`、`FailedScopeReuseDoesNotAlias` 覆盖 Review Focus 1/2，后者先装配失败、Poll 回收、复用槽位，旧 handle/令牌仍拒绝而其它 Scope 正常。对跨 Host 相同声明、错层级、未创建、纯元数据、错误 T 等使用 A 中错误语义。

  ```cpp
  const auto context = host.CreateContext(report);
  ASSERT_TRUE(context);
  const auto access = pg::engine::test::AcquireContextValue(host, *context);
  ASSERT_TRUE(access);
  auto borrow = access->TryBorrow();
  ASSERT_TRUE(borrow);
  EXPECT_EQ((*borrow)->Value, 2);
  ```

  `EngineHostScope.LocalFailureKeepsOtherScopesReady` 验证 Factory/Start 失败及未交付 pending，只能 Poll/Shutdown；父有 World 时显式 Finish Context 不能完成。`SameReportReentryKeepsOuterResult` 在 Factory/Stop 中重入创建/查询，断言 Host Busy 和外层最终报告正确。访问查询失败不得修改 StartupFailure。
- [x] **Step 2: 运行 RED。** 构建 PanguEngineHostTests 并运行 `ctest --preset windows-precommit -R 'PanguEngineHostTests[.](EngineHostScope|EngineHostService)[.]' --no-tests=error --output-on-failure`；新接口尚不存在或对应断言失败。
- [x] **Step 3: 实现 Scope 方法和 C。** Ready 的 Context 父固定 HostScope，World 父必须为明确 Context；独立关闭/局部回滚复用 Manager，不新增清理图。CreateContext/CreateWorld/BeginCloseScope/TryFinishCloseScope/PollCleanup 都只在 Ready 接纳：其他状态返回 Host InvalidState，操作重入优先 Host Busy；Closing 明确使用 TryFinishShutdown，不能用 Poll 成功转 Closed。任何底层报告已为 ManagerShutdown 时同步转 Closing。Closing 拒绝新服务查询；GetState/诊断/整体关闭仍可用。CheckedManager 不改变状态，桥接模板完成后不保存管理器指针。Linux Modular 对实际参与公开 RTTI 的 PanguEngineHostTests 设置 ENABLE_EXPORTS。
- [x] **Step 4: 运行 GREEN 并提交。** 本目标所有 Host fast 测试通过；`cmake --build --preset windows-precommit` 后运行既有 ScopeLifecycle / SharedScopeLifecycle / ScopeResourceAccess 定向回归（若本模式未登记旧 Mono 用例，由 ModuleRegression 执行，不用空选择冒充通过）。format/tidy，提交 `feat: 贯通宿主作用域与模块服务访问`。

### Task 5: 验证真实卸载、依赖顺序和两模式测试入口

**Files:**
- Create: `Tests/EngineHost/{EngineHostLifetimeFixture.cpp,EngineHostLifetimeTests.cmake,EngineHostLifetimeTests.cpp,EngineHostConcurrencyTests.cpp}`。
- Create: `Tests/EngineHost/Fixtures/{HostConsumerModule.h,HostConsumerModule.cpp}`。
- Modify: `Tests/EngineHost/CMakeLists.txt`、`Fixtures/{HostServiceModule.h,HostServiceModule.cpp,CMakeLists.txt}`、`Support/{HostTestState.h,HostTestState.cpp}`；`Tests/CMake/ModuleLinkModes/{ModuleRegressionTests.cmake,ModeMatrixTests.cmake,ModeSwitchTests.cmake}`。

**Interfaces:** Consumes Task 3 的支持库、Task 4 的具名入口。Produces CTest `EngineHostLifetime`（precommit、subprocess）、目标 `PanguEngineHostLifetimeFixture`、`PanguEngineHostConcurrencyTests`（precommit、threads）；新 Consumer 为独立 `PanguHostConsumerFixture` Module，`pg::engine::test::QueryHostConsumerFixture() noexcept -> const module::ModuleDefinition*`，各自生成原生 Query。在既有 ModuleRegression 的 Mono 子图显式构建 PanguEngineHostTests / PanguEngineHostDestructorFixture / PanguEngineHostConcurrencyTests，并将运行标签正则扩为 `^area[.](module|engine-host)$`；Destructor 子进程的正式目标名固定为 PanguEngineHostDestructorFixture。

- [x] **Step 1: 写卸载后令牌子进程验收。** `EngineHostLifetime.QueriedAccessSurvivesPhysicalUnload` 同一流程以新具名入口取得 Context/World 令牌、释放活动 Borrow、完整 Host 关闭；仅在观察 NativeUnload 后复制 ScopeAccess，断言 TryBorrow 为 Module kNotReady，最后释放全部副本。保持 observer 无 token；准备错误2–12、成功0，marker 必须证明卸载事件早于最后 token 释放。禁止链接 PanguHostServiceFixture；类型在 Windows 用无 DLL import 的普通公开类型，在 POSIX 用 `visibility("default")`；头内访问入口仅调用常驻桥接，不能暗中固定业务库。
- [x] **Step 2: 写顺序/并发断言并观察 RED。** `EngineHostLifetime.ConsumersDrainBeforeProviders` 使用上述独立 Consumer（ID `d1000000000000000000000000000004`，Required Provider 1.0.0，声明 `d5000000000000000000000000000001` Host、末字节02 World）。Consumer Host 依赖 Provider Host，Consumer World 依赖 Provider Context / World；各入口显式 ScopeDependency，不制造 Host → Context 的非法方向。断言最后对象/Deleter 早于最后 Factory、Factory 早于本库 NativeUnload、Consumer 原生释放未确认前 Provider 存活。`EngineHostConcurrency.QueriedBorrowLinearizesWithClose` 使用 latch/barrier，管理操作单线程，业务线程只对已查令牌 TryBorrow/Release；关闭后无新成功，最后 Release 不执行 OS Unload。已有行为首轮满足时记录 GREEN，不删除保护制造竞态红灯；新增路径/接线缺失的失败单独记录。
- [x] **Step 3: 实现必要最小修正与正式接线。** 仅修复新桥接/宿主暴露的问题，不复制旧 Runtime 清理逻辑。Mono 验证逻辑失效、依赖顺序和残留操作；只有 Modular 登记物理卸载用例。两个模式都登记析构/并发用例；CMake 共享输出隔离、挂接 fast/precommit/full 累计目标。Lifetime/Concurrency EXE 在 Linux Modular 设置 ENABLE_EXPORTS；不向其它平台泛化该选项。模式 Matrix/Switch 加入 PanguEngineHost 类型/链接/Query 检查，无变化构建无新增工作。
- [x] **Step 4: 运行 GREEN。** `cmake --build --preset windows-precommit`；`ctest --preset windows-precommit -R 'EngineHost|ModuleRegression|ScopeResourceAccess' --no-tests=error --output-on-failure`。再构建 windows-full 并定向 `ModuleLinkMode(Matrix|Switch)`。真实物理卸载必须是观测结果，逻辑 Finish 成功不替代；失败日志保留。
- [x] **Step 5: 提交。** format/实际 DB tidy/diff 检查后提交 `test: 覆盖宿主服务访问的动态寿命与并发关闭`。

### Task 6: 交付示例并迁移独立 Project 消费者

**Files:**
- Create: `Samples/MinimalHost/{CMakeLists.txt,Main.cpp,MinimalService.h,MinimalService.cpp,README.md}`。
- Modify: 根 `CMakeLists.txt`、`Tests/EngineHost/CMakeLists.txt`；`Tests/CMake/SourcePluginProject/MyGame/CMakeLists.txt`、`Source/{HostCheck,FixtureModules}.h`、`Source/{GameMain,EditorMain,MobileHost}.cpp`。
- Create: `Tests/CMake/SourcePluginProject/MyGame/Source/{GameplayService,EditorService}.h`。
- Modify: `MyGame/Plugins/{Gameplay,EditorExtras}/CMakeLists.txt`、`Gameplay/Private/Gameplay.cpp`、`EditorExtras/Private/EditorExtras.cpp`；`Tests/CMake/SourcePluginProject/{RunIntegration,StaticCatalogIntegrationTests,SharedDeployTests,IncrementalBuildTests,PluginTestOwnershipTests,CrossCompileTests}.cmake`。
- Modify: `Tests/MIGRATION.md`，记录 Project 旧调用路径的等效覆盖与固定业务集合下的关闭契约替代。

**Interfaces:** Consumes B / C。Produces `PANGU_BUILD_SAMPLES`（默认 OFF）、`PanguMinimalHost` / Module `PanguMinimalServices`（D 示例身份）；`pg::sample::QueryMinimalServices() noexcept -> const module::ModuleDefinition*`、`pg::sample::CounterService { int Value{}; }` 和头内 `Result<module::ScopeAccess<CounterService>> AcquireCounter(const engine::EngineHost&, module::ScopeHandle) noexcept`，固定 Context 声明 `d4000000000000000000000000000002`、初值7。Query Required EngineHost 1.0.0；原生 Query 包装仅为该服务 Target 生成。

独立 Project 的 Gameplay / EditorExtras 保留原身份和 metadata DeclarationId，但将原 Declare 改成同 ID 的 Context DeclareFactory；各提供 `GameplaySession { int Value{7}; }` / `EditorSession { int Value{11}; }` 和头内 `AcquireGameplaySession(...)` / `AcquireEditorSession(...)`，参数/返回同 AcquireCounter。Physics 保持可无 Service 的原行为；声明数量与原身份检查不随意删减。Plugin CMake PUBLIC 链接 PanguEngineHost，形成业务 → Host → Module，不反向链接。

- [x] **Step 1: 改验收先观察 RED。** Project Game/Editor 输出须包含分别读取并修改独立 Context 服务的结果（Game 初值7、Editor 初值11、另一个 Context 保持初值），同时继续检查消费集合、部署缺失失败、无变化构建及模式往返。保留原 FixtureModule 身份、依赖优先顺序、声明数量/ID 断言，更新常驻计数由2到3，声明断言通过 InspectDeclarations 的拥有型快照完成。旧 HostCheck 的逐业务单项卸载序列由固定集合宿主的整体 Shutdown 替代，旧 Manager 单项卸载正式回归不删除，迁移依据写入 Tests/MIGRATION.md。运行 SourcePluginProject/StaticCatalog/SharedDeploy 定向测试，旧 HostCheck 无服务访问输出应失败。
- [x] **Step 2: 实现示例与迁移。** 样例 Mono 显式静态 Query，Modular 要求用户传入一个绝对业务库路径；不搜索目录。Root 在 Tests 之前按显式 option 接入 Samples；PanguMinimalServices 用模式 helper，服务 Factory 为默认配置，示例不引入通用注入。Start 失败检查报告并完成可能的清理，成功创建两个 Context/World、取得并借用真实服务、释放 Borrow、显式关闭。演示资源同步结束，遇非预期未完成须清楚报错而不是无限空转；README 说明真实异步等待由 Application 实现。
- [x] **Step 3: 保持 Modular 的消费者边界。** HostCheck 改为 EngineHost；Modular 主程序只链接常驻库，通过 include 接线获得业务公开服务头，不 target_link_libraries 业务库；生成候选/显式 Deploy 保留。Mono 继续保留 Query 根引用。Linux Modular 对样例 / Game / Editor 的实际 RTTI 消费目标开启 ENABLE_EXPORTS。移动 Host 也经新宿主和服务入口形成真实代码引用；CrossCompile 检查 MobileHost 对 EngineHost::Start 的引用，Modular 的 EngineHost 库才是 ModuleManager::Create 的调用方，应在那里检查，不能继续要求 MobileHost 直接引用该符号。Modular 依赖须含 PanguEngineHost/PanguModule/PanguFoundation，Mono 的相应定义在最终产物；不得把 EngineHost 本身只有归档成功当最终链接。
- [x] **Step 4: 运行 GREEN。** `cmake --preset windows -DPANGU_BUILD_SAMPLES=ON`，构建 PanguMinimalHost 并运行其 CTest `MinimalHostSmoke`。仅在样例 Target 存在时由 Tests/EngineHost 注册，BOUNDARY system / AREA engine-host / TIER precommit，BUILD_TARGETS 为 PanguMinimalHost、PanguMinimalServices；Modular 命令显式传目标绝对路径，Mono 不传路径。程序必须根据实际 Value/隔离/关闭断言返回0，不能只打印成功。再构建 windows-full，`ctest --preset windows-full -R 'SourcePlugin(Project|StaticCatalog|SharedDeploy|Incremental|TestOwnership)' --no-tests=error --output-on-failure`。Project 正式轮次在干净提交后锁定当前 SHA、override OFF；开发期 override 的结果不能冒充正式锁验证。
- [x] **Step 5: 提交。** format/tidy/diff 检查后提交 `feat: 提供宿主服务示例并接入独立工程`。

### Task 7: 正式平台矩阵、规范同步与收尾

**Files:**
- Modify: `wiki/core/{engine-host,service,module-api}.md`、`wiki/foundation/build-system.md`、`wiki/README.md`、`Tests/README.md`；必要的 ADR 记录位于 `wiki/decisions/0016-engine-host-lifecycle.md` 并更新其索引。
- Create: `docs/superpowers/plans/2026-11-07-engine-host-lifecycle-validation.md`；更新本计划任务状态、关键提交、遗留问题。

**Interfaces:** Consumes Task 1–6 全部接口。Produces 与准确提交绑定的编译/链接/运行证据、文档状态与保留正式 Tests；不新增产品接口。本任务是正式验收和文档任务，不制造无意义 RED。

- [x] **Step 1: 静态检查并冻结验收输入。** 对变更 C++ 用仓库 clang-format dry-run/Werror；每个相关实际编译命令对应 TU 用 clang-tidy，记录新增与已有诊断及原生退出码。计划中的断言片段按执行时完整测试 TU 检查，不以未编译文档片段当通过。先提交代码/规范，记录 SHA 与完整子模块状态，再启动正式矩阵；不将忽略子模块的 status 当完整干净证明。
- [x] **Step 2: 三桌面两模式原生验收。** 每宿主按 `Monolithic → Modular` 串行执行下列三条命令，`<platform>` 分别为 windows/linux/macos；Linux 从约定 WSL SMB 源码路径、Mac 从约定挂载路径运行，禁止另一引擎源码副本。此处占位符只表示该平台 preset 名，执行记录写实际完整命令：

  ```sh
  cmake --preset <platform> -DPANGU_LINK_MODE=<mode> -DPANGU_BUILD_SAMPLES=ON
  cmake --build --preset <platform>-full
  ctest --preset <platform>-full --no-tests=error --output-on-failure
  ```

  每模式必须实际运行 HostStartup/Failure/Shutdown/Scope/Service/Concurrency、样例、Project；Modular 另有真实物理卸载。ModuleRegression 在 Modular precommit/full 构建并运行旧 Mono + 新 Host Mono，不丢旧测试。末次无变化 build 不新增扫描或重链接；按真实配置记录数量，不能拼接不同 SHA。
- [x] **Step 3: 两移动平台两模式最终链接。** 先 Windows `cmake --preset android-arm64` / Mac `cmake --preset ios-arm64`，对应 build preset 实际编译新增 EngineHost；再对每个 linkMode 执行既有 CrossCompileTests（不直接运行交叉 exe）：

  ```powershell
  cmake -DPANGU_ENGINE_SOURCE_DIR=D:/Git/PanguEngine -DPANGU_TEST_ROOT=D:/Git/PanguEngine/Build/Android/Tests/EngineHost/Monolithic -DPANGU_TARGET_PRESET=android-arm64 -DPANGU_LINK_MODE=Monolithic -DPANGU_ENGINE_ALLOW_LOCAL_OVERRIDE=OFF -P Tests/CMake/SourcePluginProject/CrossCompileTests.cmake
  ```

  同命令将工作目录/模式改为 Modular 再执行。Mac 在挂载源码内用相同参数，source=`/Users/moozen/WindowsGit/PanguEngine`、testRoot=`$HOME/PanguEngineBuild/iOS/Tests/EngineHost/<mode>`、preset=`ios-arm64`。检查 MobileHost 最终架构、EngineHost/Query/服务调用符号、动态依赖与部署，拒绝 GTest、未消费业务 Module 及平台名产物；不是设备运行。
- [x] **Step 4: 恢复正式默认并复验。** 三桌面明确恢复 Modular、Samples OFF、BUILD_TESTING/PANGU_BUILD_ENGINE_TESTS ON、Benchmark OFF，实际 build/test fast；移动根 preset 恢复 Monolithic、Samples/Benchmark OFF、BUILD_TESTING OFF，实际构建 PanguEngineHost，再用 Mono CrossCompileTests 重验 MobileHost（它属于脚本的隔离 Project 构建树，不虚构根 MobileHost 目标）。模式差异/示例可选测试数量分别记录，不通过删覆盖降低验收。
- [x] **Step 5: 同步规范与验收摘要。** 写清三种 Service 语义未变、Module 可无服务、新查询能力的实现状态与范围，补 ADR 的单次宿主/只读具名访问理由。验证 docs/wiki 相对链接、git diff --check。记录两个规格 Minor 的对应正式用例、实际平台与结果，未完成项如实列出；不把“无剩余设计问题”写成运行通过。
- [x] **Step 6: Review、提交与清理。** 按所选执行方法完成任务/分支 Review；修复支持的发现并做相关定向复验。归档必要裁决、关键提交和限制，所有正式 Tests/BuildRoot 保留。只有确认无运行任务依赖且验收通过后，解析/验证本计划临时目录绝对路径、排除 reparse point，精确删除该目录。最后提交 `docs: 归档 EngineHost 生命周期验收与实现状态`；不自行 push/merge。

Step 6 收尾状态：任务评审、整分支评审及唯一一次修复范围复审均已完成，三项发现全部关闭；控制器 fresh fast 69/69通过，裁决和限制已归档。已验证计划目录绝对路径、owner与全部reparse point，确认无运行依赖后精确清理本计划临时目录；正式 Tests/BuildRoot 全部保留。

## 计划自查与执行交接

规格覆盖：§1–3 → Task 2/6；§4–6 → Task 2/3；§7.1 → Task 1/4；§7.2 → Task 4/5；§7.3 → 全局约束/Task 6；§8 → Task 3/4；§9 两项评审补强 → Task 3/5，完整平台矩阵 → Task 7。无 Service Module 在 Task 2/4，三种 Service 模式与准确实现状态在 Task 7 同步。

步骤自查：每项代码任务有具体测试、RED/首次结果、生产接缝、GREEN 和中文提交；任务间使用 A/B/C/D 的单一定义；Review Focus 五项均有归属用例。计划内最小实现、跨平台矩阵、真实物理卸载与静态检查已按实际范围完成；首轮失败、修复后统一 SHA 重跑、已有诊断与未验证范围见正式验收记录，不以设计状态代替运行证据。

用户已选择 Subagent-driven：逐任务独立实现与评审，最后进行分支 Review。本计划建议 Subagent-driven：七项任务跨资源查询、状态机、真实 DLL 和 Project 接线，逐任务评审更容易在接口错误扩散前发现问题。用户选定的方法跨后续轮次保留，不重复要求选择。

## 实施验收摘要与关键提交

正式验收详见 [EngineHost 生命周期验收记录](2026-11-07-engine-host-lifecycle-validation.md)。最终被测源码 `08fbbfab8c4864d09f5e4df58b1b4ce7f9080097`，完整子模块状态正常；所有最终运行结果绑定该版本。初轮 `897a22e` 的 POSIX Native 错误预期缺陷已最小修复，旧失败/通过/中止日志保留，不拼接为最终矩阵。`543678f` 完整矩阵也仅作历史；最终分支评审发现的同步关闭报告、Windows 双版本证据和 ADR 表格已集中修复，全部平台再次重跑。

| 阶段 | 提交 | 验收/状态 |
| --- | --- | --- |
| Task 1 精确资源查询 | `6a50fdb` | 独立双项评审 Approved |
| Task 2 宿主基本启动 | `71774c7` | 独立双项评审 Approved |
| Task 3 失败报告与关闭 | `8b07bb4` | 独立双项评审 Approved；Minor 在 Task 7 补强 |
| Task 4 Scope 与具名访问 | `2fbf799` | 独立双项评审 Approved |
| Task 5 动态寿命与并发 | `2dc781c` | 独立双项评审 Approved；Minor 在 Task 7 补强 |
| Task 6 样例与独立 Project | `cd956c9` | 独立双项评审 Approved |
| Task 7 测试补强与规范/ADR | `897a22e` | 两项 Minor 增强、实现状态同步 |
| Task 7 POSIX 预期修复/历史冻结 | `543678f` | 历史全平台矩阵保留 |
| 最终分支评审集中修复/最终冻结 | `08fbbfa` | 首次同步清理报告修复；统一 SHA 全平台矩阵重新验收 |
| 修复后验收归档 | `4e82d2a` | 三项发现全部复审通过；无新增问题；最终清理完成 |

- windows：Monolithic full 138/138，Modular full 130/130；恢复默认 fast 69/69；原生码均 0。
- linux：Monolithic full 137/137，Modular full 129/129；恢复默认 fast 69/69；原生码均 0。
- macos：Monolithic full 137/137，Modular full 129/129；恢复默认 fast 69/69；原生码均 0。
- Android/iOS：两模式真实 MobileHost 最终链接、架构/符号/动态依赖/部署检查通过；恢复默认后再次构建 EngineHost 并完成 Mono MobileHost 最终链接。未运行交叉 GTest 或设备程序。
- 各正式模式及恢复后末次构建无新增 CXX 扫描/编译/链接；桌面聚合 utility/dyndep 动作单独如实记录。
- 所有正式 Tests 长期保留。已有静态诊断、移动设备运行/打包/签名、完整 Contract/Registry、主循环及调度/GPU 集成边界见验收记录。
- 评审无遗留：七项任务均通过独立双项评审；最终三项发现已修复并复审通过。范围限制见验收记录；本计划临时目录已安全清理，正式 Tests/BuildRoot 保留，不 push/merge。
