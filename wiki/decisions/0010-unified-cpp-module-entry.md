# ADR-0010：STATIC / SHARED 统一 C++23 Module 发现与注册接口

> Status: Accepted（Query/Registration 技术路线；独立 Pangu Plugin Preflight 由 [ADR-0014](0014-source-plugins-project-build.md) 替代）
> 日期：2026-10-21
> 权威接口规范：[ModuleDescriptor & Registration API v1](../core/module-api.md)
> 依赖：[ADR-0008 Module Registration & Lifetime](0008-module-registration-lifetime.md)、历史 [ADR-0009](0009-abi-profile-fingerprint.md)、原生构建 [ADR-0013](0013-native-build-abi-boundary.md)、源码交付 [ADR-0014](0014-source-plugins-project-build.md)
> **修订范围**：统一 C++23 Query/Registration、稳定导出名定位与代码驻留不变；STATIC v1 Profile 由 ADR-0013 取消；独立 Pangu Plugin 的可信 Preflight 路线由 ADR-0014 替代。

## Context

先前 Module Registration & Lifetime v1 为 SHARED 规定窄 C Discovery → ABI 校验 → 原生 C++ Registration。本 ADR 决定消除独立 C POD Discovery 层，使 STATIC/SHARED 共享统一 C++23 ModuleDefinition/Registration。后续 ADR-0013 又把 ABI 校验职责从普通同树构建收缩到独立交付二进制边界。

## Decision

1. **统一数据和行为**：STATIC/SHARED 共享原生 C++23 `pg::module::ModuleDefinition`、`ModuleDescriptor`、`RegistrationContext` 与 `RegisterModuleFn` 语义。Module 注册是可回滚事务，注册成功不代表 Service Ready；不要求统一 `IModule` 虚基类、`ExtensionDescriptor` 或 `BinaryImage`。
2. **STATIC**：CMake/PanguMetaTool 生成显式 Catalog，通过按 Module 唯一化的 C++ 查询函数指针直接引用 ModuleDefinition；不依赖全局静态初始化顺序。**ADR-0013 修订**：同树 STATIC 最终组合不再要求额外 v1 Profile/逐对象校验。
3. **SHARED**：使用一个版本化稳定名称的导出符号（候选 `PanguModuleQueryV1`），以 `extern "C"` 保证可被 `GetProcAddress/dlsym` 按名称定位；返回原生 C++23 `ModuleDefinition` 指针。C Language Linkage 只用于符号定位，不等于稳定 C ABI。
4. **不用独立 C Discovery**：取消 `PgModuleDiscoveryV1` / `PgModuleDependencyV1`、C 状态码转换和 `void*` RegistrationContext Bridge。发现后使用同一 C++ RegisterModuleFn 执行事务注册。
5. **独立交付插件的历史规定（已被替代）**：本 ADR 曾要求单独交付给已有宿主的 Pangu 原生 C++ 插件在 OS Load 之前完成可信 ABI/Public API Preflight；[ADR-0014](0014-source-plugins-project-build.md) 已结束此 Pangu 交付路线，不应据此恢复 AbiProfile、config-v2 或可信产物绑定工具。
6. **同树 SHARED 的校验**：OS Load 成功后才定位稳定导出符号并调用 Query；无论来源同树如何，都必须核对协议、ModuleId、依赖及必要接口契约，成功后才注册，失败则回滚。不恢复独立预检或 AbiProfile。
7. **代码驻留**：由 ModuleManager 管理实际 OS 库句柄，ModuleCodePin 保护代码，ServiceRef/Borrow 保证实例；注册失败回滚、Quiesce/Drain、Busy 等仍遵守 ADR-0008。一个 Module 仍只对应一个 STATIC/SHARED Target。
8. **约束**：本 ADR 不再规定活动 AbiProfile 格式。历史 v1 Schema/Canonical/Fingerprint 由 ADR-0009 记录；当前生产适用范围由 ADR-0013 决定。预检不替代供应链安全或沙箱。

## Consequences

- STATIC/SHARED 继续共享一套原生 C++23 Descriptor/Registration 协议。
- `extern "C"` 继续仅用于固定导出符号名，不推行通用 C Bootstrap。
- 同树/配套交付插件不再支付 AbiProfile 成本；独立 Pangu Plugin 二进制交付及其装载前 Preflight 已被 ADR-0014 从现行路线移除。
- Pangu 独立插件的可信产物绑定、TOCTOU 与 config-v2 不再是开发目标；实际生产 Loader 及运行时检查仍待后续实现。
- 不恢复 BinaryImage 层，也不照搬 O3DE/UE 的 Module 基类或构建器。

## 验收与非冻结事项

STATIC Catalog 的根引用、注册顺序和代码驻留可在同树构建中直接验收。SHARED 的 OS Load/Query、卸载仍需单独验收；显式部署不能被当成运行时激活。独立 Pangu Plugin 的可信 Preflight 不再纳入验收范围。

**尚未冻结**：完整的生产 Loader 与 Hot Reload、运行时插件激活清单；源码 `Plugin.toml` 的构建期身份格式已由 ADR-0014 决定，不等于运行时加载 Manifest。

后续更新：[ADR-0011](0011-module-api-behavior-contract.md) 冻结 Module API 行为契约；[ADR-0013](0013-native-build-abi-boundary.md) 冻结普通 CMake/Ninja 构建；[ADR-0014](0014-source-plugins-project-build.md) 冻结源码交付和独立 Project 边界。
