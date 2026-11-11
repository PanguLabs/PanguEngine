# Pangu Plugin / Module 架构基线

> 状态：Partially Frozen（源码 Plugin 身份/发现规则、单 Module 单库、Target 命名、Service 与生命周期已冻结；最小 PanguBuildTool/Project 接线已实现，ModuleManager/原生 Loader 已实现；Public ModuleDescriptor API/ABI 未冻结，平台按实际验收记录）
> 更新：2026-11-06
> 决策：[ADR-0005：单 Module 单库](../decisions/0005-one-module-one-library.md)、[ADR-0006：三种 Service 模式](../decisions/0006-three-service-modes.md)、[ADR-0008：Module 注册与生命周期](../decisions/0008-module-registration-lifetime.md)
> 参考：[EngineHost](engine-host.md)、[Service](service.md)、[ABI](../foundation/abi.md)、[内存所有权](memory-management.md)、[构建系统](../foundation/build-system.md)、[ADR-0014 源码插件](../decisions/0014-source-plugins-project-build.md)

## 目标

Plugin / Module 体系要兼顾静态构建与动态扩展、Game 与 Editor 运行场景、ThirdParty 集成、安装分发，以及严格的对象/任务/资源生命周期。

借鉴 UE5 的 Plugin/Module 体系和 O3DE Gem 的思想，但按 PanguEngine 的 C++23、Clang、CMake 与无异常要求独立设计，不照搬其实现细节。

## 已确定的概念边界

| 概念 | 主要职责 | 不能混淆为 |
| --- | --- | --- |
| **EngineHost** | 宿主运行上下文与服务/生命周期协调 | Editor 专属的 Runtime |
| **Plugin** | Engine/Project 源码发现、显式启用与交付的能力集合 | 单个动态库文件或独立预编译 SDK |
| **Module** | 逻辑代码单元；唯一 CMake Library Target，产出一个主要 STATIC / SHARED 库 | 一项 Service 或单个 C++ Translation Unit |
| **Manifest / Descriptor** | `Plugin.toml` 只描述构建期插件身份；ModuleDescriptor 描述运行时 Module | 两者共用同一编译描述或加载协议 |

一个 Plugin 可以包含多个 Module，也可以只有 Assets 而没有代码 Module。**一个 Module 是一个构建单元，不是只有一个 .cpp 文件**；可以编译多个 Translation Unit。模块内部不再增设 ExtensionDescriptor、ExtensionManager 或独立 BinaryImage 抽象。

## 单 Module 单库构建规则（Frozen）

1. 每个 Pangu Module **MUST 对应唯一一个实际 CMake Library Target**，类型为 `STATIC` 或 `SHARED`，只生成一个主要静态库或动态库。
2. 同一 Module 的源码、公开/私有依赖和 PanguMetaTool 生成的注册代码统一附着到这个目标；不得把一个 Module 拆成多个 Module Library Target，也不得把不同逻辑 Module 混为同一个 Module Target。
3. CMake `ALIAS` 不是第二个真实目标；Windows SHARED 随附的 import `.lib`、PDB 等是辅助产物，不属于第二个 Module。
4. ThirdParty/工具/宿主可以有普通 CMake Target，但它们不会因此自动变为某个 Module 的额外 Library Target；Plugin 本身不要求生成 CMake Library。
5. 全部逻辑 Pangu Module 按统一模式构建，Monolithic 归档最终进入 Host，不嵌入另一个逻辑 SHARED Module；Modular 每 Module 独立动态库。普通 ThirdParty 不受模式限制，回调和释放器仍须受实际代码驻留保护。
6. 本规则冻结 Module 构建映射及下述 CMake Target 命名空间规则，**不冻结** `pg_add_module()`、物理库文件名、Manifest、PanguMetaTool 注册入口或工具链平台加载函数。

## Module 与 ThirdParty 的构建边界（Frozen）

**CMake Target 是构建依赖节点，Pangu Module 是引擎逻辑代码单元；前者不自动获得后者的运行时身份。**

