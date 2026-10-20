# Pangu Service / Context 架构 v1

> 设计状态：Frozen（本文件列明的语义契约）；API / 实现状态：PoC、未冻结
> 更新：2026-10-20
> 决策：[ADR-0003](../decisions/0003-no-subsystem-service-context.md)
> 参考实现：[ServiceLifecycle PoC](../../.probe/service-lifecycle/README.md)

## 目标

PanguEngine 不建立 UE 式统一 `Subsystem` 层、`ISubsystem` 基类、`SubsystemManager`，也不把它们仅改名成万能 `IService` 或全局 `ServiceLocator`。能力由 Module 提供；实例归属于 Host / Context / World；动态能力才需要注册表。

以下是**设计不变量**，不等于要求整个引擎都使用当前 PoC 类签名。

## 概念与所有权

| 概念 | 权责 |
|---|---|
| Module / Plugin | 代码组织、可发现性、注册和 BinaryImage 生命周期 |
| EngineHost | Host 范围服务、共享物理执行资源与 Module 管理 |
| EngineContext | Config / VFS / Asset 等独立上下文实例 |
| WorldContext | flecs World、Tick、World 级请求与服务 |
| Service | 对外能力接口；**不强制共同基类或虚方法** |
| Manager | 模块内部实现，可作为 Service，也可不暴露 |

Service Contract Identity 独立于 Provider ModuleId；同一 Contract 在一个明确的作用域内默认最多一个有效 Provider。动态 Provider 选择来自配置和依赖解析，不采用无约束“最后注册者胜出”。

### 三个作用域

- Host：可以引用 Host。
- Context：可以引用 Context 和所属 Host。
- World：可以引用 World、所属 Context 和 Host。

长生命周期服务 **不得**强依赖短生命周期服务。跨 Scope 查找必须显式绑定所属 Context/Host，不允许全局自动找到“某个 Context”。Device 的 GPU Realm 是资源所有权域，并不要求成为通用第四个 Service Scope。Editor 同样不要求 EditorSubsystem。

## 两类服务访问

1. **静态已知核心服务**：直接通过 `host.scheduler()`、`context.assets()` 等强类型 API 使用，不经动态 Registry。
2. **Plugin 动态能力**：通过 Scoped Registry 按稳定 ServiceId 获得短期 `ServiceRef<T>`，必须检查 Ready 状态并保持实例及实现代码有效。

动态 Registry 仅承担 Provider 注册/撤销、唯一性、生命周期借用、依赖解析；**不负责**扫描所有 C++ 对象、分配任意 Gameplay 对象、自动 Tick 或充当全能 DI Container。

服务接口默认面向 C++23 强类型契约。插件 ABI 仍必须遵守 [ABI](../foundation/abi.md)；ServiceId 持久稳定，不能来自 `std::hash` 或不受控 RTTI。必要时由 PanguMeta 提供生成值，但 v1 不强制引入 `pga::service`。

## 生命周期与依赖

- Provider 注册与 Service Instance 构建分离；不是加载 DLL 就自动 Ready。
- 显式 Dependency 组成 DAG；校验同 Scope 环和跨 Scope 方向；依赖先创建，逆序销毁。
- **存活依赖是 Borrow/Lease**，不能只在 Create 时查询一次 Ready。子服务存活期间，依赖的实例必须可用。
- 创建中出现错误：以启动完成顺序反向回滚已创建实例，不发布失效服务。
- Ready → Quiescing：拒绝新的业务访问；在途 ServiceRef、Task、Callback 和资源清理继续使用之前合法取得的 Lifetime Guard。
- Draining：必须先结束异步工作、释放 Borrow 和必要的 CodePin，再销毁实例。Busy 不等于已成功停止。
- 在实现代码仍加载时执行 Destroy；只有最后的 Callback/Destroy 完成后才允许卸载 BinaryImage。
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

预期 Host/Context 对高频核心服务使用直接成员 API，只有 Plugin 的可选服务才使用 `Registry::acquire<T>()`。

## 当前 PoC 与生产边界

`.probe/service-lifecycle` 包含独立 CMake 验证探针：三个 Scope 规则、DAG、服务初始化回滚、活动借用阻止停止、真正 DLL/SO 的 Code Guard 释放。

**未完成**：统一 EngineHost 启动状态机、ModuleManager 的真实 CodePin 接口、TaskGroup/Async Drain、Provider Hot Swap、异常/致命 OOM 策略整合、完整的并发 register/start/stop、正式的 Actor/World Ownership、Android/iOS 运行验证。本 PoC 的 Registration 与辅助状态字段不能当作最终 Public ABI。

尤其注意：PoC 中 `Registry::stop_all()` 是非阻塞的 `Busy` 检查，正式 EngineHost 必须先安排 Scoped TaskGroup、回调撤销及 Content/GPU 退休，之后重试 Drain；不得直接强制释放。

## 状态冻结范围

冻结：No Subsystems；静态访问优先；Host/Context/World 作用域；显式的服务提供者唯一性；Module 与 Service 独立职责；依赖强引用和有序撤销；不自动 Tick；CodePin 与实例销毁顺序。

未冻结：C++ Public API 签名、物理容器、线程同步策略、错误枚举、Factory 函数签名、Hot Swap 与动态多提供者扩展协议。
