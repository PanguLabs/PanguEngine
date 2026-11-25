# Pangu Android 构建工具链

> 状态：Partially Frozen（NDK 内置 Clang + C++23 配置已确定，ARM64 Renderer 测试应用已验收，正式 App 包装配置待冻结）
> 更新：2026-11-22
> 参考工程：`D:\AllProject\AndroidStudio\CppDemo`（只读参考）
> 决策：[ADR-0004](../decisions/0004-platform-clang-toolchains.md)

## 已确定的工具链政策

Android 使用选定 NDK 自带 Clang 与 NDK `libc++`，采用 C++23、禁用异常，CMake 最低为 4.0；遵循 [统一工具链契约](toolchain.md)。具体 SDK/NDK 更新政策、最低 Android 版本和正式 App 包装配置仍待冻结。

## 当前验证与参考配置

下表记录本机探针及只读参考工程 CppDemo 的配置。产品支持范围以已冻结的工具链政策和对应验收为准。

| 配置 | 验证或参考值 |
| --- | --- |
| Android SDK / compileSdk | 37（本机 android-37.0） |
| Android NDK | `30.0.16248370` |
| C/C++ 编译器 | **该 NDK 自带 Clang（当前 21.0.0）** |
| C++ 语言标准 | **C++23**，禁用异常 |
| C++ 标准库 | NDK `libc++` |
| 探针 Android API Level / 参考 minSdk | 29（非冻结的产品最低版本） |
| targetSdk | 37（App 包装层参考值） |
| CMake | Android SDK `4.1.2`，最低 4.0 |
| Android Gradle Plugin | 9.4.1（来自 CppDemo） |
| Gradle Wrapper | 9.6.0（来自 CppDemo） |
| 已验证 ABI 范围 | arm64-v8a、armeabi-v7a、x86、x86_64 |

参考工程中的 `app/build.gradle.kts` 定义 SDK/NDK/CMake，`gradle/libs.versions.toml` 定义 AGP，`gradle/wrapper/gradle-wrapper.properties` 定义 Gradle。PanguEngine 的 [Android Studio Vulkan 真机测试工程](../../Tests/Renderer/NativeActivity/AndroidStudio/README.md) 通过 `externalNativeBuild` 使用引擎的 C++23 配置。

## Android 编译器政策

NDK r30 的实际 `clang++.exe --version` 输出为 Android Clang 21.0.0。**这符合 PanguEngine 针对 Android 的新决策**：Android 使用 NDK 自带 Clang，不要求和 Desktop 端 LLVM Clang23 相同主版本。

建议在 Android App/NDK Toolchain 配置中使用 NDK 原生 `build/cmake/android.toolchain.cmake`，由其设置编译器、Sysroot、CRT、libc++、linker 和其他运行时。CMake 不得使用统一 `Clang == 23` 限制拒绝合法 Android 构建，也不应自行拼接替换 LLVM23 的 Android compiler-rt。

C++23 设置片段（实际 NDK 路径由 Android SDK/构建环境注入；此片段不代替 NDK 来源校验和各编译目标的禁用异常配置）：

```cmake
cmake_minimum_required(VERSION 4.0)
project(PanguAndroid LANGUAGES CXX)
if(NOT ANDROID OR NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  message(FATAL_ERROR "Android requires the NDK Clang")
endif()
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
```

## 实际验证

通过 Android SDK CMake 4.1.2、NDK r30 官方 CMake Toolchain、API29、`ANDROID_STL=c++_shared`，完成四个 ABI 的 Configure / Compile / Link。

验证源码使用 `std::vector`、`std::span`、`std::expected`，编译选项包含 `-fno-exceptions` 和 `-std=c++23`。四个 ABI 均成功生成 ELF `.so`。探针位于被 Git 忽略的 `.probe/android-ndk-cxx23`。

早期曾验证“桌面 LLVM Clang23 + NDK r30 显式 Runtime”的手工链接，但已不再是正式实现路线；避免将早期探索误写成最终 Android 构建契约。

### ARM64 Renderer 验收

- 根 `android-arm64` preset 使用 NDK r30 和 API 29。NativeActivity 生命周期测试在 Monolithic、Modular 两种链接模式下均完成最终链接、`ANativeActivity_onCreate` 导出检查及含 `libc++_shared.so` 的 APK 打包与签名检查。Monolithic APK 在 Adreno 750 真机验证了窗口撤销、恢复和宿主关闭。 [生命周期验收](../../docs/superpowers/plans/2026-11-21-android-native-activity-host.md#验收记录)
- Gradle 的 `agdkDebug` Vulkan 开发预览已完成 ARM64 Monolithic APK 构建、安装和真机呈现。退出复验三轮均完成原生 `APP_CMD_DESTROY` 和 Java `performDestroy`。 [预览验收](../../docs/superpowers/plans/2026-11-21-android-studio-renderer-check.md#主动退出复验)

## 尚未验证

其余 ABI 的 APK 打包和设备运行、模拟器运行、完整 Engine Runtime 的多 Module ABI/Allocator 边界及 Sanitizer。Android Vulkan 的接入与验收口径见 [Renderer 接入规格](../../docs/superpowers/specs/2026-11-20-renderer-forge-igraphics-design.md#验收口径)。

本规范冻结范围为上文“已确定的工具链政策”；探针配置与结果不代表整个 Android Engine Runtime 已完成。