- `Pangu::` **MUST 仅用于真实 Pangu Module Target 的别名**（例如 `Pangu::Foundation`、`Pangu::ECS`）。每个 Module 仍对应唯一实际 `STATIC` / `SHARED` Library Target。
- `ThirdParty::` **MUST 用于项目对外提供的第三方依赖 Target 名称**（例如 `ThirdParty::NlohmannJson`、`ThirdParty::Asio`）；允许原生 CMake `ALIAS` 指向上游 Target，不为命名创建多余库。第三方 SDK 可暴露多个 Target。
- `PanguBuildOptions` 等自有构建策略、工具与辅助 Target 使用**无 `Pangu::` 前缀**的普通名称；它们不是 Pangu Module。**不保留旧 `Pangu::BuildOptions` 或 `Pangu::NlohmannJson` 别名。**
- 编译器、标准库、SDK/NDK 的基础选择由 CMake Toolchain/Preset 统一提供；`PanguBuildOptions` 为引擎代码施加 C++23、无异常和警告策略，ThirdParty 不调用 `pangu_configure_module()`。
- 第三方依赖只需进入 CMake 图（`INTERFACE` / `STATIC` / `SHARED` / `IMPORTED` 或上游 Target），不引入 External Module、ThirdParty Manager、统一 Package Adapter 或构建 Wrapper。

第三方库可直接被 Module 使用，也可在现有功能 Module 内适配；需要独立编译/平台适配时建立普通 CMake Target。
**只有当能力本身形成独立的引擎功能、Module 身份或生命周期边界时，才建立新的 Pangu Module**，不因依赖库数量而增设 Module，也不强制每个 Module 都提供 Service。

普通 ThirdParty 的动态库驻留仍需由实际链接/装载关系保证：若静态库代码被编入可卸载 Gameplay DLL，回调、线程、对象析构和借用必须在该 DLL 卸载前安全结束；仅为第三方库创建 Pangu Module 不自动解决 Hot Swap。详见 [ThirdParty 接入](../foundation/third-party.md) 和 [构建系统](../foundation/build-system.md)。

## 三种 Service 模式（Frozen）

- **Concrete Service**：具体类型和实现静态可知；由 Host / Context / World Owner 强类型持有/访问，通常不需要动态 Registry。
- **Contract Service**：调用者依赖独立于 ModuleId 的 Contract；构建期绑定优先，运行时 Provider 通过 Scoped Registry 绑定，短期 `ServiceRef<T>` 保护实例与代码；同一 Scope 的单 Provider Contract 默认只允许一个有效实现。
- **Registry Service**：已有业务 Module 提供 Importer / Serializer / Inspector 等 Registry，多个 Module 向其贡献实现，按 Key 查询或枚举；Registry 自身也可以是 Concrete Service，Contribution 不要求是独立 Service 实例。

三种模式是 Module 提供能力的不同方式，**不新增 Module 类型、不引入 `ExtensionDescriptor` / `ExtensionManager`，不要求统一 `IService` 基类**；详见 [Service 规范](service.md)。Module 注册能力与 Scope 内 Service 实例的创建/Ready 阶段必须区分。

## 已确定的架构约束

1. **区分 Plugin 与 Module**：Plugin 由 Engine/Project 固定源码根发现并显式启用，Module 负责逻辑代码单元、构建消费依赖与运行生命周期；不支持独立预编译 Pangu 原生插件接纳。
2. **Module 生命周期受控**：不是成功载入镜像就等于初始化或具备可运行服务。
3. **依赖先于使用**：启动、停止和卸载必须考虑 Module 间引用以及外部持有的资源、任务、回调。
4. **安全卸载优先**：发现仍有使用者时拒绝或推迟卸载，不允许忽略悬空引用；动态库句柄与 CodePin 由现有 Module 机制维护，不建立独立 BinaryImageManager。
5. **统一链接模式**：依据 [ADR-0015](../decisions/0015-unified-link-modes-module-manager.md)，同图 `PANGU_LINK_MODE=Monolithic|Modular` 统一全部 Module 为 STATIC/SHARED，桌面默认 Modular、移动默认 Monolithic，父 Project 控制 Engine 子目录；ThirdParty 按自身用途。不建同 Module 双变体或同库 Catalog。
6. **统一 C++23 Module 入口，无强制 C Bootstrap**：STATIC Catalog 和 SHARED Query 都返回同一种 C++23 ModuleDefinition，SHARED 仅保留固定 `extern "C"` 符号用于定位；不需要独立 C POD Discovery 或人工维护的 C 注册桥。同树源码构建不运行 AbiProfile，独立 Pangu 插件 ABI Preflight 路线已由 ADR-0014 终止；运行时入口与 CodePin 仍须校验。
7. **ABI 边界**：受控同树 C++ 构建可以在满足所有权与生命周期契约下交换兼容 STL 类型，不运行 AbiProfile；普通第三方预编译库按自身 Toolchain/ABI 契约审查，不采用独立 Pangu Plugin 的过时 Preflight 流程。历史 strict-v1 Fingerprint 不再是普通构建协议。
8. **构建与发现分离**：`Plugin.toml` 是固定两根配置期身份发现的必需文件；`CMakeLists.txt` 是唯一构建依赖权威。所有候选 Manifest 均校验；`PANGU_ENABLED_PLUGINS` 显式启用且 `EXCLUDE_FROM_ALL` 不自动编译。实际编译由消费者 Target 决定；运行时显式候选接纳/注册/激活由 ModuleManager 协调，完整产品清单另行设计。
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

