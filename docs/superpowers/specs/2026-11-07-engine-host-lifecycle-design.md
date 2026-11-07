# EngineHost 最小启动与关闭闭环

> 状态：Accepted；用户已批准按 SDD 实施，最小实现已交付，三桌面双模式与两移动最终链接已验收，等待独立及最终分支 Review；Public C++ API/ABI 未冻结。
> 日期：2026-11-07
> 起点：`ca0f8f9`；设计分支：`docs/engine-host-lifecycle`。
> 用户已确认：先设计 EngineHost 最小闭环，再制定实施计划；窗口、渲染和主循环后续推进。
> 本轮 grilling 已确认范围，后续用户已选择 SDD 执行。以下现状与起点为设计时记录；当前实现与验收以 EngineHost wiki 及正式验收记录为准。

## 1. 目的与现状

ModuleManager 已能接纳常驻定义、加载显式业务候选、注册和激活 Module、装配 Host / Context / World 资源，并可重试安全关闭。当前 Game / Editor Project fixture 在各自入口通过 `HostCheck.h` 手动完成这一流程，尚无正式 EngineHost。

本次补上 Application 与 ModuleManager 之间的宿主：拥有声明域和管理器，规定启动失败之后谁继续清理，让 Game、Editor、Tool 复用同一启动与关闭协议。成功路径应能演示：启动宿主并接纳业务 Module → 创建 Context / World → 通过服务所属 Module 的具名强类型入口取得对应实例的访问令牌 → 实际借用服务 → 关闭整个宿主。服务必须由业务 Module 提供，不能只激活业务库却仅访问应用自己创建的对象。

权威前提为 [EngineHost](../../../wiki/core/engine-host.md)、[Service](../../../wiki/core/service.md)、[Module API](../../../wiki/core/module-api.md)、[内存所有权](../../../wiki/core/memory-management.md)、[错误与诊断](../../../wiki/foundation/error-diagnostics.md)、[ADR-0015](../../../wiki/decisions/0015-unified-link-modes-module-manager.md)。本草案不替换其中已冻结的 Module / Service 语义。

## 2. 范围与成功判据

本轮设计并计划实现：

- Application 显式持有 EngineHost；宿主拥有一个独立 RegistrationScope 与至多一个 ModuleManager。
- 一次同步启动：接纳常驻根 → 注册并激活常驻根 → 接纳一个业务批次 → 注册并激活业务 Module。
- 显式创建、关闭 Context / World Scope；支持多个 Context 和一个 Context 下多个 World。
- 贯通 Concrete Service：Module 提供具名强类型访问入口，Application 显式指定 EngineHost 和 ScopeHandle，取得 ScopeAccess 并通过 ScopeBorrow 使用该实例。
- 启动失败进入整体关闭；Borrow、CodePin、Stop/Poll 错误或原生释放失败时保留必要状态，供调用方继续推进。
- Monolithic / Modular 使用同一宿主协议；Game / Editor 使用相同宿主实现并选择各自业务集合。
- 正式测试和可运行的最小示例，分别证明状态、错误和真实对象/代码寿命。

本轮不接入窗口、主循环、Tick、flecs、The Forge、TaskScheduler、GPU 排空、完整 Contract Provider 选择、业务 Registry、Plugin 运行时扫描、Hot Reload、安装发布、移动打包或签名。Host 只协调现有工厂资源的 Stop/Poll/Destroy；不能据此宣称上述系统已获得安全退出能力。

反目标：进程级单例、万能 ServiceManager、通用应用基类、隐式目录扫描、后台清理线程、固定次数轮询当作安全条件、超时强毁、强制卸载、通用构造参数注入，以及复制 ModuleManager 内部依赖图或清理器。首版 Ready 后不新增、替换或单独卸载业务 Module；底层 Manager 的增量能力不直接透传成未经定义的宿主行为。

### 2.1 沿用三种 Service 模式

三种模式及其生命周期语义沿用 Service 规范，不重新分类：

