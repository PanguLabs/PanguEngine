# Pangu Plugin / Module 架构基线

> 状态：Partially Frozen（职责分离、单 Module 单库、三种能力模式与安全卸载已冻结；Manifest/Descriptor/API 待冻结）
> 更新：2026-10-20
> 决策：[ADR-0005：单 Module 单库](../decisions/0005-one-module-one-library.md)、[ADR-0006：三种 Service 模式](../decisions/0006-three-service-modes.md)
> 参考：[EngineHost](engine-host.md)、[Service](service.md)、[ABI](../foundation/abi.md)、[内存所有权](memory-management.md)、[构建系统](../foundation/build-system.md)

## 目标

Plugin / Module 体系要兼顾静态构建与动态扩展、Game 与 Editor 运行场景、ThirdParty 集成、安装分发，以及严格的对象/任务/资源生命周期。

借鉴 UE5 的 Plugin/Module 体系和 O3DE Gem 的思想，但按 PanguEngine 的 C++23、Clang、CMake 与无异常要求独立设计，不照搬其实现细节。

## 已确定的概念边界

| 概念 | 主要职责 | 不能混淆为 |
| --- | --- | --- |
| **EngineHost** | 宿主运行上下文与服务/生命周期协调 | Editor 专属的 Runtime |
| **Plugin** | 可发现、启用、安装与分发的扩展/能力集合 | 单个动态库文件 |
| **Module** | 逻辑代码单元；唯一 CMake Library Target，产出一个主要 STATIC / SHARED 库 | 一项 Service 或单个 C++ Translation Unit |
| **Manifest / Descriptor** | 描述分发配置或必要运行时元数据 | 必须等于某个旧 TOML 方案 |

一个 Plugin 可以包含多个 Module，也可以只有 Assets 而没有代码 Module。**一个 Module 是一个构建单元，不是只有一个 .cpp 文件**；可以编译多个 Translation Unit。模块内部不再增设 ExtensionDescriptor、ExtensionManager 或独立 BinaryImage 抽象。

## 单 Module 单库构建规则（Frozen）

1. 每个 Pangu Module **MUST 对应唯一一个实际 CMake Library Target**，类型为 `STATIC` 或 `SHARED`，只生成一个主要静态库或动态库。
2. 同一 Module 的源码、公开/私有依赖和 PanguMeta 生成的注册代码统一附着到这个目标；不得把一个 Module 拆成多个 Module Library Target，也不得把不同逻辑 Module 混为同一个 Module Target。
3. CMake `ALIAS` 不是第二个真实目标；Windows SHARED 随附的 import `.lib`、PDB 等是辅助产物，不属于第二个 Module。
4. ThirdParty/工具/宿主可以有普通 CMake Target，但它们不会因此自动变为某个 Module 的额外 Library Target；Plugin 本身不要求生成 CMake Library。
5. STATIC Module 最终进入 EXE 或共享库，不存在独立的静态库运行时卸载；若静态代码进入可卸载动态库，CodePin **必须**保护真实承载代码的动态库。SHARED Module 由 Module 加载器直接管理库句柄。
6. 本规则冻结 Module 构建映射，**不冻结** `pg_add_module()`、库命名、Manifest、PanguMeta 注册入口或工具链平台加载函数。

## 三种 Service 模式（Frozen）

- **Concrete Service**：具体类型和实现静态可知；由 Host / Context / World Owner 强类型持有/访问，通常不需要动态 Registry。
- **Contract Service**：调用者依赖独立于 ModuleId 的 Contract；构建期绑定优先，运行时 Provider 通过 Scoped Registry 绑定，短期 `ServiceRef<T>` 保护实例与代码；同一 Scope 的单 Provider Contract 默认只允许一个有效实现。
- **Registry Service**：已有业务 Module 提供 Importer / Serializer / Inspector 等 Registry，多个 Module 向其贡献实现，按 Key 查询或枚举；Registry 自身也可以是 Concrete Service，Contribution 不要求是独立 Service 实例。

三种模式是 Module 提供能力的不同方式，**不新增 Module 类型、不引入 `ExtensionDescriptor` / `ExtensionManager`，不要求统一 `IService` 基类**；详见 [Service 规范](service.md)。Module 注册能力与 Scope 内 Service 实例的创建/Ready 阶段必须区分。

