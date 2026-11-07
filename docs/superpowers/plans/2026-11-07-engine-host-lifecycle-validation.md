# EngineHost 生命周期正式验收

> 被测最终源码：`08fbbfab8c4864d09f5e4df58b1b4ce7f9080097`。实现、正式矩阵、默认恢复和最终复审已完成；三项分支评审发现全部关闭。
> 日期：2026-11-07。Public C++ API/ABI 未冻结；本文仅声明实际执行范围。

关联：[实施计划](2026-11-07-engine-host-lifecycle.md)、[规格](../specs/2026-11-07-engine-host-lifecycle-design.md)、[EngineHost 规范](../../../wiki/core/engine-host.md)、[ADR-0016](../../../wiki/decisions/0016-engine-host-lifecycle.md)。

## 冻结输入与证据位置

正式最终矩阵全部使用 `08fbbfab8c4864d09f5e4df58b1b4ce7f9080097`，Project `Engine.commit` 锁定此 SHA，`local override=OFF`。最终文档提交只归档结果，不改变被测 C++、CMake 或测试脚本。冻结前完整 `git status --porcelain=v1 --untracked-files=all --ignore-submodules=none` 为空，原生码 0；没有忽略子模块来宣称干净。

| 子模块 | 固定 SHA |
| --- | --- |
| GoogleBenchmark | `a6b4a9f00b36c19af9997d68a1cf6e0b5b3d032c` |
| GoogleTest | `063de7e9578f82b369302001269680b4b1553359` |
| NlohmannJson | `55f93686c01528224f448c19128836e7df245f72` |
| TomlPlusPlus | `30172438cee64926dc41fdd9c11fb3ba5b2ba9de` |

四项 `git submodule status --recursive` 均为正常前缀，原生码 0。固定共享源码分支为 `docs/engine-host-lifecycle`，未创建工作树或复制引擎源码。

| 平台 | 正式源码 / BuildRoot | 工具链 |
| --- | --- | --- |
| Windows | `D:/Git/PanguEngine` / `D:/Git/PanguEngine/Build/Windows` | CMake 4.4.3 / 当前 Clang 驱动 23.1.5；CMake 记录 23.1.0 / MSVC STL |
| Linux（WSL Ubuntu） | `/home/moozen/WindowsGit/PanguEngine` / `/home/moozen/PanguEngineBuild/Linux` | CMake 4.4.2 / Clang 23.1.0 / libc++ |
| macOS | `/Users/moozen/WindowsGit/PanguEngine` / `/Users/moozen/PanguEngineBuild/macOS` | CMake 4.4.3 / MacPorts Clang 23.1.2 / libc++ |
| Android | `D:/Git/PanguEngine` / `D:/Git/PanguEngine/Build/Android` | NDK 30.0.16248370 / Clang 21.0.0 / arm64-v8a API29 |
| iOS | `/Users/moozen/WindowsGit/PanguEngine` / `/Users/moozen/PanguEngineBuild/iOS` | MacPorts Clang 23.1.2 / iPhoneOS18.2 SDK / arm64 iOS18.0 |

每个 BuildRoot 的永久主日志为 `Tests/EngineHostValidation/08fbbfab8c48/final-fix/`：每条命令独立 `.log` 记录 cwd、SHA、完整参数、原生退出码和耗时，`summary.json` / `results.json` 提供索引。桌面各模式保留 CMakeCache、LastTest、CTest 清单与 Project 锁，Modular 另核对当轮物理卸载 marker；`coverage-audit.json` 从本轮 LastTest 核对实际用例与锁。历史缓存只用于追溯，不替代本轮 LastTest 的正式锁证据。

Windows 同一路径 `D:/Developer/LLVM/bin/clang++.exe` 当前 `--version` 为 23.1.5，预定义宏为 23/1/5；正式 Monolithic、Modular、restore configure 日志及 `Build/Windows/CMakeFiles/4.4.3/CMakeCXXCompiler.cmake` 则记录 23.1.0。两类证据分别描述当前驱动和 CMake 保存的编译器识别结果，不能据此推断每个增量缓存产物由哪个 patch 版本编译；本轮未逐产物重建其工具链来源，不强行统一为某个 patch 版本。

## 三桌面两模式完整结果

每宿主按 Monolithic → Modular 串行执行 configure/build/CTest，Samples ON；不同宿主使用隔离 BuildRoot。编译/链接数量为根构建日志中实际发生的 CXX TU 编译与链接动作，包含静态归档；CTest 内独立 Project/Module 子图的额外构建保留在正式测试日志中，不将该计数误称为全源码 TU 数。

