# PanguEngine Agent 开发规范

本文件是 PanguEngine 仓库内各类 AI Agent 的通用开发规范（Single Source of Truth）。修改源码、工具链、文档和构建配置时均应遵守。

## 沟通与文档语言

- **中文优先，专业术语可使用英文。**
- 讨论、分析、设计说明、代码评审和项目文档优先使用简体中文；必要时保留 C++、ABI、Module、Plugin、ECS 等专业术语。
- 代码标识符、命名空间、API、命令行参数、文件路径及约定的配置键保持其原有拼写，不要为翻译而更名。

## 项目及仓库

- PanguEngine 是基于 **C++23** 的自研跨平台游戏引擎。
- 目标支持 **Windows、Linux、macOS、Android、iOS**，统一采用 **C++23 + Clang**；各平台的编译器和标准库选择以 [`wiki/foundation/toolchain.md`](wiki/foundation/toolchain.md) 为准，已验证程度以 [`wiki/foundation/platform.md`](wiki/foundation/platform.md) 为准。
- **Windows 是主要开发环境**；Linux 使用 Windows 上 **WSL 中的 Ubuntu**；macOS 使用 `ssh moozen-macos` 访问，自动化 SSH 命令可使用 `-o BatchMode=yes`。
- 项目本地源码位于 homelab 的 **`D:\Git\PanguEngine`**。
- GitHub 远程仓库：**`PanguLabs/PanguEngine`**（`https://github.com/PanguLabs/PanguEngine`）。
- 架构设计可参考下列项目；需要核对实现细节时，查阅对应的本地源码。路径均相对于本仓库根目录。应结合 PanguEngine 已确定的需求和约束进行取舍，避免直接照搬。

| 参考项目 | 本地源码路径 |
| --- | --- |
| flecs | `../flecs` |
| TheForge | `../The-Forge` |
| UnrealEngine | `../UnrealEngine` |
| O3DE | `../o3de` |
| FlaxEngine | `../FlaxEngine` |

## 开发原则

- **构建产物名称不得包含平台名**；构建目录按宿主文件系统隔离，产物名称在各平台保持一致，文件前缀与扩展名遵循平台惯例。
- **构建系统逻辑优先使用 C++ 实现**；CMake 负责接线与集成，包括目标声明、依赖连接和构建工具调用。

## Agent 工作方式

- 本项目可通过 **Codex** 或 **ChatGPT Web + Desktop Commander** 进入。
- 从 **Codex** 进入时，使用当前 Agent 提供的本地文件和终端工具。
- 从 **ChatGPT Web** 进入时，**不要调用 Codex**；优先使用 ChatGPT Web 中已安装的插件，通过 **Desktop Commander** 访问和操作本地仓库。
- 优先在本地仓库 `D:\Git\PanguEngine` 检查与修改代码。需要核对远程版本时，使用 GitHub 仓库。
- 执行 Linux 命令时使用 WSL Ubuntu，通过 SMB3/CIFS 挂载的 `/home/moozen/WindowsGit/PanguEngine` 运行正式 CMake preset、构建和 CTest，Linux 使用 WSL ext4 本地构建目录 `$HOME/PanguEngineBuild/Linux`，不在 CIFS 共享源码树下生成正式构建产物；不要通过 `/mnt/d/Git/PanguEngine` 混用同一构建树的绝对路径。执行 macOS 命令时使用 `ssh -o BatchMode=yes moozen-macos`。**macOS 通过已挂载的 `/Users/moozen/WindowsGit/PanguEngine` 直接访问同一份源码，禁止将源码复制到 macOS 临时目录再构建**。正式构建统一使用根目录 `CMakePresets.json`：Windows/Android 输出到源码仓库的 `Build/Windows`、`Build/Android`（NTFS），Linux 输出到 `$HOME/PanguEngineBuild/Linux`（ext4），macOS/iOS 输出到 Mac 的 `$HOME/PanguEngineBuild/macOS`、`$HOME/PanguEngineBuild/iOS`（APFS）。各平台 BuildRoot 必须隔离，旧 SMB 构建树不得与新构建树混用。macOS/iOS 的编译器与 SDK 细节见 [`wiki/foundation/apple-toolchain.md`](wiki/foundation/apple-toolchain.md)；Android 以只读参考项目 `D:\AllProject\AndroidStudio\CppDemo` 的构建流程为参照，具体配置见 [`wiki/foundation/android-toolchain.md`](wiki/foundation/android-toolchain.md)。不要将 Windows 路径、Shell 或平台库行为直接视为可跨平台复用。
- 共享 Agent Skills 的唯一规范目录为 `.agents/skills/`。
- 新工作区可从仓库根目录执行 `python Scripts/setup-agent-links.py`，为 Claude Code 生成本地 Skills 映射：Windows 使用 Junction，Linux/macOS 使用 Symlink。
- `.claude/skills` 是本地生成的目录映射，不应提交到 Git；Codex 可以直接使用 `.agents/skills/`。
- 新建或修改的 `CMakeLists.txt` 使用 `cmake_minimum_required(VERSION 4.0)`，并按目标平台校验编译器；具体规则以 [`wiki/foundation/toolchain.md`](wiki/foundation/toolchain.md) 和 [`wiki/foundation/build-system.md`](wiki/foundation/build-system.md) 为准。
- 修改完成后尽可能执行针对性的构建、测试或静态检查，并明确说明未验证的部分。
- 临时验证探针（Probe/PoC）统一放在仓库根目录 `.probe/`，不要放入正式 `Tests/`；正式、长期维护的回归测试仍使用 `Tests/`。
- 未经明确要求，不提交或推送 Git 变更。

## C++ 编程规范

- 编写、修改、重构或评审 C++ 代码前，阅读并遵守 [`wiki/foundation/cpp-guidelines.md`](wiki/foundation/cpp-guidelines.md) 中的开发原则与最佳实践；具体领域的契约以对应子系统规范为准。
- 代码格式以根目录 [`.clang-format`](.clang-format) 为准；命名与静态检查以根目录 [`.clang-tidy`](.clang-tidy) 为准，使用仓库配置执行检查。
- 以上规范适用于引擎源码、工具、测试、`.probe/` 探针及 `wiki/` 中的 C++ 示例；修改示例中的标识符时，同步相关正文与跨文档引用。第三方代码按独立的依赖与检查边界处理。
- 修改后运行针对性的 `clang-format`、`clang-tidy` 及构建/测试。`clang-tidy` 优先使用实际目标的编译数据库；文档代码片段可在 `.probe/` 中补齐最小依赖上下文验证，并说明验证限制。
- 确有必要的诊断豁免应限定到具体检查和最小代码范围，并就近说明原因。

## 设计文档

- 架构规范统一位于 `wiki/`；从 [`wiki/README.md`](wiki/README.md) 进入，按 Foundation / Core / Runtime 分类。Runtime 按功能领域组织，不使用统一 Subsystem 架构，见 [Service 规范](wiki/core/service.md)。
- 具体子系统文档是该子系统设计的权威来源；`wiki/decisions/` 保存重大决策的理由与替代关系。
- 严格区分 Frozen、Partially Frozen 与 Draft，不能将历史讨论草案当作已实现或全部冻结的接口。
