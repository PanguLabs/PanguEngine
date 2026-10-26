# Pangu 工具链与元数据工具

> 状态：Partially Frozen（通用工具链契约与平台编译器选型已确定；完整 Toolchain/ABI/Meta 集成待定）
> 更新：2026-10-27
> 决策：[ADR-0004](../decisions/0004-platform-clang-toolchains.md)

## 统一约束

1. 所有目标平台使用 **C++23 + Clang**，禁用 C++ Exception；不得使用 `libstdc++`。
2. **Windows 使用 MSVC STL**；Linux、macOS、Android、iOS 使用与目标工具链及 SDK/NDK（适用时）配套的 **`libc++`**。
3. 构建底座为 **CMake 4.0+**；不 Fork `llvm-project`。有关 CMake Target、Preset、Toolchain 和构建依赖的契约见 [构建系统](build-system.md)。
4. **编译器版本按平台选择**，不是全平台统一 LLVM23；配置阶段必须验证编译器类型、版本及目标 Toolchain 来源，不得在 Android 上使用通用 LLVM23 检查。
5. 编译器与标准库/SDK/NDK 的基础选择由 CMake Preset/Toolchain 统一完成（Linux 与 macOS 使用专用 Toolchain，iOS 使用既有 Device Toolchain，Android 使用 NDK Toolchain，Windows 使用 Clang/MSVC STL Preset）；普通 ThirdParty C++ Target 不需要链接 `PanguBuildOptions` 才获得这一配置。
6. 同一进程中的 Module 必须满足兼容的 ABI Profile 和所有权契约；应从实际构建记录编译器版本、Target Triple、标准库、SDK/NDK、异常与 Runtime 配置。精确 Fingerprint 规则见 [ABI Profile / Fingerprint v1](abi-profile-fingerprint.md)（设计协议已冻结，五平台 STATIC 构建内采集已实现并验收；最终 EXE/SHARED Loader 待实现）。
7. Pangu 的扩展属性使用 `pga` 命名空间，反射标记不使用 `PG_CLASS`、`PG_PROPERTY` 等宏；属性和代码生成的有效性必须按实际目标编译器验证，详见 [Reflection / Meta](../core/reflection-meta.md)。

## 目标平台编译器选择

| 平台 | 编译器约束 | C++ 标准库 | 实施细节 |
| --- | --- | --- | --- |
| Windows | LLVM Clang 23.x | MSVC STL | [平台支持](platform.md) |
| Linux | LLVM Clang 23.x | libc++ | [平台支持](platform.md) |
| macOS | MacPorts LLVM Clang 23.x | libc++ | [Apple 工具链](apple-toolchain.md) |
| Android | **所选 NDK 自带 Clang**（不限定 LLVM23） | NDK libc++ | [Android 工具链](android-toolchain.md) |
| iOS | MacPorts LLVM Clang 23.x（默认；AppleClang 仅用于兼容性对照） | iPhoneOS SDK libc++ | [Apple 工具链](apple-toolchain.md) |

对 Windows、Linux、macOS、iOS：CMake 校验 `CMAKE_CXX_COMPILER_ID == Clang` 与 `23.0 <= CMAKE_CXX_COMPILER_VERSION < 24.0`；macOS / iOS 还需显式选择 MacPorts 工具链。Android：校验使用选定 NDK 的 Clang，并通过实际 C++23 编译验收；不单凭版本号推断兼容性。

**平台版本、路径、SDK/NDK、具体命令及实测结果不在此重复维护**：

- Android NDK、Gradle、CMake、ABI 和验证记录见 [Android 工具链](android-toolchain.md)。
- MacPorts、macOS、iPhoneOS SDK、编译链接与部署验证见 [Apple 工具链](apple-toolchain.md)。
- 目标环境和支持程度见 [平台支持基线](platform.md)；“目标支持”不代表已完成完整引擎构建。

## PanguClang / PanguMeta 职责（尚未冻结具体 API）

| 职责 | 预期结果 |
| --- | --- |
| Clang 语义提取 | 读取 C++ 类型、成员、属性和源位置 |
| Meta 处理 | 校验和规范元数据，生成注册、序列化等辅助代码 |
| 构建集成 | 将工具产物纳入 CMake Target、增量依赖 |
| ABI 采集 | 为 Module 及其 STATIC/SHARED 库产物记录真实编译配置；不建立独立 BinaryImage 架构层 |
| 诊断反馈 | 在源代码位置报告属性、类型和配置错误 |

目前不预先冻结 PanguClang 是 Clang Plugin、LibTooling 还是独立程序，也不把 `[[pga::...]]` 视为未经验证即可被上游 Clang 原生识别的属性。Meta 工具可以使用 LLVM23，但应验证其产物与 Android NDK 自带 Clang 之间的属性/语言兼容性。

## 尚未冻结

现有 CMake Preset/Toolchain 已完成最小构建接线；尚未冻结编译器补丁版本与 SDK 升级政策、平台 ABI/Runtime 参数、PanguClang/PanguMeta 的具体边界、属性兼容性、CI 及完整多平台验证。细节放入其权威专题文档，本文件只记录跨平台共同契约。