| 模式 | 既定语义 | 本轮范围 |
| --- | --- | --- |
| Concrete Service | Scope 持有已确定的具体实现，强类型直接访问或注入 | 实现业务 Module → Scope 实例 → Application 访问 → 安全关闭的完整路径 |
| Contract Service | 单 Provider 契约；可静态绑定，动态绑定由 Scoped Registry 与稳定 ServiceId 协调 | 保持设计边界，完整 Provider 选择与动态获取后续实现 |
| Registry Service | 业务专用多贡献者集合，按 Key 注册、查询、枚举和撤销 Factory / Callback | 保持设计边界，不在本轮引入业务贡献系统 |

这些是 Module 提供能力的方式，不是三个 Module 类别；一个 Module 可以同时使用多种模式，也可以完全不提供 Service。没有 Service 的合法 Module 仍正常接纳、注册、激活和关闭，不要求它声明空壳服务或创建实例。

Service 可以是满足具体业务契约的任意类型，不限定 `XXXManager` 命名、共同基类或虚方法。Manager 可以作为某个 Service 的具体实现，也可以只作为 Module 内部实现；EngineHost 不据名称推断职责或生命周期。

## 3. 方案比较与推荐

| 方案 | 收益 | 代价 / 判断 |
| --- | --- | --- |
| Application 拥有 EngineHost，显式 Start 与 Shutdown | 适配现有 main；Game / Editor / Tool 共用；失败后宿主仍可承载清理 | 调用方需要推进关闭；用户已确认，符合本轮不引入主循环的范围 |
| EngineHost 拥有 Application，并提供 Run / 回调式主循环 | 一个入口完成启动和运行 | 必须提前规定执行循环、应用接口和取消协议；与本轮范围不符 |
| 只提供启动辅助函数，调用方继续拥有声明域与 Manager | 新增代码最少 | 声明域寿命与失败清理责任仍散落在每个入口，不能完成本轮目标 |

用户在 grilling 中确认第一项，同时确认同进程多个独立宿主作为正式验收能力；具体 C++ API 仍随本修订稿审阅。

## 4. 所有权与代码组织

Application 的 main / 入口对象拥有 EngineHost，EngineHost 不拥有 Application，不调用应用虚函数，不产生进程级全局宿主。独立宿主拥有独立的声明域、Manager Domain 与句柄；允许同一进程串行管理多个宿主，不承诺进程全局状态由第三方库隔离。

EngineHost 不可复制、不可移动。声明域在 Manager 创建前存在，且在 Manager 完成关闭并销毁后才销毁；这样 Manager 对声明域的借用始终稳定。不以可移动 RegistrationScope 或复制声明元数据绕过现有契约。调用方可以在栈上持有宿主，也可以以拥有指针持有。

构造只建立 Cold 状态，不执行 Query、原生加载、注册或工厂调用。启动入口采用单独的 Start，而不采用可能在失败返回时销毁半启动 Manager 的 `Result<EngineHost>` 一步工厂。标准容器 / 所有权存储的 OOM 沿用项目无异常终止政策，不声称可恢复。

新增 `Source/Runtime/EngineHost/`，对应一个真实 `PanguEngineHost` Module Target、`Pangu::EngineHost` 别名及独立导出宏，公开命名空间建议 `pg::engine`。底层依赖方向为 EngineHost → Module → Foundation；Application 消费 EngineHost 和实际业务 Module。业务 Module 的访问入口可以消费 EngineHost 的作用域访问桥接，但 EngineHost 不为了访问任意业务服务反向依赖该业务 Module；具体 Target 与运行时依赖必须保持无环。不让底层 Module Runtime 反向依赖 EngineHost。

EngineHost Module 在宿主启动时作为第三个必需常驻定义，与 Foundation / Module 一并接纳；自身 Query / Register 只提供身份与依赖，不隐式启动应用，也不在 Register 内构造 EngineHost。其稳定 ModuleId 由实施计划分配并记录，版本初始为 1.0.0，Required 依赖 Module Runtime 1.0.0；Foundation 由传递依赖保证。保持 `BuiltinResidentQueries()` 原有两项契约，EngineHost 在自身启动实现组合三项常驻定义，不改变既有 Module 测试的两根前提。

新增 Module 跟随全图链接模式产生唯一 STATIC / SHARED 目标，Modular 提供原生固定 Query 包装与必要公共导出。宿主原生链接 EngineHost / Module / Foundation；常驻接纳不重复 OS Load，整体关闭不释放这些宿主链接持有。

## 5. 输入与启动顺序

