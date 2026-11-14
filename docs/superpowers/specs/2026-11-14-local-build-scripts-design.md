# 各平台本机构建脚本

状态：用户已于 2026-11-14 确认；已实现，验收范围见[实施计划](../plans/2026-11-14-local-build-scripts.md)

## 目的与约束

用户目前通过 Codex 发起构建，需要能够自行在终端查看各种构建的用途并直接执行。用户已选择各平台分别在本机运行脚本。

脚本仅为现有 CMake Preset、构建目标和正式验证入口提供便利封装；CMake/Ninja 继续管理依赖和增量构建，不新增构建内核，不实现 SSH/WSL 远程调度。

## 方案选择

- Windows PowerShell 与 POSIX Shell 两个薄入口：无需新增运行时，分别使用宿主已有的 Shell，代价是维护两份参数解析。建议采用此方案。
- 单一 Python 脚本：可共享参数解析，但每台构建宿主需要额外准备 Python；当前需求没有必须引入这一依赖的理由。
- C++ 启动工具：适合未来出现真正的构建逻辑时采用；当前只需调用已有命令，额外引导编译会增加使用步骤。

用户已确认本机执行、两个 Shell 入口及以下接口范围。

## 文件与平台

- `Scripts/build.ps1`：Windows、Android（Windows 宿主）。
- `Scripts/build.sh`：Linux、macOS、iOS（macOS 宿主）；兼容 macOS 自带 Bash。
- `Scripts/README.md`：中文构建清单、前置条件、完整参数和五平台使用示例。

无参数时显示帮助与构建清单，不启动耗时构建。默认平台为当前桌面宿主；移动端显式选择。

Linux 在 WSL Ubuntu 的 `/home/moozen/WindowsGit/PanguEngine` 运行，macOS/iOS 在 `/Users/moozen/WindowsGit/PanguEngine` 运行。脚本定位仓库根目录后调用根 Preset，保持现有五个平台 BuildRoot，不复制源码或缓存。

## 构建清单

| 操作 | 适用平台 | 行为及边界 |
| --- | --- | --- |
| 列表/帮助 | 全部 | 列明可用操作、用途、前提、默认模式与产物目录 |
| 配置 | 全部 | 仅执行对应 configure preset |
| 普通构建 | 全部 | 配置后执行基础 build preset；桌面保持测试开启，移动默认关闭 |
| 纯引擎构建 | 全部 | 显式关闭测试、样例、基准，构建正式目标 |
| fast | 桌面 | 配置、准备 Fast 集合、运行匹配的 CTest preset |
| precommit | 桌面 | 配置、准备 Precommit 集合、运行匹配的 CTest preset |
| full | 桌面 | 配置、准备 Full 集合、运行匹配的 CTest preset |
| 样例 | 桌面 | 开启 Samples，构建 MinimalHost；文档说明运行方式 |
| 基准构建 | 桌面 | 显式开启并构建 Module、Log、Meta 基准；文档分别说明采样命令，构建不代表已测量 |
| 移动最终链接验证 | Android/iOS | 正式根编译后调用现有 CrossCompileTests.cmake；不代表设备运行、打包或签名 |

桌面测试必须先准备对应集合，不能用普通 ALL 构建替代 Full 准备。空选择沿用 CTest 的失败策略。

## 参数与行为

- 支持选择平台、操作、Release/Debug、Monolithic/Modular。
- 支持命令预览、显式 fresh 配置，以及普通构建的定向目标。
- 默认 Release；桌面 Modular、移动 Monolithic，与现有 Preset 一致。
- 每次需要配置的操作显式设置自身依赖的测试、样例和基准开关，避免上一次操作的缓存改变下一次操作的含义；帮助说明会切换当前 BuildRoot 配置。
- 工具链沿用宿主环境变量与既有 CMake 检查，不猜测安装路径或下载依赖。
- 显示执行命令；任一步失败即停止，并返回非零退出码。
- 拒绝不匹配的宿主/目标及无意义的操作组合。
- 不自动删除构建目录；fresh 仅通过 CMake 原生选项显式请求。
- 同一 BuildRoot 的会话串行执行；不新增全平台矩阵或后台任务。

## 验证与交付

实施时核对两种脚本的帮助、命令预览、参数错误、工作目录和失败返回行为，并在可用宿主进行针对性真实构建。通过已有 Preset 验证集成，不因脚本新增而重复执行五平台完整回归；明确记录未执行的宿主和构建类别。

同步构建系统文档的入口链接；不改动引擎 C++ 实现、Preset 的工具链选择或正式测试覆盖。
