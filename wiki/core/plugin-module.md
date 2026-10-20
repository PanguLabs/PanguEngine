# Pangu Plugin / Module 架构基线

> 状态：Partially Frozen（职责分离、依赖安全和卸载契约已确定；Manifest/Descriptor/API 待冻结）
> 更新：2026-10-19
> 参考：[EngineHost](engine-host.md)、[ABI](../foundation/abi.md)、[内存所有权](memory-management.md)、[构建系统](../foundation/build-system.md)

## 目标

Plugin / Module 体系要兼顾静态构建与动态扩展、Game 与 Editor 运行场景、ThirdParty 集成、安装分发，以及严格的对象/任务/资源生命周期。

借鉴 UE5 的 Plugin/Module 体系和 O3DE Gem 的思想，但按 PanguEngine 的 C++23、Clang、CMake 与无异常要求独立设计，不照搬其实现细节。

## 已确定的概念边界

| 概念 | 主要职责 | 不能混淆为 |
| --- | --- | --- |
| **EngineHost** | 宿主运行上下文与服务/生命周期协调 | Editor 专属的 Runtime |
| **Plugin** | 可发现、启用、安装与分发的扩展/能力集合 | 单个动态库文件 |
| **Module** | 逻辑代码单元，具有依赖、注册和运行生命周期 | CMake Target 或动态库的一对一别名 |
| **Binary Image** | 供平台加载的代码载体，例如动态库/可执行镜像 | 一定且只包含一个 Module |
| **Manifest / Descriptor** | 描述配置、依赖或运行时元数据的载体 | 必须等于某个旧 TOML 方案 |

一个 Plugin 可以组织一个或多个 Module；Module 与动态镜像的映射仍需要独立建模。具体允许一个镜像含多少 Module、以及静态链接时如何表示，尚未形成最终 Schema。

## 已确定的架构约束

1. **区分 Plugin 与 Module**：Plugin 管理可交付能力与扩展安装发现，Module 负责逻辑代码单元和依赖/运行生命周期。
2. **Module 生命周期受控**：不是成功载入镜像就等于初始化或具备可运行服务。
3. **依赖先于使用**：启动、停止和卸载必须考虑 Module 间引用以及外部持有的资源、任务、回调。
4. **安全卸载优先**：发现仍有使用者时拒绝或推迟卸载，不允许忽略悬空引用。
5. **动态/静态部署兼容**：不把“Plugin”定义为只能由动态库交付的扩展。
6. **无强制 C Bootstrap**：不要求引擎一切入口都通过人工维护的 C 桥启动；存在明确 ABI 边界时可以使用 C 回调。
7. **ABI 受控**：受控 C++ 构建可以跨 Module 交换兼容 STL 类型，但必须保证 ABI Fingerprint/配置兼容及所有权有效。
8. **构建与发现分离**：CMake 描述构建关系，运行时的插件发现/安装可能仍需要 Manifest；两类信息不一定共用同一种文件。
9. **第三方依赖不强制 Package Adapter**：可通过 CMake 及相应 Module/ThirdParty 目标集成。

## Module 安全卸载的不变量

依据 [Memory Ownership Contract v1](memory-management.md)，卸载须满足以下顺序和条件：

1. 阻止新的业务调用、任务以及资源使用请求，保留清理必须的入口。
2. 等待执行中/已排队任务与调用结束，等待 GPU 相关使用达到完成条件。
3. 在 Module 代码有效时取消回调与注册，完成资源延迟销毁、对象析构、所有权/借用清理。
4. 在所有依赖 Allocator 的分配消失后销毁 Allocator 上下文，最终卸载代码镜像。

任何一步不能满足，都不得假装成功卸载。生命周期顺序不是某个已冻结的 Enum 定义，也不表示所有 Module 都必须实际动态卸载。

## 与 Event / Message / Command 的关系

历史讨论将 Event、Message、Command 纳入总体通信架构，其传递与订阅行为会影响 Module 生命周期。**三者的精确语义、线程调度、队列与错误传播尚未被本文件冻结**，不能从已确认的卸载约束推导出完整消息 API。

## Manifest、Descriptor 和格式的待决问题

| 主题 | 当前结论 |
| --- | --- |
| `Plugin.toml` | 曾采用的候选形式；最终字段与是否沿用均需正式确认 |
| `Module.toml` | **不再视为强制文件**；CMake 采用后需重新评估 |
| `CMakeLists.txt` | 负责构建表达，不自动替代所有运行时 Manifest |
| `ModuleDescriptor` | 需要说明作用域、加载策略、生命周期、依赖，但字段和序列化待冻结 |
| Lifecycle / Lifetime 字段 | 曾讨论 `Durable` / `Mutable` 等词，未在本文件中宣称正式字段已冻结 |
| 发现与安装 | 搜索路径、冲突处理、版本选择、分发协议、启用策略待冻结 |
| 动态镜像 | 一个镜像多个 Module 的实现机制、符号发现及注册表待冻结 |
| Hot Reload | 安全切换、实例迁移和失败回滚待冻结 |

## 非目标

本文**不规定**具体 Plugin API、初始化函数签名、模块描述文件字段、Plugin 分发包后缀、动态库命名方式或任何没有实现与测试证明的功能。

### 后续冻结顺序

优先完成 Module/Plugin Descriptor → Build / Image 映射 → 发现与依赖解析 → 生命周期状态机 → Host 协调 → Hot Reload/分发，再据此补充可验证 C++23 接口。
