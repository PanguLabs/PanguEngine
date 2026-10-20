# Pangu Service / Context 架构 v1

> 设计状态：Frozen（Scope、生命周期及 Concrete / Contract / Registry 三种 Service 模式的语义契约）；C++ API 未冻结；实现仅有局部 PoC
> 更新：2026-10-20
> 决策：[ADR-0003](../decisions/0003-no-subsystem-service-context.md)、[ADR-0006](../decisions/0006-three-service-modes.md)
> 本地参考探针：`.probe/service-lifecycle`（Git 忽略，不随仓库分发；与正式设计的差异见下文）

## 目标

PanguEngine 不建立 UE 式统一 `Subsystem` 层、`ISubsystem` 基类、`SubsystemManager`，也不把它们仅改名成万能 `IService` 或全局 `ServiceLocator`。能力由 Module 提供；实例归属于 Host / Context / World；动态能力才需要注册表。

以下是**设计不变量**，不等于要求整个引擎都使用当前 PoC 类签名。

## 概念与所有权

| 概念 | 权责 |
|---|---|
| Module / Plugin | Plugin 管发现与分发；Module 管代码注册和库加载/代码驻留，一个 Module 只产生一个 STATIC/SHARED 库 |
| EngineHost | Host 范围服务、共享物理执行资源与 Module 管理 |
| EngineContext | Config / VFS / Asset 等独立上下文实例 |
| WorldContext | flecs World、Tick、World 级请求与服务 |
| Service | 对外能力接口；**不强制共同基类或虚方法** |
| Manager | 模块内部实现，可作为 Service，也可不暴露 |

Service Contract Identity 独立于 Provider ModuleId；同一单 Provider Contract 在一个明确的作用域内默认最多一个有效 Provider。动态 Provider 选择来自配置和依赖解析，不采用无约束“最后注册者胜出”。

**实例归属**：Host / Context / World 是 Instance Owner；Module 是 Code Owner。动态库句柄和 CodePin 纳入现有 Module 管理，不建立独立 BinaryImage 架构层。Registry Service 的注册项和 Service 实例不是同一种对象，不能用一套通用对象管理器混同它们。

## 三种 Service 模式（Frozen）

| 模式 | 语义 | 访问与注册 |
| --- | --- | --- |
| **Concrete Service** | 已确定的具体实现；Scope Owner 持有实例，通常无需动态 Registry | 使用具体 C++ 类型，直接从 Host / Context / World 获取或注入 |
| **Contract Service** | 通过独立 Contract Identity 选择 Provider；允许构建期静态绑定或启动时动态绑定 | 动态 Provider 使用 Scoped Registry、稳定 ServiceId、短期 ServiceRef / Borrow |
| **Registry Service** | 已有业务 Module 提供多实现集合，如 Importer、Serializer、Inspector、Command Registry | 多个 Module 贡献 Factory/Callback，按 Key 查询或枚举，必须支持撤销 |

1. 三种模式是 Module **提供能力的方式**，不是三个 Module 种类，彼此不互斥；一个 Module 可以同时提供多种模式，或完全没有 Service。
2. **不要求** Service 继承统一 `IService`，也不要求都有通用 `Start/Stop`；模块内部的 Manager 可暴露为 Concrete Service 或仅作为实现。
3. **不引入通用 `ExtensionDescriptor`、`ExtensionManager` 或全局 Extension Registry**。Registry Service 使用现有 Module 注册流程和业务专用 Registry 接口。Registry 对象本身也可以是 Concrete Service；Contribution 不一定需要独立 Service Instance。
4. Contract 默认是单 Provider 绑定；Registry Service 是多 Provider 集合，Provider 冲突/匹配规则必须由 Registry 明确规定，不能依赖模块加载顺序。
5. 静态已知实现优先直接调用或使用 C++23 Concept，不强制经过动态 Contract 的间接分发与 Registry 查找。所有访问仍须满足生命周期约束；静态访问可由 Scope 所有权和销毁顺序保证，不能因省略 Registry 而省略保护。
6. Module 注册仅声明或安装可用能力，并不等于某个 Scope 的 Service Ready。多 Context/World 的 Contributions 必须按归属安装到对应 Scope，不能隐式写入进程全局单例。

### 三个作用域

- Host：可以引用 Host。
- Context：可以引用 Context 和所属 Host。
- World：可以引用 World、所属 Context 和 Host。

长生命周期服务 **不得**强依赖短生命周期服务。跨 Scope 查找必须显式绑定所属 Context/Host，不允许全局自动找到“某个 Context”。Device 的 GPU Realm 是资源所有权域，并不要求成为通用第四个 Service Scope。Editor 同样不要求 EditorSubsystem。

## 两类访问与一类注册

1. **Concrete / 静态 Contract**：静态已知核心服务直接通过 `host.Scheduler()`、`context.Assets()` 等强类型 API 获取或注入，不经动态 Registry（函数名为示意，非冻结签名）。
2. **动态 Contract**：Plugin Provider 通过 Scoped Registry 按稳定 ServiceId 取得短期 `ServiceRef<T>`，必须检查 Ready；借用期间要保护实例、依赖及其实现代码。
3. **Registry Service**：已有 Module 暴露业务专用 Registry；其他 Module 注册按 Key 匹配的 Contribution。Registry 需控制撤销和在途回调，记录提供者 Module，并确保动态代码在调用/析构结束前仍驻留。

