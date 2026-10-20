# Pangu Android 构建工具链

> 状态：Partially Frozen（NDK 内置 Clang + C++23 配置已确定；设备运行/集成待验收）
> 更新：2026-10-20
> 参考工程：`D:\AllProject\AndroidStudio\CppDemo`（只读参考）
> 决策：[ADR-0004](../decisions/0004-platform-clang-toolchains.md)

## 已确定的工具链政策

Android 使用选定 NDK 自带 Clang 与 NDK `libc++`，采用 C++23、禁用异常，CMake 最低为 4.0；遵循 [统一工具链契约](toolchain.md)。具体 SDK/NDK 更新政策、最低 Android 版本和正式 App 包装配置仍待冻结。

## 当前验证与参考配置

下表记录本机探针及只读参考工程 CppDemo 的配置，不将其中的版本、路径、API Level 或 ABI 验证范围自动冻结为产品支持要求。AGP / Gradle / targetSdk 来自参考工程，不代表 PanguEngine 已完成 App 集成。

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

参考工程中的 `app/build.gradle.kts` 定义 SDK/NDK/CMake，`gradle/libs.versions.toml` 定义 AGP，`gradle/wrapper/gradle-wrapper.properties` 定义 Gradle。后续 App 集成可参考其 `externalNativeBuild.cmake` 流程，但**不继承 CppDemo 的 `-std=c++20`**，应改为 C++23；这些路径不表示 PanguEngine 已具有相应文件。

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

## 尚未验证

Gradle 包装层完整构建/安装、Android 设备或模拟器运行、JNI 入口、libc++ 打包、多 Module ABI/Allocator 边界、Sanitizer 以及正式 CMake Preset。

本规范冻结范围为上文“已确定的工具链政策”；探针配置与结果不代表整个 Android Engine Runtime 已完成。
