# PanguEngine 源码 Plugin 与独立 Project 构建 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.
>
> 状态：Completed · Task 1–12 已完成。采用用户选择的 `superpowers:executing-plans`；已完成一次独立整分支 Review，五项 Important 均经 RED→GREEN 修复，最终三桌面完整 CTest、移动平台适用编译/链接及性能闸门通过。原版本保留于 Git 历史。

**Goal:** 在保留 PanguEngine 独立构建的前提下，以 `Plugin.toml` 稳定识别源码插件，使用 `PanguBuildTool` 配置期解析，并由同一 CMake/Ninja 图按真实消费者构建独立 MyGame 的 Plugin/Module。

**Architecture:** 顶层 Engine 或 Project 负责 Toolchain/CTest；独立构建宿主上的 `PanguBuildTool` 使用 `toml++` 解析两个固定插件根，向 CMake 返回 JSON。CMake 只接入显式启用的插件目录，应用的静态链接和显式动态库依赖决定哪些目标进入构建；运行时加载器与 PanguMeta 不在本期实现。

**Tech Stack:** CMake ≥4.0、Ninja、C++23、Clang（Windows/Linux/macOS/iOS 23.x；Android NDK Clang）、toml++、CTest、Git。

**Spec:** [源码插件与独立 Project 构建设计](../specs/2026-11-02-source-plugins-project-build-design.md)（2026-11-04 版本，Grill Me Q1–Q41 已确认）。

## Global Constraints

- 仅源码交付的 Pangu Plugin；不恢复 PanguBuild、AbiProfile、逐 TU 证据、独立二进制 SDK/Plugin Preflight 或 Plugin 依赖求解器。
- 每个 Plugin 需 `Plugin.toml` + `CMakeLists.txt`；ID **全局唯一**，v1 格式 `^[a-z][a-z0-9]*(?:-[a-z0-9]+)*(?:[.][a-z][a-z0-9]*(?:-[a-z0-9]+)*)*$`；可选版本遵守 SemVer 2.0.0。
- 仅 Engine 的 `Plugins/` 和 Project 的 `Plugins/` 两个根；直接子目录扫描；**未启用插件的 Manifest 同样必须校验**，CMakeLists 不执行。
- **即使零 Plugin，每次顶层 Configure 也必须配置/请求构建并运行 PanguBuildTool**；第一次真实编译，之后 Ninja 判定零重编。普通无变更业务 `ninja` 不触发工具或插件扫描。
- `PanguBuildTool` 是独立 Build Tool，目标工具链与宿主工具链隔离；使用 `toml++` 的 `TOML_EXCEPTIONS=0`；工具及业务源码均遵守 `wiki/foundation/cpp-guidelines.md`、`.clang-format`、`.clang-tidy`，Pangu C++ 文件为 `.h`/`.cpp`、`pg::` 命名空间、简短中文注释。
- 每个 Module 一个真实 `STATIC`/`SHARED` Library；`Pangu::` 仅其 ALIAS，`ThirdParty::` 仅第三方依赖；无关插件不进入默认 `ALL`；资产与部署不使用通用 `POST_BUILD`。
- Engine standalone 保留现有五平台 Preset 与三桌面 CTest。Windows/Android BuildRoot 为 NTFS，Linux 在 WSL ext4，macOS/iOS 在本地 APFS；共享源码不复制到 Mac；不安装新的 Clang/SDK 绕过现有配置。
- 执行时先查 `git status`，保护已有修改/暂存区，串行写同一文件；锁/权限错误先停止。任务测试、原始日志放专属 `.superpowers/sdd/2026-11-04-source-plugins-project-build-superpowers/` 或独立平台 BuildRoot。每个 Task 遵循 RED → 最小实现 → GREEN → Review/Fix → Commit；不自动推送。

**脚本测试命令变量：** 本计划后续脚本命令中的 `$R` 是 Engine 真实源码路径，`$T` 是独立 MyGame fixture 的可写宿主本地根。执行 Windows PowerShell 测试前先设定：

```powershell
$R = 'D:\Git\PanguEngine'
$T = Join-Path $env:TEMP 'PanguSourcePluginsPlan'
Set-Location $R
```

WSL 使用 `R=/home/moozen/WindowsGit/PanguEngine`、`T=$HOME/PanguEngineBuild/PlanSourcePlugins/Fixture`；macOS 使用 `R=/Users/moozen/WindowsGit/PanguEngine`、`T=$HOME/PanguEngineBuild/PlanSourcePlugins/Fixture`。Linux/macOS 测试先在各自 Shell 定义变量并 `cd "$R"`；所有测试输出必须留在宿主本地文件系统，不复制 Engine 源码。`<dir>`、`<40-hex>` 等接口签名中的括号只是形参示意，不属于可执行命令。

## Review Focus

| 最易遗漏的输入/边界 | 预期行为 | 负责验证 |
|---|---|---|
| 两根无 Plugin、Android/iOS 交叉配置 | 仍用可执行的宿主 Clang 构建/运行工具，返回 `{"plugins":[]}`；不把目标 Clang 当宿主 | Task 4 |
| **未启用** Plugin 的 TOML 错误、重复 ID、文件缺失、目录越界 | Configure 失败且报告具体路径，不执行其 CMakeLists | Task 3、5 |
| 已发现的 Manifest/CMakeLists 删除；PanguBuildTool 源码修改 | 下一次 Ninja 自动触发 Configure，正确重解析；无关 Module 不重编 | Task 4、10 |
| Project 嵌入 Engine 时 `project()`、CTest、BuildOptions 重复 | 顶层 Toolchain/CTest 唯一；Engine standalone 仍保留原有默认行为 | Task 6 |
| `EXCLUDE_FROM_ALL`、晚定义 `Pangu::` 依赖、SHARED 部署 | Enabled≠Built；Game 只构建消费目标；明确依赖才能编译和部署动态库 | Task 8、9 |

## 文件/职责地图（执行时按此拆分，不重建第二套构建框架）