`HostStartupConfig` 是一次 Start 的显式输入，包含以下四类字段，不引入 Game / Editor 枚举：

| 输入 | 契约 |
| --- | --- |
| AdditionalResidentQueries | 可选的额外常驻 Query；其代码必须由宿主外部持有至整体关闭及宿主析构完成，依赖必须能在本常驻集合内解析 |
| ResidentBindings | 本次常驻集合的可选依赖选择，传给 Manager Create |
| Modules | 一个显式业务候选批次；Mono 为 StaticModuleCandidate，Modular 为 SharedModuleCandidate |
| ModuleBindings | 业务批次对既有常驻项 / 本批项的可选依赖选择，传给 Manager Load |

输入及其中的容器只在同步 Start 调用期间借用，宿主不保存配置对象、span、路径引用或回调上下文地址。Manager 按现有契约复制拥有型描述并持有必要 Query / Factory。静态或常驻 Query 指向的代码必须满足各自驻留约束；复制函数指针不等于持有代码。

业务批次允许为空：只启动常驻根，跳过不接受空批次的 Manager Load。模式错配、无效路径、重复身份、依赖和预算校验复用 Manager，不悄悄改写候选类型，不把 OS 隐式加载当作已接纳。不扫描 Plugin，不推导未知动态依赖，不自动生成或搜索路径。

Start 的确定顺序：

1. 检查操作重入与宿主状态：操作内调用返回 Host Busy；其余非 Cold 调用返回 Host InvalidState，不执行 Query / Factory。除此以外不设置另一套 Host 配置预检。
2. 进入 Starting；组合三项基础常驻定义和额外常驻项，创建 Manager。全部接纳/依赖校验仍由 Manager 决定。
3. 按 Manager `RegistrationOrder()` 依赖优先注册全部常驻项，再按同序激活全部常驻项。
4. 业务候选非空时调用一次 Load，按其返回的依赖优先句柄顺序注册全部业务项，再按同序激活全部业务项。
5. 所有启动阶段成功后进入 Ready；此时本次常驻 / 业务 Module 均已 Active，Host Scope 已准备好，不自动创建 Context / World。无 Service 的 Module 不因此失败，也不影响 Ready 判定。

Start 是同步完成协议，不接受“Busy 表示稍后继续启动”的隐式状态。任一服务的同步启动返回 Busy 也按本次启动失败处理，进入整体关闭；后续只推进清理，不能继续未完成启动。异步初始化、取消和启动恢复需以后单独设计。

进入 Starting 后任一步失败都中止后续启动，并由宿主接管整体关闭，即使失败只是本次 Manager 操作的前置拒绝。候选类型、路径、身份、依赖或预算无效也消费这一次启动机会，不允许调用方猜测某项校验是否已经执行 Query。组合常驻集合前按 `kMaxCatalogModules` 限定额外项数量并使用无溢出的数量判断，超出预算按原有 Module LimitExceeded 返回；不先复制无限额外项再交给 Manager 拒绝。不得用“该业务 Module 可单独卸载”为理由留下部分启动的 Ready 宿主。

Manager Create 失败且没有交付 Manager 时，遵守其现有常驻接纳回滚保证，不凭空构造可继续操作的 Manager；宿主进入 Closed。若为该保证发现缺口，实施须先补齐相应生产行为及正式回归，而不能在本层丢弃未完成原生持有。

## 6. 状态与入口行为

状态名建议为 Cold、Starting、Ready、Closing、Closed。Starting 只在一次同步 Start 内可见，不提供异步 ResumeStart。所有管理操作和查询由调用方串行协调；资源 Borrow / Pin 跨线程 Release 继续遵循现有 Module 契约。