| 宿主 | 模式 | 根编译 / 链接动作 | full 运行结果 | configure/build/test 原生码 | 测试秒数 |
| --- | --- | --- | --- | --- | --- |
| windows | Monolithic | 54 / 25 | 138/138 | 0 / 0 / 0 | 973.87 |
| windows | Modular | 69 / 29 | 130/130 | 0 / 0 / 0 | 895.62 |
| linux | Monolithic | 54 / 25 | 137/137 | 0 / 0 / 0 | 690.61 |
| linux | Modular | 69 / 29 | 129/129 | 0 / 0 / 0 | 703.49 |
| macos | Monolithic | 54 / 25 | 137/137 | 0 / 0 / 0 | 2060.68 |
| macos | Modular | 69 / 29 | 129/129 | 0 / 0 / 0 | 1587.92 |

各模式均实际运行 Startup、Failure、Shutdown、Scope、Service、Concurrency、MinimalHostSmoke 及 Project 集成。Modular 额外执行真实物理卸载；正式 marker 顺序精确为 `NativeUnload` 后 `TokensReleased`，不以 Manager 返回成功推断库已移除。

| Modular 宿主 | ModuleRegression 中旧 Module + 新 Host Mono 子图 |
| --- | --- |
| windows | 101 项全部通过；日志 `Build\Windows\Tests\Work\ModuleRegression\69841a82056a\commands.log` |
| linux | 101 项全部通过；日志 `/home/moozen/PanguEngineBuild/Linux/Tests/Work/ModuleRegression/ec74d2ece49c/commands.log` |
| macos | 101 项全部通过；日志 `/Users/moozen/PanguEngineBuild/macOS/Tests/Work/ModuleRegression/7cd03d6e5b34/commands.log` |

每模式末次同配置构建原生码 0，无新增 CXX 扫描/编译/重链接。桌面 full/fast 聚合目标仍执行既有 BuildToolTests 的 configure 与 dyndep 生成，不宣称所有 Ninja 动作均为空；模式子图的 no-work 断言由正式 Matrix/Switch 测试检查。Windows 比 Linux/macOS 多出的 CTest 条目来自 Windows 专属构建工具源码变更覆盖；Monolithic 与 Modular 的集合差异来自各自适用的生命周期/加载测试，不删除覆盖以统一数量。

## 移动最终链接与宿主检查

Android/iOS 均先构建正式根 Runtime，再调用版本化的 `CrossCompileTests.cmake` 产生真实 MobileHost。每模式均检查目标架构、EngineHost Start、ModuleManager Create、业务 Query、动态依赖、部署与 CTest 发现数量为 0；未运行交叉 GTest。

| 平台 | 模式 | 根实际编译 / 链接 | 独立 MobileHost 编译 / 链接 | 结果 |
| --- | --- | --- | --- | --- |
| android-arm64 | Monolithic | 1 / 1 | 21 / 6 | configure/build/cross 原生码均 0 |
| android-arm64 | Modular | 20 / 3 | 25 / 6 | configure/build/cross 原生码均 0 |
| ios-arm64 | Monolithic | 1 / 1 | 21 / 6 | configure/build/cross 原生码均 0 |
| ios-arm64 | Modular | 20 / 3 | 25 / 6 | configure/build/cross 原生码均 0 |

本轮移动根 Monolithic 实际编译修复后的 EngineHost；其他未变化对象可命中原生增量缓存；当前 SHA 的独立 Project 重新锁定并完成真实最终链接，不以旧版本统计拼接本轮结果。Android 为 AArch64 ELF `libMobileHost.so`；iOS 为 arm64 Mach-O executable `MobileHost.app/MobileHost`，LC_BUILD_VERSION platform 2。产物名称不包含平台名。

Mono 最终宿主定义 EngineHost Start、ModuleManager Create、QueryGameplay/QueryPhysics；Modular 宿主直接依赖 EngineHost/Module/Foundation，业务 Gameplay/Physics 由显式候选与部署清单提供。官方符号输出同时含 GameplaySession 的 ScopeBorrow/资源访问实现，确认不是空 main 或仅归档。Modular 部署目录仅含 Gameplay/Physics，源库与部署副本 SHA256 一致，没有 EditorExtras、SharedProbe 或 GTest。iOS 依赖没有 `/opt/local` Runtime。

