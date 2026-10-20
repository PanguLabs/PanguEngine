# PanguEngine Agent 开发规范

本文件是 PanguEngine 仓库内各类 AI Agent 的通用开发规范（Single Source of Truth）。修改源码、工具链、文档和构建配置时均应遵守。

## 沟通与文档语言

- **中文优先，专业术语可使用英文。**
- 讨论、分析、设计说明、代码评审和项目文档优先使用简体中文；必要时保留 C++、ABI、Module、Plugin、ECS 等专业术语。
- 代码标识符、命名空间、API、命令行参数、文件路径及约定的配置键保持其原有拼写，不要为翻译而更名。

## 项目及仓库

- PanguEngine 是基于 **C++23** 的自研跨平台游戏引擎。
- 目标支持 **Windows、Linux、macOS、Android、iOS**，全部目标平台统一使用 **Clang** 工具链编译；各平台的实际支持与验证状态以仓库实现和测试结果为准。
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

## Agent 工作方式

- 本项目可通过 **Codex** 或 **ChatGPT Web + Desktop Commander** 进入。
- 从 **Codex** 进入时，使用当前 Agent 提供的本地文件和终端工具。
- 从 **ChatGPT Web** 进入时，**不要调用 Codex**；优先使用 ChatGPT Web 中已安装的插件，通过 **Desktop Commander** 访问和操作本地仓库。
- 优先在本地仓库 `D:\Git\PanguEngine` 检查与修改代码。需要核对远程版本时，使用 GitHub 仓库。
- 执行 Linux 命令时使用 WSL Ubuntu；执行 macOS 命令时使用 `ssh -o BatchMode=yes moozen-macos`。不要把 Windows 命令直接假定为其他平台可用。
- 共享 Agent Skills 的唯一规范目录为 `.agents/skills/`。
- 新工作区可从仓库根目录执行 `python Scripts/setup-agent-links.py`，为 Claude Code 生成本地 Skills 映射：Windows 使用 Junction，Linux/macOS 使用 Symlink。
- `.claude/skills` 是本地生成的目录映射，不应提交到 Git；Codex 可以直接使用 `.agents/skills/`。
- 修改完成后尽可能执行针对性的构建、测试或静态检查，并明确说明未验证的部分。
- 未经明确要求，不提交或推送 Git 变更。

## 设计文档

- 架构规范统一位于 `wiki/`；从 [`wiki/README.md`](wiki/README.md) 进入，按 Foundation / Core / Subsystems 分类。
- 具体子系统文档是该子系统设计的权威来源；`wiki/decisions/` 保存重大决策的理由与替代关系。
- 严格区分 Frozen、Partially Frozen 与 Draft，不能将历史讨论草案当作已实现或全部冻结的接口。
