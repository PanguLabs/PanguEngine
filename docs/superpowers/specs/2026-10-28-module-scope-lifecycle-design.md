# STATIC Module Scope 装配与生命周期

> 状态：实施设计，C++ API/ABI 仍为 Draft。用户已要求继续此前提出的 STATIC 闭环：实际工厂、多 Scope 独立装配、激活/回滚、Busy 排空和逆序清理。下述具体存储与签名为本轮实现选择，不新增架构冻结。
> 基线：`c518990`；分支 `feature/module-scope-lifecycle`；不自动提交/推送。

## 目标与依据

依据 `wiki/core/module-api.md` 第 5—8 节、`wiki/core/service.md` 与 Memory Ownership Contract，将当前声明元数据推进到真正拥有实例的 STATIC 运行时。核心验收是：Module 只注册一次；Context A 已运行时，Context B 的装配失败只清理 B；A 可继续访问，随后能装配 C。激活失败后保留原始 Error，后台工作未完成或合法借用未释放时报告 Busy，不提前析构。

本轮不实现 EngineHost/Game/Editor、外部候选 Loader、SHARED/OS 卸载、Service Contract Provider 选择、业务多 Provider Registry、TaskScheduler 或 GPU 退休。Scope 资源日志可以承载这些能力的生命周期适配，但不把 Service 变为统一虚基类，不将 Concrete 访问强制放入动态 Registry。

## 实施路径与取舍

采用现有 `PanguModule` 内的 Scope 资源日志与强类型访问令牌：工厂和配置由宿主注册域持有，资源由具体 Scope 的记录持有。相较一次构建完整 Service Registry/ModuleManager，该路径先实现已批准的生命周期闭环，保留后续业务接口空间；相较继续扩充 PoC，所有能力进入正式 Runtime/Tests。公共 Service API 不因内部资源适配而冻结。

`RegistrationScope` 保留现有名称和元数据查询 API，本轮明确作为 Host 的**声明注册域**；运行时 Host/Context/World Owner 是新建的 Scope 记录，不能将两者混用。多个 Host 共享声明域的既有行为保留；新增声明、工厂和撤销记录使用内部 Host Domain 区分归属。

## 1. 身份、Scope 与所有权

- `pg::service::Scope` 表示 Host/Context/World 种类。每个 Host 显式拥有一个 Host Scope；Context 必须绑定该 Host，World 必须绑定具体 Context。不扫描或隐式创建所有 Context/World。
- `ScopeHandle` 包含宿主 Domain、槽位及 generation；全零无效，拒绝跨 Host、过期、错误父级与 generation 溢出。回收槽位时递增 generation，旧句柄不能访问新 Scope。Scope Owner 记录及实例容器由协调器跟踪，未来 EngineContext/WorldContext 可拥有相应 Owner 句柄。
- 创建 Host 时分配进程内唯一 Domain，仅用于临时实例句柄和撤销账本；不得将其作为 ModuleId/ServiceId。Domain 由有界原子序号分配，溢出拒绝，不回绕。
- 描述快照仍由 Host 拥有；工厂及捕获配置由注册域中的稳定堆节点拥有，资源清理结束后才撤销工厂。注册域必须比借用它的 Host 活得更久。
- 协调器使用稳定的私有状态存储，移动 Host 后已有 Scope 和访问令牌仍有效。管理操作由调用方串行化；同 Host 回调重入管理操作在副作用前返回 Busy。跨 Host 的注册域重入继续服从原注册事务守卫。

## 2. 可重复工厂与资源适配

新增 `RegistrationContext::DeclareFactory(id, kind, factory, dependencies, name)`，`factory` 为拥有型、仅移动的 `ScopeFactory`，接收可重复调用的 `pg::Status(ScopeBuildContext&) noexcept` callable。当前 Android/Linux libc++ 语法预检没有 `std::move_only_function`，因此所有平台统一采用内部类型擦除的 callable 适配器，不使用平台分叉或可复制 `std::function` 降级。空工厂、非法 Scope/依赖/声明身份、重复 ID 及预算溢出属于粘性注册失败。现有 `Declare` 继续保存纯元数据，不强制无 Service 的 Module 创建实例。

工厂在 Module 注册时仅保存，具体 Scope 装配时才调用；同一个工厂须可重复调用，为每个 Scope 构造独立状态。使用稳定节点防止后续注册导致 vector 扩容后捕获状态地址失效。

`ScopeBuildContext` 是一次工厂调用期间的借用对象，提供目标 Scope、Owner Module 及经过校验的显式依赖。工厂通过 `Own<T>(std::unique_ptr<T, Deleter>, ScopeHooks<T>)` 将**一个尚未启动**的对象转交具体 Scope，取得 `ScopeAccess<T>`。一个工厂恰好 Own 一次；零次/多次、空对象及非法 Hook 组合失败。Ctor/工厂不能在资源纳入日志之前启动工作。

