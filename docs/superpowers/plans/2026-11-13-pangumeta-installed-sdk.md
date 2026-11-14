# PanguMetaTool 复用已安装 SDK 实施计划

> **For agentic workers:** 使用 superpowers:executing-plans 在当前会话逐项执行。

**Goal:** Windows/Linux 无需自行编译 LLVM 即可构建 PanguMetaTool。

**Architecture:** 独立宿主工具匹配现成 SDK 的标准库和 RTTI；SDK 接纳由实际属性解析、string ABI 链接和错误路径验证。Engine 编译配置保持独立。

**Tech Stack:** C++23、Clang 23、CMake 4、Ninja、GoogleTest。

**Spec:** [设计](../specs/2026-11-13-pangumeta-installed-sdk-design.md)

## Global Constraints

- Windows/Linux 不编译 LLVM；使用已安装开发包，不下载或覆盖现有安装。
- Windows 使用 MSVC STL；Linux PanguMetaTool 可使用 libstdc++，Engine 仍为 libc++。
- Clang 适配代码匹配 SDK RTTI；生成代码与目标上下文仍保留目标 RTTI。
- 保留原始 pga 语法、严格诊断、正式测试和共享源码路径。

## Review Focus

- 切换 SDK 后旧标准库/RTTI 缓存污染：重新配置必须匹配所选包。
- 无 RTTI SDK 不得被本地派生类伪造能力：实际属性解析验证。
- 生成消费必须继续使用 libc++ 和目标 RTTI。
- Debug MSVC STL 与 Release SDK 错配必须明确失败。
- Prepare 不得启动 LLVM 构建或覆盖已有目录。

### Task 1: 现成 SDK 接纳与行为检查

