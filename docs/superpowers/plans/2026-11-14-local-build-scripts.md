# 各平台本机构建脚本实施计划

> 执行方式：按 superpowers:executing-plans 在当前会话实施，完成后统一评审；不拆分实施子代理。

**目标：** 用户可在各平台本机终端查看构建清单，并执行现有 CMake 构建和验证入口。

**架构：** PowerShell 与 Bash 两个薄封装只负责参数校验、显示命令和顺序调用 Preset。CMake/Ninja 继续管理所有依赖；不增加下载、远程调度、清理或新的构建内核。

**技术：** Windows PowerShell 5.1+、Bash 3.2+、CMake 4.0+、现有 Ninja/Clang 工具链。

**规格：** [已确认设计](../specs/2026-11-14-local-build-scripts-design.md)。

## 全局约束

- Windows/Android 使用 NTFS 的 Build/Windows、Build/Android；Linux 使用 ext4 的 $HOME/PanguEngineBuild/Linux；macOS/iOS 使用 APFS 的 $HOME/PanguEngineBuild/macOS、iOS。
- Linux/macOS 使用现有 WindowsGit 共享源码，不复制引擎源码或复用异平台缓存。
- 默认 Release；桌面 Modular，移动 Monolithic；每个操作显式设置相关缓存开关。
- 无参数只显示中文帮助；预览不执行外部构建命令；失败立即停止并返回非零退出码。
- 不修改引擎 C++、平台 Preset 或正式测试的既有覆盖。
- 在当前仓库完成已授权修改；不自动提交、推送或合并。

## 评审关注点

- 从任意工作目录启动时，命令仍在脚本所属仓库根目录执行，路径含空格时参数保持完整。
- 缺少参数、未知操作、错误宿主及移动端测试组合应在配置前拒绝。
- 配置、构建、CTest 任何阶段失败时，后续阶段不执行，退出码保留。
- samples/benchmarks/runtime 与普通构建之间切换时，相关开关明确复位。
- mobile-link 仅调用已有最终链接验证，不误称运行验证；Debug 不能只应用于根构建而遗漏嵌套夹具。

## Task 1：脚本与命令边界回归

**文件：** 新建 Scripts/build.ps1、Scripts/build.sh、Tests/Scripts/BuildScriptsTests.cmake；修改 Tests/CMakeLists.txt。

**接口：** 两个脚本统一操作名 help/configure/build/runtime/fast/precommit/full/samples/benchmarks/mobile-link；PS 使用 -Platform/-Action/-Config/-LinkMode/-Target/-Jobs/-Fresh/-DryRun，Bash 使用对应长选项并支持首位置操作名。

- [x] 先编写独立 CMake 脚本测试：帮助无副作用、操作与缓存选择、错误组合、命令预览、任意工作目录、含空格路径及分阶段失败传播。
- [x] 在脚本缺失时运行测试，确认失败原因是缺失待实现入口。
- [x] 实现两入口，接入宿主相应的 CTest precommit/full 集合；跨编译不注册宿主脚本测试。
- [x] 运行宿主脚本测试与 Bash 语法检查；使用隔离的假命令检查阶段顺序及参数边界，不触发真实引擎构建。

## Task 2：使用说明与实际验证

**文件：** 新建 Scripts/README.md；修改 wiki/foundation/build-system.md；更新本计划验收摘要。

**接口：** 文档使用 Task 1 的确切操作名及参数，逐平台给出前置条件、命令与产物目录。

- [x] 列明每种构建用途、移动验证边界、模式切换、fresh、并行数、样例运行与三类基准采样入口。
- [x] 按可用宿主执行针对性真实构建，检查命令和退出结果；不运行无关的完整平台矩阵。
- [x] 按 superpowers:requesting-code-review 统一评审最终变更，修复有证据的问题并重新验证受影响范围。
- [x] 归档结果、未验证范围和遗留问题；确认无运行任务依赖后清理本计划 .superpowers/sdd 临时目录。