`ScopeHooks<T>` 是可选的 `Start(T&) -> Status`、`RequestStop(T&) -> void`、`PollStopped(T&) -> Status` 适配，不要求 T 继承基类或自己实现这些方法。Stop/Poll 必须成对出现，均 noexcept。无异步工作的普通对象可不提供 Hook。框架先记录对象/依赖/销毁者，再调用 Start；Start 部分执行后失败同样经过 Stop/Poll 后销毁。对象与匹配的 Deleter 保存在原创建方模板适配中，不能由其他分配域擅自 free。

内部异构资源采用私有类型擦除；公开业务类型仍是 T。`ScopeBuildContext::Dependency<T>(DeclarationId)` 仅用于冷路径装配和注入，拒绝未声明依赖或类型不符。原始依赖借用受资源图保护，至少活到依赖方完成 Destroy。它不构成全局 ServiceLocator，也不作为业务代码访问 Concrete 的强制入口。

工厂可把强类型 `ScopeAccess<T>` 注入调用方的具体 Owner/配置槽位。该令牌不是按字符串/RTTI 找任意对象的 Registry；装配成功前和 Quiescing 后均不能取得新业务借用。

## 3. 访问、借用与发布

- `ScopeAccess<T>` 只拥有稳定的访问控制块，**不拥有 T，也不阻止资源清理**；控制块可以比实例活得更久。`TryBorrow()` 返回仅移动的 `ScopeBorrow<T>`，成功时才能访问 T；销毁/显式 Release 后解除外部借用。
- 资源、Scope 和 Module 各有访问闸门。借用获取和闸门关闭通过原子状态明确线性化；已接纳借用保护 T，关闭后新借用失败。控制块是明确需要的共享所有权，实例本身仍仅由 Scope 拥有。
- Module 初次 Activate 涉及的资源先保持业务不可见；全部目标 Scope 成功后打开该 Module 的业务闸门。新增 Scope 先关闭自己的 Scope 闸门，全部装配成功后一次打开；已有 Scope/Module 不受该局部事务影响。
- 管理操作/查询须外部串行化；已导出的 `ScopeAccess::TryBorrow` 和 Borrow Release 支持并发关闭，不通过管理容器进行跨线程查找。未知工厂/生命周期回调不在内部互斥锁中运行。
- T 的并发访问仍由其业务线程契约负责；生命周期 Borrow 不是对象数据锁。

## 4. 依赖与清理顺序

每个工厂显式声明 `ScopeDependency{DeclarationId, Scope kind}`。只允许当前 Scope 或所属祖先；禁止长寿命 Owner 依赖短寿命 Owner、跨 Context/Host 选择及未绑定/未 Ready 的父级资源。元数据、依赖数组由宿主复制。

装配前解析具体 Provider、检查重复/缺失依赖、类型访问权限以及资源 DAG。Module 依赖与工厂依赖分别维护；本切片采用显式的保守合并门槛：将本次激活候选及其依赖闭包的跨 Module 工厂依赖投影为 Module 清理约束，与既有绑定 Module 边共同检查无环；同 Module 内三种 Scope 的资源声明环在 Activate 前另行拒绝。新 Scope 只使用已 Active 模块的实际绑定，未激活的无关声明环不能阻断原有 Scope 装配。该保守门槛是本轮实现选择，后续更精细的生命周期阶段分析可放宽，但不能允许关闭顺序矛盾。

依赖先构造，逆序清理；同 Scope 的 Module 依赖也参与顺序。祖先资源由子级的内部依赖保护保持存活。开始 Scope 关闭后先处理子 Scope；仍有子级记录时父级不能完成关闭。

Quiescing 关闭业务闸门，不代表对象已停止。按依赖逆序逐个请求停止、轮询完成、检查该资源的外部借用并销毁；被依赖资源保持依赖方清理所需的可用性。不能以整个 DAG 的内部 Borrow 全部归零作为开始 Destroy 的前提。每个 Stop 请求与 Destroy 至多执行一次；Poll 可重复，Busy 或 Error 均保留实例/依赖，不强制析构。

## 5. Module 与 Scope 操作

当前 `StaticModuleHost` 作为有限的 STATIC 协调器扩展，不宣称完整 ModuleManager。