| 文件或目录 | 职责 |
|---|---|
| `Source/ThirdParty/TomlPlusPlus/{CMakeLists.txt,README.md,Repo/}` | 固定提交的 toml++，以 `ThirdParty::TomlPlusPlus` 暴露 Header-only 构建接口；独立工具可直接使用 |
| `BuildTools/PanguBuildTool/CMakeLists.txt`、`Private/PluginManifest.{h,cpp}` | 独立工具工程与单个 TOML Manifest 的类型/语义验证 |
| `BuildTools/PanguBuildTool/Private/PluginDiscovery.{h,cpp}`、`Private/PluginJson.{h,cpp}`、`Private/Main.cpp` | 两根扫描、目录安全、ID 去重、稳定 JSON CLI |
| `BuildTools/PanguBuildTool/Tests/` | 原生 CTest：解析、发现、异常输入、CLI 输出格式 |
| `cmake/PanguBuildTool.cmake` | 顶层 Configure 驱动独立宿主工具工程的配置、构建、运行 |
| `cmake/PanguPlugins.cmake` | 调用工具、解析 JSON、登记 Configure 依赖、启用目录和唯一 BinaryDir |
| `cmake/ValidateBuildEnvironment.cmake`、`CMakeLists.txt` | Engine standalone/子目录双模式、Toolchain/CTest/BuildOptions 唯一化 |
| `cmake/PanguEngineRevision.cmake` | Project 所用 Engine Git Commit/工作区干净性校验 |
| `Tests/CMake/SourcePluginProject/` | 独立 MyGame fixture：两个来源插件、STATIC Catalog、SHARED 显式部署；测试入口 |
| `Tests/CMake/SourcePluginProject/RunIntegration.cmake` | CMake/CTest 黑盒测试，实例化 fixture 到隔离根，不改共享源码 |
| `wiki/decisions/0014-source-plugins-project-build.md` 等 | 正式记录替代独立 Pangu 二进制交付范围的决策及规范 |
| `docs/superpowers/plans/2026-11-04-source-plugins-project-build-validation.md` | 测试/性能验收摘要；原始日志不入 Git |

> 现有代码事实：根 `CMakeLists.txt` 为 22 行，包含 `include(CTest)` 和三个运行时依赖子目录；`PanguBuildOptions` 为独立 INTERFACE Target；STATIC Catalog 可复用 `Tests/Module/Fixtures`。本计划的新增函数/CLI 名是**待实现的明确接口约定**，不是现有 API。

## Task 1：冻结无 Plugin Engine 构建基线

**Files:**
- Create: `docs/superpowers/plans/2026-11-04-source-plugins-project-build-validation.md`
- Inspect: `CMakePresets.json`、`CMakeLists.txt`、`Tests/CMakeLists.txt`
- Test: 现有 Windows/WSL/macOS 正式 Engine CTest；无生产代码修改

**Interfaces:**
- Consumes: 五平台现有 CMake Preset 和 `BUILD_TESTING`。
- Produces: A 基线记录：准确 HEAD、五平台工具/文件系统/并行度、各自独立 `configure_s / all_s / noop_s`、CTest；后续 Task 10 使用。

- [x] **Step 1：记录基线环境。** 运行 `git status --short --branch`、`git rev-parse HEAD`、`git submodule status`、`cmake --version`、`ninja --version`；记录当前工具链与 CTest 默认项，确认没有正在使用本 Plan 验证目录的构建进程。
- [x] **Step 2：单独配置 Engine（A 基线）。** Windows：`cmake --preset windows -B Build/PlanSourcePlugins/A-windows-1`，同条件独立目录重复 3 轮；Linux/macOS 分别使用宿主本地的 `$HOME/PanguEngineBuild/PlanSourcePlugins/A-linux-1`、`A-macos-1`。预期当前空 `Plugins/` 不阻止配置；若失败，保留失败记录而不是改工具链或删用户缓存。
- [x] **Step 3：构建并运行现有测试。** Windows：`cmake --build Build/PlanSourcePlugins/A-windows-1`，`ctest --test-dir Build/PlanSourcePlugins/A-windows-1 --output-on-failure`；WSL/macOS 用各自目录。预期与现有 Engine 结果一致；第二次 `cmake --build` 无额外 C++ 任务。
- [x] **Step 4：写入验收记录。** 把每次 Configure、`ALL`、无变更 Ninja 的真实墙钟、命令/对象数量、CTest 数和退出码写入 validation 文件；冷缓存和 warm 配置分列，严禁虚构结果。
- [x] **Step 5：Review 并提交。** 检查 `git diff --check`、验收记录的命令可复跑且没有触碰现有产物；提交 `docs: 记录源码插件改造前构建基线`（仅验收文档）。

## Task 2：冻结新的源码交付边界 ADR

**Files:**
- Create: `wiki/decisions/0014-source-plugins-project-build.md`（实际编号在执行时先核查未被占用）
- Modify: `wiki/decisions/README.md`、`wiki/decisions/0010-unified-cpp-module-entry.md`、`wiki/decisions/0013-native-build-abi-boundary.md`、`wiki/core/plugin-module.md`、`wiki/foundation/{abi,build-system,third-party}.md`
- Test: 文档引用/冲突审计，无 C++ Target 变化

**Interfaces:**
- Consumes: Spec §1–3、§8 及既有 ADR-0005/0010/0013。
- Produces: 新 Accepted ADR 和对独立 Pangu C++ 二进制交付条款的**精确**替代关系。

- [x] **Step 1：写审查矩阵（RED）。** 找出当前 `ADR-0013` 和 `plugin-module.md` 仍宣称“独立 Pangu C++ 插件/SDK 交付 + Preflight”的现行句子；记录未修订前与 Spec 冲突的条目。
- [x] **Step 2：新增 ADR。** 决定所有 Pangu Plugin 以源码交付并由同树编译；不需要独立 ABI Preflight；同时明确系统 SDK/Toolchain/普通 ThirdParty 预编译依赖不因此失效，STATIC/SHARED CodePin、ModuleId 和生命周期不变。
- [x] **Step 3：同步现行规范。** 只修订现行适用范围与双向引用，历史 ADR/Spec/Plan 保留原结论并标明 superseded 的边界；不删除测试/生产源码。
- [x] **Step 4：验证（GREEN）。** 用文本核查新旧 ADR 引用、`Pangu::`/`ThirdParty::`、单 Module 单库、Tests 不经 AbiProfile、无稳定 C Bootstrap 等条款一致；`git diff --check` 为零错误。
- [x] **Step 5：Review 并提交。** 将 ADR 范围与 Spec 逐项对照后提交 `docs: 冻结源码插件交付与独立 Project 构建边界`。


