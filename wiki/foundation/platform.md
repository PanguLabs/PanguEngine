# Pangu 平台支持基线

> 状态：Partially Frozen（目标平台与开发环境冻结；HAL、运行时能力和 CI 矩阵尚未冻结）
> 更新：2026-10-20

## 目标平台

| 平台 | 编译器 / C++ 标准库 | 开发/访问环境 | 实施状态 |
| --- | --- | --- | --- |
| Windows | Clang + MSVC STL | 主要开发环境 | 以实际源码/测试为准 |
| Linux | Clang + `libc++` | Windows 上 WSL Ubuntu | 以实际源码/测试为准 |
| macOS | Clang + `libc++` | `ssh -o BatchMode=yes moozen-macos` | 以实际源码/测试为准 |
| Android | Clang + `libc++` | Android SDK/NDK 方案待确认 | 以实际源码/测试为准 |
| iOS | Clang + `libc++` | Apple SDK / 构建环境待确认 | 以实际源码/测试为准 |

全部目标平台的 C++ 构建使用 Clang，语言标准为 C++23。**Windows 使用 MSVC STL，Linux、macOS、Android、iOS 使用 `libc++`；全平台不使用 `libstdc++`**。**“目标支持”不等于“已通过编译和运行验证”。**

## 工程原则

1. 平台无关契约应与 OS、SDK、文件系统、线程、虚拟内存和窗口系统的具体实现分离。
2. 编译期平台选择适用于确知的目标差异；真正需要动态能力检测时允许运行时判断。
3. 公共接口需明确其错误模式、线程与资源生命周期，不用平台私有错误码泄漏替代通用契约。
4. 同一源代码项目在不同目标平台会具有不同 ABI Profile；**ABI 兼容性是单目标进程约束，不是跨系统二进制互操作声明**。
5. 跨平台测试应覆盖平台专属路径，不以 Windows 的测试结果直接代表 Linux/macOS/移动端。

## 平台抽象的设计边界（待冻结）

Platform HAL 的具体 Module 结构、Window/Input/File/Socket/Thread/VirtualMemory/Crash API、动态库装载方式、SDK 版本、最低系统版本、Android/iOS 沙盒限制、交叉编译策略均需另行审议。

## 访问规范

- 本地仓库：`D:\Git\PanguEngine`，通过已授权的 Desktop Commander 访问。
- WSL Linux 命令应在 WSL Ubuntu 下执行。
- macOS 命令通过 `ssh moozen-macos` 执行；自动化可加 `-o BatchMode=yes`。
- 不得将这三个环境的文件路径、Shell 命令或平台库行为视为可直接互换。

相关文档：[工具链](toolchain.md)、[ABI](abi.md)、[内存管理](../core/memory-management.md)。
