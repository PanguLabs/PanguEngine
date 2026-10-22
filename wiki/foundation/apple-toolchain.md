# Pangu Apple 平台工具链（macOS / iOS）

> 设计状态：Partially Frozen（macOS/iOS 默认 MacPorts Clang23，iOS AppleClang 仅作对照；正式 Toolchain 与部署流程未冻结）
> 验证状态：macOS 原生探针编译、链接、运行通过；iOS 两个编译器的 Compile+Link 通过，部署待验证
> 更新：2026-10-22
> 决策：[ADR-0004](../decisions/0004-platform-clang-toolchains.md)

## macOS：MacPorts LLVM Clang 23

通过 `ssh -o BatchMode=yes moozen-macos` 实测：

- 宿主：`Darwin 23.6.0 x86_64`
- MacPorts：`clang-23 @23.1.2_0+analyzer` 与 `llvm-23 @23.1.2_0`
- C 编译器：`/opt/local/bin/clang-mp-23`
- C++ 编译器：`/opt/local/bin/clang++-mp-23`
- CMake：4.4.3；libc++；macOS Sysroot 为 CLT `/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk`

macOS 下已完成 C++23 原生 Clang23 + libc++ 编译、链接和运行。须显式选择 MacPorts 编译器，不依赖默认 AppleClang。

正式 CMake 配置从环境变量 `LLVM_ROOT` 派生 `bin/clang++`、`bin/clang-scan-deps` 等路径，不写死 MacPorts 安装位置。本机对应 `LLVM_ROOT=/opt/local/libexec/llvm-23`，可用 `llvm-config-mp-23 --prefix` 查询；表中 `/opt/local/bin/*-mp-23` 是版本化命令入口。切换安装目录后需重新配置构建树。

## iOS：独立安装的 iPhoneOS 18.2 SDK

**已确认 SDK 路径存在：**

```text
/Users/moozen/Library/Developer/SDKs/iPhoneOS18.2.sdk
```

SDK 内有 `SDKSettings.json`、`usr/include`、`usr/lib/libc++.1.tbd`、`usr/lib/libSystem.B.tbd` 及 `Foundation.framework`。SDK 声明 `CanonicalName=iphoneos18.2`，包含 iOS ARM64 目标平台。

当前活动 Developer Directory 仍是 `/Library/Developer/CommandLineTools`，所以 `xcrun --sdk iphoneos --show-sdk-path` 无法自动发现这份位于用户目录的 SDK，`xcodebuild` 也无法正常用于 iOS 工程。这**不代表 SDK 不存在或不能直接使用**。

iOS CMake Toolchain 同样从 `LLVM_ROOT` 选择编译器。SDK 优先读取 `PANGU_IOS_SDK_PATH`，未设置时使用 `xcrun --sdk iphoneos --show-sdk-path`；若查找失败或返回的目录不存在，则回退到 `$HOME/Library/Developer/SDKs/iPhoneOS18.2.sdk`，兼容当前未安装 Xcode 的宿主。该目录也不存在时要求显式设置 `PANGU_IOS_SDK_PATH`；显式配置的无效路径会直接报错。

## iOS 编译器验证及选择

| 编译器 | C++23 + libc++ 编译 | iPhoneOS18.2 SDK 链接 | 角色 |
| --- | --- | --- | --- |
| AppleClang 16.0.0（`/usr/bin/clang++`） | 通过 | 通过 | 兼容性对照 |
| MacPorts LLVM Clang 23.1.2 | 通过 | 通过 | **默认工具链** |

探针包含 `std::vector` 与 `std::expected`，禁用异常，以 iOS 18.0 为 Deployment Target，产出 iOS ARM64 Mach-O 动态库，链接 Foundation、系统 libc++、libSystem。两个产物的 `LC_BUILD_VERSION` 均为 **platform iOS、minOS 18.0、sdk 18.2**；`otool -L` 确认 MacPorts Clang23 产物没有依赖 `/opt/local` 下的运行时库。

MacPorts LLVM Clang23 适合与 PanguEngine 桌面端 PanguClang/Meta 的 LLVM 23 属性和 AST 行为保持接近；AppleClang 与 Apple SDK 的集成优势可作兼容性验证参考，但当前没有技术必要性强制改用 AppleClang 编译 iOS 引擎。

示意（`-isysroot` 需显式传入）：

```bash
SDK="$HOME/Library/Developer/SDKs/iPhoneOS18.2.sdk"
CXX=/opt/local/bin/clang++-mp-23
"$CXX" --target=arm64-apple-ios18.0 -isysroot "$SDK" \
  -std=c++23 -stdlib=libc++ -fno-exceptions -fPIC \
  -dynamiclib smoke.cpp -framework Foundation -o libPanguSmoke.dylib
```

此处 `iOS 18.0` 仅为**验证时使用的 Deployment Target**，不等于已冻结 PanguEngine 最低 iOS 版本。

## iOS 仍需的正式验收

- 目前只完成独立 SDK 的编译与链接；未验证代码签名、打包、真机/模拟器安装和运行。
- 仅确认 **iPhoneOS Device SDK**，尚未找到或验证 `iPhoneSimulator.sdk`；不能因此宣称 Simulator 可用。
- 签名、Provisioning、完整 Xcode/App 包装工作流可能仍需可用的 Xcode；即使 SDK 能由 Clang 链接，也不等于可在设备上部署。
- 需统一 Apple 平台的 Target Triple、Sysroot、Framework 搜索路径、ABI Fingerprint 和运行时支持情况，并测试正式 CMake Toolchain。

**macOS/iOS 默认使用 MacPorts Clang23**，AppleClang 留作兼容性对照。用户目录中的 iPhoneOS18.2 SDK 是本次验证环境，SDK 版本及安装路径不因此成为永久构建要求；这不是完整 iOS 发布链路通过的声明。