各移动 BuildRoot 的 `Tests/EngineHostValidation/08fbbfab8c48/mobile-artifacts.json` 记录最终文件、SHA256、部署路径和实际构建日志；`Tests/EngineHost/<mode>/<run-id>/` 保留架构、完整符号、Query 导出、依赖、目标命令、Engine.commit 及每个原生命令日志。这里的部署是构建目录复制，不是设备安装、签名、打包或运行。

## 默认恢复与复验

| 平台 | 恢复配置与实际复验 | 原生码 |
| --- | --- | --- |
| windows | Modular；Samples/Benchmark OFF；BUILD_TESTING/PANGU_BUILD_ENGINE_TESTS ON；fast 69/69；末次无变化 build | 全部 0 |
| linux | Modular；Samples/Benchmark OFF；BUILD_TESTING/PANGU_BUILD_ENGINE_TESTS ON；fast 69/69；末次无变化 build | 全部 0 |
| macos | Modular；Samples/Benchmark OFF；BUILD_TESTING/PANGU_BUILD_ENGINE_TESTS ON；fast 69/69；末次无变化 build | 全部 0 |
| android-arm64 | Monolithic；Samples/Benchmark OFF；BUILD_TESTING OFF；构建 PanguEngineHost；独立 Project Mono MobileHost 再次最终链接；末次无变化 build | 全部 0 |
| ios-arm64 | Monolithic；Samples/Benchmark OFF；BUILD_TESTING OFF；构建 PanguEngineHost；独立 Project Mono MobileHost 再次最终链接；末次无变化 build | 全部 0 |

MobileHost 属于 CrossCompileTests 的隔离 Project 构建树，未虚构正式根 MobileHost 目标。所有正式 Tests 与平台 BuildRoot 保留。

## 完整正式命令

以下命令均从列出的正式源码目录执行，原生退出码均为 0。Linux 外层为 `wsl -d Ubuntu -- bash -lc`，macOS/iOS 外层为 `ssh -o BatchMode=yes moozen-macos`；Windows/Android 使用本地 PowerShell。每条命令的完整输出和耗时见对应同名主日志。

### windows

工作目录：`D:/Git/PanguEngine`；BuildRoot：`D:/Git/PanguEngine/Build/Windows`。

```text
cmake --preset windows -DPANGU_LINK_MODE=Monolithic -DPANGU_BUILD_SAMPLES=ON
cmake --build --preset windows-full
ctest --preset windows-full --no-tests=error --output-on-failure
ctest --preset windows-full --show-only=json-v1
cmake --build --preset windows-full
cmake --preset windows -DPANGU_LINK_MODE=Modular -DPANGU_BUILD_SAMPLES=ON
cmake --build --preset windows-full
ctest --preset windows-full --no-tests=error --output-on-failure
ctest --preset windows-full --show-only=json-v1
cmake --build --preset windows-full
cmake --preset windows -DPANGU_LINK_MODE=Modular -DPANGU_BUILD_SAMPLES=OFF -DBUILD_TESTING=ON -DPANGU_BUILD_ENGINE_TESTS=ON -DPANGU_BUILD_BENCHMARKS=OFF
cmake --build --preset windows-fast
ctest --preset windows-fast --no-tests=error --output-on-failure
ctest --preset windows-full --show-only=json-v1
cmake --build --preset windows-fast
```

### linux

工作目录：`/home/moozen/WindowsGit/PanguEngine`；BuildRoot：`/home/moozen/PanguEngineBuild/Linux`。

```text
cmake --preset linux -DPANGU_LINK_MODE=Monolithic -DPANGU_BUILD_SAMPLES=ON
cmake --build --preset linux-full
ctest --preset linux-full --no-tests=error --output-on-failure
ctest --preset linux-full --show-only=json-v1
cmake --build --preset linux-full
cmake --preset linux -DPANGU_LINK_MODE=Modular -DPANGU_BUILD_SAMPLES=ON
cmake --build --preset linux-full
ctest --preset linux-full --no-tests=error --output-on-failure
ctest --preset linux-full --show-only=json-v1
cmake --build --preset linux-full
cmake --preset linux -DPANGU_LINK_MODE=Modular -DPANGU_BUILD_SAMPLES=OFF -DBUILD_TESTING=ON -DPANGU_BUILD_ENGINE_TESTS=ON -DPANGU_BUILD_BENCHMARKS=OFF
cmake --build --preset linux-fast
ctest --preset linux-fast --no-tests=error --output-on-failure
ctest --preset linux-full --show-only=json-v1
cmake --build --preset linux-fast
```

### macos

工作目录：`/Users/moozen/WindowsGit/PanguEngine`；BuildRoot：`/Users/moozen/PanguEngineBuild/macOS`。