## Task 3：引入 toml++ 并实现单个 Plugin.toml 解析

**Files:**
- Create: `Source/ThirdParty/TomlPlusPlus/{CMakeLists.txt,README.md,Repo/}`、`BuildTools/PanguBuildTool/{CMakeLists.txt,Private/PluginManifest.h,Private/PluginManifest.cpp,Tests/PluginManifestTests.cpp}`
- Test: `BuildTools/PanguBuildTool/Tests/PluginManifestTests.cpp`

**Interfaces:**
- Produces: `pg::build::plugin::PluginManifest { std::string Id, Name, Version; }`。
- Produces: `[[nodiscard]] std::expected<PluginManifest, std::string> ParsePluginManifest(const std::filesystem::path& manifestPath) noexcept;`（头在 `Private/PluginManifest.h`）；失败信息须含文件路径。
- 不提供 `Pangu::` Alias；`ThirdParty::TomlPlusPlus` 仅供独立工具消费，不要求 Engine 的 `PanguBuildOptions`。

- [x] **Step 1：固定第三方版本。** 选择一个正式上游 toml++ Release 的明确 Git Commit，创建符合 `wiki/foundation/third-party.md` 的固定 Submodule 或 Vendor Snapshot；记录 Commit、许可证、必要 Include 根，CMake Configure 不联网。
- [x] **Step 2：写失败测试（RED）。** `PluginManifestTests.cpp` 至少断言：`pangu.physics` + 中文 `name` + `1.2.3-alpha.1+sha.7` 成功；缺 `id`、大写 ID、`foo..bar`、`01.2.3`、`1.0.0-alpha.01`、未知字段或 Section、损坏 TOML 均返回错误；不抛异常。
- [x] **Step 3：执行失败测试。** Windows 工具独立配置示例：`cmake -S BuildTools/PanguBuildTool -B Build/PlanSourcePlugins/Tool -G Ninja -DCMAKE_CXX_COMPILER="$env:LLVM_ROOT/bin/clang++.exe" -DBUILD_TESTING=ON`；`cmake --build Build/PlanSourcePlugins/Tool --target PanguPluginManifestTests`。实现前预期缺接口/断言失败（工具链缺失不是有效 RED）。
- [x] **Step 4：实现最小解析。** `PluginManifest.cpp` 使用 `TOML_EXCEPTIONS=0` 的显式 `toml::parse_result`；严格校验 ID 的固定 ASCII 规则与 SemVer 2.0.0、已知键及值类型；所有路径/文件系统失败使用 `std::error_code` 和返回错误，不用异常传播。
- [x] **Step 5：执行成功测试（GREEN）。** 上述目标构建后执行 `ctest --test-dir Build/PlanSourcePlugins/Tool --output-on-failure -R '^PluginManifest'`，预期所有正/负样例通过；工具编译数据库不包含 Pangu Runtime，修改 Manifest 不触发其重新编译。
- [x] **Step 6：Review 并提交。** 验证 `clang-format`/`clang-tidy`、`git diff --check` 与 submodule gitlink；提交 `feat: 引入 toml++ 并实现 Plugin.toml 严格解析`。

## Task 4：插件发现、身份去重与 JSON CLI

**Files:**
- Create: `BuildTools/PanguBuildTool/Private/{PluginDiscovery.h,PluginDiscovery.cpp,PluginJson.h,PluginJson.cpp,Main.cpp}`、`BuildTools/PanguBuildTool/Tests/{PluginDiscoveryTests.cpp,PluginCliTests.cmake}`
- Modify: `BuildTools/PanguBuildTool/CMakeLists.txt`
- Test: 独立 CTest `PluginDiscovery` 与 `PluginCli`

**Interfaces:**
- Consumes: Task 3 的 `ParsePluginManifest(path)`。
- Produces: `PluginEntry { PluginManifest Metadata; std::filesystem::path SourceDir, ManifestPath; }`。
- Produces: `[[nodiscard]] std::expected<std::vector<PluginEntry>, std::string> DiscoverPlugins(const std::filesystem::path& engineRoot, const std::optional<std::filesystem::path>& projectRoot) noexcept;`。
- CLI: `PanguBuildTool discover --engine-root <dir> [--project-root <dir>]`；stdout 仅一个严格 JSON 对象，形如 `{"plugins":[{"id":"pangu.physics","name":"Physics","version":"1.0.0","source_dir":"...","manifest_path":"..."}]}`。`name/version` 缺失时为空字符串；数组按 ID 排序。失败时 exit≠0，错误写 stderr 并含路径。空目录（或不存在的固定插件根）输出 `{"plugins":[]}`。

- [x] **Step 1：先写目录/身份 RED 测试。** 两个根各含 Plugin 时返回按 ID 排序的 2 个条目；两个目录同 ID、同真实目录重复映射、路径越根的 Symlink、仅有一份必需文件、CMakeLists 类型异常、无权限目录均失败；两文件都没有的普通子目录忽略。
- [x] **Step 2：写 CLI JSON RED 测试。** `PluginCliTests.cmake` 通过 `execute_process()` 获取 stdout/stderr/返回码，用 `string(JSON)` 检查 `plugins` 数组长度、字段与 Unicode/引号/反斜杠的转义；重复 ID 即使位于未启用插件也失败，stdout 不含日志。
- [x] **Step 3：运行 RED。** `cmake --build Build/PlanSourcePlugins/Tool --target PanguPluginDiscoveryTests` 与 `ctest --test-dir Build/PlanSourcePlugins/Tool -R '^(PluginDiscovery|PluginCli)' --output-on-failure`；未实现前测试必须因功能缺失/断言失败，而非工具链或文件路径错误。
- [x] **Step 4：实现扫描与 CLI。** 仅枚举直接子目录，规范化根/子目录实际路径，处理相对路径、Symlink、大小写碰撞、重复 ID；使用固定字段写 JSON（不用生成 CMake 脚本、不依赖第二个 JSON 大库），stderr 诊断完整。
- [x] **Step 5：运行 GREEN。** `cmake --build Build/PlanSourcePlugins/Tool` 与 `ctest --test-dir Build/PlanSourcePlugins/Tool --output-on-failure` 全通过；手工执行空根 CLI 验证精确结果 `{"plugins":[]}`（可有结尾换行）。
- [x] **Step 6：Review 并提交。** 格式、静态检查、路径攻击/非法字段复核；提交 `feat: 完成源码插件发现与稳定 JSON 输出`。

