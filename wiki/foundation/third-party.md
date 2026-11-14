# ThirdParty 接入与功能封装

正确性测试使用固定版本 GoogleTest/GoogleMock，性能基准使用固定版本 Google Benchmark；它们是 ThirdParty 辅助依赖，不取得 Pangu Module 身份。基准默认关闭且独立于正确性测试，测试运行契约见[测试规范](testing.md)。

> 状态：Partially Frozen（CMake Target 命名、Module/ThirdParty 边界已冻结；获取方式、逐库补丁及部署策略按依赖确定）
> 更新：2026-11-04
> 相关：[构建系统](build-system.md)、[工具链](toolchain.md)、[Plugin / Module](../core/plugin-module.md)、[ABI](abi.md)、[ADR-0014 源码 Plugin](../decisions/0014-source-plugins-project-build.md)

## 源码与构建入口

第三方库作为**普通 CMake Target** 纳入构建。对于需要将上游源码保存在仓库中的开源库，优先使用固定提交的 Git submodule；也允许固定版本的 Vendor Snapshot、预编译 SDK、系统库或 SDK/Framework。具体获取方式由依赖特点决定，**不引入统一 ThirdParty Package Manager**。

目前 nlohmann/json 使用以下目录约定（其他依赖按需要调整）：

```text
Source/ThirdParty/<DependencyName>/
  Repo/                  # 本依赖选择 submodule 时使用
  CMakeLists.txt         # 需要项目适配时维护，也可直接使用上游 CMake
  README.md              # 来源、许可证、版本说明、构建选项与验证要求
  Patch/                 # 按需建立，保存对上游源码的补丁
```

`DependencyName` 是稳定的依赖接入名称，不强制与远程仓库 basename 相同。
例如 `NlohmannJson` 对应上游 `nlohmann/json`；一个 SDK 可以包含多个实际库。
`Repo/` 内的文件归上游仓库管理，外层文件归 PanguEngine 管理。

`TomlPlusPlus/Repo` 同样固定为 Submodule（v3.4.0 提交 `30172438cee64926dc41fdd9c11fb3ba5b2ba9de`）。`ThirdParty::TomlPlusPlus` 仅供独立 PanguBuildTool 使用，以 `TOML_EXCEPTIONS=0` 解析 Manifest，不传播到 Runtime 公共接口；工具不依赖 Engine 的 `PanguBuildOptions`，详见[BuildTools](../../BuildTools/README.md)。

- 采用 submodule 时固定到明确提交，由主仓库 gitlink 记录，不以浮动分支决定构建输入。预编译 SDK 或 Snapshot 同样必须记录版本、来源、许可证和校验信息。
- 对采用 submodule 的依赖，首次检出可使用 `git clone --recurse-submodules <PanguEngine URL>`；已有工作区执行 `git submodule update --init --recursive`。
- 切换主仓库版本后，对 submodule 重新执行初始化/更新命令，使源码与 gitlink 一致。
- CMake 配置阶段不联网、不隐式下载或执行 submodule 更新；缺失必要文件时给出明确诊断。
- 升级 submodule 前先检查 `Repo/` 的本地修改，再检出目标提交、复核许可证及补丁、执行验证，最后更新 gitlink；不得自动覆盖本地修改。

## 项目构建配置

外层 `CMakeLists.txt` 最低要求 CMake 4.0，并在根工程已校验的平台与工具链上下文中定义 Target。
编译型第三方 Target 自动使用对应平台 Toolchain/Preset 选定的编译器与标准库；
第三方特有的 ABI 宏、编译参数和链接要求仍由其自身 Target 显式设置。不得将引擎警告策略
无差别施加到上游源码，也不调用 `pangu_configure_module()` 将依赖标记为引擎 Module。

可以由外层直接列出源码创建 STATIC/SHARED Target；需要上游构建逻辑时，也可设置上游选项后
使用 `add_subdirectory(Repo)`，再对实际 Target 调整必要配置。不得仅因同处一棵 CMake 构建树，
便假定上游没有覆盖项目默认值。header-only 库使用 INTERFACE Target，预编译 SDK 使用 IMPORTED Target；
其工具链兼容性必须另行核验，不能把预编译库视为已按本项目配置重新编译。

