# 本机构建脚本

不依赖 Codex，在相应平台的终端运行即可。Windows/Android 使用 [build.ps1](build.ps1)，Linux/macOS/iOS 使用 [build.sh](build.sh)。两个入口只封装根目录 CMake Preset 和正式验证入口，CMake/Ninja 负责增量构建。

无参数、`help` 或列表选项只显示中文帮助，不启动构建：

```powershell
.\Scripts\build.ps1
.\Scripts\build.ps1 help
.\Scripts\build.ps1 -List
```

```sh
bash Scripts/build.sh
bash Scripts/build.sh help
bash Scripts/build.sh --list
```

脚本会定位其所在的仓库根目录，从其他工作目录传入脚本绝对路径也能执行。下文示例默认已进入相应宿主的源码根目录。

## 平台、工具链和产物

| 目标平台 | 在哪里运行 | 参数 | 正式 Preset | BuildRoot | 默认模式 |
| --- | --- | --- | --- | --- | --- |
| Windows | Windows PowerShell 5.1+ / PowerShell 7，`D:/Git/PanguEngine` | `-Platform windows`（默认） | `windows` | `Build/Windows` | Modular |
| Android | Windows，同一源码根 | `-Platform android` | `android-arm64` | `Build/Android` | Monolithic |
| Linux | WSL Ubuntu，`/home/moozen/WindowsGit/PanguEngine` | `--platform linux`（Linux 默认） | `linux` | `$HOME/PanguEngineBuild/Linux` | Modular |
| macOS | Mac 本机终端，`/Users/moozen/WindowsGit/PanguEngine` | `--platform macos`（macOS 默认） | `macos` | `$HOME/PanguEngineBuild/macOS` | Modular |
| iOS | Mac 本机终端，同一共享源码根 | `--platform ios` | `ios-arm64` | `$HOME/PanguEngineBuild/iOS` | Monolithic |

每个平台的动态库和可执行文件进入 `<BuildRoot>/Bin`，静态库进入 `<BuildRoot>/Lib`，生成代码进入 `<BuildRoot>/Generated/PanguMeta/<Config>/<CMakeTarget>`。编译数据库位于 `<BuildRoot>/compile_commands.json`。普通输出名称不添加平台名，扩展名遵循目标平台规则。

运行前需要：

- CMake 4.0+、Ninja、Git 和已初始化的仓库依赖；这些程序应能从当前终端的 PATH 找到。
- Windows 桌面使用 LLVM Clang 23、MSVC STL 和 Windows SDK；需要时从 VS Developer PowerShell 启动。
- Linux 使用 LLVM Clang 23 及匹配的 libc++；macOS/iOS 使用 MacPorts LLVM Clang 23。设置 `LLVM_ROOT`，其 `bin` 内应有 `clang++` 和 `clang-scan-deps`。
- Android 目标编译器使用官方 NDK Clang，设置 `ANDROID_HOME`、`ANDROID_NDK_HOME`；宿主构建工具仍需要有效的 `LLVM_ROOT`。当前 Preset 为 ARM64、API29、`c++_shared`。
- iOS 需要有效的 iPhoneOS Device SDK；可通过 `PANGU_IOS_SDK_PATH` 指定。当前 Preset 为 arm64、deployment target iOS18.0。

工具链安装方式及具体校验以 [Toolchain](../wiki/foundation/toolchain.md)、[Apple 工具链](../wiki/foundation/apple-toolchain.md)、[Android 工具链](../wiki/foundation/android-toolchain.md)为准。脚本不下载或猜测工具链。首次配置可能需要编译宿主构建工具，耗时会明显长于增量构建。

Linux 构建产物写入 WSL ext4，macOS/iOS 写入 Mac APFS；二者只从 WindowsGit 共享源码读取输入。不从 `/mnt/d/Git/PanguEngine` 运行同一 Linux 构建树，不把源码复制到 Mac 临时目录构建。