## Task 5：顶层 Configure 必须启动 PanguBuildTool

**Files:**
- Create: `cmake/PanguBuildTool.cmake`、`cmake/PanguPlugins.cmake`、`Tests/CMake/SourcePlugins/{CMakeLists.txt,BuildToolConfigureTests.cmake}`
- Modify: `CMakeLists.txt`（当前 1–22 行；初版只接 Engine standalone 空插件发现）、`Tests/CMakeLists.txt`（在非交叉编译的测试分支显式注册脚本测试）
- Test: `Tests/CMake/SourcePlugins/BuildToolConfigureTests.cmake`，以及实际空 Engine `cmake --preset windows -B Build/PlanSourcePlugins/EngineEmpty`

**Interfaces:**
- Consumes: Task 4 CLI `PanguBuildTool discover --engine-root ... [--project-root ...]`。
- Produces: CMake `pangu_prepare_build_tool(<outputVariable>)`：在调用者作用域设置可执行文件绝对路径；嵌套工具构建根固定为 `${CMAKE_BINARY_DIR}/BuildTools/PanguBuildTool`。
- Produces: CMake `pangu_discover_plugins(ENGINE_ROOT <dir> [PROJECT_ROOT <dir>] OUTPUT_JSON <variable>)`：执行工具，检查状态/JSON，返回原始 JSON 字符串并登记 Configure 依赖。Task 8 通过它取得 Plugin 映射。

- [x] **Step 1：写空根 RED。** 最小 CMake 测试工程调用 `pangu_discover_plugins`；断言首次 Configure 仍创建并实际编译宿主工具，运行工具后 `plugins` 长度为 0；当前函数未实现时失败。
- [x] **Step 2：写变更与错误 RED。** 删除已发现 Manifest/CMakeLists、修改工具 `.cpp` 或其 CMakeLists 后运行下一次 `ninja`，必须触发 CMake Configure（删除应转为诊断）；故意使独立工具编译失败时 Configure 直接失败，不能运行旧二进制。操作只在隔离 fixture 副本，不动共享源码。
- [x] **Step 3：实现宿主工具接线。** 每次顶层 Configure 用独立 `cmake -S BuildTools/PanguBuildTool -B <top-build>/BuildTools/PanguBuildTool -G Ninja` 及 `cmake --build ... --target PanguBuildTool`；宿主编译器取自 Windows/Android Windows 宿主的 `$ENV{LLVM_ROOT}/bin/clang++.exe` 或 Linux/macOS/iOS macOS 宿主的 `$ENV{LLVM_ROOT}/bin/clang++`，验证 Clang 23.x/C++23；Android/iOS 不继承 NDK/iOS 的 `CMAKE_TOOLCHAIN_FILE`、sysroot 与平台设置；工具输出留在本 BuildRoot。
- [x] **Step 4：实现 CMake 输入失效与发现。** `execute_process()` 分开捕获退出码、stdout、stderr；通过 `string(JSON)` 验证协议；对所有已发现 `Plugin.toml`、`CMakeLists.txt` 以及工具 CMakeLists/源文件/必要的 toml++ 输入登记 `CMAKE_CONFIGURE_DEPENDS`。新增 Plugin 目录需显式 Configure，不使用 `GLOB_RECURSE`、`GLOB CONFIGURE_DEPENDS` 或永久索引。
- [x] **Step 5：跑 GREEN 与增量检查。** Windows `cmake --preset windows -B Build/PlanSourcePlugins/EngineEmpty` 后 `cmake --build Build/PlanSourcePlugins/EngineEmpty` 两次；首次输出显示工具构建与空 JSON，第二次 Ninja 不编译/扫描插件。再显式重复 Configure：工具构建检查可以运行，但不得重编/链接。在 `Tests/CMakeLists.txt`（交叉编译 return 后）接入 `Tests/CMake/SourcePlugins/CMakeLists.txt`，运行 `ctest --test-dir Build/PlanSourcePlugins/EngineEmpty -R '^SourcePluginBuildTool' --output-on-failure`；原 Foundation/Module Tests 不变。
- [x] **Step 6：Review 并提交。** 确认未引入 Host Tool 作为新架构命名、额外 `ALL` 或二次依赖图；提交 `build: 在 Configure 阶段独立构建并运行 PanguBuildTool`。


## Task 6：保留 Engine standalone，支持作为独立 Project 子目录

**Files:**
- Modify: `CMakeLists.txt:1-22`、`cmake/ValidateBuildEnvironment.cmake`、必要时 `cmake/PanguPlatform.cmake`、`Tests/CMakeLists.txt`
- Create: `Tests/CMake/SourcePluginProject/{RunIntegration.cmake,MyGame/CMakeLists.txt,MyGame/CMakePresets.json,MyGame/Source/Main.cpp}`；将 `RunIntegration.cmake` 经 `Tests/CMake/SourcePlugins/CMakeLists.txt` 注册为桌面 CTest
- Test: `Tests/CMake/SourcePluginProject/RunIntegration.cmake`（隔离的 MyGame 根，不作为 Engine `Projects/` 子目录）

**Interfaces:**
- Consumes: Task 5 的 `pangu_discover_plugins(...)`，以及现有 `PanguBuildOptions`、`Pangu::Foundation`、`Pangu::Module`。
- Produces: 顶层 `BUILD_TESTING`；Engine 专用 `PANGU_BUILD_ENGINE_TESTS`（Engine standalone 默认 ON，Engine 作为 Project 子目录默认 OFF）；有效 `PANGU_ENGINE_SOURCE_DIR`；Engine 不在子目录模式自行重复发现插件。
- Produces: 最小 `MyGameProbe` 可执行 Target（当前仅链接真实 Engine Core，项目插件在后续 Task 加入）。

