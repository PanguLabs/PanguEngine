# ADR-0008：统一 Module 注册与生命周期语义

> Status: Accepted（仅设计语义；不代表 API/实现完成）
> 日期：2026-10-21
> 权威规范：[Module Registration & Lifetime v1](../core/module-registration-lifetime.md)
> 基于：[ADR-0001 CMake](0001-adopt-cmake.md)、[ADR-0003 Service + Context](0003-no-subsystem-service-context.md)、[ADR-0005 单 Module 单库](0005-one-module-one-library.md)、[ADR-0006 三种 Service](0006-three-service-modes.md)、[ADR-0007 错误与诊断](0007-error-diagnostics.md)

## Context

此前已确定 Plugin / Module 分离、一个 Module 对应一个 CMake STATIC/SHARED 库、三种 Service 模式及安全卸载，但缺少贯穿静态/动态发现、注册、依赖、作用域可见性、回滚和代码驻留的统一契约。若让 EngineHost、PanguMeta 或业务 Registry 各自设计这套机制，将重复管理生命周期，且难以安全卸载。

## Decision

1. 一个 Module 使用稳定且与 Plugin/Service/Error 分离的逻辑身份；v1 在受管装载域同时至多有一个生效版本，重复装载明确失败。实际 ID 编码和版本协议另定。
2. STATIC 使用构建阶段显式生成的 Catalog；SHARED 采用窄版本化动态发现协议，先验证协议/ABI，再进入受控 C++ 注册。共享**语义注册流程**，不强求相同的物理发现入口；不强制全引擎 C Bootstrap。
3. Module 注册与 Scope 内 Service 实例 Ready 分离；静态 Concrete Service 不必进入动态 Registry。多 Scope 的贡献项在所属 Scope 安装，不用进程全局默认实例。
4. 注册采用可回滚事务：单目标 Scope/Registry 的发布对业务观察者全成或全败，跨 Scope 装配通过 Host 可见性门槛协调，不虚构全局原子事务。
5. Module 依赖与 Service 依赖是两张不同的图；绑定后必须验证有效的生命周期约束及依赖闭包。缺失、冲突、环路、错误 Scope 方向要在发布/激活前拒绝。
6. 显式区分发现/校验、加载、注册、业务活跃、Quiesce、Drain、销毁/注销及卸载等阶段；名称与 Enum 不冻结。失败按已完成阶段逆序回滚；保留主错误及有界失败报告。
7. Module 机制发放的 CodePin 保护**实际代码承载者**；ServiceRef/Borrow 保护实例。Quiesce 关闭新业务入口，但现存借用及内部清理所需的驻留/依赖权限保持有效。Busy/Timeout 不是卸载成功。
8. 不引入 BinaryImage 架构层、ExtensionDescriptor/Manager、SubsystemManager 或万能 IService。单 Module 单 Library 及已有 ABI/Memory/Service/Error 决策保持不变。

## Consequences

- PanguMeta、CMake、EngineHost 和 Service Registry 可以围绕同一套 Module 注册/撤销语义构建接口，不再重复发明生命周期体系。
- 动态库装载之前需要尽可能完成外部元数据预检，但 LoadLibrary/dlopen 可能先运行库初始化代码，装载后检查**不能声称阻止此前所有代码执行**；外部不可信二进制的安全性需额外机制。
- STATIC 归档不能运行时卸载；当静态代码最终位于可卸载共享库时，CodePin 必须保护那个实际承载库。
- 注册事务的并发可见性、借用和在途回调撤销需在 API/实现阶段针对性测试。

## Open Questions

ModuleId/Descriptor 的精确结构、动态入口布局和符号名、ABI Profile/Fingerprint 算法、ModuleManager/CodePin/Token Public API、静态 Catalog 生成、同步算法、完整 Hot Reload 与 Plugin Manifest/Packaging，以及五平台运行验证。

后续更新：ABI Profile/Fingerprint 的 Schema、序列化和算法已由 [ADR-0009](0009-abi-profile-fingerprint.md) 冻结；上列相关待决项由该决策接续，具体 API、采集工具与 Loader 接入仍待实现。

本 ADR **不宣称已经实现** ModuleManager、动态装载或跨平台测试，也不冻结此处列出的待决项。