| 入口 / 条件 | 后置状态与结果 |
| --- | --- |
| Start / Cold，成功 | Ready，成功 |
| Start / Starting 后失败，清理已完成 | Closed，返回原始启动错误 |
| Start / Starting 后失败，清理仍待完成 | Closing，返回原始启动错误；继续持有 Manager / 声明域 |
| Start / Ready、Closing 或 Closed | 原状态，Host InvalidState，不重新执行 Query / Factory |
| BeginShutdown / Cold | Closed，成功，无 Manager 操作 |
| BeginShutdown / Ready | 进入 Closing，调用 Manager BeginShutdown，拒绝新的宿主业务接纳；成功仅表示关闭已请求 |
| BeginShutdown / Closing | 保持 Closing，幂等成功，不重启关闭事务 |
| TryFinishShutdown / Closing | 调用一次 Manager TryFinishShutdown；待完成返回当前失败 / Busy 并保持 Closing，成功后 Closed |
| BeginShutdown 或 TryFinishShutdown / Closed | Closed，幂等成功；不复活句柄，不重复 Stop / Destroy |
| TryFinishShutdown / Cold 或 Ready | 原状态，Host InvalidState；不隐式发起关闭 |
| 管理操作重入 / Starting 或其它操作内 | Host Busy，不进入第二个 Manager 操作，不改变外层状态 |

启动失败时记录第一个失败现场，发起整体关闭并至多主动尝试一次 TryFinishShutdown，不阻塞等待、不自行循环。不论本次同步清理结果如何，Start 返回原始启动错误；报告明确 Closed 或 Closing，不能把“资源已回收”改写为启动成功。

Closed 是该 EngineHost 对象的终态。需要重新启动时构造新宿主，产生新 Manager Domain；不增加 Reset / Restart 来复用旧句柄。宿主保留最后拥有型诊断，不能保留卸载库中的视图或对象。

析构是有限兜底：Cold / Closed 无业务清理；Ready 请求关闭并至多尝试一次完成；Closing 至多尝试一次完成。未完成时进入所有构建配置有效的终止路径，与现有 Manager 契约一致。正常调用必须显式完成关闭；析构不忙等、不记录“成功退出”后遗弃所有者，也不产生第二轮隐藏重试。

## 7. Context / World 与能力边界

宿主在 Ready 提供 CreateContext 和 CreateWorld 两个强类型用途入口，返回现有 ScopeHandle：Context 的父级固定为自身 HostScope，World 必须显式给出所属 Context。Parent 属于同一宿主且层级有效的检查复用 Manager。首轮不新增只有句柄转发作用的 EngineContext / WorldContext C++ 类，也不把这些 ScopeHandle 宣称为已实现 flecs World。

BeginCloseScope / TryFinishCloseScope 继续保留独立关闭的子先父规则；根 Scope 只随整体关闭。Ready 下局部装配失败不必关闭整个 Host：保留现有局部回滚与未交付失败 Scope 的清理语义，提供 PollCleanup 以推进。未交付的失败 Scope 不泄露可操作关闭句柄。若底层报告已经由 ManagerShutdown 接管，则宿主进入 Closing，后续用整体完成入口。

Closing 拒绝创建 Context / World 和新的宿主业务接纳；只保留诊断与关闭推进。已经关闭的 Scope、旧 ModuleHandle 和旧 ScopeAccess 遵循现有 generation / gate 契约，不能因新宿主或新 Scope 复用槽位而访问新实例。

EngineHost 不公开可变 ModuleManager / RegistrationScope 引用，不允许调用方绕过宿主状态机直接重新 Load、Register、Activate 或 Shutdown。允许提供按值的 State / ModuleSnapshot / 句柄查询；精确查询命名由实施计划确定。首轮不新增业务运行中的动态增量装载门面，已有 ModuleManager 可继续独立用于底层消费者。

### 7.1 Module 自有的 Concrete Service 访问入口

服务所属 Module 公开具名、强类型的访问入口；调用方显式提供 EngineHost 和 ScopeHandle，成功取得对应具体类型的 ScopeAccess。入口固定本服务预期的 Module Owner、DeclarationId、目标 Scope Kind 与 C++ 类型，Application 无需手工拼接身份或回调输出对象。示例使用普通服务类型，验证访问协议不依赖 Manager 命名或继承关系。

现有公开 API 只有工厂内 `ScopeBuildContext::Own()` 可以生成有效 ScopeAccess；Manager 尚无按句柄获取已构造资源令牌的入口。本轮必须补齐实际资源访问能力，不能只为示例建立全局 Map / TLS 或让业务库缓存各 Host 的实例地址。