- [x] **Step 1：写两个入口 RED 测试。** `RunIntegration.cmake` 实例化 MyGame 到宿主本地独立测试根（**不**复制 Engine 源码）；使用 `add_subdirectory("${PANGU_ENGINE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/Engine")`。断言顶层 `CMAKE_PROJECT_NAME=MyGame`、编译器不变，¬重复 `PanguBuildOptions`；Engine standalone 不依赖 MyGame。
- [x] **Step 2：写 CTest 范围 RED。** Project `BUILD_TESTING=ON` 时可以注册自己的测试，默认不出现 `ModuleIdentity`、`StaticModuleCatalog` 等 Engine CTest；Engine standalone `BUILD_TESTING=ON` 仍保持这些测试。Project `BUILD_TESTING=OFF` 时两边测试均未注册。
- [x] **Step 3：验证 RED。** 运行 `cmake "-DPANGU_ENGINE_SOURCE_DIR=$R" "-DPANGU_TEST_ROOT=$T" -P Tests/CMake/SourcePluginProject/RunIntegration.cmake`；现有 Engine 的重复 `include(CTest)`/顶层预检或缺少接口应造成有意义的失败。
- [x] **Step 4：最小重构 Engine 根。** Engine 单独作为顶层时保持 `project(PanguEngine ...)`、平台校验、CTest 和现有 Preset；作为子工程时跳过重复全局初始化/CTest，仅复用一次平台规则和 `PanguBuildOptions`；允许合法的子目录 `project()`，不得改动父项目的 Toolchain/标准库；继续构建 Foundation/Module。
- [x] **Step 5：验证 GREEN。** 独立 MyGame Configure/Build `--target MyGameProbe` 成功；相同 `LLVM_ROOT`/平台策略只创建一个 CMake/Ninja 图；Engine Windows 独立配置、默认构建与 CTest 正常；Project 的测试开关与 Engine 内部测试开关独立。
- [x] **Step 6：Review 并提交。** 检查 `Tests/CMakeLists.txt` 使用的 `PROJECT_SOURCE_DIR` 在子工程模式下是否仍指向 Engine；将 `RunIntegration.cmake` 正式接入桌面 CTest，执行 `ctest --test-dir Build/PlanSourcePlugins/EngineEmpty -R '^SourcePluginProject' --output-on-failure`；提交 `build: 支持 Engine 独立和 Project 子工程两种入口`。

## Task 7：独立 Project 锁定 Engine Git Commit

**Files:**
- Create: `cmake/PanguEngineRevision.cmake`、`Tests/CMake/SourcePluginProject/EngineRevisionTests.cmake`、`Tests/CMake/SourcePluginProject/MyGame/Engine.commit`（fixture 实例化时填入当前 HEAD）
- Modify: `Tests/CMake/SourcePluginProject/MyGame/CMakeLists.txt`
- Test: `Tests/CMake/SourcePluginProject/EngineRevisionTests.cmake`

**Interfaces:**
- Produces: `pangu_validate_engine_revision(SOURCE_DIR <dir> EXPECTED_COMMIT <40-hex> ALLOW_LOCAL_OVERRIDE <ON|OFF>)`，只能用于顶层 Project；真实受检仓库为 `PANGU_ENGINE_SOURCE_DIR`。
- Produces: Project 变量 `PANGU_ENGINE_EXPECTED_COMMIT`（从 Project 自己记录的 `Engine.commit` 读取）和默认 `PANGU_ENGINE_ALLOW_LOCAL_OVERRIDE=OFF`；覆盖必须显式开启并提示实际 SHA/脏状态。
- 不自动 `git checkout`、下载更新 Engine 或修改 Git 工作区。

- [x] **Step 1：建立隔离 Git 测试输入。** 在宿主本地临时目录生成一个极小的测试 Git 仓库（不复制真实 Engine），制作两个本地提交及一处未提交 tracked 变更；记录两个准确 SHA。
- [x] **Step 2：写 RED。** `EngineRevisionTests.cmake` 对锁定 SHA 匹配且干净→通过；SHA 不匹配→失败；tracked 脏→失败；Submodule gitlink 不一致或未初始化→失败；本地显式 override→可继续但诊断包含预期/实际 SHA 和脏状态。
- [x] **Step 3：运行 RED。** `cmake "-DPANGU_SOURCE_ROOT=$R" "-DPANGU_TEST_ROOT=$T" -P Tests/CMake/SourcePluginProject/EngineRevisionTests.cmake`，缺检查函数/错误判定须使测试失败。
- [x] **Step 4：实现 Git 验证。** 用 `execute_process(COMMAND git -C <dir> ...)` 判断完整 Commit、tracked 状态及 Submodule gitlink，检验 `Engine.commit` 格式；CMake 不写共享 Git 或修改 Submodule。
- [x] **Step 5：运行 GREEN。** 上述测试及 MyGame Configure 通过；本地工作树正在开发时须显式设置 `-DPANGU_ENGINE_ALLOW_LOCAL_OVERRIDE=ON`，否则严格失败；在干净提交上 CI/正式配置不用 override 也能通过。
- [x] **Step 6：Review 并提交。** 确认验证脚本无联网/Checkout、副作用仅在测试 Git 仓库；提交 `build: 为独立 Project 增加 Engine Commit 锁定校验`。

## Task 8：显式启用 Plugin，消费者 Target 决定编译

**Files:**
- Modify: `cmake/PanguPlugins.cmake`、`CMakeLists.txt`、`Tests/CMake/SourcePluginProject/MyGame/CMakeLists.txt`
- Create: `Tests/CMake/SourcePluginProject/FixtureEnginePlugins/Physics/{Plugin.toml,CMakeLists.txt,Private/Physics.cpp}`、`Tests/CMake/SourcePluginProject/MyGame/Plugins/Gameplay/{Plugin.toml,CMakeLists.txt,Private/Gameplay.cpp}`、`Tests/CMake/SourcePluginProject/MyGame/Plugins/EditorExtras/{Plugin.toml,CMakeLists.txt,Private/EditorExtras.cpp}`、`Tests/CMake/SourcePluginProject/PluginEnableTests.cmake`
- Test: `Tests/CMake/SourcePluginProject/PluginEnableTests.cmake`，检查真实 Ninja 图和产物