## 已确定的架构约束

1. **区分 Plugin 与 Module**：Plugin 管理可交付能力与扩展安装发现，Module 负责逻辑代码单元和依赖/运行生命周期。
2. **Module 生命周期受控**：不是成功载入镜像就等于初始化或具备可运行服务。
3. **依赖先于使用**：启动、停止和卸载必须考虑 Module 间引用以及外部持有的资源、任务、回调。
4. **安全卸载优先**：发现仍有使用者时拒绝或推迟卸载，不允许忽略悬空引用；动态库句柄与 CodePin 由现有 Module 机制维护，不建立独立 BinaryImageManager。
5. **动态/静态部署兼容**：不把“Plugin”定义为只能由动态库交付的扩展。
6. **无强制 C Bootstrap**：不要求引擎一切入口都通过人工维护的 C 桥启动；存在明确 ABI 边界时可以使用 C 回调。
7. **ABI 受控**：受控 C++ 构建可以跨 Module 交换兼容 STL 类型，但必须保证 ABI Fingerprint/配置兼容及所有权有效。
8. **构建与发现分离**：CMake 描述构建关系，运行时的插件发现/安装可能仍需要 Manifest；两类信息不一定共用同一种文件。
9. **第三方依赖不强制 Package Adapter**：可通过 CMake 及相应 Module/ThirdParty 目标集成。

## Module 安全卸载的不变量

依据 [Memory Ownership Contract v1](memory-management.md)，卸载须满足以下顺序和条件：

1. 阻止新的业务调用、任务以及资源使用请求，保留清理必须的入口。
2. 等待执行中/已排队任务与调用结束，等待 GPU 相关使用达到完成条件。
3. 在 Module 代码仍有效时撤销 Registry Contribution 与其他注册，完成延迟资源回收、Service 和对象析构、分配器所有权/借用清理。
4. 所有依赖 Allocator 的分配必须先消失，再销毁 Allocator 上下文；最后一个必要 CodePin 释放后，才允许卸载实际承载代码的动态库。

任何一步不能满足，都不得假装成功卸载。生命周期顺序不是某个已冻结的 Enum 定义，也不表示所有 Module 都必须实际动态卸载。

## 与 Event / Message / Command 的关系

历史讨论将 Event、Message、Command 纳入总体通信架构，其传递与订阅行为会影响 Module 生命周期。**三者的精确语义、线程调度、队列与错误传播尚未被本文件冻结**，不能从已确认的卸载约束推导出完整消息 API。

## Manifest、Descriptor 和格式的待决问题

| 主题 | 当前结论 |
| --- | --- |
| `Plugin.toml` | 曾采用的候选形式；最终字段与是否沿用均需正式确认 |
| `Module.toml` | **不再视为强制文件**；CMake 采用后需重新评估 |
| `CMakeLists.txt` | 负责构建表达，不自动替代所有运行时 Manifest |
| `ModuleDescriptor` | 可由 PanguMeta 生成必要的模块运行时元数据；结构和序列化未冻结，不引入 ExtensionDescriptor |
| Lifecycle / Lifetime 字段 | 曾讨论 `Durable` / `Mutable` 等词，未在本文件中宣称正式字段已冻结 |
| 发现与安装 | 搜索路径、冲突处理、版本选择、分发协议、启用策略待冻结 |
| Module/库映射 | **已冻结为一个 Module 对应一个 STATIC/SHARED Library Target 和一个主要库产物**；动态库符号发现、代码驻留实现待冻结 |
| Hot Reload | 安全切换、实例迁移和失败回滚待冻结 |

## 非目标

本文**不规定**具体 Plugin API、初始化函数签名、模块描述文件字段、Plugin 分发包后缀、动态库命名方式或任何没有实现与测试证明的功能。

### 后续冻结顺序

优先完成现有 Module 注册契约 → 三种 Service 的绑定/撤销 → CMake 与 PanguMeta 接入 → Host 生命周期协调和 CodePin → Plugin 分发及 Hot Reload，再据此补充可验证 C++23 接口。