底层增加只读的已声明资源访问桥接：以明确 Manager Domain / ScopeHandle、预期 Module Owner、DeclarationId、Scope Kind 和强类型校验定位已有资源，取得由常驻 Runtime 提供的控制块。EngineHost 只暴露受限查询所需的桥接，不交付可变 Manager / RegistrationScope，不回调业务代码。具体导出位置、模板与非模板分工由实施计划确定，不能靠每个业务类型成为 EngineHost 的新成员或新增反向链接实现。

桥接及 Module 入口遵守以下规则：

- 宿主只在 Ready 且无管理操作进行时接纳新的服务访问查询；其他状态按宿主状态 / 重入规则返回失败。查询失败不改变宿主状态，也不覆盖已保存的 StartupFailure。
- 精确核对 ScopeHandle 的 Domain、槽位和 generation；只查指定 Scope，不自动转向父 Scope、其他 Context 或另一个宿主。错误归属、错误层级和过期句柄均失败。
- 按明确 DeclarationId 查资源，并验证其预期 Owner 与 Kind。当前声明域内 DeclarationId 唯一，每次合法工厂装配恰好 Own 一个对象；同一声明可在不同运行时 Scope 产生不同实例，同类型的不同声明可以共存。不得按类型挑第一个对象或由访问行为创建实例。
- 仅对 Module Active、Scope 已成功交付且 Ready、对象 Ready 且未进入关闭的资源交付令牌；没有 Service 的 Module、纯元数据声明、尚未创建的 Scope 或失败装配都不能伪造令牌。资源不存在与资源未 Ready 必须明确失败；精确错误枚举由实施计划落实。
- 内部擦除按现有 RTTI 值比较验证具体类型，不用 RTTI 地址、仅名称/hash 或人工类型身份替代。查询期间受调用方串行管理契约保护，完成对 DLL 类型信息的检查后只交付常驻控制块，不能把 DLL 的 RTTI 指针保存进残留令牌。
- 新入口不是 Contract Provider 选择或全局 ServiceLocator：不按类型枚举，不扫描 C++ 对象，不选替代 Provider，不隐式跨 Scope 查找，也不接受任意创建请求。

服务类型和具名入口随所属 Module 的公共接口提供，符合受控 C++ ABI。某个访问入口或服务方法若造成宿主直接原生导入业务库，会保留额外 OS 持有；关闭成功只表示 Manager 自身释放完成，不冒充库必然物理移除。需要正式证明物理卸载的夹具须使用不会隐式保持该库加载的访问路径，并实际观测卸载。

### 7.2 访问与关闭的寿命保证

ScopeAccess 可以保存，但只保护访问控制块；Application 实际使用时调用 TryBorrow，成功所得 ScopeBorrow 才保护实例和所需代码。开始关闭后拒绝新的 Borrow，已合法取得的 Borrow 可以完成其业务；退出前释放该 Borrow，再继续推进关闭。不强制所有内部 Concrete 依赖都经过外部访问入口，既有静态所有权和资源 DAG 的保护方式保持有效。

服务获取、Scope 创建和关闭推进由调用方串行协调；已导出令牌的并发 TryBorrow / Release 继续按三道 Gate 契约处理。查询成功与紧随其后的借用是两件事：若中间开始关闭，TryBorrow 可以失败，Application 必须检查。旧 ScopeAccess 在 Scope/Module 关闭、销毁或句柄槽位复用后不能访问新实例。

新 Scope 必须完成本次全部资源的创建和 Start 后才交付；其候选对象不能因为某个单对象 Start 成功就被外部提前取得或借用。装配期已声明依赖继续通过现有 Dependency 解析，不能用新查询绕过声明、跨 Scope 方向或 Ready 门槛。

### 7.3 首版配置边界

本轮服务实例由 Module 自身的可重复工厂创建，工厂可以使用既有 ScopeBuildContext 中的 Scope 身份和 Module 自己定义的初始化逻辑。多个宿主 / Context / World 必须产生独立实例，但首版不承诺由 Application 为每个服务传入任意构造 / Start 参数。

不引入 ConfigureInstance，不增加附加注册动作或 Manager Register 回调重载，也不要求 Application 准备输出容器接收工厂令牌。将来具体服务若需要每实例初始化参数，应先设计其明确契约和激活前的交付路径，不能把创建后设置参数当作已经完成了所需的启动配置注入。

## 8. 整体清理、报告与错误

