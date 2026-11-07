# Pangu EngineHost 启动与生命周期

> 状态：Partially Frozen（宿主职责、单次启动、Scope 与安全关闭行为已确定；Public C++ API/ABI 未冻结）
> 更新：2026-11-07
> 决策：[ADR-0002](../decisions/0002-name-engine-host.md)、[ADR-0016](../decisions/0016-engine-host-lifecycle.md)

## 职责与所有权

Application 拥有 `pg::engine::EngineHost`。它不可复制、不可移动，构造不执行 Query、Load 或 Register；内部声明域比 ModuleManager 活得更久。同一进程可拥有多个独立宿主。Game、Editor、Tool 共用此协议，窗口、渲染和主循环仍由 Application 负责。

`PanguEngineHost` / `Pangu::EngineHost` 是独立第三常驻 Module，单一真实 STATIC/SHARED Target 跟随统一链接模式。身份为 `8b07b8da6c9647469f8a31608f7e2193`，版本 1.0.0、协议 1，Required 依赖 Module Runtime 1.0.0。`BuiltinResidentQueries()` 仍只返回 Foundation/Module 两项，EngineHost 自行组合自己的 Query；不反向依赖业务 Module。

EngineHost 复用 [ModuleManager](module-api.md) 的资源 DAG、Scope、原生持有与清理器，不引入第二套加载/资源管理系统。Plugin 仍管理发现和分发，Module 管理代码与能力声明；Module 可以完全没有 Service。三种 [Service](service.md) 模式的既有语义不变。

## 单次启动

状态依次为 Cold → Starting → Ready → Closing → Closed。`Start(config, report)` 只在 Cold 接纳一次，同步执行六阶段：常驻接纳、常驻注册、常驻激活、业务整批接纳、业务注册、业务激活。业务空批次跳过 Load；启动 Busy 也是失败，任何阶段失败均请求整体关闭，不交付部分 Ready 宿主。Closed 不重启，重新启动创建新的 EngineHost。

`HostStartupConfig` 的四个 span 借用额外常驻 Query/Bindings 与业务 Candidates/Bindings，仅在调用期间使用。Ready 后业务集合固定；首版不提供增量加载、通用构造参数注入、ConfigureInstance 或额外 Register 回调。

`HostOperationReport` 是拥有型值快照，区分当前阶段/错误与 `StartupFailure` 的首次启动阶段/错误/Module 报告。清理重试返回非 Busy 错误也不能覆盖首错或丢失所有者。回调重入管理操作返回 Host Busy，局部报告在外部代码返回后整体发布，复用同一 report 不污染外层结果。

## Scope 与具名 Concrete Service 访问

Ready 后可创建 Context，以及指定 Context 下的 World。创建失败只回滚该 Scope；已有 Scope 与服务继续有效。失败槽位复用受 Domain/Slot/Generation 保护，不能使旧句柄访问新对象。Host 根 Scope 不单独关闭；子 Scope 显式 BeginClose/TryFinishClose，失败装配的残留由 PollCleanup 继续推进。

Module 自有头内具名入口接收 `const EngineHost&` 和 `ScopeHandle`，固定自己的 Owner、DeclarationId、Kind 和具体 T，通过 `detail::HostResourceAccess` 与 Module 内部只读查询获得 `ScopeAccess<T>`。只在 Ready 且宿主未执行管理操作时接纳；不返回 Manager 指针，不允许调用方自行 adopt 控制块。

查询精确匹配宿主、句柄、Module、声明、Scope Kind 和 RTTI 类型值。它只读取已有 Ready 实例，不执行 Factory、不自动搜索祖先、不选择 Provider；同类型的不同声明相互独立，纯元数据声明和无 Service Module 不产生可访问对象。需要祖先实例时显式传祖先句柄。业务服务无需共同基类、虚方法或 Manager 命名。

`ScopeAccess` 仅拥有常驻控制块，`ScopeBorrow` 才保护实例及其代码。查询和管理操作由调用方串行协调；已发出的令牌可并发 TryBorrow/Release，与关闭闸门线性化。服务自身数据访问仍遵循业务线程契约。RTTI 只在受保护的查询期间按值比较，不进入残留令牌。

## 关闭与诊断

Application 显式调用 BeginShutdown，然后在 Borrow/Pin、异步工作等排空后重试 TryFinishShutdown。Closing 拒绝新查询和 Borrow，保留已有保护以及清理所需资源、声明和代码；停止错误或原生释放失败均可重试。没有 ForceUnload、超时强毁或后台清理线程。析构只做有限同步兜底，仍不能安全完成时走所有配置有效的终止路径。

GetState/HostScope 为无副作用观察，Cold/Closed 无有效 HostScope。InspectModules/InspectDeclarations 返回拥有型快照，操作重入返回 Busy；成功关闭后为空。清理责任升级为 ManagerShutdown 时，宿主同步进入 Closing。

真实物理卸载须由无额外导入持有的业务夹具库事件证明。新具名入口产生的 ScopeAccess 在业务库真实卸载后仍可复制、失败 TryBorrow 和最终释放，其控制块及释放实现位于常驻 Runtime。系统仍持有某库时不承诺它物理移除。

ELF 可执行宿主参与跨 DSO 公共 RTTI 时需要正确的类型可见性及可执行文件动态符号导出（CMake `ENABLE_EXPORTS`）。这不替代受控 ABI/ODR 约束或最小公共导出，也不采用逐类型身份锚点。

## 实现与验收边界

最小生命周期、Concrete 查询、[MinimalHost](../../Samples/MinimalHost/README.md) 以及独立 Project Game/Editor/MobileHost 消费已实现。正式回归保留在 `Tests/EngineHost` 和 `Tests/Module`；实际平台、模式、失败与重跑记录见[本轮验收](../../docs/superpowers/plans/2026-11-07-engine-host-lifecycle-validation.md)。设计规格见[生命周期规格](../../docs/superpowers/specs/2026-11-07-engine-host-lifecycle-design.md)。实现存在不等于平台已验收。

完整 Contract Provider 选择、Registry Contribution、TaskScheduler/GPU 排空集成、热更新、窗口/渲染/主循环、设备运行、打包和签名仍未交付。Public C++ 签名、布局与 ABI 不作为长期冻结接口；现有三种 Service 模式、依赖方向和安全销毁语义继续有效。
