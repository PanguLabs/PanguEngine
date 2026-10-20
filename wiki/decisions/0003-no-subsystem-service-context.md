# ADR-0003：不建立 Subsystem 架构，采用 Service + Context

> Status: Accepted
> 日期：2026-10-20
> 规范：[Service Architecture v1](../core/service-architecture.md)

## Context

项目已有 EngineHost、EngineContext、WorldContext、Plugin/Module 等职责边界。如果再模仿 UE5 建立一套统一的 Subsystem 自动发现、生命周期与 Tick 系统，容易与 ModuleManager、World Tick、TaskScheduler 形成重叠。文档曾把 Subsystems 作为功能领域分层，但**它并非已冻结的运行时基类**。

## Decision

1. 不设计 `ISubsystem`、`SubsystemManager` 或自动 Subsystem Tick。
2. 不用统一 `IService` 基类替代上述机制。
3. Service 是能力契约；Context 是实例 Owner，Module 是代码 Owner，Plugin 是分发与发现单元。
4. Host/Context/World 构成三种明确的实例作用域；跨 Scope 依赖由真实 Parent 关系解析。
5. 静态已知的核心服务直接使用强类型成员访问；仅动态扩展服务注册到 Scoped Registry。
6. 动态 Service 注册具有撤销与 Borrow 语义；生命周期依赖按 DAG 构建、逆序排空和销毁。
7. 所有 Module/Service 卸载遵守任务、回调、资源、GPU 及析构代码的驻留约束。

## Consequences

- 文档原 Subsystems 目录改成 Runtime 功能目录；ECS 使用 flecs、Rendering 使用 The Forge 的技术选型不变。
- 简化了引擎的生命周期概念，但必须明确动态 Service 获取和 ModuleCodePin 边界。
- 动态服务不等同于所有 CMake 模块；Module 与 Service 不是一对一。

## Open Questions

ServiceDescriptor 最终布局、依赖动态切换、可多 Provider 扩展点、EngineHost Bootstrap、错误传播和大型生命周期图的性能。
