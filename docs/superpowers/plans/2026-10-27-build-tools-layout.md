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
- macOS 正式 Preset 的完整 CTest 36/37 首轮通过；`AbiCollectionBuildContractInterfaceCycle` 首轮触发 `collection_output_input_alias`，检查对应请求和输出目录后，使用相同固定 PATH 单独重跑该项，1/1 通过（42.05 秒）。当时尚未确认原因，没有修改相关校验逻辑；不能把该次结果描述为无重跑的 37/37 全绿。后续原因与修复见下文追查记录。
- Android/iOS ARM64 正式 Preset 的配置、clean 后构建及两个 Module 的 ABI Collection 通过；Windows/Android 复用同一个已发布工具，macOS/iOS 也复用同一个已发布工具。
- Windows 再次配置/增量构建通过，两个 Module 的 Guard 均返回 `current`，BuildTools 发布版本仍为 1；PanguModule ABI Fingerprint 与历史同配置记录相等。
- 全部受影响 C++ 通过 clang-format 检查；Windows 实际编译数据库的六项 clang-tidy 返回成功，跨平台条件编译处存在既有未使用 include 提示；Linux Collector 使用 Linux 真实编译数据库的 clang-tidy 无诊断通过。
- Apple Collector 使用 macOS 真实编译数据库的 clang-tidy 无诊断通过。
- macOS SSH 登录脚本每次生成不同的 fnm PATH；验证命令使用固定 PATH，并在相同环境内配置、构建和测试，未改变项目环境校验逻辑。
- 独立只读评审未发现源码遗漏或运行接线问题；指出旧设计/计划中两处全平台目录表述不准确，已修正为 Windows NTFS 与 Linux/macOS 本地 HOME 布局。
- 新增说明文档的相对链接检查、旧运行路径/标识符残留检查及 `git diff --check` 通过；未提交或推送。迁移完成后，按用户明确要求清理了 WSL `/home/moozen/PanguEngineBuild/Host` 和 macOS `/Users/moozen/PanguEngineBuild/Host`；Windows `Build/Host` 检查时已不存在，三宿主的新 BuildTools 缓存均保留。
- Android/iOS 本次验证为 ARM64 STATIC 构建和宿主 ABI Collection，没有执行设备端程序；未扩展 Debug/Release、STATIC/SHARED 策略。
- 后续双轴 code-review：Standards 0 项，Spec 2 项文档遗漏（第三方接入说明中的旧源码路径、旧缓存清理记录），均已修正；本轮仅修改文档，执行路径/链接及差异检查，不重复运行构建和 CTest。

## macOS 首轮失败追查（2026-10-27）

### 根因与复现

- 在 macOS 原生 APFS 目录中，另一线程反复创建、删除 `.invalid` 和 `.collection-lock`，会使实际加载的系统 `/usr/lib/libc++.1.dylib` 的 `std::filesystem::weakly_canonical` 返回**空路径且错误码为零**。独立标准库调用在 10,000 轮中观察到 639 次该结果；不依赖 SMB 源码挂载即可复现。
- 原请求解析仅检查错误码；空输出路径进入 `Within(output, input)` 后，被当成所有非空输入的祖先，触发原始 `collection_output_input_alias`。并行 Guard、Compile 和完成状态清理使该窗口可达；误报发生在完成记录发布前。
- 新增正式回归 [`CheckConcurrentOutputs`](../../../Tests/AbiProfile/CollectionRequestTests.cpp)：调用真实输入守卫，同时模拟上述标记和锁生命周期。修复前 `ctest --preset macos -R '^AbiCollectionRequest$' --output-on-failure` 在 4,000 轮中有 **491 次同类误报**，CTest 失败。

### 修复边界

- [`ResolvePath`](../../../BuildTools/AbiProfile/Private/ToolIo.cpp) 使用 `canonical` 解析实际存在的前缀；只在 ENOENT 时剥离缺失后缀，拼回后作词法规范化。成功结果必须为绝对路径，其他文件系统错误仍失败。
- 请求解析、编译命令解析、生命周期输出检查和 `IsSameFile` 统一使用该工具私有函数；原有 symlink、hardlink、保留工作目录及 `equivalent` 身份校验保留。未改变 ABI Schema、Fingerprint 或 Loader 保证范围。
- 并发回归限定于 `__APPLE__`：Windows 删除中的文件可能返回 `access_denied`，应保持 Fail Closed，不能要求这种权限错误被接受。其他平台继续执行既有路径与文件身份回归。
- 临时诊断已移除；独立只读评审及限定回归平台后的复审均无阻断项。临时探针和日志保留在本地 `.probe/macos-interface-cycle/`；正式回归不依赖该忽略目录。

