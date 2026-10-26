# Pangu 平台支持基线

> 状态：Partially Frozen（目标平台与访问环境已确定；Runtime/HAL 与完整 CI 尚未冻结）
> 更新：2026-10-27

## 目标平台与验证状态

| 目标平台 | 开发 / 访问环境 | 当前验证状态 |
| --- | --- | --- |
| Windows | 主开发环境（homelab） | 已有针对性的 C++23 构建与 Runtime 探针测试；非完整 Engine 验收 |
| Linux | Windows 上 WSL Ubuntu | 已有针对性的 C++23 构建与 Runtime 探针测试；非完整 Engine 验收 |
| macOS | `ssh -o BatchMode=yes moozen-macos` | C++23 原生编译、链接、运行探针通过 |
| Android | Windows 上 Android SDK/NDK 工具链 | 四种 ABI 的 C++23 动态库 Configure / Compile / Link 通过；尚未在设备运行或完成 App 打包 |
| iOS | macOS 主机交叉编译 iPhoneOS Device | iOS ARM64 动态库编译与链接通过；签名、打包及真机/模拟器验证未完成 |

**目标支持不等于完整引擎实现或设备运行验证。** 上表只列出验证层级，不作为平台工具链版本、路径和参数的权威来源。

截至 2026-10-27，五平台受管 STATIC Module 的 ABI Collection 已完成验收：Windows x64、WSL Ubuntu Linux x64、macOS x64 通过完整 CTest，最近一轮分别为 **36/36、34/34、34/34（104/104）**；Android ARM64、iOS ARM64 通过真实目标编译/归档、Host 采集、增量 Guard 和完成引用摘要校验。Windows 另覆盖 Release MD、Debug MD、Release MT。该验收未执行 Android/iOS 目标程序，也不扩展至其他移动 ABI、Apple Silicon 或 Simulator。详见 [ABI Profile 实施状态](abi-profile-fingerprint.md)与[计划验收记录](../../docs/superpowers/plans/2026-10-23-abi-collection.md)。

- 统一的编译器、C++ 标准库和版本选择：见 [Toolchain](toolchain.md)。
- Android SDK/NDK、CMake、ABI 与验证细节：见 [Android 工具链](android-toolchain.md)。
- macOS / iOS MacPorts、SDK、目标平台链接与部署细节：见 [Apple 工具链](apple-toolchain.md)。

## 工程原则

1. 平台无关的 API 契约与 OS/SDK、文件系统、线程、虚拟内存、窗口系统实现分离。
2. 确定的目标平台差异优先编译期选择，真正需要动态能力检测时允许运行时判断。
3. 对外接口明确错误模式、线程约束与资源生命周期，不将平台私有码替代通用契约。
4. 同一源代码在不同目标平台具有不同 ABI Profile；**ABI 兼容性是同一目标进程内的契约**，不是跨系统二进制互操作承诺。
5. 验证需覆盖各平台专属路径，不能以 Windows 探针通过代表移动端或其他桌面端已通过。

## 尚未冻结的跨平台边界

Platform HAL 的 Module 布局、Window/Input/File/Socket/Thread/VirtualMemory/Crash API、动态库装载、最低操作系统版本、沙盒限制、跨平台 CI、交叉编译与设备验收规则。

## 访问规范

从 Codex 进入时，使用当前 Agent 的本地文件和终端工具；从 ChatGPT Web 进入时，优先通过已授权的 Desktop Commander 访问本地仓库，不调用 Codex。Linux 使用 WSL Ubuntu；macOS 使用 `ssh moozen-macos`（自动化可加 `-o BatchMode=yes`）。访问方式以根目录 [AGENTS.md](../../AGENTS.md) 为准；不同平台的路径、Shell、SDK 不能互换。实际 SDK 路径请查阅上述平台工具链文档。

相关契约：[ABI](abi.md)、[Memory](../core/memory-management.md)。
