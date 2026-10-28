# Toolchain / ThirdParty 编译验证（2026-10-23）

本目录是长期维护的 **Toolchain / ThirdParty 编译与链接回归测试**，遵守 [Tests 维护规范](../README.md)。测试夹具中的 `Probe` 命名表示验证角色，Plan 完成后仍保留；这些夹具不构成正式 Runtime API，也不创建新的生产 Engine Module 身份。
本目录由 `Tests/CMakeLists.txt` 统一接入；只有 `BUILD_TESTING=ON` 时才配置测试。
`PANGU_VALIDATE_TOOLCHAIN=ON` 是测试开启时的默认值；如需仅禁用本目录的校验，可在 Preset 上覆盖为 OFF。
`BUILD_TESTING=OFF` 完整排除测试库、测试程序和宏审计目标，即使缓存中保留该子选项为 ON。
这些选项不调整真实 Toolchain / Pangu Target Policy；交叉编译开启测试时保留编译验证，跳过设备测试程序。
Android/iOS Preset 默认 `BUILD_TESTING=OFF`；需要编译验证时，分别使用
`cmake --preset android-arm64 -DBUILD_TESTING=ON` 或在 macOS 使用
`cmake --preset ios-arm64 -DBUILD_TESTING=ON`，再执行对应构建 Preset。

- `ThirdParty/CompiledProbe.cpp` 使用普通 STATIC Library `ThirdParty::CompiledProbe`，单独设定 C++23，
  **不调用** `pangu_configure_module()`，也不继承 `PanguBuildOptions`。
- `Module/PolicyProbe.cpp` 使用真实 STATIC Pangu Module `Pangu::ToolchainProbe`，
  验证其 C++23、无异常和平台 STL 约束。
- `LinkProbe.cpp` 在桌面宿主构建真实可执行程序，同时链接两个 STATIC Library Target，运行验证 `std::expected<int, int>` 值跨两库访问，CTest 名称 `ToolchainLibraryLink`。
- `AuditCompilerMacros.cmake` 是本目录的回归测试脚本，读取 CMake 实际生成的
  `compile_commands.json`；从正式 `PanguFoundation`、`PanguModule`、
  普通 ThirdParty、Pangu 探针共四个 Translation Unit 的**真实编译命令**中
  去掉输出/依赖文件参数，调用同一 Clang `-E -dM` 获取包含平台标准库后的宏。
- 在四个目标间对比 Clang Patch、C++ 标准、指针/宽字符宽度、STL 版本及 ABI Namespace、
  Iterator Debug Level、CRT 宏、RTTI、CPU 指令集能力、Android API 宏；
  对三个 Pangu 目标另外验证异常宏全部关闭。
- 编译时默认执行 `PanguAbiCompileAudit`（`ALL` 目标），生成
  对应 `<BuildRoot>/Abi/compile-evidence.json`（各平台 BuildRoot 见[构建系统](../../wiki/foundation/build-system.md)）。
  证据包含编译命令的 SHA-256、完整预定义宏文本 SHA-256，以及经筛选的 ABI 宏值。
  **这些是审计溯源摘要，不是最终 ABI Fingerprint。**
- `NegativeAudit.cmake` 向 *真实* 编译命令附加与基线状态相反的 RTTI 选项
  （`-frtti` 或 `-fno-rtti`），或附加 `-fexceptions`，要求 Clang 宏不一致/编译策略拒绝。
  每次负测预先写入旧 Evidence，验证失败后审计器会将其清理。
- `RttiBaselineAudit.cmake` 分别验证全局开启、关闭 RTTI 的一致配置能够通过审计，
  且两种基线下都能制造并拒绝真实的 RTTI 冲突。
  `MissingDatabaseAudit.cmake` 验证编译数据库缺失时同样清理旧 Evidence。

主机 CTest：`AbiCompileAudit`、`AbiCompileAuditRejectRTTI`、
`AbiCompileAuditRejectEXCEPTIONS`、`AbiCompileAuditRTTIBaselineON/OFF`、
`AbiCompileAuditMissingDatabase`；交叉 Android/iOS 由宿主构建阶段运行 CMake/Clang，
无需执行 Android ELF / iOS Mach-O。

**限制（不能夸大）**：当前审计覆盖四个*代表性 TU*，不是所有 TU；没有验证
Toolchain 文件/SDK/NDK/CRT 的来源链与链接期 Runtime，也没有构建冻结 Schema 的完整
`target/compiler/language/stdlib/runtime` Profile、签署产物或消除 TOCTOU。
例如 Clang 预定义的 `__ANDROID_API__` 可以是另一个宏的名称，
必须进一步结合实际 Toolchain 命令和 NDK 文件验证后才能认定数值。
`compile-evidence.json` 明确写有 `trusted: false`，不得据此放行 SHARED Loader。
