# PanguMetaTool 普通工具构建实施计划

> **For agentic workers:** 使用 superpowers:executing-plans 在现有共享源码功能分支执行，结尾做一次独立评审。

**Goal:** 从 LLVM_ROOT 直接构建 PanguMetaTool，删除独立 SDK 准备层与产物目录。

**Architecture:** 安装版 LLVM/Clang + 同图 ThirdParty 静态依赖 + 构建后自检；父 Engine 仍以独立子进程构建宿主工具，清单提供输入和资源目录。

**Tech Stack:** CMake 4、Ninja、Clang 23、现有 zlib/zstd submodule。

**Spec:** [设计](../specs/2026-11-14-pangumeta-normal-build-design.md)

## Global Constraints

- 不构建或下载 LLVM，不使用 vcpkg；配置时不联网、不更新 submodule。
- 不改变 Engine/生成目标的标准库、RTTI 或移动 Toolchain。
- 取消独立 SDK 配置 API；同图依赖不安装到单独前缀。
- 保留源码/编译器身份、失败传播及正常无变化 Ninja 行为。
- Windows/Linux/macOS 共享同一源码并分别使用约定 BuildRoot。

## Review Focus

- 旧 LLVM_DIR/Clang_DIR、旧 HostToolchain 或 SDK 身份缓存不能影响自动选择。
- 配置期不依赖尚未构建的普通目标，链接后的真实检查不能被失败残留 exe 绕过。
- 私有压缩库 CRT 与所选 LLVM 一致，Clang 类层次 RTTI 单独匹配。
- 清单必须覆盖压缩库源/头、资源与验证脚本，不能让 no-op 每次重跑。
- 无 Meta 任务不启动工具；Query/Catalog-only 仍正确依赖可用工具。

### Task 1: 普通工具工程

**Files:** BuildTools/PanguMetaTool/CMakeLists.txt、cmake/{FindZLIB,Findzstd,CheckMetaTool}.cmake、Source/ThirdParty/{Zlib,Zstd}/CMakeLists.txt、Tests/BuildTools/PanguMetaTool。

**Interfaces:** LLVM_ROOT → PanguMetaTool；配置清单额外提供资源目录与所选 LLVM 根。

- [x] 添加无需 SDK 配置的自动构建/自检/增量测试，观察旧实现 RED。
- [x] 接入普通压缩库目标，替换配置期 SDK 链接探针为构建后验证。
- [x] 迁移 SDK 测试为发现、错误输入、固定依赖与失败重试；运行工具全套。

### Task 2: 原生父图及测试接线

**Files:** cmake/{PanguMetaTool,BuildMetaTool,PanguBuildToolsTesting}.cmake、根 CMakeLists、Tests/CMake 与调用辅助文件。

**Interfaces:** 宿主输入清单提供资源与依赖；父图不接收 SDK 配置文件。

- [x] 去掉 SDK 配置传递与测试登记条件，支持旧缓存迁移。
- [x] 验证三桌面工具测试及原生 Meta 注册、增量、失败恢复接线；未重跑完整布局/双模式矩阵。
- [x] 核对移动交叉入口不再传 SDK 文件；Android/iOS ARM64 Monolithic Meta 最终链接定向验证通过。

### Task 3: 清理与验收

**Files:** BuildTools/PanguMetaTool/Sdk（删除）、SDK/Prepare 测试（替代）、README/wiki/ADR 与宿主 BuildRoot。

- [x] 同步设计规范，解释被替代的历史流程与保留覆盖。
- [x] 独立只读评审未发现有证据的实质问题，完成格式/静态与定向验证。
- [x] 确认任务退出后删除旧 Sdk/PanguMeta 产物，归档验收并清理本计划临时目录。

## 执行记录

基线 `00a9607`。用户已批准上述方案；沿用共享源码与功能分支，不重复询问工作区或设计确认。

### 实现摘要

已删除整个源码 `BuildTools/PanguMetaTool/Sdk`（Prepare、Check、Inspect、HostToolchain 和依赖安装工程）。普通独立工具直接使用 LLVM_ROOT，固定 zlib/zstd 目标在同图构建，通过私有 Find 模块提供 LLVM 导出的命名链接目标；不先安装成另一套 SDK。Windows 读取 LLVM CRT 后设置源码依赖目标属性，Clang 类层次 RTTI 独立匹配 SDK，Engine/目标工具链不变。LLVM_ROOT 是选择入口，不再消费 PanguMetaSdk.cmake 或 SDK 配置传递参数。

保留现有原生输入清单，并额外输出所选 LLVM 根和资源目录，覆盖私有依赖、资源头和验证脚本。父图仅接线，不导入 LLVM targets。旧相同源码/编译器的 SDK 身份可一次性 --fresh 迁移，其他来源冲突仍拒绝。普通工具不继承 Engine Toolchain。

真实 ABI 链接使用普通目标。链接后执行 --check-sdk；失败删除当前构建目录内该次工具 exe 并报错，修复后重新链接和自检。移除配置期对未构建普通库执行的链接/运行探针，只保留版本/标准库/路径检查。

### 正式验收（2026-11-14）