## Manifest、Descriptor 的冻结边界与剩余问题

| 主题 | 当前结论 |
| --- | --- |
| `Plugin.toml` | 源码 Plugin 必需；`[plugin].id` 全局唯一，形如 `pangu.physics` 的受限小写 ID，`name` 为可选字符串，`version` 为可选 SemVer 2.0.0；ADR-0014 的严格解析、双根发现和显式启用已实现，见[验收](../../docs/superpowers/plans/2026-11-04-source-plugins-project-build-validation.md) |
| `Module.toml` | **不再视为强制文件**；CMake 采用后需重新评估 |
| `CMakeLists.txt` | 负责构建表达，不自动替代所有运行时 Manifest |
| `ModuleDescriptor` | 可由 PanguMetaTool 生成必要的模块运行时元数据；结构和序列化未冻结，不引入 ExtensionDescriptor |
| Lifecycle / Lifetime 字段 | 曾讨论 `Durable` / `Mutable` 等词，未在本文件中宣称正式字段已冻结 |
| 发现与安装 | 仅 Engine `Plugins/`、Project `Plugins/` 两根直接子目录；所有候选即使未启用也校验身份、重复与 Manifest 错误；不在配置期求解插件依赖/版本或联网；安装包及运行时启停尚未冻结 |
| Module/库映射 | **已冻结为一个 Module 对应一个 STATIC/SHARED Library Target 和一个主要库产物**；动态库符号发现、代码驻留实现待冻结 |
| Hot Reload | 安全切换、实例迁移和失败回滚待冻结 |

## 非目标

本文**不规定**具体 Plugin API、初始化函数签名、模块描述文件字段、Plugin 分发包后缀或动态库命名方式。上述已冻结的设计约束不表示相应功能已经实现或通过测试。

### 后续冻结顺序

[Module Registration & Lifetime v1](module-registration-lifetime.md) 的**注册、依赖、状态、CodePin、回滚语义已 Frozen**；依 [ADR-0010](../decisions/0010-unified-cpp-module-entry.md) **STATIC/SHARED 统一 C++23 ModuleDefinition/Registration，SHARED 以稳定 `extern "C"` 导出名定位 C++ Query**，不需要独立 C POD。ABI 普通构建范围见 ADR-0013；源码 Plugin/独立 Project 政策由 [ADR-0014](../decisions/0014-source-plugins-project-build.md) 冻结，不再实施独立 Pangu Plugin Preflight；当前 ModuleManager、PanguModuleQueryV1、公共导出与原生 Loader 已实现，Public API/ABI 仍未冻结；实际矩阵见[验收](../../docs/superpowers/plans/2026-11-06-module-manager-link-modes-validation.md)。

[Module API v1](module-api.md) 已按 [ADR-0011](../decisions/0011-module-api-behavior-contract.md) 冻结身份/版本、依赖绑定、Scope 局部回滚和操作后置条件；同库 Catalog 已由 ADR-0015 替代为单库单 Module，并新增常驻失败整体 Shutdown 例外；Public API/ABI 仍待验证，不由该行为冻结替代实现验收。

优先完成现有 Module 注册契约 → 三种 Service 的绑定/撤销 → CMake 与 PanguMetaTool 接入 → Host 生命周期协调和 CodePin → Plugin 分发及 Hot Reload，再据此补充可验证 C++23 接口。
