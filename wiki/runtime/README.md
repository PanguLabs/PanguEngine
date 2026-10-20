# Pangu Runtime 功能领域

> 状态：Partially Frozen（选型确定；各模块接口与集成仍需独立规范）
> 更新：2026-10-20
> 说明：**Runtime 不是统一 Subsystem 基类或额外生命周期层级**；遵循 [ADR-0003](../decisions/0003-no-subsystem-service-context.md) 和 [Service 架构](../core/service.md)。

## 已确定的选型

| 功能领域 | 已确定 | 尚未冻结 |
| --- | --- | --- |
| ECS | 采用 **flecs** | World/Entity/Component 与 pg Object、Meta、Task 的集成 |
| Rendering | 采用 **The Forge** 作为底层图形/渲染基础库 | Renderer 后端真实设备集成、Pipeline、Shader/Material、场景与编辑器预览 |

选型不等于对应 CMake 集成已经完成，也不表示整个 ECS/Renderer API 或五平台实现已经通过验证。

## 待独立规范

Asset/Resource/Streaming、VFS/Package、Input、Physics、Audio、UI、Editor、Gameplay、Networking 等根据实现进展分别撰写规范。目录中的功能组织既不要求一个目录等于一个 Module，也不要求每种功能注册 Service。

## 跨层依赖

- 资源和代码所有权遵守 [内存管理](../core/memory-management.md)。
- 模块化部署遵守 [Plugin / Module](../core/plugin-module.md)。
- 静态服务/动态注册和作用域遵守 [Service](../core/service.md)。
- 自动注册与元数据受 [Reflection / Meta](../core/reflection-meta.md) 约束。