## 各种构建的区别

| 操作 | 适用范围 | 执行内容 | 适合什么时候用 |
| --- | --- | --- | --- |
| `configure` | 五平台 | 仅 `cmake --preset <平台>` 并设置所选配置 | 检查环境、重新生成构建图和编译数据库 |
| `build` | 五平台 | 配置后普通 ALL 构建；可指定单个实际 Target | 日常编译或只编译改动目标；不会自动运行测试 |
| `runtime` | 五平台 | 关闭测试、样例、所有基准开关，配置并构建正式目标 | 只需要引擎库，减少测试目标构建成本 |
| `fast` | 三桌面 | 配置 → `<平台>-fast` build preset → 同名 test preset | 高频快速检查 |
| `precommit` | 三桌面 | 配置 → `<平台>-precommit` build preset → 同名 test preset | 提交前检查，累计包含 fast |
| `full` | 三桌面 | 配置 → `<平台>-full` build preset → 同名 test preset | 当前平台/配置的完整回归，累计包含 precommit；耗时可能较长 |
| `samples` | 三桌面 | 开启 `PANGU_BUILD_SAMPLES`，构建 `PanguMinimalHost` 及其服务依赖 | 编译最小 EngineHost 样例；不自动运行 |
| `benchmarks` | 三桌面 | 开启普通/Meta 基准，构建 `PanguModuleBenchmarks`、`PanguLogBenchmarks`、`PanguMetaAccessBenchmarks` | 准备性能测量程序；不自动采样 |
| `mobile-link` | Android/iOS | 开启根编译型测试并构建，再显式运行 `CrossCompileTests.cmake` | 检查真实 Runtime/业务 Module、目标架构、动态依赖和最小宿主最终链接 |

`fast/precommit/full` 始终先准备对应测试集合，再运行匹配的 CTest Preset，空集合会报错。普通 ALL 构建不保证独立宿主工具测试已准备，不能在普通 `build` 之后直接把基础 CTest 结果当成 Full 验收。

`mobile-link` 沿用现有固定 Release 的正式夹具，仅支持 Release。它只做编译、最终链接和宿主检查，不运行设备程序，不执行 APK/IPA 打包或签名。验证产物在 `<BuildRoot>/Tests/ModuleLinkModes/<模式>/` 下；源码 Git 锁校验保持开启，正式验收应使用已提交、干净的源码。普通移动 `build/runtime/configure` 支持 Debug。

## 完整参数

| 含义 | PowerShell | Bash | 默认值或限制 |
| --- | --- | --- | --- |
| 操作 | 首位置参数或 `-Action` | 首位置参数或 `--action` | 无参数为帮助 |
| 平台 | `-Platform` | `--platform` | 当前桌面宿主；移动显式指定 |
| 编译配置 | `-Config Release/Debug` | `--config Release/Debug` | Release |
| 统一链接模式 | `-LinkMode Modular/Monolithic` | `--link-mode Modular/Monolithic` | 桌面 Modular，移动 Monolithic |
| 单目标构建 | `-Target <目标>` | `--target <目标>` | 仅 `build`；传真实 Target，例如 `PanguEngineHost`，不传 `Pangu::EngineHost` Alias |
| 并行数 | `-Jobs <正整数>` | `--jobs <正整数>` | 省略时沿用 Preset/环境；测试 build preset 当前为 1，不自动放大并行度 |
| 重新初始化配置 | `-Fresh` | `--fresh` | 默认关闭；对应 CMake 原生 `--fresh`，不删除整个构建目录 |
| 命令预览 | `-DryRun` | `--dry-run` | 默认关闭；不执行构建，不要求工具链已就绪 |
| 帮助/列表 | `-Help` / `-List` | `--help` / `--list` | 只显示说明 |

脚本会显示源码目录、BuildRoot 和每条实际命令。任一步失败立即停止并返回其退出码；非法参数或组合返回 2，缺少命令返回 127。Bash 的配置及模式参数按表中大小写填写。

