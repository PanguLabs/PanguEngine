# ADR-0004：按平台配置 Clang 编译器，统一使用 C++23

> Status: Accepted
> 日期：2026-10-20
> 规范：[统一工具链契约](../foundation/toolchain.md)
> 平台实现：[Android](../foundation/android-toolchain.md)、[Apple](../foundation/apple-toolchain.md)

## Context

PanguEngine 需要可预测的 C++23、Clang 语义处理、`pga` 元数据和 Module ABI。曾考虑强制五个平台全部使用上游 LLVM23，以最大程度统一 AST 和代码生成行为。

实测表明，Android NDK 的 Clang 与其 Sysroot、compiler-rt、libc++ 和链接工具集成紧密，虽然其主版本不同，却可以直接满足 C++23。强制 Android 更换独立 LLVM23 会引入额外运行库和构建适配成本。Apple 平台的 MacPorts LLVM Clang23 已具备原生 macOS 编译能力，配合独立的 iPhoneOS SDK 也可以成功链接 iOS ARM64 动态库。

因此统一**语言标准与 ABI 契约**，允许**目标平台选择受控且已验证的 Clang 发行版**，比要求五个平台 Clang 主版本一致更稳健。

## Decision

1. 所有平台统一使用 **C++23 + Clang**、禁用 C++ Exception，不使用 `libstdc++`，不 Fork LLVM，CMake 最低为 4.0。
2. Windows、Linux、macOS 采用 LLVM Clang23 系列；macOS 明确使用 MacPorts 而非系统默认 AppleClang。
3. Android 使用选定 NDK **自带的 Clang** 和官方工具链，而不是为统一 LLVM23 强行替换其编译前端。
4. iOS 默认使用 MacPorts LLVM Clang23 + iPhoneOS SDK；AppleClang 作为独立兼容性对照。
5. 构建时按目标平台验证实际 Clang 来源、版本和 C++23 能力；ABI Profile 必须体现编译器、目标、标准库、SDK/NDK 与运行时等信息。

**精确的版本选择矩阵、校验约束均以 [toolchain.md](../foundation/toolchain.md) 为准**；NDK/Apple SDK 路径、构建参数及验证结果只在各平台专题文档维护，不在 ADR 复制。

## Consequences

- Android 更换 NDK 仍需重新验证 C++23、Clang 属性/生成产物、标准库和 Module ABI。
- PanguClang/PanguMeta 可以按 LLVM23 开发，但必须验证生成结果与 Android 实际 NDK Clang 的兼容性。
- 不同目标平台使用不同 Clang 主版本，不意味着**同一进程**的 Module 可以混用不兼容 ABI。
- 平台探针通过不代表引擎集成、移动端签名、打包或真机运行已完成。

## Open Questions

正式 Toolchain/CMake Preset、SDK/NDK 更新策略、ABI Fingerprint、Android App 打包、iOS 设备运行/Simulator、Meta 兼容性及 CI 矩阵。