动态 Contract 的 Scoped Registry 仅承担 Provider 注册/撤销、单 Provider 唯一性、生命周期借用与依赖解析；业务 Registry 的多 Provider 语义由该 Registry 自行约定。二者均**不负责**扫描所有 C++ 对象、分配任意 Gameplay 对象、自动 Tick 或充当全能 DI Container。

服务接口默认面向 C++23 强类型契约。插件 ABI 仍必须遵守 [ABI](../foundation/abi.md)；ServiceId 持久稳定，不能来自 `std::hash` 或不受控 RTTI。必要时由 PanguMeta 提供生成值，但 v1 不强制引入 `pga::service`。

## 生命周期与依赖

- Provider 注册与 Service Instance 构建分离；不是加载 DLL 就自动 Ready。
- 显式 Dependency 组成 DAG；校验同 Scope 环和跨 Scope 方向；依赖先创建，逆序销毁。
- **存活依赖具有 Borrow/Lease 保护语义**，不能只在 Create 时查询一次 Ready。依赖方存活及执行 Destroy/清理期间，被依赖实例及所需代码必须保持有效；静态服务可由 Scope 所有权和依赖销毁顺序实现这一保证，不强制使用 PoC 的 Borrow 类型。
- 创建中出现错误：以启动完成顺序反向回滚已创建实例，不发布失效服务。
- Ready → Quiescing：拒绝新的业务访问；在途 ServiceRef、Task、Callback 和资源清理继续使用之前合法取得的 Lifetime Guard。
- Draining：结束异步工作，并等待使用者释放对当前服务的外部 Borrow/Lease 及对应 CodePin；**当前服务持有的依赖 Borrow/Lease，以及 Destroy/清理所需的实例和代码保护，须保留到清理完成之后**。Busy 不等于已成功停止。
- 按依赖逆序逐个排空并销毁服务：在实现代码及被依赖实例仍有效时执行 Destroy，之后释放该服务持有的依赖保护，再处理被依赖服务；不得要求整个 DAG 的依赖 Borrow 在任何 Destroy 之前全部归零。最后的 Callback/Destroy、Registry Contribution 使用及相关 CodePin 都结束后，才允许卸载实际承载代码的动态库。
- 不自动调度 Tick/Update。World Tick 由 WorldContext/flecs 管理，GPU 执行由 Renderer 管理，任务由 TaskScheduler 管理。
- Module Dependency 与 Service Dependency 是**不同的图**；协调器最终必须验证二者的关闭顺序。

## 服务注册的最小 API 方向

```cpp
namespace pg::service {
enum class Scope : std::uint8_t { Host, Context, World };
struct ServiceId;
struct Dependency { ServiceId id; Scope scope; };
class Registry;
class Registration;
template<class T> class ServiceRef;
}
```

预期 Host/Context 对核心 Concrete Service 使用强类型成员 API；动态 Contract 才需要 Scoped `Registry::acquire<T>()`；Registry Service 使用各业务 Module 自己的 Registry API，不引入 ExtensionDescriptor。

## 当前 PoC 与生产边界

`.probe/service-lifecycle` 包含独立 CMake 验证探针：三个 Scope 规则、DAG、服务初始化回滚、活动借用阻止停止、真正 DLL/SO 的 Code Guard 释放。

该本地原型将所有 Provider 放入 Registry，并用名为 `BinaryImage` 的辅助对象提供 Code Guard；这些是原型实现手段，不属于正式设计。生产设计仍遵循静态服务直接访问、Module 机制管理 CodePin、不建立独立 BinaryImage 架构层的约束。

**未完成**：统一 EngineHost 启动状态机、ModuleManager 的真实 CodePin 接口、TaskGroup/Async Drain、Provider Hot Swap、异常/致命 OOM 策略整合、完整的并发 register/start/stop、正式的 Actor/World Ownership、Android/iOS 运行验证。本 PoC 的 Registration 与辅助状态字段不能当作最终 Public ABI。

尤其注意：PoC 中 `Registry::stop_all()` 是非阻塞的 `Busy` 检查，正式 EngineHost 必须先安排 Scoped TaskGroup、回调撤销及 Content/GPU 退休，之后重试 Drain；不得直接强制释放。

## 状态冻结范围

冻结：No Subsystems；Concrete / Contract / Registry Service 三种模式；静态访问优先；Host/Context/World 作用域；单 Provider Contract 唯一性与业务 Registry 多 Provider 原则；Module 与 Service 独立职责；依赖 Borrow/Lease 和有序撤销；不自动 Tick；CodePin 与实例销毁顺序。

未冻结：C++ Public API 签名、Registry 的匹配/优先级与 Contribution 身份、物理容器、线程同步策略、错误枚举、Factory 函数签名、PanguMeta 注册生成协议、Hot Swap / Hot Reload。
