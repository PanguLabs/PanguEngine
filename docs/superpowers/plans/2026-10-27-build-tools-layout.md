# BuildTools 目录与命名迁移计划

> 执行方式：在当前共享源码工作区直接实施。用户已通过“按讨论结果修改”授权；不提交或推送。

**Goal:** 将构建工具与普通开发工具区分，消除独立工具构建中的 Host 命名歧义。

**Architecture:** 普通工具源码保留在 `Tools/`；ABI 构建工具移到 `BuildTools/AbiProfile/`。构建工具仍在宿主平台独立编译、按输入哈希发布；Bootstrap 只表示准备过程，EngineHost 及 Host/Target 平台术语保持原义。

**Tech Stack:** C++23、Clang 23、CMake 4.0+、Ninja。

**Spec:** 本次会话确认的目录分类，以及 `wiki/foundation/build-system.md`、`wiki/foundation/cpp-guidelines.md`。

## 范围与约束

- `cmake/HostTools.cmake` → `cmake/BuildTools.cmake`。
- `pangu_prepare_host_tools()` → `pangu_prepare_build_tools()`。
- `Tools/AbiProfile/` → `BuildTools/AbiProfile/`；其中 `Host/` → `Bootstrap/`。
- 工具独立工程名改为 `PanguAbiProfileBootstrap`；相关私有变量、测试文件及测试目标同步使用 BuildTools 命名。
- Windows 工具输出使用 `Build/BuildTools/Windows/`；Linux/macOS 使用宿主本地 `$HOME/PanguEngineBuild/BuildTools/<HostPlatform>/`。
- `PanguAbiProfile`、`PanguAbiProfileTool`、ABI Schema、算法及正式引擎目标保持原有职责。
- 保留独立本机 Release、工具链环境隔离、锁、输入哈希与不可变发布机制；不改 STATIC/SHARED 或 Debug/Release 策略。
- 不迁移旧构建树或复用旧 CMake Cache；新路径重新生成工具，正式目标按 Preset 重新配置。迁移程序不自动删除历史缓存；用户随后明确授权清理旧 Host 缓存，按该授权执行清理。

## Review Focus

- Probe 源码绝对路径、C++ 相对 include 和测试夹具须同时迁移。
- Windows/Linux/macOS 必须使用宿主本地的 BuildTools 输出，Android/iOS 不得执行目标平台工具。
- 并行准备、缓存复用和目标工具链隔离须继续由既有回归覆盖。
- 历史验收结果不能作为本次迁移后的测试结果；文档链接须可解析。
- 不保留运行路径上的 HostTools/Tools/AbiProfile 旧名称，也不改变 CMAKE_HOST_SYSTEM_NAME 等技术概念。

## Task 1：源码、构建接线与测试迁移

- [x] 移动工具源码、Bootstrap 入口及 CMake/测试文件，同步所有执行路径和标识符。
- [x] 复用既有 BuildTools/ABI 回归，不为机械路径重命名新增镜像测试。
- [x] 执行 Windows Preset 配置、构建及完整 CTest；验证新发布路径和缓存复用。

## Task 2：文档与质量检查

- [x] 说明 Tools/BuildTools 的职责及输出目录，更新使用说明、规范和现有计划引用。
- [x] 对修改涉及的 C++ 执行 clang-format 检查，并使用实际编译数据库执行针对性 clang-tidy。
- [x] 检查旧路径残留、文档链接及 git diff；进行独立代码评审并处理有效问题。
- [x] 记录实测平台与未验证范围。

## 执行记录

- 初始工作区干净，基线提交 `a949bd60b105c0c48259e1a2e10610118dc5d148`。
- 在现有共享源码工作区执行，保证 Windows/WSL/macOS Preset 继续读取同一份源码。
- Windows/Linux/macOS 的正式 Preset 配置和全量构建均通过；既有对象缺少当前编译收据时由 Guard 拒绝，通过正式 Preset 的 clean 重建生成物恢复，不补写或放宽证据。
- Windows 完整 CTest 的 34 项直接通过；两项 Android Collector 因沙箱无法执行用户 SDK 目录中的 NDK 失败，在沙箱外使用 `ctest --preset windows --rerun-failed --parallel 2` 重跑后 2/2 通过，合计覆盖 36/36。
- Linux 正式 Preset 的完整 CTest 34/34 通过。
- macOS 正式 Preset 的完整 CTest 36/37 首轮通过；`AbiCollectionBuildContractInterfaceCycle` 首轮触发 `collection_output_input_alias`，检查对应请求和输出目录后，使用相同固定 PATH 单独重跑该项，1/1 通过（42.05 秒）。首轮失败原因尚未确认，本次没有修改相关校验逻辑；不能把此结果描述为无重跑的 37/37 全绿。
- Android/iOS ARM64 正式 Preset 的配置、clean 后构建及两个 Module 的 ABI Collection 通过；Windows/Android 复用同一个已发布工具，macOS/iOS 也复用同一个已发布工具。
- Windows 再次配置/增量构建通过，两个 Module 的 Guard 均返回 `current`，BuildTools 发布版本仍为 1；PanguModule ABI Fingerprint 与历史同配置记录相等。
- 全部受影响 C++ 通过 clang-format 检查；Windows 实际编译数据库的六项 clang-tidy 返回成功，跨平台条件编译处存在既有未使用 include 提示；Linux Collector 使用 Linux 真实编译数据库的 clang-tidy 无诊断通过。
- Apple Collector 使用 macOS 真实编译数据库的 clang-tidy 无诊断通过。
- macOS SSH 登录脚本每次生成不同的 fnm PATH；验证命令使用固定 PATH，并在相同环境内配置、构建和测试，未改变项目环境校验逻辑。
- 独立只读评审未发现源码遗漏或运行接线问题；指出旧设计/计划中两处全平台目录表述不准确，已修正为 Windows NTFS 与 Linux/macOS 本地 HOME 布局。
- 新增说明文档的相对链接检查、旧运行路径/标识符残留检查及 `git diff --check` 通过；未提交或推送。迁移完成后，按用户明确要求清理了 WSL `/home/moozen/PanguEngineBuild/Host` 和 macOS `/Users/moozen/PanguEngineBuild/Host`；Windows `Build/Host` 检查时已不存在，三宿主的新 BuildTools 缓存均保留。
- Android/iOS 本次验证为 ARM64 STATIC 构建和宿主 ABI Collection，没有执行设备端程序；未扩展 Debug/Release、STATIC/SHARED 策略。
- 后续双轴 code-review：Standards 0 项，Spec 2 项文档遗漏（第三方接入说明中的旧源码路径、旧缓存清理记录），均已修正；本轮仅修改文档，执行路径/链接及差异检查，不重复运行构建和 CTest。
