# Architecture Decision Records (ADR)

> 状态：规范性索引
> 更新：2026-10-20

本目录记录影响架构长期演化的重要决策，包括备选方案、选择原因、后果及被替代关系。**ADR 不是子系统的第二份详细规范**；日常实施以相应 Foundation/Core/Runtime 文档为准。

## 记录

| 编号 | 标题 | 决策状态 | 主要规范 |
| --- | --- | --- | --- |
| [0001](0001-adopt-cmake.md) | 采用 CMake | Accepted | [Build System](../foundation/build-system.md) |
| [0002](0002-name-engine-host.md) | 采用 EngineHost 名称 | Accepted | [EngineHost](../core/engine-host.md) |
| [0003](0003-no-subsystem-service-context.md) | 不使用 Subsystem，采用 Service + Context | Accepted | [Service Architecture](../core/service.md) |
| [0004](0004-platform-clang-toolchains.md) | 按平台选择 Clang（Android NDK 自带）+ 统一 C++23 | Accepted | [Toolchain](../foundation/toolchain.md) |
| [0005](0005-one-module-one-library.md) | 一个 Module 对应一个静态库或动态库 | Accepted | [Plugin / Module](../core/plugin-module.md)、[Build System](../foundation/build-system.md) |
| [0006](0006-three-service-modes.md) | Concrete / Contract / Registry Service 三模式 | Accepted | [Service](../core/service.md) |

## 推荐格式

新 ADR 应包含：

1. **Status**：Proposed / Accepted / Superseded（以及被哪个 ADR 替代）。
2. **Context**：当时解决的问题与约束。
3. **Decision**：仅写已确认的具体选择，不擅自扩展为未讨论的 API。
4. **Consequences**：收益、代价、需更新的文档、迁移和验证任务。
5. **Open Questions**：尚未确定的部分。

ADR 采用 `NNNN-english-kebab-case.md` 命名。文档文件名英文；正文中文优先。

## 规则

- ADR Accepted 表示**决策已确认**，不等于相应组件已实现。
- 如后续决策代替旧决策，增加新的 ADR，旧 ADR 状态改为 Superseded，并互相链接。
- 不应悄悄把旧方案和新方案拼接在一起；以最新 Accepted 决策为优先。
