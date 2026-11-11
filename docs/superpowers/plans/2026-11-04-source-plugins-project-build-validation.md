# 源码 Plugin 与独立 Project 构建验收记录

> 状态：In Progress（Task 1 基线已测；后续 Task 需追加 B 基线与五平台验收）
> 计划：[2026-11-04-source-plugins-project-build.md](2026-11-04-source-plugins-project-build.md)
> Spec：[源码插件与独立 Project 构建设计](../specs/2026-11-02-source-plugins-project-build-design.md)

## A：改造前 Engine standalone 基线（Task 1，2026-11-04）

- 仓库：`D:\Git\PanguEngine`，分支 `feat/source-plugin-project-build`，HEAD `a258d9e92eb162d8b21b30df273059e894bc866c`；基线前 `git status --short --branch` 为干净状态。
- 当时唯一 Submodule：`Source/ThirdParty/NlohmannJson/Repo`，gitlink `55f93686c01528224f448c19128836e7df245f72`；未安装新编译器或修改现有构建缓存。
- 共有 3 个平台 × 3 个**互不复用的全新 BinaryDir**，均为 Ninja/Release，`BUILD_TESTING=TRUE`，并行度 8。首次 Configure 是**新目录冷配置**；同目录的 `ConfigureWarm` 则是在 Build/CTest 后再次显式 Configure，不把已预热的操作系统缓存伪称为冷机器。
- Windows：NTFS，CMake 4.4.3、Ninja 1.13.2、Clang 23.1.0 / MSVC STL，构建根 `D:\Git\PanguEngine\Build\PlanSourcePlugins\A-windows-<i>`。
- WSL Ubuntu：ext4，CMake 4.4.2、Ninja 1.13.2、Clang 23.1.0 / libc++，源码经 `/home/moozen/WindowsGit/PanguEngine` 的 CIFS 挂载读取，构建根 `/home/moozen/PanguEngineBuild/PlanSourcePlugins/A-linux-<i>`；执行时显式导出既有 `LLVM_ROOT=/opt/llvm-23.1.0`。
- macOS：APFS Data Volume，CMake 4.4.3、Ninja 1.13.2、MacPorts Clang 23.1.2 / libc++，源码经 `/Users/moozen/WindowsGit/PanguEngine` 读取，构建根 `/Users/moozen/PanguEngineBuild/PlanSourcePlugins/A-macos-<i>`。

### 实测数据（单位：秒）

| Host | 样本 | ConfigureCold | ALL | Noop | ConfigureWarm | CTest | Ninja ALL 任务/对象/链接 | CTest 结果 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- | --- |
| Windows | 1 | 10.503 | 15.708 | 0.052 | 0.113 | 1.030 | 62 / 21 / 16 | 13/13 |
| Windows | 2 | 0.927 | 4.861 | 0.033 | 0.115 | 0.168 | 62 / 21 / 16 | 13/13 |
| Windows | 3 | 1.527 | 4.474 | 0.032 | 0.121 | 0.170 | 62 / 21 / 16 | 13/13 |
| Linux | 1 | 0.589 | 3.614 | 0.034 | 0.089 | 0.086 | 62 / 21 / 16 | 13/13 |
| Linux | 2 | 0.327 | 3.093 | 0.033 | 0.082 | 0.062 | 62 / 21 / 16 | 13/13 |
| Linux | 3 | 0.313 | 3.078 | **1.149** | 0.096 | 0.066 | 62 / 21 / 16 | 13/13 |
| macOS | 1 | 2.549 | 24.519 | 0.035 | 0.252 | 4.830 | 62 / 21 / 16 | 13/13 |
| macOS | 2 | 1.728 | 22.141 | 0.045 | 0.246 | 5.243 | 62 / 21 / 16 | 13/13 |
| macOS | 3 | 1.492 | 22.667 | 0.084 | 0.314 | 5.857 | 62 / 21 / 16 | 13/13 |