构建选项、Include、宏和链接依赖均通过 Target 表达，不使用全局 Include/Link 目录。
源代码获取方式与功能接口封装分开处理；submodule 不自动提供 ABI 兼容、安全卸载或状态迁移。

## CMake Target 命名与构建职责（Frozen）

- **`Pangu::` 只表示真实的 Pangu Module**（如 `Pangu::Foundation`、`Pangu::ECS`）。
- **`ThirdParty::` 表示第三方依赖**（如 `ThirdParty::NlohmannJson`、`ThirdParty::Asio`）。上游已提供 CMake Target 时可用原生 `ALIAS` 统一名称；不为命名强制编译新的 Wrapper Library。
- 引擎构建策略和工具使用普通 Target 名称（如 `PanguBuildOptions`），**不使用 `Pangu::`**。
- 依赖通过 `target_link_libraries(... PRIVATE|PUBLIC|INTERFACE ...)` 明确表达。第三方 Target 不调用 `pangu_configure_module()`，不获得 ModuleId、注册、Service 或生命周期身份。

本规范不额外定义 `PanguThirdParty.cmake`、`pangu_add_third_party()`、External Module 或 Package Adapter。

## 第三方库与功能 Module 的四种关系

| 模式 | 适用情况 | Pangu Module |
| --- | --- | --- |
| ① 直接依赖 | JSON、哈希等普通依赖由现有目标直接引用 | 不新增 |
| ② 现有 Module 内适配 | 第三方用于当前功能 Module 的私有实现 | 不新增 |
| ③ 独立编译/技术适配 | 如 Asio 单独编译、配置宏或处理平台差异 | 使用普通 CMake Target，不新增 |
| ④ 独立引擎功能 | ECS、Renderer、Physics 等需要独立功能 API、身份或生命周期 | 建立对应功能 Pangu Module |

**Pangu Module 封装的是引擎功能，而不是每个第三方库。** 源码 Pangu Plugin 的交付限制只适用于 Pangu 自有 Plugin/SDK 路线，**不禁止普通 ThirdParty 预编译 SDK、系统库和 OS Framework**；它们继续按照 Target、Toolchain、ABI 与 Ownership 契约独立审核，也不自动继承 Pangu 独立插件 Preflight。一个功能 Module 可以依赖多个第三方 Target，不强制一库一 Module，也不为每个依赖新增 Service/Registry。

第三方 API 默认优先通过使用方的 `PRIVATE` 依赖实现隔离；**不强制所有第三方类型都隐藏在 Pangu Module Public API 之外**。确需公开时应审查版本、API/ABI、对象/分配器所有权及销毁代码驻留。宿主工具和测试可以直接使用 `ThirdParty::` Target；`JsonExtras` 测试是当前实例。

对于 Gameplay Hot Swap，第三方代码实际链接/装载到哪个 DLL，必须据此管理对象、回调、任务、线程和卸载时序；静态链接进入可卸载 DLL 的代码也需要 CodePin/Drain 约束。ThirdParty Target 名称及 `PRIVATE` 传播设置本身不提供运行时卸载安全。一个 Pangu Module 仍对应唯一实际 STATIC/SHARED Library Target。

## Patch 管理

没有上游修改时不建立空的 `Patch/`。确需修改时：

1. 在 `Patch/` 保存标准 Git diff，使用有序文件名，并在依赖 README 中明确应用顺序、对应上游提交、原因和上游反馈位置。
2. 外层构建接入在 `Build/<Platform>/` 内准备源码副本，对该副本按明确顺序应用补丁；不要在配置/构建时直接修改 `Repo/`。
3. 补丁不匹配应终止配置，不得静默跳过。基线提交或补丁内容变化后重新准备副本，避免重复应用或沿用旧源码。
4. 验证补丁后的源码和功能，并检查 `git -C <Dependency>/Repo status --short` 保持干净。

