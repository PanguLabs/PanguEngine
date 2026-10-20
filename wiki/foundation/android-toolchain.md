# Pangu Android 构建工具链

> 状态：Partially Frozen（NDK 内置 Clang + C++23 配置已确定；设备运行/集成待验收）
> 更新：2026-10-20
> 参考工程：`D:\AllProject\AndroidStudio\CppDemo`（只读参考）
> 决策：[ADR-0004](../decisions/0004-platform-clang-toolchains.md)

## 已确定配置

| 配置 | PanguEngine |
| --- | --- |
| Android SDK / compileSdk | 37（本机 android-37.0） |
| Android NDK | `30.0.16248370` |
| C/C++ 编译器 | **该 NDK 自带 Clang（当前 21.0.0）** |
| C++ 语言标准 | **C++23**，禁用异常 |
| C++ 标准库 | NDK `libc++` |
| minSdk / Android API Level | 29 |
| targetSdk | 37（App 包装层参考值） |
| CMake | Android SDK `4.1.2`，最低 4.0 |
| Android Gradle Plugin | 9.4.1（来自 CppDemo） |
| Gradle Wrapper | 9.6.0（来自 CppDemo） |
| 初始 ABI 范围 | arm64-v8a、armeabi-v7a、x86、x86_64 |

`app/build.gradle.kts` 定义 SDK/NDK/CMake，`gradle/libs.versions.toml` 定义 AGP，`gradle/wrapper/gradle-wrapper.properties` 定义 Gradle。使用 `externalNativeBuild.cmake`，但**不继承 CppDemo 的 `-std=c++20`**，应改为 C++23。

## Android 编译器政策

NDK r30 的实际 `clang++.exe --version` 输出为 Android Clang 21.0.0。**这符合 PanguEngine 针对 Android 的新决策**：Android 使用 NDK 自带 Clang，不要求和 Desktop 端 LLVM Clang23 相同主版本。

建议在 Android App/NDK Toolchain 配置中使用 NDK 原生 `build/cmake/android.toolchain.cmake`，由其设置编译器、Sysroot、CRT、libc++、linker 和其他运行时。CMake 不得使用统一 `Clang == 23` 限制拒绝合法 Android 构建，也不应自行拼接替换 LLVM23 的 Android compiler-rt。

示意配置（实际 NDK 路径由 Android SDK/构建环境注入）：

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

## 尚未验证

Gradle 包装层完整构建/安装、Android 设备或模拟器运行、JNI 入口、libc++ 打包、多 Module ABI/Allocator 边界、Sanitizer 以及正式 CMake Preset。

本规范只冻结选用 NDK 内置 Clang 与 C++23 的方向，不声称整个 Android Engine Runtime 已完成。