```text
cmake --preset macos -DPANGU_LINK_MODE=Monolithic -DPANGU_BUILD_SAMPLES=ON
cmake --build --preset macos-full
ctest --preset macos-full --no-tests=error --output-on-failure
ctest --preset macos-full --show-only=json-v1
cmake --build --preset macos-full
cmake --preset macos -DPANGU_LINK_MODE=Modular -DPANGU_BUILD_SAMPLES=ON
cmake --build --preset macos-full
ctest --preset macos-full --no-tests=error --output-on-failure
ctest --preset macos-full --show-only=json-v1
cmake --build --preset macos-full
cmake --preset macos -DPANGU_LINK_MODE=Modular -DPANGU_BUILD_SAMPLES=OFF -DBUILD_TESTING=ON -DPANGU_BUILD_ENGINE_TESTS=ON -DPANGU_BUILD_BENCHMARKS=OFF
cmake --build --preset macos-fast
ctest --preset macos-fast --no-tests=error --output-on-failure
ctest --preset macos-full --show-only=json-v1
cmake --build --preset macos-fast
```

### android-arm64

工作目录：`D:/Git/PanguEngine`；BuildRoot：`D:/Git/PanguEngine/Build/Android`。

```text
cmake --preset android-arm64 -DPANGU_LINK_MODE=Monolithic -DPANGU_BUILD_SAMPLES=OFF -DPANGU_BUILD_BENCHMARKS=OFF -DBUILD_TESTING=OFF
cmake --build --preset android-arm64
cmake -DPANGU_ENGINE_SOURCE_DIR=D:/Git/PanguEngine -DPANGU_TEST_ROOT=D:/Git/PanguEngine/Build/Android/Tests/EngineHost/Monolithic -DPANGU_TARGET_PRESET=android-arm64 -DPANGU_LINK_MODE=Monolithic -DPANGU_ENGINE_ALLOW_LOCAL_OVERRIDE=OFF -P Tests/CMake/SourcePluginProject/CrossCompileTests.cmake
cmake --build --preset android-arm64
cmake --preset android-arm64 -DPANGU_LINK_MODE=Modular -DPANGU_BUILD_SAMPLES=OFF -DPANGU_BUILD_BENCHMARKS=OFF -DBUILD_TESTING=OFF
cmake --build --preset android-arm64
cmake -DPANGU_ENGINE_SOURCE_DIR=D:/Git/PanguEngine -DPANGU_TEST_ROOT=D:/Git/PanguEngine/Build/Android/Tests/EngineHost/Modular -DPANGU_TARGET_PRESET=android-arm64 -DPANGU_LINK_MODE=Modular -DPANGU_ENGINE_ALLOW_LOCAL_OVERRIDE=OFF -P Tests/CMake/SourcePluginProject/CrossCompileTests.cmake
cmake --build --preset android-arm64
cmake --preset android-arm64 -DPANGU_LINK_MODE=Monolithic -DPANGU_BUILD_SAMPLES=OFF -DPANGU_BUILD_BENCHMARKS=OFF -DBUILD_TESTING=OFF
cmake --build --preset android-arm64 --target PanguEngineHost
cmake -DPANGU_ENGINE_SOURCE_DIR=D:/Git/PanguEngine -DPANGU_TEST_ROOT=D:/Git/PanguEngine/Build/Android/Tests/EngineHost/Monolithic -DPANGU_TARGET_PRESET=android-arm64 -DPANGU_LINK_MODE=Monolithic -DPANGU_ENGINE_ALLOW_LOCAL_OVERRIDE=OFF -P Tests/CMake/SourcePluginProject/CrossCompileTests.cmake
cmake --build --preset android-arm64 --target PanguEngineHost
```

### ios-arm64

工作目录：`/Users/moozen/WindowsGit/PanguEngine`；BuildRoot：`/Users/moozen/PanguEngineBuild/iOS`。