所有表列中的 Configure、ALL、Noop、CTest 和 ConfigureWarm 退出码均为 **0**。`Ninja ALL 任务/对象/链接` 根据各次**实际首次 Build 日志**计数，包含编译依赖扫描/生成等其它任务；不是仅依据预构建 `ninja -n` 推断。首次构建时 Ninja 的 dyndep 尚未生成，`ninja -n` 可在显示部分任务后因缺少 `CXX.dd` 停止，不能用它当作完整任务总数。

**基线异常（保留，不篡改）：** Linux 样本 3 第一次 `Noop` 实际执行 **18 个 Ninja 任务**（重链接和部分 C++ 对象），并非 `ninja: no work to do.`，但命令退出码和 CTest 仍为 0。随后 `ninja -n -d explain` 指向多个 `CXX.dd` 的记录时间早于 `CXXDependInfo.json`，再次 `cmake --build` 生成部分 dyndep，下一次构建输出 `ninja: no work to do.`。这属于改造**之前**观察到的基线失效/时间戳现象，不能归因于 PanguBuildTool；Task 11 需要独立重复取样并把真正空构建作为硬门槛，不将此样本当作零任务通过。

### 可复跑命令与原始日志

Windows（PowerShell，在仓库根）：

```powershell
cmake --preset windows -B Build/PlanSourcePlugins/A-windows-1
cmake --build Build/PlanSourcePlugins/A-windows-1 --parallel 8
ctest --test-dir Build/PlanSourcePlugins/A-windows-1 --output-on-failure --parallel 8
cmake --build Build/PlanSourcePlugins/A-windows-1 --parallel 8
cmake --preset windows -B Build/PlanSourcePlugins/A-windows-1
```

Linux/macOS 均使用相应宿主 Shell 在共享源码根执行 `cmake --preset linux|macos -B "$HOME/PanguEngineBuild/PlanSourcePlugins/A-<host>-1"`，再执行同目录的 `cmake --build ... --parallel 8`、`ctest --test-dir ... --output-on-failure --parallel 8` 和第二次 Build/Configure；实际采样顺序为 ConfigureCold → ALL → Noop → CTest → ConfigureWarm。重新获取冷配置样本时必须另选**全新目录**，不得在已有基线根清缓存。

- Windows 原始日志、CSV 和命令脚本：`.superpowers/sdd/2026-11-04-source-plugins-project-build-superpowers/{A-windows-*-*.log,baseline-windows.csv,baseline-windows.ps1}`（忽略目录，仅用于 Plan 执行期）。
- Linux 原始日志、CSV：`$HOME/PanguEngineBuild/PlanSourcePlugins/A-logs-linux/`；macOS 原始日志、CSV：`$HOME/PanguEngineBuild/PlanSourcePlugins/A-logs-macos/`，均位于相应宿主本地构建盘。
- Linux/macOS 采样脚本：共享源码的忽略目录 `.superpowers/sdd/2026-11-04-source-plugins-project-build-superpowers/baseline-posix.py`；不会把 Engine 源码复制到其它机器。

## B：改造后数据（2026-11-04）

测量版本为 `88fba18` 加 Task 11 工作区；与 A 相同的 Clang、CMake、Ninja、Release、`BUILD_TESTING=ON`、并行度 8。每组使用三个全新 BinaryDir，不共享工具缓存。所有 ALL 均为 **62 个命令、21 个对象、16 次链接/归档**；新脚本 CTest 不增加 ALL 的 C++ 目标。

| Host / 样本组 | 冷 Configure 三样本（秒） | ALL 三样本（秒） | ALL 中位数 / 最差值 | 普通 noop 三样本（秒） | 重复 Configure 中位数 / 最差值 |
| --- | --- | --- | --- | --- | --- |
| Windows B-resume | 4.691 / 5.145 / 5.135 | 4.937 / 4.673 / 4.508 | 4.673 / 4.937 | 0.032 / 0.032 / 0.032，均零动作 | 0.401 / 0.453 |
| Linux B-resume（并行诊断初轮） | 2.448 / 2.459 / 2.536 | 3.515 / 3.607 / 3.625 | 3.607 / 3.625 | 0.040 / 0.054 / 0.051，均零动作 | 0.121 / 0.126 |
| Linux B-controlled（修复时钟前） | 2.282 / 2.256 / 2.190 | 3.726 / 3.236 / 3.180 | 3.236 / 3.726 | 0.036 / 0.036 / **1.179，第三样本重建 18 项** | 0.121 / 0.138 |
| Linux B-stable（修复时钟后） | 2.517 / 2.937 / 2.350 | 3.658 / 3.416 / 3.438 | 3.438 / 3.658 | 0.037 / 0.040 / 0.036，均零动作 | 0.118 / 0.122 |
| macOS B-resume | 13.331 / 11.699 / 10.368 | 26.887 / 24.653 / 22.737 | 24.653 / 26.887 | 0.152 / 0.126 / 0.135，均零动作 | 0.584 / 0.618 |