整体关闭直接交给 Manager 的实际有效资源 DAG / 同 Module 跨 Scope 后代顺序；不得在宿主先逐 Scope 全量销毁，再卸载 Module。等待期间保留 Provider、Factory、Deleter、代码 Pin、原生持有和声明域；额外 OS 引用不消失不等于 Manager 关闭失败。最终成功必须经过 Manager TryFinishShutdown，不能把 PollCleanup 的成功当成常驻退出已确认。

Application 负责让外部 Borrow / Pin 与业务活动结束，再显式重试。宿主不在管理锁或串行操作内等待一个需要同一调用线程释放的 Borrow。关闭长期未完成时调用方可以报告和等待外部条件，不能超时释放仍使用中的资源。

建议 `HostOperationReport` 为固定容量的拥有型值，至少包含：

- 调用结束的 HostState、当前阶段及当前 PrimaryError。
- 当前底层操作的可选完整 ModuleOperationReport，保留 Progress、CleanupOwner、身份和各项错误，不把它重新解释为 Host State。
- 可选 StartupFailure：第一次启动失败的阶段、原始 Error、当时的可选 ModuleOperationReport。

阶段至少区分常驻接纳、常驻注册、常驻激活、业务接纳、业务注册、业务激活、Scope 操作、整体关闭；查询 / 宿主前置拒绝具有明确无底层报告状态。一个当前报告加一个固定启动失败快照，不累积无限日志，不在错误收集时扩容。

Start 失败返回值与 PrimaryError 为原始启动错误，随后的同步清理结果放入当前底层报告；原始启动现场保存在 StartupFailure。后续 TryFinishShutdown 返回的是本轮清理错误或成功，PrimaryError 与该返回值一致，StartupFailure 始终保留，即使底层完成报告不再携带原始业务错误。关闭成功后报告仍说明此前启动失败，不能把旧失败作为新清理操作的返回错误。

报告可在调用后独立持有，销毁旧报告不影响宿主继续清理；不保存调用方报告地址。实现采用局部构造并在外部 Query / Factory / 原生释放回调返回后发布最终报告，覆盖嵌套调用曾改写同一报告的情况。没有底层调用时不复制上次底层报告冒充本次现场。

Host 自身前置拒绝使用独立 Host 错误域，首轮只需 InvalidState、Busy；底层失败原样返回其 Error。具体错误域值 / 枚举值在实施计划分配。日志不承担错误或清理责任；本轮不引入通用 ErrorReport / Logging / Fatal 子系统。

## 9. 验收设计

新增测试纳入正式 `Tests/` 和 CMake / CTest；遵守 [Tests 维护规范](../../../Tests/README.md) 与[分类契约](../../../wiki/foundation/testing.md)。下表是待实现覆盖，当前没有任何一项因此被视为已通过。