**Interfaces:**
- Consumes: Task 5 的 `pangu_discover_plugins(...)`。
- Produces: `pangu_add_enabled_plugins(ENGINE_ROOT <dir> [PROJECT_ROOT <dir>] ENABLED <id>...)`；按 ID 排序，将启用目录加入 `${CMAKE_BINARY_DIR}/Plugins/<id>` 的独立 BinaryDir，使用 `add_subdirectory(source binary EXCLUDE_FROM_ALL)`。
- `PANGU_ENABLED_PLUGINS` 值使用 `pangu.physics;mygame.gameplay;mygame.editor-extras`；Target/ModuleId 不由 Plugin ID 自动重命名。测试 Engine-root 的插件放在**隔离 fixture 根**中模拟，不写入共享 Engine `Plugins/`，Engine standalone 空根的性能验收仍成立。

- [x] **Step 1：写启用/未启用 RED。** `PluginEnableTests.cmake` 创建双根 fixture：三个 Manifest 全部合法，启用 Physics 和 Gameplay，EditorExtras 未启用；断言只有两个插件 CMakeLists 被执行；无消费者时 `ALL` 不编译任何插件目标。
- [x] **Step 2：写依赖/失败 RED。** 不存在的 ID、重复启用、缺少需要的 `Pangu::Physics`、同名 Module Target、大小写错误均配置/生成失败；开启未使用 Plugin 也不能为此创建 `ALL` 目标。用 `Pangu::Physics` 验证 Consumer 在稍早的 CMakeLists 中引用、Target 较晚定义时 Generate 可以成功。
- [x] **Step 3：运行 RED。** `cmake "-DPANGU_ENGINE_SOURCE_DIR=$R" "-DPANGU_TEST_ROOT=$T" -P Tests/CMake/SourcePluginProject/PluginEnableTests.cmake`；未实现的 `pangu_add_enabled_plugins` 或 `ALL` 泄漏必须被检出。
- [x] **Step 4：实现薄接线。** CMake 仅用 `string(JSON)` 建立 ID → SourceDir/ManifestPath，校验 `ENABLED` 项，再调用原生 `add_subdirectory(... EXCLUDE_FROM_ALL)`；不生成第二依赖 DAG、不递归启用、不写新 `Module.toml`。
- [x] **Step 5：运行 GREEN。** 单独 `--target MyGameProbe` 只编译其链接依赖；直接请求插件 Target 才单独构建它；`ninja -t targets all`、`ninja -t commands`、`compile_commands.json` 与生成的静态库数量一致；未启用插件 `.cpp` 改动不触发构建。
- [x] **Step 6：Review 并提交。** 检查 `Pangu::`、`ThirdParty::` 约束、无隐式资产处理与通用 `POST_BUILD`；提交 `build: 以稳定 Plugin ID 按消费者依赖接入源码插件`。


## Task 9：Game/Editor 独立 STATIC Catalog 与纯 Assets Plugin

**Files:**
- Create: `Tests/CMake/SourcePluginProject/MyGame/Source/{GameMain.cpp,EditorMain.cpp,GameCatalog.cpp,EditorCatalog.cpp}`、`Tests/CMake/SourcePluginProject/MyGame/Plugins/AssetsOnly/{Plugin.toml,CMakeLists.txt}`、`Tests/CMake/SourcePluginProject/StaticCatalogIntegrationTests.cmake`
- Modify: `Tests/CMake/SourcePluginProject/MyGame/CMakeLists.txt`、Task 8 的示例 Module `Physics.cpp`、`Gameplay.cpp`、`EditorExtras.cpp`
- Test: `Tests/CMake/SourcePluginProject/StaticCatalogIntegrationTests.cmake` 和两个实际 Host 可执行文件

**Interfaces:**
- Consumes: 现有 `pg::module::StaticModuleCatalog`、`StaticCatalogEntry`、`StaticModuleHost::Create(...)`、`ModuleDefinition`、`QueryModuleFn`（见 `Source/Runtime/Module/Public/{ModuleDescriptor,StaticModuleHost}.h`）。
- Produces: 两个独立可执行 Target `MyGame`、`MyEditor`；Game Catalog 引用 Physics/Gameplay，Editor Catalog 引用 Physics/EditorExtras，二者不通过静态初始化或全局 Catalog 自动注册；每个示例 Module 只有一个真实 STATIC Library Target。

- [x] **Step 1：写独立 Catalog RED。** 断言构建 `--target MyGame` 时 Game Catalog 含 Physics+Gameplay 两个稳定 ModuleId、可创建/注册，EditorExtras 的库/对象不构建；单独 `--target MyEditor` 时 Editor Catalog 含 Physics+EditorExtras，Gameplay 不因这一单目标构建被需要。
- [x] **Step 2：写纯 Assets RED。** 启用 `mygame.assets-only` 且存在合法 `Plugin.toml` 与空代码 `CMakeLists.txt` 时 Configure 成功、没有代码 Module Target/资产命令进入默认 `ALL`；若其 Manifest 缺失仍必须失败。
- [x] **Step 3：运行 RED。** `cmake "-DPANGU_ENGINE_SOURCE_DIR=$R" "-DPANGU_TEST_ROOT=$T" -P Tests/CMake/SourcePluginProject/StaticCatalogIntegrationTests.cmake`；没有独立 Catalog/Target 或错误目标集合时失败。
- [x] **Step 4：最小实现。** 参照现有 `Tests/Module/Fixtures/{StaticCatalog.cpp.in,FixtureModules.h}` 用显式 Query 函数指针建立两个 Catalog；例子中的真实 C++ ModuleId 与 Plugin ID 分离。fixture 内 Catalog 是手写测试装配，**不实现 PanguMeta**。
- [x] **Step 5：运行 GREEN。** 先在全新隔离 BinaryDir 分别构建 `MyGame`、`MyEditor` 并运行桌面可执行文件，检查 RegistrationOrder、`Modules().size()` 与 Ninja object/link 命令；之后单独构建默认 `ALL` 也不能产生未启用 Plugin 的 Target。
- [x] **Step 6：Review 并提交。** 核实静态库唯一性、Catalog 强引用、Assets 仅由消费者触发；提交 `test: 验证 Game Editor 独立静态装配及纯资源插件`。

