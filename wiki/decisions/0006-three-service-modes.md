# ADR-0006：Module 内采用三种 Service 模式

> Status: Accepted
> 日期：2026-10-20
> 权威规范：[Service Architecture v1](../core/service.md)、[Plugin / Module](../core/plugin-module.md)
> 基于：[ADR-0003：No Subsystem / Service + Context](0003-no-subsystem-service-context.md)

## Context

早期讨论把 Concrete Service、Contract Service 和 Extension Registry 作为能力组合方式，并考虑另建 `ExtensionDescriptor` / 管理层。PanguEngine 已经有 Plugin / Module 组织、Host/Context/World Scope、Scoped Service Registry 和 CodePin/Borrow 约束，无须平行搭建扩展生命周期框架。

## Decision

1. 三种正式名称为 **Concrete Service**、**Contract Service**、**Registry Service**；它们是一个 Module 对外提供能力的三种方式，并不是新的 Module 类型。
2. Concrete Service：静态已知具体实现，由相应 Scope Owner 管理并强类型访问，通常不进入动态 Registry；不强制 `IService` 或统一 Start/Stop。
3. Contract Service：独立 Contract Identity 与 Provider ModuleId 分离；静态绑定优先，动态 Provider 使用 Scoped Registry 和短期 ServiceRef/Borrow；同 Scope 单 Provider Contract 默认至多一个有效实现。
4. Registry Service：已有业务 Module 提供 Registry（如 Importer、Serializer、Inspector），允许多个 Module 注册 Contribution，并按 Key 匹配/枚举；Registry 自身可以是 Concrete Service，Contribution 不必是 Service 实例。
5. 不引入通用 `ExtensionDescriptor`、`ExtensionManager` 或全局 Extension Registry。Registry Contribution 使用原有 Module 注册/撤销流程，仍须持有正确的 Module Owner 与 CodePin 约束。
6. 保留 ADR-0003 的三个 Scope、显式依赖 DAG、生命周期 Borrow/Lease、Quiesce/Drain 与 CodePin；Module 的加载与注册不能代替 Service Ready。
7. 允许一个 Module 同时采用多种模式，也允许一个 Module 不提供任何 Service。

## Consequences

- Service 访问模式清晰，不引入重复的 Subsystem / ServiceManager / ExtensionManager。
- Registry Service 多 Provider 与 Contract Service 单 Provider 唯一性语义不同。
- 对多 Context/World 的实例，Module 注册声明需在所属 Scope 中安装实际 Provider/Contribution，不能假定单一全局 Registry。
- 具体 API、注册方式、Provider 匹配、PanguMeta 是否参与自动化等仍须在后续协议设计中确认。

## Open Questions

Registry Contribution 的具体身份与撤销接口、动态 Contract API/Fingerprint、线程同步、Provider 切换、Scope 创建时的注册事务及失败回滚算法。
