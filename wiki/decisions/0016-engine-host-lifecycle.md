# ADR-0016：单次 EngineHost 生命周期与只读具名服务访问

> Status：Accepted（最小实现已交付，Public C++ API/ABI 未冻结）
> 日期：2026-11-07
> 主要规范：[EngineHost](../core/engine-host.md)、[Service](../core/service.md)、[Module API](../core/module-api.md)

## Context

ModuleManager 已提供加载、注册、激活、Scope 和可重试清理，但 Application 手写编排容易丢失启动失败后的清理责任。业务 Concrete Service 还需要从已创建的精确 Scope 取得实例，同时保证关闭和动态库卸载后残留令牌安全。三种 Service 模式及单 Module 单库的既有决策不变。

## Decision

Application 拥有不可复制、不可移动的 EngineHost；它持有声明域和稳定 Manager，声明域最后销毁。EngineHost 自身是第三常驻 Module，保留基础 BuiltinResidentQueries 两项契约。

一个宿主只同步 Start 一次，先常驻后三阶段业务整批启动。失败整体关闭，Ready 固定业务集合，Closed 不重启。这样将失败清理与重新构造的责任分开，避免在同一身份域恢复部分旧状态。Context/World 后续按需创建并局部回滚。

业务 Module 提供具名强类型访问器，固定 Owner/Declaration/Kind/T；宿主和 Module 内部桥接只查询已有 Ready 资源，按 RTTI 值检查精确类型。不暴露通用 ServiceLocator、Manager 指针、unchecked/adopt 或业务令牌 Map，不增加 Factory 配置回调。借用保活复用 ScopeAccess/ScopeBorrow，清理复用既有 Manager DAG。

报告分别保留启动首错和本轮清理错误；重入拒绝不得污染外层报告。关闭继续责任属于 Application，没有后台线程、ForceUnload 或超时强毁。析构有限兜底不能替代显式排空。

## Alternatives 与 Consequences

- 不采用可重启同一宿主：减少跨代状态恢复和失败资源复用歧义，代价是重新运行必须创建新宿主。
- 不采用按类型全局搜索或自动祖先查找：同类型多声明、多 Context、多宿主的身份保持明确，代价是调用方显式持有正确 Scope。
- 不复制 Manager 清理器：保留统一资源/代码寿命规则，宿主根据 CleanupOwner 同步状态。
- 不把所有 Service 转为动态 Contract：Concrete 无需共同基类、虚方法或 Registry。完整 Contract/Registry 仍为后续范围，Module 可无 Service。

样例及 Project 使用相同入口协议；桌面双模式需运行正式回归并观察真实物理卸载，移动端需最终链接和宿主产物检查。实际证据见[验收记录](../../docs/superpowers/plans/2026-11-07-engine-host-lifecycle-validation.md)，不从 Accepted 推断设备、打包或签名通过。

## Open Questions

Public C++ API/ABI、Contract Provider 选择、Registry Contribution、TaskScheduler/GPU 集成、热更新及平台 Application 主循环另行设计；不重新开放既有 Service 三模式和安全清理语义。
