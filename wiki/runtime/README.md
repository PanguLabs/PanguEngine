# Pangu Runtime 功能领域

> 状态：Partially Frozen（选型确定；各模块接口与集成仍需独立规范）
> 更新：2026-11-21
> 说明：**Runtime 不是统一 Subsystem 基类或额外生命周期层级**；遵循 [ADR-0003](../decisions/0003-no-subsystem-service-context.md) 和 [Service 架构](../core/service.md)。

## 已确定的选型

| 功能领域 | 已确定 | 尚未冻结 |
| --- | --- | --- |
| ECS | **flecs v4.2.0**。已交付并验收原生 API、登记、推进与 World 关闭顺序。[首版 World 契约](ecs.md) | 公开 C++ API/ABI、后续 Meta/Task 集成 |
| Rendering | 底层库采用 **The Forge v1.63**，固定提交 [`7443530ef7e888771f307de74b7872becfd64450`](https://github.com/ConfettiFX/The-Forge/tree/7443530ef7e888771f307de74b7872becfd64450)。首版清屏呈现的目标平台为 Windows、Linux、macOS、Android 和 iOS。Windows 已通过实际窗口呈现验收。Linux 和 Android 的图形应用使用 Forge Vulkan，参见[真机测试入口](../../Tests/Toolchain/README.md)和[平台验证记录](../../docs/superpowers/plans/2026-11-20-renderer-forge-igraphics.md)。 | Renderer 接口、各平台设备集成、Pipeline、Shader/Material、场景与编辑器预览 |

ECS 的桌面运行和移动链接覆盖见[最终验收](../../docs/superpowers/plans/2026-11-18-ecs-world.md#ecs-交付验收)。Renderer 首版由 Engine 向 Project 提供 Application 入口，源码位于 `Source/Runtime/Application/`。Renderer 采用原版 Forge `IGraphics` 公开接口，平台验收口径见 [Renderer 接入规格](../../docs/superpowers/specs/2026-11-20-renderer-forge-igraphics-design.md)。

图形与渲染功能的取舍见[渲染接入原则](rendering.md)。

## C++ 最佳实践对 Runtime 的指导（SHOULD；非 API 冻结）

- 数据布局、Typed Query、Event / Command / Message 的通用建议见 [C++ 规范第 8 节](../foundation/cpp-guidelines.md#8-ecsdata-oriented-与-event-driven)；确定性与调度建议见[第 9 节](../foundation/cpp-guidelines.md#9-determinismscheduling-与-thread-safety)。具体 flecs Query、消息语义及 Scheduler API 仍待领域规范确定。
- Task、Callback 和 Registry Contribution 的存活保护遵循 [Service](../core/service.md)、[Module](../core/plugin-module.md) 与[内存所有权](../core/memory-management.md)的既有契约。
- Asset/Shader 的预处理方向见[构建系统](../foundation/build-system.md)；运行时成本评审遵循 [C++ 规范第 4 节](../foundation/cpp-guidelines.md#4-zero-overhead-abstraction零额外抽象开销)。

通用准则不替代各领域专题规范；后续专题可将建议进一步冻结，本节不表示 Event、Scheduler、Asset Pipeline 已实现。

## 待独立规范

Asset/Resource/Streaming、VFS/Package、Input、Physics、Audio、UI、Editor、Gameplay、Networking 等根据实现进展分别撰写规范。目录中的功能组织既不要求一个目录等于一个 Module，也不要求每种功能注册 Service。

## 跨层依赖

- 资源和代码所有权遵守 [内存管理](../core/memory-management.md)。
- 模块化部署遵守 [Plugin / Module](../core/plugin-module.md)。
- 静态服务/动态注册和作用域遵守 [Service](../core/service.md)。
- 自动注册与元数据受 [Reflection / Meta](../core/reflection-meta.md) 约束。
