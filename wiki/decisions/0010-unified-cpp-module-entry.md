# ADR-0010：STATIC / SHARED 统一 C++23 Module 发现与注册接口

> Status: Accepted（发现/注册技术路线；ABI 预检适用范围由 [ADR-0013](0013-native-build-abi-boundary.md) 修订）
> 日期：2026-10-21
> 权威接口规范：[ModuleDescriptor & Registration API v1](../core/module-api.md)
> 依赖：[ADR-0008 Module Registration & Lifetime](0008-module-registration-lifetime.md)、历史 [ADR-0009](0009-abi-profile-fingerprint.md)、当前边界 [ADR-0013](0013-native-build-abi-boundary.md)
> **修订范围**：本 ADR 仍冻结统一 C++23 Query/Registration、稳定导出名定位与代码驻留语义；STATIC 的 v1 最终 Profile 校验和“所有 SHARED 一律 v1 预检”由 ADR-0013 替代。

## Context

先前 Module Registration & Lifetime v1 为 SHARED 规定窄 C Discovery → ABI 校验 → 原生 C++ Registration。本 ADR 决定消除独立 C POD Discovery 层，使 STATIC/SHARED 共享统一 C++23 ModuleDefinition/Registration。后续 ADR-0013 又把 ABI 校验职责从普通同树构建收缩到独立交付二进制边界。

## Decision

1. **统一数据和行为**：STATIC/SHARED 共享原生 C++23 `pg::module::ModuleDefinition`、`ModuleDescriptor`、`RegistrationContext` 与 `RegisterModuleFn` 语义。Module 注册是可回滚事务，注册成功不代表 Service Ready；不要求统一 `IModule` 虚基类、`ExtensionDescriptor` 或 `BinaryImage`。
2. **STATIC**：CMake/PanguMeta 生成显式 Catalog，通过按 Module 唯一化的 C++ 查询函数指针直接引用 ModuleDefinition；不依赖全局静态初始化顺序。**ADR-0013 修订**：同树 STATIC 最终组合不再要求额外 v1 Profile/逐对象校验。
3. **SHARED**：使用一个版本化稳定名称的导出符号（候选 `PanguModuleQueryV1`），以 `extern "C"` 保证可被 `GetProcAddress/dlsym` 按名称定位；返回原生 C++23 `ModuleDefinition` 指针。C Language Linkage 只用于符号定位，不等于稳定 C ABI。
4. **不用独立 C Discovery**：取消 `PgModuleDiscoveryV1` / `PgModuleDependencyV1`、C 状态码转换和 `void*` RegistrationContext Bridge。发现后使用同一 C++ RegisterModuleFn 执行事务注册。
5. **独立交付插件的 Preflight**：只有单独交付给已有宿主的原生 C++ 插件，必须在 OS Load 和首次 C++ Query 前基于与实际加载产物可信绑定的非执行元数据完成目标 ABI、协议和 Public Module API Contract 预检；无法确认时 Fail Closed。与宿主同树、同配置并配套交付的插件依 ADR-0013 免 AbiProfile 预检。
6. **装载后复核**：对需要 Preflight 的独立插件，OS Load 成功后才获取稳定导出符号并调用 Query；返回定义继续核对协议、ModuleId、依赖及接口契约与预检材料的一致性，成功后才能注册。配套插件仍遵守入口有效性、ModuleId、依赖、注册和生命周期检查，但不因本条恢复 AbiProfile。
7. **代码驻留**：由 ModuleManager 管理实际 OS 库句柄，ModuleCodePin 保护代码，ServiceRef/Borrow 保证实例；注册失败回滚、Quiesce/Drain、Busy 等仍遵守 ADR-0008。一个 Module 仍只对应一个 STATIC/SHARED Target。
8. **约束**：本 ADR 不再规定活动 AbiProfile 格式。历史 v1 Schema/Canonical/Fingerprint 由 ADR-0009 记录；当前生产适用范围由 ADR-0013 决定。预检不替代供应链安全或沙箱。

## Consequences

- STATIC/SHARED 继续共享一套原生 C++23 Descriptor/Registration 协议。
- `extern "C"` 继续仅用于固定导出符号名，不推行通用 C Bootstrap。
- 同树/配套交付插件不再因动态加载方式支付 AbiProfile 成本；独立交付插件仍保留装载前可信接纳边界。
- 可信产物绑定、TOCTOU、Public Module API Contract 和真实生产 Loader 仍待后续设计。
- 不恢复 BinaryImage 层，也不照搬 O3DE/UE 的 Module 基类或构建器。

## 验收与非冻结事项

STATIC Catalog 的根引用、注册顺序和代码驻留可在同树构建中直接验收。独立交付 SHARED 的可信 Preflight、实际产物绑定、OS Load/Query 和卸载需在出现真实独立消费者后单独验收；同树固定夹具不能冒充该证明。

**尚未冻结**：精确导出符号名称与调用约定、ModuleDefinition/ModuleDescriptor 的 C++ 类型布局、Public Module API Contract/Digest、独立交付元数据格式、可信绑定与 TOCTOU 技术、Plugin Manifest 和完整 Hot Reload。

后续更新：[ADR-0011](0011-module-api-behavior-contract.md) 冻结 Module API 行为契约；[ADR-0013](0013-native-build-abi-boundary.md) 冻结构建与独立交付的 ABI 边界。