## Task 10：SHARED Module 显式依赖与部署

**Files:**
- Create: `Tests/CMake/SourcePluginProject/MyGame/Plugins/SharedProbe/{Plugin.toml,CMakeLists.txt,Private/SharedProbe.cpp}`、`Tests/CMake/SourcePluginProject/SharedDeployTests.cmake`
- Modify: `Tests/CMake/SourcePluginProject/MyGame/CMakeLists.txt`
- Test: `Tests/CMake/SourcePluginProject/SharedDeployTests.cmake`（检查构建产物和部署，而非未实现的运行时 Loader）

**Interfaces:**
- Consumes: Task 8 的 `pangu_add_enabled_plugins(...)`、真实 `Pangu::SharedProbe` Module Target 及其实际目标名 `PanguSharedProbe`。
- Produces: 显式部署目标 `MyGameDeployShared`，仅在请求时依赖 `PanguSharedProbe` 并通过 `$<TARGET_FILE:PanguSharedProbe>` 取得真实 DLL/.so/.dylib；部署目录位于当前 Project BinaryDir，不使用固定平台后缀或共享可写目录。

- [x] **Step 1：写 Shared RED。** 启用 `mygame.shared-probe` 但仅构建 `--target MyGame` 时，`PanguSharedProbe` 不编译；执行 `--target MyGameDeployShared` 时必须构建 SHARED Target 并拷贝到显式部署目录；未请求部署不拷贝。
- [x] **Step 2：写错误与增量 RED。** 缺少 SHARED Target 依赖时测试应发现部署空文件；重复请求部署、库未变化时不重复写入 DLL；Windows 下可用 import lib 不计作第二个 Module，产物命名不含平台名。
- [x] **Step 3：运行 RED。** `cmake "-DPANGU_ENGINE_SOURCE_DIR=$R" "-DPANGU_TEST_ROOT=$T" -P Tests/CMake/SourcePluginProject/SharedDeployTests.cmake`；预期缺目标/部署时按指定断言失败。
- [x] **Step 4：实现 Target 和部署。** 使用原生 `add_library(... SHARED)` + `pangu_configure_module()`；fixture 的导出符号仅用于构建/链接冒烟，不冻结正式 Loader 符号。部署由显式 `add_custom_command(OUTPUT ... DEPENDS PanguSharedProbe ...)` 和 `add_custom_target(MyGameDeployShared DEPENDS ...)` 表达，使用 `cmake -E copy_if_different`，不对所有 Module 注入 `POST_BUILD`。
- [x] **Step 5：运行 GREEN。** 先单独构建 Game 检查未构建 SHARED；再构建 deploy 目标检查库文件与清单路径，第二次 deploy 无多余拷贝；Windows/WSL/macOS 分别验证实际文件类型，移动平台只验证适用的链接/构建路径。
- [x] **Step 6：Review 并提交。** 明确 `Built≠Deployed≠Runtime Activated`，不能将文件拷贝称为 OS 动态加载/CodePin 验证；提交 `build: 添加显式 SHARED Module 部署集成验收`。

## Task 11：配置期失效、Native 增量与构建性能闸门

**Files:**
- Create: `Tests/CMake/SourcePluginProject/IncrementalBuildTests.cmake`
- Modify: `docs/superpowers/plans/2026-11-04-source-plugins-project-build-validation.md`
- Test: 可独立重复的 0、10、100 个 Plugin 夹具及 A/B 构建日志

**Interfaces:**
- Consumes: Tasks 1–10 的构建图、Manifest CLI 与性能 A 基线。
- Produces: B 基线：首次/重复 Configure、工具编译时间、默认 `ALL`、无改动 Ninja、单 TU 改动、Manifest/工具变更的可复算结果。

- [x] **Step 1：写增量 RED 测试。** `IncrementalBuildTests.cmake` 使用隔离副本记录 Ninja 命令与文件时间：修改未启用插件 `.cpp` 不触发编译；修改已消费插件私有 `.cpp` 只重编对应对象/归档及必要链接；修改工具源文件不重编 Foundation/其它无关 Module。
- [x] **Step 2：写 Configure 失效 RED。** 修改/删除**未启用**插件的已发现 `Plugin.toml` 或 `CMakeLists.txt` 仍触发顶层重新 Configure 并按要求失败；改变 `PANGU_ENABLED_PLUGINS` 仅更新目标图；新增 Plugin 目录需要显式 Configure；纯 `ninja` 不额外运行工具。
- [x] **Step 3：写性能输入矩阵。** 分别在独立 fixture 根准备 N=0/10/100 份合法的小 Manifest，记录工具启动/解析耗时、JSON 输出长度、默认目标数量；无 Plugin 的首次 Configure **必须编译并运行 PanguBuildTool**，新增成本单独列出。
- [x] **Step 4：执行 RED→GREEN 修复。** 先运行 `cmake "-DPANGU_ENGINE_SOURCE_DIR=$R" "-DPANGU_TEST_ROOT=$T" -P Tests/CMake/SourcePluginProject/IncrementalBuildTests.cmake`；失败后只修复失效边界或实际 CMake/Ninja 依赖，不引入自制 TU 证据、永久索引或共享工具缓存；再运行直到测试通过。
- [x] **Step 5：对照 A/B。** 在同宿主同版本 Toolchain、相同并行度/测试开关下各取 ≥3 次独立样本，列出 Configure/`ALL`/noop 中位数、最差值与命令数；无 Plugin 默认 `ALL` 的可比构建阶段回退 `>10%` 时阻断并调查；**首次工具编译导致的 Configure 增量是已确认成本，不以零成本判不合格**，但单独记录异常额外工作。
- [x] **Step 6：Review 并提交。** 保留验收结论与日志路径，原始大日志留在专属工作区；提交 `test: 锁定插件 Configure 与 Ninja 增量性能回归`。

