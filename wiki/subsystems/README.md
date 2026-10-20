# Pangu Engine Subsystems

> 状态：Partially Frozen（技术选型已确定，具体集成/设计仍待冻结）
> 更新：2026-10-19

Subsystem 指建立在 Foundation 与 Core 架构之上的游戏开发能力。与核心设计解耦，并不意味着所有实现都需要动态插件或独立动态库。

## 已确认的选型

| 子系统 | 已确定 | 尚未冻结 |
| --- | --- | --- |
| ECS | 采用 **flecs** | World/Entity/Component 与 pg Object、Meta、Task 的集成 |
| Renderer | 采用 **The Forge** 作为底层图形/渲染基础库 | Render Graph、资源所有权、Pipeline、Shader/Material、场景与编辑器预览 |

以上**只冻结第三方库的选型**；不代表其 CMake 集成已经完成，也不代表整个 ECS/Renderer API 已完成设计或可在五个平台运行。

## 待独立规范的子系统

Asset/Resource/Streaming、VFS/Package、Input、Physics、Audio、UI、Editor、Gameplay、Networking，以及具体 Renderer/ECS 的集成契约，都需要各自明确状态的设计文档。

在形成实质性的、已确认规范之前，不创建大量只有标题的空白文件。完成讨论后可新增 `ecs.md`、`renderer.md`、`asset-resource.md` 等英文文件名的文档，并从上级 [文档索引](../README.md) 链接。

## 跨层依赖

- ECS 与 Renderer 等子系统不得绕过 [内存与资源所有权规范](../core/memory-management.md)。
- 模块化实现需遵守 [Plugin / Module](../core/plugin-module.md) 的生命周期与卸载限制。
- 反射或自动注册能力应遵守 [Reflection / Meta](../core/reflection-meta.md) 的声明与生成策略；现行 Meta Schema 未冻结。