**Files:** BuildTools/PanguMetaTool/CMakeLists.txt、Sdk/*、Driver/SdkInfo.*、Tests/BuildTools/PanguMetaTool/{SdkLinkTests.cpp,SdkTests.cmake,CMakeLists.txt}。

**Interfaces:** 保留 CheckSdk() 与 SDK 配置键；HasRtti 如实报告。

- [x] 修改 SDK 测试，支持无 RTTI SDK，保留 ABI/资源错误及显式准备安全性覆盖。
- [x] 用 Windows 现成 SDK 配置，观察旧策略拒绝（RED）。
- [x] 实现独立宿主标准库配置、按 SDK 配置 Clang 目标 RTTI、真实属性 SDK 检查及只检查的 Prepare。
- [x] 构建并运行独立 PanguMetaTool 全部测试，验证通过（GREEN）。

### Task 2: 跨平台验证与规范同步

**Files:** BuildTools/PanguMetaTool/README.md、wiki/foundation/{toolchain,cpp-guidelines}.md、wiki/core/reflection-meta.md、wiki/decisions/0017-standalone-pangu-meta.md、必要测试消费接线。

**Interfaces:** Linux 主工具使用 libstdc++，生成消费使用 Engine libc++ 工具链。

- [x] 同步局部政策例外和显式接入命令，标明历史验证范围。
- [x] Windows/Linux 现成 SDK 全部工具测试；macOS 同范围回归。
- [x] 运行 clang-format、针对性 clang-tidy、git diff --check。
- [x] 独立评审，修复实质问题，归档验收摘要；确认任务退出后清理本计划临时目录。

## 执行记录

用户选择方案 1 后直接在共享源码上实施，功能分支为 `feat/pangumeta-installed-sdk`；改动保留在工作区供审阅。本轮不合并、不发布。新 [ADR-0018](../../../wiki/decisions/0018-installed-pangumeta-sdk.md) 部分替代旧宿主 SDK 政策，其余 Meta 架构仍有效。

### 验收摘要（2026-11-13）

| 宿主 | 现成 SDK | 独立工具完整 CTest | 实际边界 |
| --- | --- | --- | --- |
| Windows | `D:/Developer/LLVM`，23.1.5，RTTI OFF | 68/68，通过 | MSVC STL；真实静态 SDK 链接、属性/常量求值、生成消费；配套 zlib/zstd 与 DIA SDK |
| WSL Ubuntu | `/opt/llvm-23.1.0`，23.1.0，RTTI OFF | 69/69，通过 | 现成 clang-cpp 共享库和 libstdc++；生成消费仍为 Engine libc++ |
| macOS | MacPorts LLVM 23.1.2，RTTI ON | 69/69，通过 | 现成 clang-cpp/LLVM 动态库和 libc++；同一共享源码直接构建 |

测试缓存为各宿主 BuildRoot 下 `Tests/BuildTools/PanguMetaInstalledSdk`。配置命令使用 `cmake -S BuildTools/PanguMetaTool -B <测试缓存> -G Ninja -DBUILD_TESTING=ON` 及显式 SDK 路径；验收为 `cmake --build <测试缓存> --parallel 4` 后运行 `ctest --test-dir <测试缓存> --output-on-failure --no-tests=error --parallel 4`。SDK 含资源头、真实开发库和必要运行依赖，未编译或替换 LLVM。

Windows/Linux 本机 `Sdk/PanguMeta/PanguMetaSdk.cmake` 已经真实预检后改指向现成安装。两平台正式 `BuildTools/PanguMetaTool` 已通过 `cmake/BuildMetaTool.cmake` 构建；生产工具 `--check-sdk` 通过。Linux 的 Check、生产工具、正式工具测试缓存已用带原参数的 `cmake --fresh` 迁移，避免继续继承旧 Engine libc++ Toolchain；旧 LLVM 源码、SDK 构建树和其他平台状态保留。

Linux `ldd` 验证工具依赖现成 `libclang-cpp.so.23.1` 和 `libstdc++.so.6`；`ConsumerBoth` 依赖所选 LLVM 安装的 `libc++.so.1`/`libc++abi.so.1`，未引入 libstdc++。目标侧 RTTI 和原始属性语法保持，由生成消费正式测试覆盖。

### 失败、修复与评审

- RED：改造前现成 Windows SDK 配置在原 `LLVM_ENABLE_RTTI=ON` 强制检查失败；改造后真实解析与工具全套通过。
- macOS 首轮 `MetaSdk.RejectsWrongStdlib` 失败：无 libstdc++ 导致 `project()` 先报通用编译器错误。已恢复平台对应的显式标准库错误诊断，修正后完整 69/69 通过。
- 独立评审发现两项 P2：SDK 负例未透传 `PANGU_META_HOST_COMPILER`；多项依赖前缀跨 COMMAND 展开可能拆分。已显式传编译器并用配置文件传递完整依赖上下文。
- 编译器透传回归在新缓存、清空 `LLVM_ROOT` 时复现旧失败；新增正式 `MetaSdk.AcceptsExplicitContext`，所有 SDK 子配置清空该环境变量并使用新配置元数据。最终三桌面全套包含该用例。
- 新 ContextParser 将测试的 Clang 继承代码与 GoogleTest 分开编译；GTest 继续保留 RTTI，不修改其全局适配契约。
- `SdkInfo.cpp`、`ContextParser.cpp/.h`、`ContextTests.cpp`、`SdkLinkTests.cpp` 的 clang-format 检查通过。四个变更 C++ TU 的仓库 clang-tidy 检查通过；使用实际 Windows 编译数据库，仅在临时副本中将 CMake `.modmap` 响应文件路径的反斜杠规范化为斜杠，其他参数及工作目录不变。未对整个仓库运行 clang-tidy。
- `git diff --ignore-submodules --check` 通过。测试维护保留原 ABI 错配与 Prepare 目录保护；废弃的 RejectsNoRtti 政策由真实 SDK 接纳、资源缺失拒绝及显式上下文测试替代，说明见工具 README。

### 遗留边界

未重跑完整 Engine full、Android/iOS 或两种链接模式的全矩阵，不将独立工具测试数量与根调度数量累加。未新增预编译 SDK 安装器；包获取、SDK 升级和任意跨前端语义兼容仍属后续工作。
