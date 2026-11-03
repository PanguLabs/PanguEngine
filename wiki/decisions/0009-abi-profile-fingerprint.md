# ADR-0009：冻结 ABI Profile / Fingerprint v1

> Status: Superseded（生产适用范围由 [ADR-0013](0013-native-build-abi-boundary.md) 替代；v1 格式作为历史协议资料保留）
> 日期：2026-10-21
> 历史规范：[ABI Profile / Fingerprint v1](../foundation/abi-profile-fingerprint.md)
> SHARED 入口形式曾由 [ADR-0010](0010-unified-cpp-module-entry.md) 修订；当前生产边界以 ADR-0013 为准
> 基于：[ABI 基线](../foundation/abi.md)、[工具链决策](0004-platform-clang-toolchains.md)、[Module 注册与生命周期](0008-module-registration-lifetime.md)

## Context

同一个受控进程允许 C++ Module 跨库交换 STL 类型，但不能只凭 Clang 主版本或“同一种 STL”判定兼容。该阶段为此定义了 Profile/Fingerprint v1，并把逐 TU 证明、STATIC 最终组合校验和 SHARED 预检接入构建。后续实践证明这种生产适用范围给普通增量构建带来过高成本，故由 ADR-0013 收缩。

## Decision（历史 v1）

1. v1 定义固定七个顶层字段的 Profile Schema：`schema=1`、`policy=strict-v1`、`target`、`compiler`、`language`、`stdlib`、`runtime`。
2. 使用 RFC 8785（JCS）规范化 JSON 为 UTF-8 字节；Profile 禁止任意额外字段，ABI 选项列表在输入验证时排序/去重检查。
3. Fingerprint = SHA-256(`ASCII("PANGU_ABI_PROFILE_V1") + 0x00 + canonical UTF-8 bytes`)，完整 32 字节/64 字符小写十六进制，不截断。
4. v1 采用严格配置等值兼容策略；工具链/标准库 ABI、CRT、RTTI、目标 ABI、布局选项、SDK/NDK/Sysroot ABI 身份不匹配时拒绝。
5. **已被 ADR-0013 替代**：Profile/Fingerprint 不再要求覆盖同树普通构建的每个 ABI 关键 TU，也不再通过逐 TU 收据证明本地对象来源。未来独立交付边界的配置描述不得冒充 strict-v1。
6. **已被 ADR-0013 替代**：STATIC 最终组合不再强制执行 v1 Profile 校验；与宿主同树、同配置且配套交付的 SHARED 插件免 AbiProfile 预检。只有独立交付给已有宿主的原生插件继续保留 OS Load 前可信预检原则。
7. Fingerprint 不包含 BuildIdentity、ModuleId、PluginId；它不是签名，不证明 Public API/ODR、Allocator、Lifetime 或代码来源安全。此限制作为历史协议属性仍成立。

## Consequences

- Schema、Canonical、SHA-256 和测试向量保留，用于解释历史产物和决策，不再作为普通生产构建或 Tests 的活动协议。
- 旧 strict-v1 的逐 TU、收据、Collector/Guard/Collect 生产接线由 ADR-0013 明确退役；实现迁移另由对应实施计划完成。
- 独立交付 C++ 二进制仍需要兼容性与产物身份策略，但其下一版格式、可信绑定和 Loader 另行冻结。
- 不增加 BinaryImage 层，也不恢复自研 PanguBuild。

## Open Questions

独立交付二进制的最小配置格式、Public Module API Contract/Digest、可信产物绑定与 TOCTOU、SDK/Plugin Manifest 和生产 Loader，见 ADR-0013 的后续范围。v1 本身不再扩展。

**本 ADR 记录历史 v1 协议，不表示当前生产构建仍应运行 v1 工具链。**