```text
cmake --preset ios-arm64 -DPANGU_LINK_MODE=Monolithic -DPANGU_BUILD_SAMPLES=OFF -DPANGU_BUILD_BENCHMARKS=OFF -DBUILD_TESTING=OFF
cmake --build --preset ios-arm64
cmake -DPANGU_ENGINE_SOURCE_DIR=/Users/moozen/WindowsGit/PanguEngine -DPANGU_TEST_ROOT=/Users/moozen/PanguEngineBuild/iOS/Tests/EngineHost/Monolithic -DPANGU_TARGET_PRESET=ios-arm64 -DPANGU_LINK_MODE=Monolithic -DPANGU_ENGINE_ALLOW_LOCAL_OVERRIDE=OFF -P Tests/CMake/SourcePluginProject/CrossCompileTests.cmake
cmake --build --preset ios-arm64
cmake --preset ios-arm64 -DPANGU_LINK_MODE=Modular -DPANGU_BUILD_SAMPLES=OFF -DPANGU_BUILD_BENCHMARKS=OFF -DBUILD_TESTING=OFF
cmake --build --preset ios-arm64
cmake -DPANGU_ENGINE_SOURCE_DIR=/Users/moozen/WindowsGit/PanguEngine -DPANGU_TEST_ROOT=/Users/moozen/PanguEngineBuild/iOS/Tests/EngineHost/Modular -DPANGU_TARGET_PRESET=ios-arm64 -DPANGU_LINK_MODE=Modular -DPANGU_ENGINE_ALLOW_LOCAL_OVERRIDE=OFF -P Tests/CMake/SourcePluginProject/CrossCompileTests.cmake
cmake --build --preset ios-arm64
cmake --preset ios-arm64 -DPANGU_LINK_MODE=Monolithic -DPANGU_BUILD_SAMPLES=OFF -DPANGU_BUILD_BENCHMARKS=OFF -DBUILD_TESTING=OFF
cmake --build --preset ios-arm64 --target PanguEngineHost
cmake -DPANGU_ENGINE_SOURCE_DIR=/Users/moozen/WindowsGit/PanguEngine -DPANGU_TEST_ROOT=/Users/moozen/PanguEngineBuild/iOS/Tests/EngineHost/Monolithic -DPANGU_TARGET_PRESET=ios-arm64 -DPANGU_LINK_MODE=Monolithic -DPANGU_ENGINE_ALLOW_LOCAL_OVERRIDE=OFF -P Tests/CMake/SourcePluginProject/CrossCompileTests.cmake
cmake --build --preset ios-arm64 --target PanguEngineHost
```

## 最终分支评审修复与重新验收

评审基线 `778406b262d2461d4b590de0b10fb56888b71fe2` 的三项发现均已处理，代码冻结为 `08fbbfab8c4864d09f5e4df58b1b4ce7f9080097`：

- `FailStartup` 保留启动首错拥有型快照，并将最后实际执行的同步 Begin/Finish 底层报告发布到当前 ModuleReport；只改报告保存，不改清理次数、所有权、Gate、状态或公开签名。
- 正式失败测试补充首次 pending 的 ManagerShutdown 与原 CallerUnload 分离、已完成关闭的 Unloaded、业务预算拒绝无启动底层报告但有当前清理报告，以及无 Manager 不伪造报告。返回值、PrimaryError、StartupFailure.Cause 仍为首错。
- Windows 编译器双版本证据及缓存来源限制如前表说明；ADR0016 已并入连续 Markdown 表格。

冻结前先执行 `cmake --build --preset windows-fast --target PanguEngineHostTests`，再执行 `ctest --preset windows-precommit -R 'PanguEngineHostTests[.](EngineHostFailure|Shutdown)[.]' --no-tests=error --output-on-failure`。新增断言在原实现真实 RED：7/10，原生码8；失败为 ResidentAndBusinessStagesStopAtFirstError、BusinessLimitChecksBeforeQuery、NonBusyStopFailurePreservesStartupFailure。最小修复后同命令 GREEN 10/10、原生0；最终 Windows fast 69/69、原生0。

上述定向不计入正式 full。五平台新矩阵全部从统一新 SHA 重跑并完成默认恢复，未拼接旧 SHA。旧 `543678f84f96ad95051e1916fc4b66d414c44f48` 的三桌面双模式、移动双模式及恢复结果仅作为历史，日志继续保留在各 BuildRoot 的 `Tests/EngineHostValidation/543678f84f96/v1/`。本轮正式矩阵没有失败重跑；历史故障说明如下。

## 历史首轮失败、修复与重跑

首轮冻结 `897a22e97684c4c53af64e2ade5573cce4ccfac6` 已含两项测试 Minor 补强与规范。Windows Mono 138/138、Linux Mono 137/137、两移动双模式和恢复通过；这些是历史结果，不计入上面的最终 SHA 矩阵。