| 宿主 | 独立普通工具完整 CTest | 原生父图 Meta 接线 | 正式缓存迁移定向检查 |
| --- | --- | --- | --- |
| Windows / LLVM 23.1.5 | 73/73，通过 | Incremental、FailureRecovery 通过；Registration 修复后单独 1/1 通过 | 3/3，通过 |
| WSL Ubuntu / LLVM 23.1.0 | 74/74，通过 | Registration、Incremental、FailureRecovery 3/3，通过 | 3/3，通过 |
| macOS / MacPorts 23.1.2 | 74/74，通过 | Registration、Incremental、FailureRecovery 3/3，通过 | 3/3，通过 |

独立完整测试缓存为各宿主 BuildRoot 的 `Tests/BuildTools/PanguMetaNormalBuild`。命令为 `cmake -S BuildTools/PanguMetaTool -B <cache> -G Ninja -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Release`，随后 `cmake --build <cache> --parallel 4` 和 `ctest --test-dir <cache> --output-on-failure --no-tests=error --parallel 4`；唯一环境选择为 LLVM_ROOT，不传 SDK 配置。

父图使用各桌面正式根 preset，随后定向选择 `^PanguMeta(Registration|Incremental|FailureRecovery)$`。Windows 首轮为 2/3，缺失工具源码的负例命中旧清单身份诊断而非预期入口诊断；修复入口预检及默认来源重置后单独 Registration 1/1。未把不同轮次拼接成一次 3/3 运行。

原正式缓存 `Tests/BuildTools/PanguMetaTool` 已迁移，不再保留旧 HostToolchain/SDK 输入；定向构建 PanguMetaTool、PanguMetaSdkTests、PanguMetaDependencySmoke，运行 AcceptsCompatibleSdk、DependencyRoundTrip、CliCheck（各平台 3/3）。生产工具在旧 SDK 目录删除后仍能通过普通父图/子图构建和 --check-sdk。Windows/Linux 父图与活动 Ninja/清单的旧目录引用扫描无匹配。

Android/iOS 均使用正式 ARM64 根 preset。PanguEngineHost 定向编译通过；随后使用 `Tests/CMake/PanguMeta/CrossCompileTests.cmake`，参数为对应目标 preset、Monolithic、实际宿主工具、独立 Tests/PanguMetaNormalBuild 输出根及 `PANGU_ENGINE_ALLOW_LOCAL_OVERRIDE=ON`。两平台均通过真实目标常量、最终链接、架构、系统依赖和最终 no-op；不再传 SDK 文件，不运行设备代码。

### 测试替代、失败与诊断

- RED：旧工程仅设置 LLVM_ROOT 不能配置，要求显式 PANGU_META_LLVM_DIR；新 Automatic 测试完成自动发现、构建后自检与第二次 no-op。
- 退役独立 Prepare 的未知目录/候选发布测试；这些流程随目录一起删除。固定源码缺失/版本拒绝、资源缺失、错误标准库、压缩往返在普通入口验证；CLI 用法原断言迁入 CliUsageTests.cmake。Attr/IR/Request/Context/CodeGen/Publish/Ownership 原有用例保留。
- ValidationFailureRetries 在配置后破坏独立资源夹具，实际链接后的检查失败；确认 exe 已移除，恢复资源后可重建并再次 no-op。测试不修改安装版 LLVM。
- Linux/macOS 初轮将 clang++ 符号链接解析为 C 驱动名称，出现缺失 C++ 标准库/unused stdlib 诊断；已显式保留 --driver-mode=g++，并刷新本任务错误的编译器元数据。修正后两平台完整工具测试通过。
- macOS 首次并发运行 ConsumerPipeline 超时，而独立自动构建用例接近 300 秒。根因为嵌套 Ninja 未按 CTest 配额限制并行。现将两个 --parallel 2 自动构建用例声明 PROCESSORS=2，ConsumerPipeline 同样声明 2 并设置 CMAKE_BUILD_PARALLEL_LEVEL=2；保持原 TIMEOUT 240/300，复验 74/74 通过。
- 独立只读评审核对 Find 模块、CRT/RTTI、失败传播、缓存迁移和清单，未发现有证据的实质问题。没有新增 C++ TU 修改；原 C 压缩夹具未改动，通过 clang-format，CMake 接线由三桌面配置/构建及定向测试验证，git diff --check 通过；不重复全库 clang-tidy。

### 清理与范围

源码 Sdk 目录及 Windows/Linux/macOS 各宿主 `Sdk/PanguMeta` 均已移除。Windows 历史 zstd 源码内两个相对文件符号链接先单独解除，再删除旧产物根，不跟随删除目标。保留安装版 LLVM、固定 submodule、普通工具/ThirdParty 构建缓存及其他任务产物；无包管理器或 LLVM 构建命令。

已同步 README、Toolchain、ThirdParty、Reflection/Meta 和 ADR-0019；旧两份 SDK 规格标明其准备层已被替代，历史计划与正式测试保留。本轮不推送/合并，不重跑完整 Engine full、双链接模式全矩阵或设备运行测试。收尾归档必要结论后，仅清理 `.superpowers/sdd/2026-11-14-pangumeta-normal-build/`。