Windows 与原 A 的 ALL 中位数相比 **-3.87%**；macOS 为 **+8.76%**，均未超过 10%。冷 Configure 包含首次构建 PanguBuildTool，其成本按设计接受。独立冷工具工程的 Configure / Build 实测分别为 Windows **1.046 / 2.838 秒**、Linux（修复后）**0.228 / 1.779 秒**、macOS **2.112 / 6.764 秒**；这是额外独立样本，不从顶层 Configure 总时间中重复扣减。

### Linux 时钟异常、修复与同条件对照

保留初轮失败：Linux B-resume 的 ALL 中位数相对原 A 增长 **16.62%**，触发调查；B-controlled 又复现了 A 中已有的首次 noop 重建。`ninja -n -d explain` 在独立 D-linux-1 样本发现对象/链接产物时间早于其依赖，并计划重建 8 项。系统日志约每 28 秒报告 `Time jumped backwards, rotating.`，这会使基于时间戳的 Ninja 判定及计时失真。

经用户明确授权，在 WSL Ubuntu 执行 `systemctl disable --now systemd-timesyncd.service`，保留 Hyper-V implicit sync。服务从 enabled/active 变为 disabled/inactive；可用 `systemctl enable --now systemd-timesyncd.service` 回滚。不修改 Windows 时间服务、WSL 内核、clocksource 或工具链，不重启 WSL。依据 [Ubuntu 官方时间同步说明](https://ubuntu.com/wsl/docs/stable/explanation/time-sync/)；同类现象见 [WSL #11790](https://github.com/microsoft/WSL/issues/11790)。停用后观察覆盖多个原回跳周期，系统日志未出现新的回跳；三个全新 B-stable 样本的首次 noop 均为零动作。

**计时条件已变化，不能把修复后的 B 直接与旧时钟状态的 A 作性能放行。** 补测保留历史顶层 CMake 的 A-stable 对照（ALL 3.325 / 3.030 / 2.981 秒），并最终使用交错的同条件 A/B 对照隔离 Bootstrap 成本：在宿主本地生成一个测试顶层 CMake，Source/Tests/cmake/BuildTools 仅通过 Symlink 指向共享 Engine；A 只跳过工具准备及发现调用，B 执行真实 `pangu_add_enabled_plugins` 空根路径，未复制 Engine 源码。两者使用同一个源码路径别名、相同编译命令与扫描设置；每对 `compile_commands.json` 规范化 BinaryDir 后完全相同，均为 62/21/16 个原生命令/对象/链接。

| Linux 同条件交错对照 | ALL 三样本（秒） | ALL 中位数 / 最差值 | 首次 noop 三样本（秒） |
| --- | --- | --- | --- |
| A：无 Bootstrap/发现 | 3.239 / 3.323 / 2.961 | 3.239 / 3.323 | 0.032 / 0.029 / 0.034 |
| B：真实空插件 Bootstrap/发现 | 3.120 / 3.029 / 3.046 | 3.046 / 3.120 | 0.034 / 0.040 / 0.036 |

对照顺序为 A1→B1→B2→A2→A3→B3，各自全新 BinaryDir，未在计时期间运行其它构建；中位数差 **-5.96%**，六次首次 noop 均零动作。旧样本与失败日志全部保留，不把失败首轮改写成通过；此交错对照是时钟修复后的 10% 闸门依据，不将噪声下的负差值宣称为优化收益。

### 插件规模与增量

`SourcePluginIncremental` 正式 CTest 覆盖：未启用源码变化零动作；被消费的 Gameplay 私有 TU 只重编本对象/归档及必要链接；未启用 Manifest/CMakeLists 修改与删除仍自动重配；新目录需显式 Configure；启用列表只改变目标图；隔离副本中工具源码变更重建工具，但不重编 Foundation、Physics 或 Gameplay；再次 Ninja 零动作。隔离复制仅限可变工具输入，Runtime 始终使用真实 Engine 源码。

0/10/100 插件的正式夹具均验证未启用 CMakeLists 不执行、默认 ALL 零编译命令。另行 CLI 微测三次，包含进程启动：

| Host | N=0 耗时 ms / JSON bytes | N=10 耗时 ms / JSON bytes | N=100 耗时 ms / JSON bytes |
| --- | --- | --- | --- |
| Windows | 42.267 / 5.228 / 4.817；16 | 9.693 / 6.537 / 6.645；2785 | 27.548 / 20.404 / 20.593；28185 |
| Linux（修复后） | 1.761 / 1.356 / 1.266；15 | 1.898 / 2.254 / 1.793；2684 | 7.982 / 6.955 / 6.658；27184 |
| macOS | 229.207 / 5.885 / 2.825；15 | 6.319 / 14.567 / 23.653；2704 | 22.550 / 29.084 / 26.543；27384 |

JSON byte 数受宿主绝对路径和换行影响，不作跨宿主定长断言。原始计时、日志和脚本最终归档于 Windows `Build/Windows/PlanSourcePlugins/ValidationLogs/`，Linux `$HOME/PanguEngineBuild/PlanSourcePlugins/{B-logs-*-linux,A-logs-stable-linux,PairedLogs-linux,dyndep-diagnosis}/`，macOS `$HOME/PanguEngineBuild/PlanSourcePlugins/B-logs-resume-macos/`。计时脚本使用 `time.perf_counter()`，实际构建命令为正式 Preset Configure、`cmake --build <dir> --parallel 8`；普通 noop 与显式重配后的扫描动作分开记录，后者不称为普通零动作。

## 五平台集成验收（Task 12）

初轮基于 `0aa72f2` 前后的功能工作区；最终整分支 Review 的五项修复提交为 `fca8669`，下表对应场景已在该修复后的源码上全部复跑。文档同步不改变 C++ Targets。Engine 的 Configure / Build 使用根目录正式 Preset；Project 由正式 fixture 实例化到独立源目录，直接引用同一份 Engine 源码。全部最终表列命令退出码 **0**，没有复制 Engine 到 Mac、创建正式 MyGame 仓库或复用其它顶层的 Cache。

| 目标 / 宿主 | Engine BuildRoot | 正式 Engine CTest | 独立工具 CTest | 独立 Project 实际验证 |
| --- | --- | --- | --- | --- |
| Windows x64 / Windows | `Build/Windows/PlanSourcePlugins/EngineResume` | **22/22** | **3/3**，`Build/Windows/PlanSourcePlugins/ToolFinal` | 顶层/CTest 所有权、Git 锁定、双根启用、Game/Editor 独立 Catalog 运行、Assets、PE DLL 显式部署、增量与规模 |
| Linux x64 / WSL Ubuntu | `$HOME/PanguEngineBuild/PlanSourcePlugins/B-resume-linux-1` | **21/21** | **3/3**，`ToolFinal-linux` | 同上，SHARED 为 ELF；时钟修复后单独重跑 `IncrementalStable` 通过 |
| macOS x64 / Mac | `$HOME/PanguEngineBuild/PlanSourcePlugins/B-resume-macos-1` | **21/21** | **3/3**，`ToolFinal-macos` | 同上，SHARED 为 Mach-O；桌面 Game/Editor 程序实际运行 |
| Android ARM64 / Windows | `Build/Android/PlanSourcePlugins/EngineFinal` | 未运行设备测试，Preset 关闭 BUILD_TESTING | Configure 实际运行 Windows 宿主工具 | Engine STATIC 归档；MyGame/MyEditor 最终链接为 AArch64 ELF，显式 SHARED 构建和文件部署 |
| iOS ARM64 / Mac | `$HOME/PanguEngineBuild/iOS/PlanSourcePlugins/EngineFinal` | 未运行设备测试，Preset 关闭 BUILD_TESTING | Configure 实际运行 macOS 宿主工具 | Engine STATIC 归档；MyGame/MyEditor 最终链接为 ARM64 Mach-O，显式 SHARED 构建和文件部署 |

Windows 比 Linux/macOS 多一项 `SourcePluginBuildToolSourceChange`，其余正式插件集成测试均注册在三桌面 CTest。三个原生工具测试为 `PluginManifest`、`PluginDiscovery`、`PluginCli`；原有 13 个 Engine 测试保留。窗口/完整 EngineHost、PanguMetaTool、生产 SHARED Loader、动态 CodePin、签名、App 打包、Simulator、设备执行/加载/卸载均未据此验收。

### 可复跑命令与宿主/目标隔离

桌面在对应共享源码根执行：

```sh
cmake --preset windows -B Build/Windows/PlanSourcePlugins/EngineResume
cmake --build Build/Windows/PlanSourcePlugins/EngineResume --parallel 8
ctest --test-dir Build/Windows/PlanSourcePlugins/EngineResume --output-on-failure --parallel 4
```

WSL/macOS 将 Preset 换为 `linux` / `macos`，BuildRoot 换为表列的宿主本地路径；日志分别在 `$HOME/PanguEngineBuild/PlanSourcePlugins/FinalLogs-linux/`、`FinalLogs-macos/`。插件脚本测试各自创建随机子目录，可重复执行，不共享可变 fixture。

移动端先在相应宿主从 Engine 根执行 `cmake --preset android-arm64|ios-arm64 -B <表列 BuildRoot>` 与 `cmake --build <dir> --parallel 8`，再执行正式编译型测试：

```sh
cmake -DPANGU_ENGINE_SOURCE_DIR=<共享 Engine 根> -DPANGU_TEST_ROOT=<宿主隔离 Project 根> -DPANGU_TARGET_PRESET=android-arm64 -P Tests/CMake/SourcePluginProject/CrossCompileTests.cmake
```

iOS 使用 `PANGU_TARGET_PRESET=ios-arm64`。Android Project 根为 `Build/Android/PlanSourcePlugins/ProjectFinal/<run-id>`；iOS 为 `$HOME/PanguEngineBuild/iOS/PlanSourcePlugins/ProjectFinal/<run-id>`。各自保存 `TargetCommands.txt`、独立工具和目标的 `compile_commands.json`，以及实际目标产物；这里只检查目标文件和链接，不尝试在宿主运行移动二进制。

- Android 实际目标命令使用 NDK `30.0.16248370` 自带 Clang 21，`--target=aarch64-none-linux-android29 --sysroot=.../ndk/.../sysroot -std=c++23 -fno-exceptions`。宿主工具编译则为 `D:/Developer/LLVM/bin/clang++.exe`（23.1.0）、MSVC STL/CRT、`TOML_EXCEPTIONS=0`；宿主工具能够执行并发现 Physics。LLVM 文件头检查确认 MyGame 为 `elf64-littleaarch64` / `EM_AARCH64`。
- iOS 使用 `/opt/local/libexec/llvm-23/bin/clang++`（23.1.2），目标命令包含 `-arch arm64`、iPhoneOS SDK 和 iOS Deployment Target；独立工具使用同一宿主编译器但不继承 iOS Toolchain/SDK/arch 参数，并可在 Mac 执行。原始日志在 `$HOME/PanguEngineBuild/iOS/PlanSourcePlugins/Logs/`。

### 实现与质量边界

`pangu_add_enabled_plugins` 的根参数是持有 `Plugins/` 的工程根，与 CLI `--engine-root` / `--project-root` 一致；Spec §6.2 草案例子直接传 `Plugins/` 的写法不作为最终接口。运行时 ModuleId 与 Plugin ID 分离；真实 STATIC/SHARED Library 各一份，`Pangu::` 只作 Alias，未新增 Module.toml、Plugin 依赖 DAG、AbiProfile 或通用 POST_BUILD。

新增 C++ 已使用仓库 clang-format 与实际编译数据库运行 clang-tidy；Task 9 修复 include-cleaner/容器边界提示后无用户代码诊断，Task 10 SHARED fixture 检查通过。Review 前独立工具六个 TU 的 clang-tidy 退出码为 0，有 65 条 warning；修复后的最终运行退出码仍为 0，剩余 **64 条 warning**（包括 include-cleaner、const-correctness、已检查边界处的指针/容器访问建议），作为非阻断项延期，没有把 warning 省略成无诊断通过。仅 Windows CRT 固定 `wmain` 名称和外部链接的两个误报做了就近、最小范围豁免，并注明原因。现行文档区分最小构建接线与尚未实现的生产 Loader；历史 Spec/ADR 的原有事实不改写。

## 最终整分支 Review 与修复验收

按用户选择的 executing-plans 进行一次 fresh-context 整分支 Review，范围为 `da7a0e4..0aa72f2` 加当时 Task 12 暂存区。Reviewer 报告 **0 Critical、5 Important、0 单列 Minor**；五项均由执行者独立复现 RED，再在唯一 fix pass 修复，不进行第二次 Review：

| Important | 正式回归与修复 | 最终验证 |
| --- | --- | --- |
| Windows Unicode 路径 | PluginCli 覆盖中文/Emoji 根、目录、JSON 路径和失败诊断；原生宽字符入口及 UTF-8 边界 | RED 0xc0000409/路径错误 → GREEN，三桌面工具各 3/3 |
| Linux 工具误用 libstdc++ | SourcePluginBuildTool 检查匹配 libc++ 头与 ldd；重用宿主 Linux Toolchain，迁移旧私有缓存 | RED 无 libc++ 参数 → GREEN；实际链接 LLVM23 的 libc++.so.1/libc++abi.so.1，无 libstdc++ |
| 锁误接受父 Git 仓库 | EngineRevisionTests 拒绝父仓库内未纳管的 Engine 副本，保留 linked worktree 正例 | RED 错误接受 → GREEN |
| Engine.commit 不失效 | SourcePluginProject 只修改锁文件后请求 Ninja，必须重配并拒绝无效新锁 | RED no work → GREEN |
| Manifest symlink 入口未追踪 | SourcePluginBuildToolInvalidation 覆盖删除、改指较早目标及恢复；同时追踪入口/真实目标，必要时追踪插件自身目录项 | Linux/macOS RED no work → GREEN，普通 Private 源码仍零动作；Windows 常规失效测试连续两次通过，symlink 子案例因系统权限不足跳过 |

最终完整 CTest 为 Windows **22/22**、Linux/macOS 各 **21/21**，独立工具三宿主各 **3/3**；Android/iOS 的 Engine、Game/Editor 链接和 SHARED 部署均在修复后复跑通过。Windows/WSL/macOS 源码在最后整套回归期间保持稳定。新增断言最初读取错误输出变量导致的误报已修正，保留首轮失败记录后整套重跑；Mac 忽略区驱动的 Bash 3 空数组问题在完整 CTest 已通过后修正，并单独执行尚未运行的工具三项测试，不将驱动失败隐去。

命名 CTest 通过数不意味着所有条件子案例均执行：最终 `ctest -V -R '^SourcePluginBuildToolInvalidation$'` 明确报告 Windows `A required privilege is not held by the client`，故仅该平台的 symlink 删除/改指子案例未运行，未修改 Windows 安全策略。三桌面其余命名测试及 Linux/macOS 的 symlink 回归实际执行通过。另在干净提交 `fd805c7` 上实例化真实 `StrictProjectFinal`，写入精确 Engine SHA，使用默认 `PANGU_ENGINE_ALLOW_LOCAL_OVERRIDE:BOOL=OFF` 完成 Configure/Build；原始日志为 `strict-final-{configure,build}.log`。

iOS `MyGame.app/MyGame` 的 `file` / `otool` 检查为 ARM64 Mach-O，`LC_BUILD_VERSION` 为 platform 2、minOS 18.0、SDK 18.2；只依赖系统 libc++/libSystem，没有 `/opt/local` 运行库。CMake 生成的这个 `.app` 目录不代表签名、安装或完整发布验收。独立工具编译命令不含 iPhoneOS SDK 或 `-arch arm64`。

### Linux libc++ 修复后的最终性能补测

标准库纠正会改变工具冷构建条件，因此使用最终修复源码、无其它构建并发，另取三个真实 Engine B 样本（`B-reviewed-linux-1..3`）：

| 阶段 | 三样本（秒） | 中位数 / 最差值 |
| --- | --- | --- |
| 首次 Configure（含真实宿主工具编译/发现） | 2.391 / 2.309 / 2.312 | 2.312 / 2.391 |
| 默认 ALL | 2.879 / 3.363 / 3.207 | 3.207 / 3.363 |
| 普通 noop，均零动作 | 0.031 / 0.034 / 0.040 | 0.034 / 0.040 |
| 重复 Configure | 0.127 / 0.124 / 0.134 | 0.127 / 0.134 |

匹配 libc++ 的独立冷工具 Configure / Build 为 **0.238 / 2.085 秒**。同条件交错对照再次各取三个全新样本，仍校验 A/B 编译命令相同且均为 **62/21/16**：A 的 ALL 为 **3.008 / 2.870 / 2.874 秒**，B 为 **2.897 / 2.875 / 2.872 秒**，中位数 **2.874 → 2.875 秒（+0.035%）**，未超过 10%；六次首次 noop 全部零动作。该结果替代前一轮 libc++ 纠正前的 Linux 放行依据，前面的历史样本仍保留。最终数据分别在 `$HOME/PanguEngineBuild/PlanSourcePlugins/B-logs-reviewed-linux/` 与 `FinalPairedLogs-linux/`，脚本及 Review/修复摘要同时归档到 Windows ValidationLogs。

## Matt 双轴复核后的修复（基于 2dcad41 的后续工作区）

后续 Matt Review 提出 P3（注册回调缺少显式 `[[nodiscard]]`）和 P2（多级链接链路遗漏配置失效）。三个 `RegisterPhysics` / `RegisterGameplay` / `RegisterEditorExtras` 已补齐属性；新增 C++ `PluginPathInputs` 收集每个必需文件的实际目标及完整链接链路的父目录，通过 JSON `configure_inputs` 交给 CMake 登记。原始 Engine/Project 根路径保留至扫描，另用 canonical 根验证边界，覆盖工程根参数本身的别名。元数据缓存仅限单次发现调用，未新增永久索引或构建依赖求解器。

正式 `SourcePluginBuildToolInvalidation` 新增：多级 Manifest/CMakeLists 中间链接改指到较早目标、删除、恢复；Plugins 入口及工程根参数的多级目录链接/Junction 改指；恢复后与 Private 源码修改后的 noop；新增 Plugin 仍需显式 Configure。旧实现及第一次遗漏工程根参数的修复均真实观察到 RED（非法 Manifest 已改指，Ninja 仍 no work），完整修复后 GREEN。目录目标不作为扫描依赖；链接父目录中的其它目录项增删可能额外重配，这是检测链接绑定变化的原生依赖成本。

最终 Windows 完整 CTest **22/22**、Linux/macOS 各 **21/21**，工具三桌面各 **3/3**；三桌面真实 Engine noop 均零动作。Windows 文件 symlink 子案例仍因创建权限不足跳过，但多级目录 Junction、Plugins 入口和工程根入口案例已实际执行，未改变系统权限。Linux/macOS 实际执行多级文件和目录 symlink。Standards 与 Spec reviewer 分别对修复增量只读复核，确认 P3 及 P2（含工程根入口残余）已解决，未提出新的阻断问题。

完整回归最初把 Git 脏工作树警告中的 `Physics.cpp` 误识别为编译动作；测试辅助函数已分离 stdout/stderr，增量断言检查构建 stdout，保留完整诊断与返回码检查，随后三桌面整套重跑通过。clang-format 和 diff 检查通过；clang-tidy 使用实际编译数据库退出 0，新 helper 仅有外部 MSVC SDK `xfilesystem_abi.h` 的枚举组合诊断，三个回调无用户代码诊断；Windows SDK umbrella 的 include-cleaner 豁免仅限原生 Junction 查询函数并就近解释原因。本轮未重新运行 Android/iOS 交叉编译，之前的移动验收保持上述历史源码版本范围。

本轮日志与复核资料位于 Windows `Build/Windows/Review/Matt-main-2dcad41/Fix/`，WSL/macOS 各自 `$HOME/PanguEngineBuild/Review/Matt-main-2dcad41/Fix/Logs/`；未覆盖旧 Review 的快照结论。

## Windows 文件 symlink 管理员补验（2026-11-04）

用户明确授权后，通过 UAC 仅提升本次 CMake 测试进程，未修改开发者模式、PowerShell 执行策略或系统权限配置。普通令牌创建文件 symlink 仍返回权限不足；管理员进程执行相同正式 `BuildToolInvalidationTests.cmake` 全部通过，外层验证器将任何 `symlink cases skipped` 视为失败，最终记录 `PASS / exit=0 / file_symlink_cases=executed`。实际检查 `Plugin.toml`、`Links/Current.toml`、`CMakeLists.txt`、`Links/Current.cmake` 均为 Windows `SymbolicLink`。

补验首次暴露了测试夹具的两个平台差异：Windows 相对链接目标需写入原生反斜杠，正斜杠目标不能被 Win32 正常打开；文件链接中途改指目录时 Windows 返回 Access denied，不能固定断言 Linux 的 regular-file 诊断。夹具已统一转换 Windows 目标分隔符，将 CMakeLists 中间改指负例改为预先存在且可正常打开的越界文件，继续严格断言 discovery 拒绝并报告边界错误；直接目录替代 CMakeLists 的类型验证仍由原 `PluginDiscoveryTests` 保留。

最终实跑覆盖文件 symlink 删除/恢复、多级 Manifest 与 CMakeLists 改指、越界拒绝、目录 Junction/工程根入口改指、私有源码及恢复后的 noop、新增 Plugin 显式发现。Linux/macOS 在夹具修改后定向重跑相同脚本，均退出 0。本次只补跑相关回归，未将结果冒充重新执行全部桌面或移动套件。

此前“Windows 文件 symlink 子案例未实跑”的验证缺口已补齐；普通非管理员进程的权限仍未改变，日常运行在缺少权限时仍会条件跳过。日志、严格验证脚本及最终状态原位于 `Build/Windows/Review/WindowsSymlink-20261104-204343/`；Linux/macOS 定向日志原位于各自 `$HOME/PanguEngineBuild/Review/WindowsSymlink-20261104-204343/`。本轮修复和补验记录已纳入 `ec0c811`。

## 提交与临时构建清理

用户明确要求提交并清理临时构建目录后，已提交 `ec0c811`（完整链接依赖追踪、注册回调诊断、测试夹具与验收记录，共 16 个文件）。提交前 clang-format、暂存 diff 检查及 Windows 定向失效回归退出 0，管理员文件 symlink 补验状态为 PASS。

清理前逐项校验绝对路径归属、活动进程和链接：Windows 删除 `Build/Windows/PlanSourcePlugins/`、`Build/Windows/Review/Matt-main-2dcad41/`、`Build/Windows/Review/WindowsSymlink-20261104-204343/`；WSL 删除 `$HOME/PanguEngineBuild/PlanSourcePlugins/` 及对应两个 Review 目录；macOS 删除相同本机目录和 `$HOME/PanguEngineBuild/iOS/PlanSourcePlugins/`。Windows 先移除 38 个 reparse point 本身再删除目录，POSIX 删除不跟随嵌套 symlink。共享源码、正式 Tests 和其它任务的构建树保留，正式 Windows/Android 主构建目录未删除。

本文上方的临时日志路径是历史执行位置，相关原始日志与缓存已按用户要求清理；长期保留的验收结论、失败原因、平台限制、Review 修复和正式回归源码以 Git 记录为准。未推送或合并。
