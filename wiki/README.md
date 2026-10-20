# PanguEngine 设计文档索引

> 状态：维护中（Index）
> 更新：2026-10-20
> 约定：文档文件名使用英文 kebab-case，正文中文优先，专业术语可使用英文。

## 如何阅读

- [总体架构](architecture.md)：技术基线、分层与边界。
- [Foundation](foundation/)：语言、构建、工具链、ABI 与平台约束。
- [Core](core/)：宿主、Plugin / Module、反射和内存所有权。
- [Runtime](runtime/)：Runtime 功能领域与选型（不引入 Subsystem 架构）。
- [Decisions](decisions/)：重大决策的原因、替代关系与历史。

**状态是按“文档描述的范围”判定，不代表整个子系统已经实现。**

| 状态 | 定义 |
| --- | --- |
| Frozen | 文档限定的决策已确认；变更应说明兼容性和理由 |
| Partially Frozen | 仅“已确定”小节冻结，其余仍待设计 |
| Draft | 尚未形成最终约束 |
| Superseded | 被新决策替代；仅作历史记录 |

各文档务必区分**设计状态**与**实现状态**。历史讨论产生的草案代码不等于已经完成的产品功能；实现情况以源码和测试为准。

## Foundation

| 文档 | 决策范围 | 状态 |
| --- | --- | --- |
| [C++ 规范](foundation/cpp-guidelines.md) | C++23、禁用异常、编译期优先、现行风格配置 | Partially Frozen |
| [构建系统](foundation/build-system.md) | CMake 4.0+、一个 Module 一个 STATIC/SHARED 库 Target | Partially Frozen |
| [工具链](foundation/toolchain.md) | 全平台共性契约与编译器选择矩阵 | Partially Frozen |
| [Android 工具链](foundation/android-toolchain.md) | NDK/SDK、官方 Clang、CMake/Gradle 配置与验证 | Partially Frozen |
| [Apple 工具链](foundation/apple-toolchain.md) | MacPorts、macOS/iOS SDK、构建及验证 | Partially Frozen |
| [ABI](foundation/abi.md) | 同受控构建内的兼容性与所有权原则 | Partially Frozen |
| [平台](foundation/platform.md) | 五平台目标与开发环境 | Partially Frozen |

## Core

| 文档 | 决策范围 | 状态 |
| --- | --- | --- |
| [Engine Host](core/engine-host.md) | 采用 EngineHost 命名与宿主职责方向 | Partially Frozen |
| [Plugin 与 Module](core/plugin-module.md) | Plugin/Module 职责、单 Module 单库、CodePin 与安全卸载 | Partially Frozen |
| [内存管理](core/memory-management.md) | Pangu Memory Ownership Contract v1 | Frozen（规范 v1） |
| [反射和元数据](core/reflection-meta.md) | pga 属性、禁止反射标记宏、不 Fork LLVM | Partially Frozen |
| [Service / Context](core/service.md) | No Subsystem、Host/Context/World 与 Concrete / Contract / Registry Service 三模式 | Frozen（设计语义）；API PoC |

## Runtime

- [Runtime 功能目录](runtime/README.md)：flecs / The Forge 选型；具体集成实现尚未冻结。

## 架构决策记录（ADR）

- [ADR 索引与规则](decisions/README.md)
- [ADR-0001：采用 CMake](decisions/0001-adopt-cmake.md)
- [ADR-0002：采用 EngineHost](decisions/0002-name-engine-host.md)
- [ADR-0003：采用 Service + Context，不使用 Subsystem](decisions/0003-no-subsystem-service-context.md)
- [ADR-0004：按平台配置 Clang、统一 C++23](decisions/0004-platform-clang-toolchains.md)
- [ADR-0005：一个 Module 一个静态库或动态库](decisions/0005-one-module-one-library.md)
- [ADR-0006：三种 Service 模式](decisions/0006-three-service-modes.md)

## 文档维护规则

1. 一个问题以一个**权威设计文档**为主；其他文档只总结并链接，避免重复维护规范条款。
2. 新决策应记录范围、当前状态、未决问题及与旧方案的关系。
3. Frozen 只冻结明确列出的边界；不得把“方向一致”包装成“字段/API 全部冻结”。
4. 重大替代性决策写入 ADR，原文档同步更新；不要在旧文档上静默覆盖历史。
5. 规范正文中的 MUST / SHOULD / MAY 分别表示必须、应该和可以；只有带具体约束的措辞才构成可执行规范。
6. 此索引不代表实现完成清单；源码、测试和跨平台验证应单独追踪。
