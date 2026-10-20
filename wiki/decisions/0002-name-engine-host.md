# ADR-0002：使用 EngineHost 作为引擎宿主名称

> Status: Accepted
> 决策背景：2026-10-13 的基础架构讨论
> 归档日期：2026-10-19

## Context

引擎需要共同服务 Game Runtime、Editor Runtime 和工具场景。曾考虑 `RuntimeHost` 这一名称，但“Runtime”容易与后续的 Editor Runtime / Game Runtime 代码层级产生歧义，使统一宿主的职责不清晰。

## Decision

统一引擎宿主概念采用 **`EngineHost`**。它是引擎在宿主应用中进行运行期装配、模块/服务协调与生命周期控制的语义名称，不是 Game-only Runtime 的别名。

## Consequences

- Game / Editor 可以使用同一个宿主概念，各应用允许不同的服务与模块集合。
- Plugin / Module 的发现、依赖、注册和安全卸载需要与 Host 的关闭/初始化行为协调。
- 历史设计草案中的 `RuntimeHost` 统一宿主命名应迁移为 `EngineHost`；不因此认定已完成 API 重命名。
- 不把 Editor Runtime 理解为必须依赖另一套互不兼容的核心。
- 此 ADR **不冻结** C++ 类构造函数、初始化参数、状态枚举、Host 注册机制或 Service Locator/DI 的最终选择。

## Open Questions

具体 Host API、模块装配关系、Application 生命周期、多个 Host、Host 错误传播以及 Editor/Game 的执行形态。

详细约束以 [engine-host.md](../core/engine-host.md) 为准。