## Task 12：五平台集成、现行文档同步与完工验收

**Files:**
- Modify: `BuildTools/README.md`、`wiki/core/{plugin-module,module-api,module-registration-lifetime,engine-host}.md`、`wiki/foundation/{abi,build-system,third-party,platform}.md`（仅确需同步现行状态的条款）
- Modify: `docs/superpowers/plans/2026-11-04-source-plugins-project-build-validation.md`、完成后本 Plan 的任务状态
- Test: 现有正式 Preset/CTest、独立 MyGame fixture 的五平台 Configure/编译/链接；不引入真机设备承诺

**Interfaces:**
- Consumes: 所有前置 Task 的真实代码、CTest 和已记录的性能证据。
- Produces: 明确“已实现 / 已验证 / 未验证”的五平台最终验收矩阵及工具/插件文档。

- [x] **Step 1：桌面 GREEN。** 在 Windows、WSL Linux、macOS 各自独立 BuildRoot 运行对应 `cmake --preset <windows|linux|macos> -B <isolated>`、`cmake --build <isolated>`、`ctest --test-dir <isolated> --output-on-failure`。Engine standalone **空 Plugin** 路径仍须编译/执行工具，保留全部原有正式测试。
- [x] **Step 2：移动端交叉 GREEN。** Windows 上使用 `android-arm64`、Mac 上使用 `ios-arm64` Preset，在各自本地隔离目录配置并构建 Engine 与适用的 MyGame fixture；分别记录宿主 PanguBuildTool 的编译器/可执行文件与 Android/iOS 目标 Module 的实际编译/链接命令。不能把只产出静态库冒充完整 App/真机运行。
- [x] **Step 3：独立 Project 与锁定。** Project CMake 顶层、独立源码与 BuildRoot、Git Commit Lock、测试开关与两个发现根都经集成 fixture 验证；不创建正式 `D:\Git\MyGame` 仓库、不复制 Engine 源码，也不借现有 Engine CMake Cache。
- [x] **Step 4：文档同步。** `BuildTools/README.md` 改为“PanguBuildTool 在所有顶层 Configure 固定编译/复用并执行”；现行 Plugin/ABI/CMake/ThirdParty/EngineHost 规范引用新 ADR；不删除或伪改历史 Spec/Plan/ADR。
- [x] **Step 5：质量核查。** 对新增 C++ 执行仓库 `clang-format`/`clang-tidy`（使用编译数据库）；执行 `git diff --check`，核对单 Module 单库、Target 命名、没有 `AbiProfile` 接线/第二依赖图、无关工具不触发对象重编。
- [x] **Step 6：验收并提交。** 汇总每个平台的命令、实际退出码、CTest 数、A/B 性能表和未跑设备加载/卸载的限制；Reviewer 通过后提交 `docs: 完成源码插件与 Project 构建五平台验收`；未通过的硬门槛必须保持 In Progress，不得伪标完成或自动推送/合并。

## 完工检查与执行交接

- **任务顺序：** 1 基线 → 2 ADR → 3 TOML 解析 → 4 Plugin 扫描 → 5 工具 Bootstrap → 6 Engine/Project 双模式 → 7 Git 锁 → 8 按需插件 → 9 STATIC/Assets → 10 SHARED 部署 → 11 增量/性能 → 12 五平台验收。
- **任务级验证 Gate（inline 执行，最终统一独立整分支 Review）：** 每个 Task 的 RED 与 GREEN、命令输出、`git diff --check`、有价值的负例和提交摘要必须通过后再推进下一项；修复同一文件只能串行写入。
- **绝不牺牲的约束：** 空 Plugin 也编译并运行 `PanguBuildTool`；无变更普通 Ninja 零动作；不允许新增 Plugin 级依赖 DAG；原有 Engine standalone/CTest 不退化；桌面集成脚本须接入正式 CTest（移动平台仅执行编译型验证）；对 iOS/Android 只报告实际验证的层级。
- **工作状态：** 已完成任务以复选框、台账与 Git 提交为准；性能、五平台证据及限制见配套 validation 文档。最终 Review 的五项 Important 已修复并通过全部适用回归；不自动推送或合并。

## 完成验收摘要

- Task 1–2：`9d35419` 记录基线，`cb5a07c` 冻结 ADR-0014。
- Task 3–6：`9e5d253` 固定 toml++ Submodule/严格解析，`7a10dc3` 发现与 JSON，`42807d8` Configure Bootstrap，`8953ea9` 双入口。
- Task 7–10：`f3761f5` Git 锁，`eebed2f` 按消费者启用，`72f44df` 独立 STATIC/Assets，`88fba18` 显式 SHARED 部署。
- Task 11：`0aa72f2` 正式增量/规模回归与性能记录；最终 Review 五项 Important 由 `fca8669` 修复，均有 RED→GREEN 证据。
- 最终验收：Windows Engine CTest 22/22，Linux/macOS 各 21/21，独立工具三桌面各 3/3；Android/iOS ARM64 Engine 与独立 Project 最终链接及文件部署通过。最终 Linux 同条件 A/B ALL 中位数 2.874/2.875 秒（+0.035%），普通 Ninja 零动作。完整数据、命令、环境修复和失败保留记录见[验收文档](2026-11-04-source-plugins-project-build-validation.md)。
- 遗留：独立工具 64 条非阻断 clang-tidy 提示延期清理；Windows symlink 删除/改指子案例因创建权限不足未运行，Linux/macOS 已验证；生产 Loader、动态 CodePin、PanguMeta、完整 App/签名及设备运行不在本次交付范围。WSL timesyncd 停用已获用户授权，保留 Hyper-V 同步，可用官方命令回滚。
- 执行裁决：沿用专用分支及共享源码路径；toml++ 最终采用固定 Submodule；根参数使用工程根；旧时钟条件数据不改写，追加同条件性能对照；symlink 必需文件追踪入口/目标及目录项；未跟踪文件不属于 Git 干净性保证。必要结论与 Review 摘要归档后清理本计划专属临时目录，正式 Tests 保留。