1. macOS 首次 configure 原生码 1，CMake 报相同源码路径不匹配旧缓存。核对正式挂载路径与 CMAKE_HOME_DIRECTORY 后保留旧 cache，运行 `cmake --fresh --preset macos -DPANGU_LINK_MODE=Monolithic -DPANGU_BUILD_SAMPLES=ON` 原生码 0，再次正常配置。属于缓存识别故障；内部文件身份原因未进一步孤立，未改源码。旧日志位于 macOS `Tests/EngineHostValidation/897a22e97684/v1/`，后续旧轮为 v2。
2. Linux Modular 首轮 full 原生码 8，128/129。唯一失败是 `EngineHostFailure.InvalidBusinessBatchReleasesAlreadyLoadedCandidate` 无条件要求 `Error.Native()` 为真。POSIX Loader 的实际实现只返回确定 ModuleError，立即消费 dlerror 文本；[Module API §6.3](../../../wiki/core/module-api.md) 与 `Source/Runtime/Module/Private/Platform/Posix/NativeModuleLibrary.cpp` 的 Open 失败分支明确该契约。
3. 仅修正测试：Windows 仍要求 Native 存在，POSIX 明确要求不存在；保留 Domain/Code、报告完整等于返回错误、Query/注册/资源/真实卸载和最终状态断言。没有改变生产行为或降低清理覆盖。当时修复提交为 `543678f`，属于历史验收基线。
4. 修改前仅停止本任务自身 Windows Modular 与 macOS Monolithic 活动进程；它们记作受控中止，不能记作完整通过。旧 full 输出、CTest 原始/临时日志及 Linux 失败完整日志均保留在 897a22e 目录。
5. 修复后 Windows 定向 1/1，Linux/macOS Modular Host 与 ScopeResourceAccess 预检各 28/28，构建/测试/format/tidy 原生码均 0。预检单独保留在两 POSIX BuildRoot 的 `Tests/EngineHostValidation/preflight-posix-fix/`，不冒充正式 full。之后重新提交、完整状态冻结，再从 Mono→Modular 重做三桌面及两移动所有正式轮。

## Review Focus 与两类 Minor

| 关注点 | 正式证据与最终实际平台 |
| --- | --- |
| 元数据、同类型不同声明、无 Service Module | MetadataAndIdentityAreExact、ServiceLessBusinessParticipatesNormally；三桌面双模式通过 |
| 失败槽位复用、跨宿主同声明 | FailedScopeReuseDoesNotAlias；三桌面双模式通过 |
| 同报告重入 Busy，外层报告不污染 | Shutdown/EngineHostScope.SameReportReentryKeepsOuterResult；三桌面双模式通过 |
| 规格补强：启动 E1 与后续非 Busy E2 分离 | Shutdown.NonBusyStopFailurePreservesStartupFailure；三桌面双模式通过 |
| 规格补强：新具名令牌跨真实物理卸载 | EngineHostLifetime.QueriedAccessSurvivesPhysicalUnload；三桌面 Modular 的真实 NativeUnload→TokensReleased marker 通过 |
| Task3 Review Minor | 常驻失败仍配置业务候选，QueryCount=1、NativeUnloadCount=0；六阶段测试三桌面双模式通过 |
| Task5 Review Minor | 每次竞态 TryBorrow 检查成功值/失败错误；保留成功竞态 Borrow，初始 Borrow 释放后仍阻止关闭；三桌面双模式通过，不要求调度必然成功 |

## 格式、链接与静态诊断

本计划 51 个变更 C++ 文件使用仓库 `clang-format --dry-run --Werror` 最终复验，原生码 0；本轮三项变更 TU 使用实际 Windows DB 重新 tidy，各原生码0且无新增显示诊断。最终 11 个文档的 159 项相对链接检查无错误，`git diff --check` 原生码 0。计划断言由完整正式测试 TU 执行，不以未编译片段作通过。此前同代码实际编译数据库检查可复用，运行矩阵则全部重新绑定最终 SHA。静态检查范围为 Windows 实际 DB 与 MobileHost Android 实际 DB，不声称对每个平台全部 TU 重复运行 tidy。

## 静态检查逐 TU 证据

所有命令均使用根 `.clang-tidy`，形式为 `clang-tidy -p <DB> <TU>`；下表原生码均为 0。DB=Root 表示 `Build/Windows/compile_commands.json`；Fixtures 为该正式测试生成的实际编译数据库，不伪造独立编译命令。本轮重新检查 EngineHost.cpp、EngineHostFailureTests.cpp、EngineHostShutdownTests.cpp；其余 TU 复用最后变更后已通过的实际 DB 检查，旧证据目录与诊断保留。旧 warning 保留，不扩大修复。