| 操作 | 行为 |
| --- | --- |
| `CreateScope(kind, parent, report) -> Result<ScopeHandle>` | 明确创建 Owner，给当前 Active Module 按依赖顺序装配对应工厂。成功交付 Ready Scope；失败不交付有效句柄，局部清理由 Host 跟踪；不能影响旧 Scope |
| `Activate(module, report) -> Status` | 前置 Registered，已绑定依赖 Active；给所有已明确创建的 Ready Scope 装配本 Module 工厂。全成功后 Active；失败关闭该 Module 入口、保留原 Error 并进入 CleanupPending，清理本次相关资源 |
| `BeginCloseScope(scope, report) -> Status` | 关闭该 Scope/子 Scope 的新增业务与装配，安排逆序清理 |
| `TryFinishCloseScope(scope, report) -> Status` | 非阻塞推进；成功使句柄失效，Busy/Error 保留记录。根 Scope 只能随 Host Shutdown 关闭 |
| `BeginUnload(module, report) -> Status` | 前置无仍生效的依赖方，关闭 Module 业务/装配入口，安排其所有 Scope 资源清理；已开始清理时幂等 |
| `TryFinishUnload(module, report) -> Status` | 推进清理，完成后撤销声明/工厂并 Unloaded；仅逻辑 STATIC 退出，没有 OS 卸载 |
| `Load(module, report) -> Status` | 固定 Catalog 中 Unloaded 项重新进入 Loaded；新控制块/代际，不复活旧访问令牌。之后仍需 Register/Activate |
| `PollCleanup(report) -> Status` | 推进未交付句柄的失败 Scope 及待清理记录，正常运行/关闭均可调用 |
| `BeginShutdown` / `TryFinishShutdown` | 关闭新管理入口，先完成所有 Scope，再按 Module 依赖逆序退出；未完成返回 Busy/Error，调用方须保持 Host/注册域存活 |

Module 状态为 Loaded / Registered / Active / CleanupPending / Unloaded。注册回调失败也进入 CleanupPending，报告进度同样明确该责任，不能直接重试 Register；原 `kRegistrationFailed` 仅保留为 CleanupPending 的源码兼容别名。静态 Catalog、版本和 Optional 选择在该 Host 内保持固定；不新增候选发现或动态重绑定。

扩展的 `ModuleOperationReport` 为定长值快照，包含涉及的 Module/Scope/声明身份、操作进度、原始失败和当前清理错误。相同 Error 的前置拒绝和执行失败仍可区分，嵌套回调不能污染外层最终报告。`OriginalError`/`PrimaryError` 保留本次注册/激活/装配失败，`CleanupError` 仅表示实际回滚/清理产生的 Busy 或其他错误，同步清理完成时为空。Shutdown 在开始时锁存尚未完成的失败并跨多次 Poll 保存，即使原失败 Scope 被回收；此前已完成的历史卸载错误不自动导入新 Shutdown。

析构仅能释放已安全完成的状态；Host 析构可尝试一次同步 Shutdown，未完成时采用始终生效的终止路径，不能悄悄销毁仍被任务/借用访问的对象。正常调用必须显式推进 Shutdown 并保持协调器存活。该契约需子进程回归，不能只依赖 Debug Assert。

## 6. 边界、预算与验收

- 沿用已有 256 Module、每个 64 个依赖、256 字节文本、每次 256 声明、注册域 4096 声明上限。每 Host 最多 256 个同时在用 Scope，含 Root；每工厂最多 64 条 Scope 依赖。槽位可回收，generation 耗尽后不再复用。预算失败先拒绝，不半发布。
- 冷路径使用拥有型标准容器和仅移动工厂；OOM 沿用禁用异常下的终止政策，不承诺可恢复 OOM。实例访问不承诺无原子/零分配，必要控制块分配在装配冷路径完成。
- 仅同次构建 STATIC；代码在最终承载二进制驻留。内部生命周期保护不冒充真实 SHARED CodePin、可信预检或最终 EXE ABI 证明。
- 正式测试包括真实非平凡对象、仅移动配置工厂、多 Scope 数据独立、B 失败/A 保持/C 后续成功、初次激活跨 Scope 回滚、Start 部分失败且 Poll Busy、外部 Borrow 阻止 Destroy、内部依赖不自锁、合并约束环、跨 Owner/过期句柄拒绝、并发借用/关闭、未交付句柄清理、错误/报告生命周期及析构误用子进程。
- Windows 完整 CTest；Linux/macOS 正式 Preset 定向生命周期回归；Android/iOS 正式交叉编译及宿主采集。标准库预检已确认 Windows 支持、Android/Linux libc++ 缺少 `std::move_only_function`，工厂使用统一的内部拥有型仅移动适配；Apple 最终以正式 Preset 的 SDK/头文件参数验证。
- 所有源码、测试及文档留在同一共享工作区，BuildRoot 继续按宿主隔离；临时记录存 `.superpowers/sdd/2026-10-28-module-scope-lifecycle/`；本轮不自动提交/推送。

## 留给实现者的选择

按已列出的所有权和原子线性化要求决定私有容器/闸门位编码、模板资源适配布局、非持久 C++ 类型核验方式与错误代码。不得把这些选择提升为已冻结的 Plugin ABI，也不得在资源管理适配中引入统一业务 Service 基类。