| 场景 | 必须观察到的行为 |
| --- | --- |
| 无业务宿主 | 不调用空 Load；三基础常驻均 Active，关闭最终确认成功 |
| 业务启动 | 输入可乱序，注册 / 激活遵循真实依赖；Ready 前不交付成功 |
| Module 无 Service | 合法 Module 可以没有工厂或实例，仍正常依赖、接纳、注册、激活和关闭；无服务查询明确失败 |
| 具名服务访问 | 经业务 Module 公开入口、明确 Host/Scope 与声明取得 ScopeAccess，实际 TryBorrow 并调用普通服务类型；不依赖回调输出或 Manager 命名 |
| 精确实例与类型 | 多宿主、多个 Context/World、同类型不同声明互不串用；错误 Owner / Kind / 类型 / 句柄拒绝，不隐式向父 Scope 搜索 |
| 发布与关闭门槛 | Starting、失败装配、尚未交付、Closing 和 Closed 不交付新访问；关闭开始后已有令牌也不能新 Borrow，旧 Borrow 阻止提前销毁；旧令牌不指向复用实例 |
| 新入口令牌的卸载后寿命 | 经业务 Module 具名入口与新查询桥接取得 ScopeAccess；释放活动 Borrow 并完成 Host 关闭，实际观测业务库物理卸载后，再复制令牌、验证 TryBorrow 失败并最终释放原令牌及副本 |
| 三阶段常驻失败 | 额外常驻定义接纳 / Register / Activate 各失败，首错正确，后续业务不执行，整体清理责任明确 |
| 三阶段业务失败 | Load / Register / Activate 各失败；已创建资源、工厂和原生持有均纳入整体关闭 |
| 失败后仍 Busy | 真实 Borrow / Pin 或受控一次原生释放失败；Start 返回原错且 Host 为 Closing；释放 / 恢复后 Finish 到 Closed |
| 非 Busy 停止错误恢复 | 启动先失败 E1，整体关闭时服务 PollStopped 返回非 Busy 错误 E2；Host 保持 Closing，对象及声明域继续存活；StartupFailure 保留 E1，后续 Finish 返回 E2 且当前 PrimaryError 一致；故障恢复后 Finish 成功并进入 Closed |
| 服务同步启动 Busy | Start 按原始 Busy 失败并整体关闭，不发布 Ready、不恢复启动；清理 Busy 与启动首错分别保留 |
| 报告寿命与重入 | 原报告销毁仍能读取启动首错；相同报告被嵌套调用使用时，外层最终结果仍一致 |
| 多 Context / World | 每 Scope 工厂独立创建；显式父归属；跨 Host / 错层级 / 过期句柄拒绝；局部失败不破坏其它 Ready Scope |
| 独立 Scope 关闭 | 父有子记录时不能完成；未交付失败 Scope 只能 Poll / Shutdown；结束后访问令牌拒绝 Borrow |
| 整体关闭顺序 | 真实跨 Module / Scope 依赖先排空 Consumer；Deleter / Factory 早于原生释放；不用全局子先父替代实际 DAG |
| 幂等和单次启动 | 重复 Begin / Finish 不重复 Destroy；Closed 不可 Start；新宿主具有新 Domain |
| 析构兜底 | 可同步关闭的对象正确清理；未完成则子进程确定终止，准备错误不能冒充终止成功 |
| 独立宿主 | 同一进程两个宿主同时存活，串行管理且分别创建 Scope / 访问 / 关闭；关闭其中一个不撤销另一个的声明或有效访问 |
| 最小 Game / Editor | 同一宿主实现、不同业务集合；创建 Context / World 后经业务 Module 的强类型入口实际借用服务并完成业务操作，随后成功关闭 |

可复用既有真实 DLL 与作用域夹具，但 Host 层覆盖必须真正经由 EngineHost，不能只重跑 Manager 直接调用用例。需要一次原生释放故障或获取内部 Pin 时，只能用私有 per-instance 测试访问器接续现有 Manager 接缝；不增加生产公共测试选项。不重复复制全部 Manager 回归矩阵；旧覆盖继续保留。

卸载后令牌用例须在同一条完整路径中覆盖新入口取令牌、关闭、物理卸载观察及卸载后的令牌操作；不能用既有 Own → 测试 Observer 导出的令牌替代新查询结果，也不能仅凭 Manager 原生释放成功判定物理卸载。该物理卸载验收适用于三桌面 Modular，须排除访问入口或服务方法造成的额外原生导入持有；Monolithic 验证逻辑关闭及残留令牌操作，不要求静态归档物理卸载。

非 Busy 停止错误用例须保留故障直到后续 Finish 实际返回 E2，随后显式恢复；Start 的返回值及当次 PrimaryError 为 E1，后续清理调用的 PrimaryError 与本轮结果一致，清理成功后 StartupFailure 仍保留 E1。非 Busy 错误同样不能被当作清理完成或丢弃必要所有者。

三桌面分别验证 Monolithic / Modular 的启动、失败、Scope 与关闭；Modular 使用真正独立业务库，覆盖 RTTI、跨库 Factory / Deleter、原生持有释放。Windows 为日常第一门，Linux / macOS 遵守共享源码与宿主隔离 BuildRoot；ELF Host 若参与公共 RTTI，按现有契约审查 ENABLE_EXPORTS。

Android / iOS 通过正式 preset 构建新增 Module，并分别做两模式最小宿主最终链接和目标产物检查，不发现 / 运行交叉目标 GTest；不声称真机、模拟器、设备卸载或发布通过。所有新增目标继续遵守同图模式，产物名不含平台名。

最小示例建议纳入 `Samples/MinimalHost/` 的正式源码，并在独立 Project Game / Editor fixture 中复用 EngineHost；示例显式展示 Start 的失败清理分支及正常关闭推进，不用 busy-spin 当作实际应用等待策略。示例若只覆盖同步资源，须写明这一边界，异步等待策略留给应用。

