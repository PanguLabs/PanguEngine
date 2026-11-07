# Toolchain / ThirdParty 编译与链接测试

本目录保留真实 C++23 编译和链接回归，不经过 AbiProfile，不生成逐 TU 收据，也不扫描 `compile_commands.json` 来判断编译失效。对应 [Tests 维护规范](../README.md) 与 [ADR-0013](../../wiki/decisions/0013-native-build-abi-boundary.md)。

平台检查的编译、最终链接、宿主检查与设备运行边界见[测试分类与运行契约](../../wiki/foundation/testing.md)。

`BUILD_TESTING=ON` 且 `PANGU_BUILD_ENGINE_TESTS=ON` 时始终配置 Toolchain；任一关闭则不接入 Engine 测试。交叉编译时只构建编译型探针，不运行设备程序。

- `ThirdParty/CompiledProbe.cpp`：真实 `ThirdParty::CompiledProbe` STATIC 库，直接使用平台 Clang、STL、C++23，不继承 Pangu Module 专属告警/异常选项。
- `Module/PolicyProbe.cpp`：真实 `Pangu::ToolchainProbe` Module，服从 `PANGU_LINK_MODE`，编译期检查 C++23、无异常和平台 STL。
- `LinkProbe.cpp`：桌面构建 `PanguToolchainLinkTests`，实际链接两座库，CTest 名 `ToolchainLibraryLink`，运行时验证跨库 `std::expected<int, int>`；Modular 下显式导入 Probe 函数。

桌面正式构建：

```sh
cmake --preset windows
cmake --build --preset windows-full
ctest --preset windows-full -R '^ToolchainLibraryLink$' --output-on-failure
```

完整验证可用 `cmake --workflow --preset windows-full`；普通 ALL 构建不保证独立宿主测试已准备。Linux/macOS 改用相应 preset。Android/iOS 默认 `BUILD_TESTING=OFF`；要检查上述编译型 target，可在 configure 时显式追加 `-DBUILD_TESTING=ON`。这不验证设备执行或独立预编译 SDK 的 ABI 兼容。

## 历史空程序工具链冒烟（仅 SDK 链接能力）

`LinkSmoke/` 是与引擎根工程独立的最小 CMake 项目，目标为 `PanguPlatformLinkSmoke`。只验证目标 Clang + SDK 可以**最终链接一个 C++23 程序**，不链接 Pangu Runtime，不在设备执行，不加入根工程 `ALL` 或 CTest。

Android（Windows 宿主，`S` 为新的 NTFS 目标目录）：

```powershell
cmake -S Tests/Toolchain/LinkSmoke -B $S -G Ninja "-DCMAKE_TOOLCHAIN_FILE=$env:ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 -DANDROID_STL=c++_shared -DCMAKE_BUILD_TYPE=Release
cmake --build $S --target PanguPlatformLinkSmoke --verbose
& "$env:ANDROID_NDK_HOME/toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-readobj.exe" --file-headers "$S/PanguPlatformLinkSmoke"
```

iOS（macOS 宿主，`S` 为新的 APFS 目标目录）：

```sh
cmake -S Tests/Toolchain/LinkSmoke -B "$S" -G Ninja -DCMAKE_TOOLCHAIN_FILE="$PWD/cmake/Platform/iOS/Toolchain.cmake" -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=18.0 -DCMAKE_BUILD_TYPE=Release
cmake --build "$S" --target PanguPlatformLinkSmoke --verbose
otool -hv "$S/PanguPlatformLinkSmoke.app/PanguPlatformLinkSmoke"
otool -l "$S/PanguPlatformLinkSmoke.app/PanguPlatformLinkSmoke"
```

Android 需确认 ELF/AArch64；iOS 需确认 Mach-O/ARM64 可执行文件，且 `LC_BUILD_VERSION` 为 iOS 平台（`platform 2`），不可把 iOS CMake 的 `try_compile(STATIC_LIBRARY)` 当作最终链接。

## 现行移动端真实 Module 最终链接

上方 LinkSmoke 不消费 Runtime，不能代替本轮验收。正式根分别 `cmake --preset android-arm64|ios-arm64 -DPANGU_LINK_MODE=<Monolithic|Modular> -DBUILD_TESTING=ON -DPANGU_BUILD_ENGINE_TESTS=ON` 并用对应 build preset 编译，再显式执行：

```sh
cmake -DPANGU_ENGINE_SOURCE_DIR=<共享源码根> -DPANGU_TEST_ROOT=<本平台BuildRoot>/Tests/ModuleLinkModes/<模式> -DPANGU_TARGET_PRESET=<android-arm64|ios-arm64> -DPANGU_LINK_MODE=<模式> -P Tests/CMake/SourcePluginProject/CrossCompileTests.cmake
```

Android Windows 宿主根为 `D:/Git/PanguEngine`，iOS 在 `ssh -o BatchMode=yes moozen-macos` 的 `/Users/moozen/WindowsGit/PanguEngine` 执行，源码不能复制到临时目录。夹具只在平台 BuildRoot 生成可变 Project；Engine 仍指共享源码。Monolithic 真归档进入 Android 原生宿主 .so/iOS Mach-O executable；Modular 真实 Foundation/Module/业务库、固定 Query、Runtime 依赖及最小宿主最终链接均需宿主工具断言。Android 确认 ELF/AArch64，iOS 确认 ARM64/platform 2。精确命令、结果、原始路径见[验收记录](../../docs/superpowers/plans/2026-11-06-module-manager-link-modes-validation.md)。完成恢复移动 Monolithic、benchmark OFF。只做编译/链接/宿主检查，不是设备运行、打包、签名或设备级卸载证据。