当前 NlohmannJson 无补丁；本次不预建通用补丁应用工具。第一个需要补丁的依赖必须随接入实现上述步骤，
并加入补丁不匹配与重复配置的验证。若通过 fork 保存修改，也应固定明确提交并记录与上游的差异。

## 当前接入

PanguMetaTool 的 SDK 配套依赖固定 zlib 1.3.2（`da607da739fa6047df13e66a2af6b8bec7c2a498`）和 zstd 1.5.7（`f8745da6ff1ad1e7bab384bd1f9d742439278e99`），来源、许可证及选项见 `Source/ThirdParty/Zlib` 与 `Source/ThirdParty/Zstd`。`ThirdParty::Zlib`、`ThirdParty::Zstd` 是上游静态目标的别名，作为 PanguMetaTool 同图普通私有依赖，不接入 Engine 主图；标准命名目标连接安装版 LLVM 包导出，不先安装成另一个 SDK。无独立准备命令、包管理器或配置期下载，不编译 LLVM；现行流程见 [ADR-0019](../decisions/0019-meta-tool-normal-build.md)，实际验证见[普通工具计划](../../docs/superpowers/plans/2026-11-14-pangumeta-normal-build.md)。

日志适配固定 fmt 12.2.1（208219daff58f2a7438f6931cc2ddc358acf33ef）与 spdlog v1.17.0（79524ddd08a4ec981b7fea76afd08ee05f83755d），来源及许可证见各自 Source/ThirdParty 外层 README。ThirdParty::Fmt/ThirdParty::Spdlog 都是编译型 STATIC/PIC 库，统一外部 fmt；设置 FMT_EXCEPTIONS=0、SPDLOG_FMT_EXTERNAL、SPDLOG_NO_EXCEPTIONS、SPDLOG_NO_THREAD_ID、SPDLOG_DISABLE_DEFAULT_LOGGER，保持 RTTI，不继承 PanguBuildOptions 的警告策略，不启用上游 tests/examples/install。无上游补丁。私有 SpdlogBackend 直接通过两个内联 RawSink（spdlog 同步 Sink）消费已经编码的有界文本，不实例化 logger 的字符串、sinks/backtracer 容器，避开 formatter、默认 logger/registry/thread pool；无分配和可恢复初始化的结论限定在已审计路径与实际探针覆盖，详见[正式验收](../../docs/superpowers/plans/2026-11-12-foundation-diagnostics-validation.md)。

这两个普通目标的翻译单元显式关闭CXX_SCAN_FOR_MODULES，避免共享原生产物在跨Project配置回切时
受无用模块扫描影响。未为第三方注入Pangu强制上下文或诊断宏；正式ProjectSwitch同时读取实际对象的
CRT/Debug信息并验证消费链接和no-op，私有上下文依赖实验已撤回。扫描接线调整不修改上游源码。

`NlohmannJson/Repo` 固定到官方 v3.12.0 正式发布版，外层直接暴露 `single_include` 并设置
`JSON_USE_GLOBAL_UDLS=0`。当前保留 `ThirdParty::NlohmannJson` 和独立 JSON 回归测试，
不再依赖已退役的 ABI Profile 工具；没有新增 Runtime Module、上游测试或配置时下载。

## 编译型第三方的实际验证（2026-10-23）

`Tests/Toolchain/ThirdParty` 创建一个编译真实 C++23 源码的普通 `ThirdParty::CompiledProbe` STATIC Target，不调用 `pangu_configure_module()`，不链接 `PanguBuildOptions`。曾通过 Windows、WSL Linux、macOS、Android ARM64、iOS ARM64 五平台编译及旧 TU 宏审计；现行回归保留真实 C++ 编译，并在 Windows/Linux/macOS 上执行 ThirdParty + Pangu STATIC 库链接和 CTest，验证此前区分「Toolchain 选择」和「Pangu Target Policy」的方案至少适用于该代表性编译型 ThirdParty。该目标仅用于回归，不是引擎功能依赖，也不证明任意外部库都已具备相同 ABI。详见 [验证说明](../../Tests/Toolchain/README.md)。