| TU（相对仓库） | 实际 DB / 检查阶段 | 显示诊断 |
| --- | --- | --- |
| Source/Runtime/Module/Private/ScopeResourceAccess.cpp | Root / Task1 final | 0 |
| Source/Runtime/Module/Private/ScopeRuntime.cpp | Root / Task1 final | 4 条既有 const/loop |
| Source/Runtime/Module/Private/ModuleManager.cpp | Root / Task1 final | 5 条既有 const/loop/嵌套条件 |
| Tests/Module/ScopeResourceAccessTests.cpp | Root / Task1 final | 0 |
| Tests/Module/Fixtures/ResourceAccess/ResourceAccessFixture.cpp | Root / Task1 final | 0 |
| Source/Runtime/EngineHost/Private/EngineHostModule.cpp | Root / Task2 final | 0 |
| Tests/EngineHost/EngineHostStartupTests.cpp | Root / Task2 final | 0 |
| Source/Runtime/EngineHost/Private/EngineHost.cpp | Root / final-fix | 0 |
| Source/Runtime/EngineHost/Private/HostServiceAccess.cpp | Root / Task4 final | 0 |
| Tests/EngineHost/EngineHostFailureTests.cpp | Root / final-fix | 0 |
| Tests/EngineHost/EngineHostShutdownTests.cpp | Root / final-fix | 0 |
| Tests/EngineHost/EngineHostDestructorFixture.cpp | Root / Task3 final | 0 |
| Tests/EngineHost/Fixtures/HostServiceModule.cpp | Root / Task4 final | 0 |
| Tests/EngineHost/Support/HostTestState.cpp | Root / Task4 | 0 |
| Tests/EngineHost/EngineHostScopeTests.cpp | Root / Task4 final | 0 |
| Tests/EngineHost/EngineHostServiceTests.cpp | Root / Task4 final | 0 |
| Tests/EngineHost/EngineHostLifetimeFixture.cpp | Root / Task5 final | 0 |
| Tests/EngineHost/EngineHostLifetimeTests.cpp | Root / Task5 final | 0 |
| Tests/EngineHost/EngineHostConcurrencyTests.cpp | Root / Task7 v2 | 0 |
| Tests/EngineHost/Fixtures/HostConsumerModule.cpp | Root / Task5 final | 0 |
| Tests/CMake/ModuleLinkModes/Fixtures/Consumer.cpp | Build/Windows/Tests/Work/ModuleLinkModeMatrix/429f2d2be280/Default / Task5 final | 0 |
| Tests/CMake/ModuleLinkModes/Fixtures/QueryEngineHostConsumer.cpp | 同上 / Task5 | 0 |
| Samples/MinimalHost/Main.cpp | Root / Task6 v3 | 0 |
| Samples/MinimalHost/MinimalService.cpp | Root / Task6 v3 | 0 |
| Tests/CMake/SourcePluginProject/MyGame/Source/GameMain.cpp | Build/Windows/Tests/Work/SourcePluginProject/ServiceHosts / Task6 v3，实际复制夹具 TU | 0 |
| Tests/CMake/SourcePluginProject/MyGame/Source/EditorMain.cpp | 同上 | 0 |
| Tests/CMake/SourcePluginProject/MyGame/Plugins/Gameplay/Private/Gameplay.cpp | 同上 | 0 |
| Tests/CMake/SourcePluginProject/MyGame/Plugins/EditorExtras/Private/EditorExtras.cpp | 同上 | 0 |
| Tests/CMake/SourcePluginProject/MyGame/Source/MobileHost.cpp | Build/Android/Tests/EngineHost/Modular/646e423b7c0f/CrossBuild / Task7，实际 MyGame/Source/MobileHost.cpp | ModuleIdentity.h 3 条既有 qualified-auto / pointer arithmetic |

历史 Task1 的 clang-tidy `--fix` 崩溃原生码 -1073741819 保留在静态证据中；后续只读检查成功，不能把崩溃算通过。Task7 两 TU 首轮新增 cstddef include-cleaner 已修，最终无新增显示诊断。原始日志和各任务报告位于正式 Windows BuildRoot 的 `Tests/EngineHostValidation/543678f84f96/static/`。