所有构建默认 Release。`Modular` 将 Pangu Module 构建为原生动态库；`Monolithic` 为原生静态库，不表示把全部 Module 合成一个库。模式、编译配置、样例与基准会重配同一平台 BuildRoot，**不要与其他使用同一 BuildRoot 的 configure/build/test 会话并发运行**。

`-Jobs/--jobs` 调整本次显式构建的并行数；`mobile-link` 还将其传给既有夹具。CTest 并行和测试内部处理器预算仍由现有配置管理。

## Windows：常用命令

```powershell
Set-Location D:/Git/PanguEngine

# 查看实际命令，不启动构建
.\Scripts\build.ps1 full -LinkMode Monolithic -DryRun

# 日常编译与纯引擎库
.\Scripts\build.ps1 build
.\Scripts\build.ps1 runtime

# Debug 单目标；真正目标名不带 Pangu:: Alias 前缀
.\Scripts\build.ps1 build -Config Debug -Target PanguEngineHost

# 三档测试，按需要选择一种
.\Scripts\build.ps1 fast
.\Scripts\build.ps1 precommit
.\Scripts\build.ps1 full

# 静态模式、样例和基准
.\Scripts\build.ps1 full -LinkMode Monolithic
.\Scripts\build.ps1 samples
.\Scripts\build.ps1 benchmarks

# 更换 LLVM_ROOT 或 SDK 后显式重新初始化配置
.\Scripts\build.ps1 configure -Fresh
```

若本机 PowerShell 执行策略阻止启动仓库脚本，可以只对本次进程使用：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\Scripts\build.ps1 fast
```

## Android：Windows 上交叉编译

在上述 Windows 目录和已设置好 SDK/NDK 环境变量的终端执行：

```powershell
.\Scripts\build.ps1 build -Platform android
.\Scripts\build.ps1 runtime -Platform android -Config Debug
.\Scripts\build.ps1 build -Platform android -LinkMode Modular
.\Scripts\build.ps1 mobile-link -Platform android -LinkMode Monolithic
.\Scripts\build.ps1 mobile-link -Platform android -LinkMode Modular
```

## Linux：WSL Ubuntu 本机执行

先自行打开 WSL Ubuntu 终端，让其登录环境提供 `LLVM_ROOT` 和工具路径：

```sh
cd /home/moozen/WindowsGit/PanguEngine
bash Scripts/build.sh build
bash Scripts/build.sh runtime
bash Scripts/build.sh fast
bash Scripts/build.sh precommit
bash Scripts/build.sh full --link-mode Monolithic
bash Scripts/build.sh build --config Debug --target PanguEngineHost
bash Scripts/build.sh samples
bash Scripts/build.sh benchmarks
```

## macOS / iOS：Mac 本机执行

在 Mac 终端使用已有 SMB 源码挂载；不需要通过 Windows 脚本转发：

```sh
cd /Users/moozen/WindowsGit/PanguEngine
bash Scripts/build.sh build
bash Scripts/build.sh fast
bash Scripts/build.sh precommit
bash Scripts/build.sh full --link-mode Monolithic
bash Scripts/build.sh samples
bash Scripts/build.sh benchmarks