## 验收摘要

完成日期：2026-11-14。验证源码基于 `2f78dc79bd4011b8f315ad9732fb623987ee4a29` 加本次变更；用户随后要求提交，最终提交以 Git 历史为准，未推送或合并。

| 验证 | 命令 / 环境 | 结果 |
| --- | --- | --- |
| Windows CLI 回归 | `cmake -DPANGU_SOURCE_ROOT=D:/Git/PanguEngine -DPANGU_TEST_ROOT=D:/Git/PanguEngine/Build/Windows/Tests/LocalBuildScripts -P Tests/Scripts/BuildScriptsTests.cmake`，Windows PowerShell 5.1 | 通过；帮助/预览无外部调用、开关复位、错误组合、含空格脚本路径、跨工作目录及配置/构建/测试失败退出码均覆盖 |
| Linux CLI 回归及语法 | WSL Ubuntu，在 `/home/moozen/WindowsGit/PanguEngine` 运行 `bash -n Scripts/build.sh` 及同一 CMake 测试，测试根 `$HOME/PanguEngineBuild/Linux/Tests/LocalBuildScripts` | 通过 |
| macOS CLI 回归及语法 | 在共享源码根运行同一检查，测试根 `$HOME/PanguEngineBuild/macOS/Tests/LocalBuildScripts`；实际 Bash 为 `/bin/bash` 3.2.57 | 通过；包含 iOS 参数与命令预览 |
| Windows 实际构建 | `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/build.ps1 build -Target PanguEngineHost -Jobs 2` | Release/Modular 配置、宿主工具及真实 EngineHost 依赖编译链接成功，生成 `Build/Windows/Bin/PanguEngineHost.dll` |
| 正式 CTest 接线 | `ctest --preset windows-precommit -R '^LocalBuildScripts$'` | 1/1 通过；具备 precommit/full、owner.engine、area.toolchain 等正式标签 |
| 文档与 Diff | PowerShell 检查新增文档本地链接；`git -c submodule.recurse=false diff --check --ignore-submodules` | 通过 |
| 独立只读评审 | 按已确认设计/计划检查两入口、文档及回归接线 | 无 Critical、Important 或有具体证据的 Minor 问题 |

首轮 RED：脚本不存在，测试明确以 `Missing build entry` 失败；实现后 Windows 回归通过。Linux 首轮测试断言未考虑 `printf %q` 对空格的转义，修正测试显示断言后通过。Windows 实际构建首次受沙箱路径规范化权限限制，按原命令在获准环境重跑成功；未变更构建逻辑绕过校验。

实施裁定：`mobile-link` 拒绝 Debug，因为既有 MyGame 交叉 Preset 固定 Release，不能仅修改根工程后把夹具结果标为 Debug。普通移动配置/构建仍支持 Debug。含空格 Target 由预览覆盖，真实 shim 验证执行顺序、工作目录及退出码；实际工具参数仍通过数组传递。

未执行：Linux/macOS 的真实引擎构建、Android/iOS 的真实编译/最终链接、各宿主的 Full、样例运行及基准采样。本次针对薄封装验证，不宣称完成五平台引擎矩阵或设备验收；未修改 C++，不适用 clang-format/clang-tidy。

后续用户运行 Windows CTest，报告 `PanguLogUnitTests.LogDrain.NormalHistoryReclamationDoesNotInvalidateDeliveredPrefix` 失败；当前 `LastTest.log` 记录测试进程退出码 `0xc0000409`，随后 CMake GoogleTest 启动器无法将该退出描述作为整数传给 `cmake_language(EXIT)`。该问题尚未定位或修复，本次构建脚本提交不包含 LogDrain 修复，不将这轮运行记录为全部通过。

已将有价值结论归档至本节；脚本交付评审未发现阻塞问题。临时执行日志与 ledger 随本计划 `.superpowers/sdd/2026-11-14-local-build-scripts/` 一并清理，正式脚本回归源码及各平台 BuildRoot 测试产物保留。