本轮原始 RED/GREEN、format/tidy 与完整冻结身份日志在 Windows `Tests/EngineHostValidation/08fbbfab8c48/static/`。三个 tidy 命令分别为 `clang-tidy -p Build/Windows Source/Runtime/EngineHost/Private/EngineHost.cpp`、`clang-tidy -p Build/Windows Tests/EngineHost/EngineHostFailureTests.cpp`、`clang-tidy -p Build/Windows Tests/EngineHost/EngineHostShutdownTests.cpp`。首轮新增 include-cleaner/unchecked-optional 诊断已通过直接 include 与显式 guard 修正；最终可见诊断为0，非用户及既有 NOLINT 抑制分别5251、5543、5541条，原生码均0。

## 遗留范围与评审交接

实现覆盖 Application 所有权、单次同步启动、失败清理、精确 Concrete 访问、Scope 局部回滚和显式关闭。Concrete/Contract/Registry 三种既有语义不变，Module 可无 Service。完整 Contract Provider 选择、Registry Contribution、TaskScheduler/GPU 集成、窗口/渲染/主循环和热更新仍属后续。移动设备运行、打包、签名未验证。

最终 whole-branch review 的一项 Important 与两项 Minor 已集中修复，唯一一次 scoped re-review 判定全部 ADDRESSED；修复范围无新增 Critical、Important 或 Minor，也无 out-of-scope 观察。七项任务的独立规格/质量评审均已通过。控制器再次执行 windows-fast build 与 CTest，原生码均0、69/69通过；确认最终文档提交相对08fbbfa无产品代码差异、完整Git与子模块状态正常。日志位于 Windows BuildRoot 的 Tests/EngineHostValidation/controller-close-fast-{build,test}.log。已先归档全部必要结论，验证计划目录绝对路径、owner与全部reparse point，确认没有活动依赖后精确删除本计划临时目录；正式 Tests/BuildRoot 保留，分支原位保留，未 push/merge。

## 执行裁决归档

以下按发生顺序保留控制器裁决、理由及误判代价；没有 parked 或未解决的评审发现。

- Ruling: 沿用固定共享源码目录与现有功能分支，不创建 worktree — 计划明确指定不新建/搬迁且五平台共享同份源码，用户已选择执行该计划 — 若误解则需后续隔离提交，但不影响现有主分支。
- Ruling: 将用户本次选择 SDD 视作实施授权 — 最新明确执行请求覆盖规格/计划的历史待批准文字 — 若误解则实现提交可回退，不自行推送或合并。
- Ruling: 全部任务完成后才清理临时目录，Task 7 内步骤清理延后至最终分支 Review — 满足仓库与技能共同要求且防止丢失最终评审记录 — 若验收未通过保留目录并记录限制。
- Ruling: Task 1 允许新增 ModuleManager 私有只读 helper，供 friend Resolve 调用 — State 完整定义仅在 ModuleManager.cpp，避免迁移布局/空新 cpp，公开 A 不变且不产生 unchecked 入口 — 若增加冗余则后续可合并同 TU 定义。
- Ruling: Task3 若 E1/E2 跨轮断言首跑自然 GREEN，记录事实并以实际重入/析构缺口取得 RED，不删改既有保护 — Task2 已保存 StartupFailure，计划又禁止人为制造失败，规格行为优先于 RED 形式要求 — 若因此漏检回归，则需靠完整断言与后续任务/分支评审发现。
- Ruling: Task3 允许测试通过同一 Private EngineHostTestAccess::Manager 调用既有 TryPinForBusiness 取得真实 Pin，只用于关闭/析构验收 — 本任务要求真实 Pin 且 Task4 桥接未交付，保持 per-instance 私有测试边界，禁止生产 Pin API 或支持库缓存 Pin/令牌 — 若边界理解过宽则测试接缝需后续收窄，不改变产品公开协议。
- Ruling: Task4 父Context关闭测试持有World Borrow以保持子记录pending；仅对实际报告CleanupOwner=ManagerShutdown的操作验证Host转Closing — 复用既有Manager级联清理语义且不推断未报告的私有状态 — 若测试前提不足可能漏检父子约束，需独立评审核对真实pending与报告分支。
- Ruling: 移动设备运行/卸载/打包/签名不纳入本轮 — 规格与计划只要求编译/最终链接及宿主检查，采纳最终评审scope外判断 — 若应用依赖设备行为仍须后续设备验收。
- Ruling: 完整Contract/Registry、调度/GPU排空和热更新保持后续范围 — 当前规格明确排除，采纳最终评审scope外判断 — 若产品依赖这些功能需另行实施与验收。
- Ruling: 不补推每个Windows增量缓存产物的编译器patch版本，记录当前二进制与CMake识别版本差异 — 证据不能还原产物逐个来源，禁止从当前工具版本倒推 — 若必须逐产物精确证明则需另行干净构建与记录。