构建回归验证父 Project 的目标消费与部署、无变化构建、模式往返和原有测试集合不丢失；新增 CMake 保持 4.0 / 各目标平台 Clang 检查，构建业务逻辑不迁入 CMake。C++ 按仓库 format / tidy 和真实编译数据库验证。验收按提交、宿主、模式与运行 / 编译 / 链接层次分别记录，不沿用上一计划的通过数量。

## 10. 后续实施接缝与待批准选择

批准规格后再编制正式实施计划，预计按以下接缝拆分：EngineHost Module 身份与构建 → 所有权 / 启动 / 报告 → 受限的已声明资源访问与 ScopeAccess 交付 → Scope 与可重试关闭 → 业务 Module 服务示例与独立 Project → 三桌面双模式、移动最终链接及规范同步。此处不是已批准的任务清单或开始实施的授权替代物。

实施时同步 `wiki/core/engine-host.md` 的新契约与准确实现状态、Service 的宿主与访问边界、Module 的已声明资源查询能力、构建目标和 Tests 入口；重大所有权选择如需 ADR，采用仓库 `wiki/decisions/` 规则。不把本 Draft 直接提升为 wiki 的 Frozen 契约，不放宽既有 ModuleManager 的事务 / 依赖 / 安全清理行为来迁就上层。

grilling 已确认的设计决定：

- Q1 / 既有规范澄清：验收必须实际使用业务 Module 提供的服务；沿用三种模式，Service 不限定 Manager，Module 可以完全没有 Service。
- Q2–Q5：Application 拥有 EngineHost 并掌握执行节奏；一次宿主一次完整启动，失败整体关闭、Closed 不重启；多个独立宿主纳入验收；EngineHost 为独立第三常驻 Module。
- Q6–Q9：首版先贯通 Concrete Service，Ready 后业务集合固定；运行中的 Scope 创建失败局部恢复；Start 同步完成，启动 Busy 按失败处理。
- Q10–Q12：Module 提供具名强类型服务入口；显式 EngineHost + ScopeHandle；取得 ScopeAccess 后实际使用必须成功取得 ScopeBorrow。
- Q13–Q14：按明确声明精确访问指定 Scope，不自动搜索 / 选实例；首版不增加通用构造参数注入，移除 ConfigureInstance 及附加 Register 回调方案。

这些逐轮确认不等于具体 C++ API 或整个修订稿已经获批实施。不可复制/移动宿主、私有声明域稳定存储、有限析构兜底和固定容量报告为草案沿用的工程选择，须与已确认行为一起接受最终规格审阅。

实施者可自行确定且须在计划记录：头文件拆分、私有 State 布局、固定身份数值、符合项目风格的具体 API 拼写、可复用夹具及测试标签、示例目标布局；不得借这些细节扩大上述产品范围。

## 11. 设计检查记录

初版 Builder Check 曾用附加注册动作处理每实例状态注入 / 令牌输出缺口；该方案已被本轮 grilling 替代，不作为当前设计的通过依据。

本轮事实核对确认：当前没有按 Scope/声明获取 ScopeAccess 的公共 API；正式实现与测试要求每次工厂恰好 Own 一个对象；声明域内 DeclarationId 唯一，而同一工厂可在不同运行时 Scope 创建独立实例；外部借用须同时通过 Module、Scope 和对象 Gate。当前修订据此新增受限的资源访问能力，并明确将通用参数注入排除在首版之外。

修订后的独立 Builder Check 结论为无剩余重要规划问题：§7 的精确身份、Ready 门槛、Gate、RTTI 与残留令牌寿命边界足以支持实施规划，与既有 Module 依赖和无 Service Module 契约无实质冲突。这不是资源访问 API 已经实现或通过运行验收的证明。

后续规格专项评审覆盖生命周期与服务访问两个方向，Critical 0、Important 0、Minor 2；本次修订已在 §9 补齐两项 Minor 对应的明确验收要求：新查询令牌的真实卸载后操作，以及非 Busy 停止错误的资源保留、错误区分与恢复。此记录仅说明规格已补充要求，正式测试仍待实施及运行验收。
