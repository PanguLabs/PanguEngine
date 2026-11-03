# Toolchain / ThirdParty 编译与链接测试

本目录保留真实 C++23 编译和链接回归，不经过 AbiProfile，不生成逐 TU 收据，也不扫描 `compile_commands.json` 来判断编译失效。对应 [Tests 维护规范](../README.md) 与 [ADR-0013](../../wiki/decisions/0013-native-build-abi-boundary.md)。

`BUILD_TESTING=ON` 时默认启用 `PANGU_VALIDATE_TOOLCHAIN=ON`；前者关闭则不配置任何测试。交叉编译时只构建编译型探针，不运行设备程序。

- `ThirdParty/CompiledProbe.cpp`：真实 `ThirdParty::CompiledProbe` STATIC 库，直接使用平台 Clang、STL、C++23，不继承 Pangu Module 专属告警/异常选项。
- `Module/PolicyProbe.cpp`：真实 `Pangu::ToolchainProbe` STATIC Module，编译期检查 C++23、无异常和平台 STL。
- `LinkProbe.cpp`：桌面构建 `PanguToolchainLinkTests`，实际链接两座静态库，CTest 名 `ToolchainLibraryLink`，运行时验证跨库 `std::expected<int, int>`。

桌面正式构建：

```sh
cmake --preset windows
cmake --build --preset windows
ctest --preset windows -R '^ToolchainLibraryLink$' --output-on-failure
```

Linux/macOS 改用相应 preset。Android/iOS 默认 `BUILD_TESTING=OFF`；要检查上述编译型 target，可在 configure 时显式追加 `-DBUILD_TESTING=ON`。这不验证设备执行或独立预编译 SDK 的 ABI 兼容。

## 移动端最终链接冒烟（显式运行）

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
