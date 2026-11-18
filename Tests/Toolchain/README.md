# Toolchain / ThirdParty 编译与链接测试

本目录保留真实 C++23 编译和链接回归，不经过 AbiProfile，不生成逐 TU 收据，也不扫描 `compile_commands.json` 来判断编译失效。对应 [Tests 维护规范](../README.md) 与 [ADR-0013](../../wiki/decisions/0013-native-build-abi-boundary.md)。

平台检查的编译、最终链接、宿主检查与设备运行边界见[测试分类与运行契约](../../wiki/foundation/testing.md)。

`BUILD_TESTING=ON` 且 `PANGU_BUILD_ENGINE_TESTS=ON` 时始终配置 Toolchain；任一关闭则不接入 Engine 测试。交叉编译时只构建编译型探针，不运行设备程序。

- `ThirdParty/CompiledProbe.cpp`：真实 `ThirdParty::CompiledProbe` STATIC 库，直接使用平台 Clang、STL、C++23，不继承 Pangu Module 专属告警/异常选项。
- `Module/PolicyProbe.cpp`：真实 `PanguToolchainProbe` Module，服从 `PANGU_LINK_MODE`，编译期检查 C++23、无异常和平台 STL。
- `LinkProbe.cpp`：桌面构建 `PanguToolchainLinkTests`，实际链接两座库，CTest 名 `ToolchainLibraryLink`，运行时验证跨库 `std::expected<int, int>`；Modular 下显式导入 Probe 函数。
- `Flecs/CompileProbe.cpp`：`FlecsCompileProbe` 验证 `ThirdParty::Flecs` 的链接模式、公开宏和原生 C++23 消费。桌面生成可执行文件，交叉编译生成 STATIC 归档。

上述 Toolchain 测试的静态库、导入库、动态库及链接验证程序统一输出到所属宿主
`<BuildRoot>/Tests/Toolchain/`，不在 BuildRoot 顶层另建 `Probe` 目录。

桌面正式构建：

```sh
cmake --preset windows
cmake --build --preset windows-full
ctest --preset windows-full -R '^ToolchainLibraryLink$' --output-on-failure
```

完整验证可用 `cmake --workflow --preset windows-full`；普通 ALL 构建不保证独立宿主测试已准备。Linux/macOS 改用相应 preset。Android/iOS 默认 `BUILD_TESTING=OFF`；要检查上述编译型 target，可在 configure 时显式追加 `-DBUILD_TESTING=ON`。这不验证设备执行或独立预编译 SDK 的 ABI 兼容。

## 现行移动端真实 Module 最终链接

旧 `LinkSmoke/` 空程序 SDK 冒烟已删除，其最终链接能力由下面的真实 Runtime/业务宿主检查承接。
正式根分别 `cmake --preset android-arm64|ios-arm64 -DPANGU_LINK_MODE=<Monolithic|Modular> -DBUILD_TESTING=ON -DPANGU_BUILD_ENGINE_TESTS=ON` 并用对应 build preset 编译，再显式执行：

```sh
cmake -DPANGU_ENGINE_SOURCE_DIR=<共享源码根> -DPANGU_TEST_ROOT=<本平台BuildRoot>/Tests/ModuleLinkModes/<模式> -DPANGU_TARGET_PRESET=<android-arm64|ios-arm64> -DPANGU_LINK_MODE=<模式> -P Tests/BuildSystem/ProjectIntegration/CrossCompileTests.cmake
```

Android Windows 宿主根为 `D:/Git/PanguEngine`，iOS 在 `ssh -o BatchMode=yes moozen-macos` 的 `/Users/moozen/WindowsGit/PanguEngine` 执行，源码不能复制到临时目录。夹具只在平台 BuildRoot 生成可变 Project；Engine 仍指共享源码。Monolithic 真归档进入 Android 原生宿主 .so/iOS Mach-O executable；Modular 真实 Foundation/Module/业务库、固定 Query、Runtime 依赖及最小宿主最终链接均需宿主工具断言。Android 确认 ELF/AArch64，iOS 确认 ARM64/platform 2。精确命令、结果、原始路径见[验收记录](../../docs/superpowers/plans/2026-11-06-module-manager-link-modes-validation.md)。完成恢复移动 Monolithic、benchmark OFF。只做编译/链接/宿主检查，不是设备运行、打包、签名或设备级卸载证据。

正式 `mobile-link` 同时准备 ECS 业务夹具。Android 构建 SHARED `MyGameEcsHost` 和导入 `PanguEcsHostCheck` 的可执行 `MyGameEcsHostCaller`，检查 ELF AArch64 和 Android ELF note。iOS 构建同源 `MyGameEcsHost` 可执行文件，检查 arm64 与 iPhoneOS Load Command。Modular 核对实际 ECS/flecs 动态依赖和各业务库唯一的 Module Query，Monolithic 核对最终镜像中的业务 Query 与静态 flecs。交叉目标不构建 GoogleTest，宿主 Meta 工具与目标链接隔离。验收后恢复移动 Monolithic、`BUILD_TESTING=OFF`、`PANGU_BUILD_BENCHMARKS=OFF`。[ECS 测试入口](../ECS/README.md) · [ECS 最终验收](../../docs/superpowers/plans/2026-11-18-ecs-world.md#ecs-交付验收)