bash Scripts/build.sh build --platform ios
bash Scripts/build.sh runtime --platform ios --config Debug
bash Scripts/build.sh build --platform ios --link-mode Modular
bash Scripts/build.sh mobile-link --platform ios --link-mode Monolithic
bash Scripts/build.sh mobile-link --platform ios --link-mode Modular
```

## 运行样例

`samples` 只构建；运行前进入 Bin，样例日志将写到其工作目录下的 `MinimalHostLogs`。

Windows 默认 Modular，需要传入服务动态库的绝对路径：

```powershell
.\Scripts\build.ps1 samples
Push-Location Build/Windows/Bin
try { .\PanguMinimalHost.exe (Join-Path (Get-Location).Path 'PanguMinimalServices.dll') }
finally { Pop-Location }
```

Linux 对应 `libPanguMinimalServices.so`，macOS 对应 `libPanguMinimalServices.dylib`：

```sh
bash Scripts/build.sh samples
(cd "$HOME/PanguEngineBuild/Linux/Bin" && ./PanguMinimalHost "$PWD/libPanguMinimalServices.so")
# Mac 本机改用：
(cd "$HOME/PanguEngineBuild/macOS/Bin" && ./PanguMinimalHost "$PWD/libPanguMinimalServices.dylib")
```

如果用 `samples -LinkMode Monolithic` / `samples --link-mode Monolithic` 构建，则直接运行 `PanguMinimalHost`，不传动态库路径。

## 运行基准

`benchmarks` 只准备三个程序。Module/Meta 基于 Google Benchmark；Log 有独立采样协议，不能把 Google Benchmark 参数传给它。

Windows 示例将运行产物保存在 BuildRoot 内：

```powershell
.\Scripts\build.ps1 benchmarks
Push-Location Build/Windows
try {
    # Module 仅冒烟；正式测量改用 --benchmark_repetitions=5，并另存 JSON
    .\Bin\PanguModuleBenchmarks.exe --benchmark_dry_run --benchmark_out=module-smoke.json --benchmark_out_format=json
    .\Bin\PanguLogBenchmarks.exe
    .\Bin\PanguMetaAccessBenchmarks.exe --benchmark_min_time=0.1s --benchmark_repetitions=3
}
finally { Pop-Location }
```

Linux/macOS 先运行 `bash Scripts/build.sh benchmarks`，再进入 `$HOME/PanguEngineBuild/Linux` 或 `$HOME/PanguEngineBuild/macOS`，执行对应的 `./Bin/PanguModuleBenchmarks`、`./Bin/PanguLogBenchmarks`、`./Bin/PanguMetaAccessBenchmarks`，参数与上例相同。

具体测量协议与结果解释见 [Module 基准](../Tests/Module/Benchmarks/README.md)、[Log 基准](../Tests/Log/README.md)、[Meta 基准](../Tests/Meta/Benchmarks/README.md)。dry run 只证明可运行，不是性能基线。

## 恢复日常配置与定向测试

每种操作都会显式设置 `BUILD_TESTING`、`PANGU_BUILD_ENGINE_TESTS`、`PANGU_BUILD_SAMPLES`、`PANGU_BUILD_BENCHMARKS`、`PANGU_BUILD_META_BENCHMARKS`。下次普通 `build` 会关闭样例/基准，并恢复桌面测试 ON、移动测试 OFF；默认 Release 和默认链接模式也会恢复。需要保留 Debug 或另一链接模式时，每次都显式传参。

例如：

```powershell
.\Scripts\build.ps1 build                         # 恢复 Windows 默认日常配置并编译
.\Scripts\build.ps1 build -Platform android       # 恢复 Android 默认配置并编译
```

```sh
bash Scripts/build.sh build                       # 恢复当前桌面默认配置并编译
bash Scripts/build.sh build --platform ios        # 恢复 iOS 默认配置并编译
```

仅想重新配置而不编译时，把 `build` 换成 `configure`。若只需定向测试，可直接使用原生 Preset：

```powershell
cmake --preset windows -DBUILD_TESTING=ON -DPANGU_BUILD_ENGINE_TESTS=ON
cmake --build --preset windows-precommit
ctest --preset windows-precommit -R '^LocalBuildScripts$'
```

Linux/macOS 改为相应 Preset 名。脚本未封装的专用配置、单独宿主工具构建和其他测试筛选继续使用原生 CMake/CTest；参见 [Tests 维护说明](../Tests/README.md)与[构建系统](../wiki/foundation/build-system.md)。
