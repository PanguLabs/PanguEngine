# ADR-0009：冻结 ABI Profile / Fingerprint v1

> Status: Accepted（Schema、Canonical、哈希与兼容判定的设计协议；不代表平台实现完成）
> 日期：2026-10-21
> 权威规范：[ABI Profile / Fingerprint v1](../foundation/abi-profile-fingerprint.md)
> 基于：[ABI 基线](../foundation/abi.md)、[工具链决策](0004-platform-clang-toolchains.md)、[Module 注册与生命周期](0008-module-registration-lifetime.md)

## Context

同一个受控进程允许 C++ Module 跨库交换 STL 类型，但不能只凭 Clang 主版本或“同一种 STL”判定兼容。现有 ABI 基线要求 Profile/Fingerprint，却没有固定 Schema、序列化、哈希和拒绝策略；Module v1 的 STATIC Catalog/SHARED 发现也需要一致的构建与装载闸门。

## Decision

1. v1 定义固定七个顶层字段的 Profile Schema：`schema=1`、`policy=strict-v1`、`target`、`compiler`、`language`、`stdlib`、`runtime`。字段和 ABI 选项的完整规范见权威文档及机器可读 Schema；未知或缺少必须字段必须拒绝。
2. 使用 RFC 8785（JCS）规范化 JSON 为 UTF-8 字节；Profile 禁止任意额外字段，使用固定 ASCII 令牌，ABI 选项列表在输入验证时排序/去重检查。
3. Fingerprint = SHA-256(`ASCII("PANGU_ABI_PROFILE_V1") + 0x00 + canonical UTF-8 bytes`)，完整 32 字节/64 字符小写十六进制，不截断。规范测试向量与变异向量随 Wiki 保存。
4. v1 仅采用**严格配置等值**兼容策略；工具链/标准库 ABI、CRT、RTTI、目标 ABI、布局选项、SDK/NDK/Sysroot ABI 身份不匹配时拒绝 C++ 边界。Compiler Patch 或 SDK 身份变化在 v1 亦保守拒绝；需要经验证新兼容政策才允许放宽。
5. Profile 和 Fingerprint 来自**真实构建**，必须覆盖 Host 及 STATIC/SHARED Module 的受控 C++ 交互目标与 ABI 关键 TU 配置；不能由用户手填声明兼容。
6. SHARED 装载先预检兼容元数据，OS Load 后通过版本化窄 C Discovery 二次检查，只有成功后调用受控 C++ 注册入口；STATIC 在最终组合/装配时进行校验。`LoadLibrary/dlopen` 可能提前运行初始化代码，二次校验不能宣称阻止任意代码提前执行。
7. Fingerprint 不包含 BuildIdentity、ModuleId、PluginId；它不是签名，不证明 Public API/ODR、Allocator、Lifetime 或代码来源安全。Module 协议、公共接口契约及 ModuleCodePin/ServiceRef 仍单独检查。

## Consequences

- Canonical 输出和 Fingerprint 可跨构建环境用固定测试向量实现，不用维护自制 JSON Canonicalization。
- 严格比较可能拒绝事实上 ABI 兼容的 Compiler Patch、SDK Patch 或其他配置，但降低 v1 假阳性风险；未来兼容例外需独立 Policy 与跨平台测试。
- Windows MSVC STL Iterator Debug Level/CRT 与其它平台 libc++ ABI namespace/version 必须来自实际目标构建。
- v1 指纹成为 Build/Loader 的边界协议，不增加 BinaryImage 层或恢复自研 PanguBuild。

## Open Questions

AbiProfile/AbiFingerprint 的 C++ API 内存布局、生成工具、Target 每 TU 的选项采集、平台 SDK/Runtime 具体探针、SHARED 查询入口二进制布局与符号、可信预检元数据嵌入方式、Public ABI Surface Digest、Plugin Manifest、真实五平台验收。以上不属于本 ADR 的已冻结范围。

**本决策不等于已构建引擎 Module、完成动态装载或做完跨平台 ABI 实测。**