### 验证与独立问题

- 修复后的 macOS 正式完整 CTest **37/37**、Linux 正式完整 CTest **34/34** 通过，均无失败重跑；macOS 包含原始接口环测试及新增并发回归。
- Windows 首次完整 CTest 为 **33/36**：新增并发回归因 DeletePending 权限错误失败，随后已修正为 Apple 专属；另外两项为 `AbiCollectorAndroidCli`、`AbiCollectorAndroid`。使用修复前的已发布工具对照，Android CLI 同样失败，不归因于本次路径修复。
- Android 当时的 NDK 原始宏输出使用 CRLF，既有 `AndroidCollector::Macro` 仅按 LF 截取，保留的行末 `\r` 导致与冻结宏值比较失败。该问题未在 macOS 追查轮修改，当次不能宣称 Windows 完整测试全绿；后续修复见下节。
- 限定回归平台并清理两条新增静态检查提示后，三桌面正式构建均通过；各自再次执行路径请求、编译命令、CLI、IO 与依赖图五项回归，均为 **5/5**。Windows 的请求回归已恢复通过，仍保留上述两项独立 Android 失败。
- 最终 macOS 最小探针在同一轮 10,000 次校验中仍直接观察到系统标准库 860 次空路径成功，项目守卫为 **0 次误报**；证明修复没有依赖问题窗口消失。
- 最终版本的原始 `AbiCollectionBuildContractInterfaceCycle` 使用 `--repeat until-fail:5` 连续 **5 次通过**（44.29 / 41.40 / 40.96 / 40.73 / 41.00 秒，总计 208.44 秒），无失败重跑。
- clang-format 检查、实际 Windows/macOS 编译数据库的针对性 clang-tidy 和 `git diff --check` 均返回成功。已清理本次新增的未使用 include 与循环提示；仍有既有 MSVC STL 枚举、macOS 文件所有权/包含及条件编译处 const 等提示，未扩大修改范围或增加豁免。

## Android CRLF 解析后续修复（2026-10-28）

- 确认两处同类问题：[`AndroidCollector::Macro`](../../../BuildTools/AbiProfile/Private/AndroidCollector.cpp) 把 CRLF 的 CR 保留为宏值；[`LibcxxConfiguration`](../../../BuildTools/AbiProfile/Private/HeaderTrace.cpp) 也保留行末 CR，导致普通 LF 源码与 CRLF 基线的配置比较冲突。
- 两处解析都仅移除**紧邻已找到的 LF 的一个 CR**，不作通用 trim；ABI 值中的空格、嵌入 CR、额外 CR 和未配对的 EOF CR 仍参与严格校验。原始编译器输出和 Evidence 不改写，Profile/Fingerprint 协议不变。
- [`AndroidCollectorTests`](../../../Tests/AbiProfile/AndroidCollectorTests.cpp) 使用真实 NDK 观测构造 LF/CRLF 变体，验证生成相同 Profile、原始 Evidence 保留，以及两种格式下尾随空格和嵌入 CR 的错误 ABI 值被拒绝。修复前 LF 通过、CRLF 在 `android_abi_macros_or_triple` 失败；修复后原始 API/CLI 两项 Android 测试 **2/2 通过**，包括普通源码和既有负测。
- [`AbiProfileTests`](../../../Tests/AbiProfile/AbiProfileTests.cpp) 新增 10 项快速共享配置解析回归，覆盖双向 LF/CRLF、混合换行、末行无终止换行、值变化、缺失空值宏及显著字节保留。修复前 **60/63**（三项换行等价失败），修复后通过。
- 最终正式 Android ARM64 Preset 的配置、构建和两 Module 的宿主 ABI Collection 通过；Linux 的共享回归及 Collector **2/2**、macOS/iOS 的共享回归及 Collector **3/3** 通过。未执行 Android/iOS 设备程序，也未重新运行 Linux/macOS 的完整测试集。
- 最终 Windows 正式完整 CTest **36/36 通过**（470.17 秒，无失败重跑），包含原先失败的两项 Android Collector、两组新增换行回归及既有负测；本轮 Android CRLF 解析问题已关闭。
- 四个 C++ 文件的 clang-format、实际 Windows 编译数据库的 clang-tidy 和差异检查通过；两轮独立只读代码评审无阻断项。本轮临时证据保留在 `.probe/android-crlf/`；既有暂存内容保持原样，未提交或推送。
