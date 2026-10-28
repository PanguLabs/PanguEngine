# 五平台 ABI Profile 采集器 Implementation Plan

> 状态：Completed（批准的 STATIC 范围，2026-10-27）；Task 1—12 的 68 项检查全部完成，独立复审通过。追加评审修复与文档已按用户授权通过 amend 合入同一提交，未推送。最终 EXE/SHARED Loader 不在本计划范围。
>
> 阅读约定：以“最新执行进度”和已勾选的任务清单为当前结论；折叠区中的未完成项、评审阻断和测试数量仅描述当时状态。

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 普通 STATIC Module Target 构建成功时，目标库、完整 v1 Profile 和构建内完成记录同时有效；五个平台使用同一 C++ 采集流程。
**Architecture:** 独立 Build tool bootstrap 提供只读版本化工具；CMake 导出真实对象、配置与链接依赖，仅负责构建接线。C++ 工具拥有编译记录、采集、Guard、依赖检查和发布。平台后端解释实际编译器/STL/SDK/运行库证据。
**Tech Stack:** C++23、Clang（Android 使用所选 NDK Clang）、CMake 4.0+、Ninja、现有 nlohmann/json、CTest、各平台 SDK/LLVM 二进制检查工具。
**Spec:** [已批准设计](../specs/2026-10-23-abi-collection.md)，实现基线 `dbfe7aa`；冻结协议以 `wiki/foundation/abi-profile-fingerprint.md` 为准。

## 临时工作区清理（2026-10-28）

计划与独立评审修复均已完成，必要结论已归档到本计划、[评审修复计划](2026-10-26-abi-collection-review-fixes.md)及相关 wiki。确认无运行任务依赖后，按用户要求删除了 `.superpowers/sdd/2026-10-23-abi-collection/` 和 `.superpowers/sdd/9561-code-review-fixes/`，保留 `.superpowers/sdd/.gitignore`。

下文提及上述目录的详细命令、临时 Diff、阶段报告和评审文件均为历史路径，已随临时工作区清理，不再作为当前可访问的证据入口。正式设计、计划、源码、`Tests/` 和各平台 BuildRoot 保留；历史验收摘要不代表之后每个提交的测试结果。

后续提交 `0a3b79d` 修复 Windows 私有探针目录清理竞态，独立复审通过；Windows 压力测试 50/50、Linux/macOS 定向测试各连续 5 次通过。该轮 Windows 完整 CTest 为 35/36，剩余失败定位为 `AbiCollectorWindowsCli` 在 `.collection-lock` 的 `output_symlink` 检查中遇到 system error 5：锁目录进入 `DeletePending` 时，等待方把短暂 `ACCESS_DENIED` 当成永久失败。

后续修复让 Windows 等待路径重新执行输出校验与锁获取，只对此锁路径的 `ERROR_ACCESS_DENIED` 重试，并在截止时间到达后保留原始错误；其他路径及非该错误仍立即失败。回归使用真实暂停的编译器子进程复现 `DeletePending`，修复前在 `reserved_signature_path` 以 native code 5 失败，修复后定向及完整测试通过。Windows 完整 CTest 为 **36/36**（899.25 秒），Linux/macOS 的 `AbiCollectionState` 定向测试分别通过；`clang-format` 与 `git diff --check` 通过，`clang-tidy` 无新增用户代码诊断。该后续修复目前仍在工作区，尚未提交或推送。

## 最新执行进度（2026-10-27）

**Task 1—12 均已完成，无剩余计划内验收门禁。** 原 R1—10 和提交 `9561fe6` 的追加修复均已完成独立复审；最终 Spec **Compliant**、Quality **Approved**。

### 提交 9561fe6 的追加评审修复已完成

- Standards / Spec 双轴评审的 8 项问题均已修复，并完成独立复审；验证中发现的输入检查并发和完成记录失效恢复问题也已修复。没有剩余 Critical/Important。
- 当前源码的最终完整 CTest：Windows **36/36**、Linux **34/34**、macOS **34/34**，合计 **104/104 PASS**，均 exit 0。Android/iOS ARM64、Windows Debug MD/Release MT 的构建、Host 采集、增量 Guard 和完成引用摘要校验通过；移动端未执行目标程序。
- 七种配置的构建前后源码摘要一致；Windows 三种 CRT 配置中 Foundation/Module 同配置指纹相同、跨配置指纹不同。当时的详细验收报告 `.superpowers/sdd/9561-code-review-fixes/final-verification.md` 已随临时工作区清理；上述验收摘要保留，原始审计输出位置为 `.probe/Fix9561VerifiedEvidence/audit.json`（非版本化文件，不作为正式计划的读取前提）。
- 最终验收时 HEAD 为 `9561fe6d443f21da1c0af02ecc87435f35e9301c`；用户随后授权将追加修复和文档通过 amend 合入该提交，提交 ID 随之改变；未推送。以下保留此前实施与验收的历史记录。

<details>
<summary>历史执行记录（2026-10-24～26，不代表当前待办）</summary>

## 历史执行进度（2026-10-26）

### 当日最终状态：评审修复与验收已完成

- **Task 1—12 在批准的 STATIC 范围均已完成。** R1—10 已修复并独立复审，Spec **Compliant**、Quality **Approved**，没有剩余 Critical/Important。
- 最终新树完整 CTest：Windows **33/33**、Linux **31/31**、macOS **31/31**，合计 **95/95 PASS**，均 exit 0；Android/iOS ARM64 及 Windows Debug MD/Release MT 的真实 Module 构建、Host 采集、完成引用摘要和增量 Guard 均通过。
- 本节汇总最终验收结论，详细执行证据保留在本地审计记录中。验收时未提交、推送或操作 index/HEAD；最终 EXE/SHARED Loader 不在本轮范围。

### 修复前独立补审：Needs fixes（历史记录）

- 使用 `superpowers:requesting-code-review` 派发三个独立上下文的只读审查者，已补齐 **Task 4—8、10—12** 的独立评审；Task 1—3、9 保留已有独立评审记录。
- **Spec: Issues；Quality: Needs fixes；没有累计 Approval。** 共 10 项 Important，其中 8 项已由隔离探针复现，另外 2 项为静态审查结论。包括旧归档与新对象收据错配、辅助输出覆盖请求、libc++ ABI 宏和位域参数漏检、诊断丢失、冲突 CRT、环境基线缺失及 Build toolchain 环境污染。逐 Task 结论与复现证据保留在本地审查记录中。
- 本轮新验证：Windows 全量 29/31（Android 两项受沙箱启动限制），允许 NDK 执行后原范围 2/2 通过；Linux 全量 29/29；macOS 全量 28/29（取消耗时断言），该项单独连续三次通过。**不能将本轮写成新的 89/89 全量通过。**
- 本轮仅补审及更新记录，未修复生产源码；Task 4—8、10—11 的实施记录保留，但当前评审结论为待修复。**Task 12 仍未完成，剩余门禁已从缺少 Reviewer 转为解决审查阻断项并获复审批准。** 未提交、未推送、未操作 index。

### 补审前的实施与验证记录

- **Task 1—8**：已有分阶段实施和验证记录；完整提交尚未执行。
- **Task 9（已完成，独立复审通过）**：CMake 4.0 真实 Target Link Graph 与 C++ `DependencyGraph`、依赖优先 `CollectionGroups`、技术 STATIC SCC 组级真实采集/逐字段校验/失败撤销与 `group-complete.json` 封口均已实现；三平台实际 CMake 链接夹具保留重复归档与最终 Host 符号引用。独立复审结果为 Spec **Compliant**、Quality **Approved**。最终 Windows **35/35**、WSL Linux（CIFS、Build/Linux）**25/25**、macOS **28/28** 完整 CTest 通过，Linux Group **208.26 秒**，保留 600 秒限时；末轮测试源码改动在 Windows/macOS 另经定向构建和测试。详见 `.superpowers/sdd/2026-10-23-abi-collection/task-9-independent-review.md`。生产 Root 自动接线、Host Bootstrap、设备最终消费者均不属于 Task 9，未实施。
- **Task 10（已完成，Self-Review）**：独立 Host Release/原生工具链自举、基于源码与编译器身份的版本化发布、CMake 锁及完整性标记均已实现；Windows/Linux/macOS 的并发双配置 CTest 各 1/1 通过，旧已发布版本不原地覆盖。桌面完整 CTest Windows 36/36、Linux 26/26、macOS 29/29，通过后增加的真实发布工具启动断言也分别 1/1 通过。详细过程见 `.superpowers/sdd/2026-10-23-abi-collection/task-10-execute-and-review.md`。尚无独立 Reviewer，记录为 Self-Review。
- **Task 11（已完成，Self-Review）**：Root 统一 Host Guard → 实际编译收据/Launcher → STATIC 归档 → Collect、依赖优先 Module/技术 STATIC SCC、INTERFACE/条件 Generator Expression 接线已完成。旧 Windows CMake 四脚本及 `--collect-windows` 生产入口已移除。最新完整 CTest **Windows 31/31、Linux（ext4）29/29、macOS（APFS）29/29，合计 89/89 通过**；Android/iOS ARM64 的 Root Module 构建及 Host ABI 完成记录通过。Mac 重复链接随机 `LC_UUID` 污染摘要的问题经 RED→GREEN 修复。旧对象缺失收据保持 Fail Closed，需新 BuildRoot 正规重建。详见 `.superpowers/sdd/2026-10-23-abi-collection/task-11-execute-and-review.md`；审查为 Self-Review（没有独立 Reviewer），现有 CMake 4.x 构建验证与 Windows Debug MD/Release MT 矩阵交由 Task 12 完成；SHARED Loader/最终消费者仍不在当前保证范围。
- **Task 12（正在实施与 Self-Review，未完成）**：Windows Release MD/Debug MD/Release MT 的真实工具链参数、Module 归档和 ABI 完成记录已验证；五平台在独立 BuildRoot 上完成 `BUILD_TESTING=OFF` 的 Foundation/Module 真实构建和 Host 采集，再次增量构建 Guard 均为 `current`。Host Probe 版本追踪修复后，在全新 BuildRoot **重新执行**三个桌面平台完整 CTest：Windows **31/31**、Linux（ext4）**29/29**、macOS（APFS）**29/29**，合计 **89/89**（均退出码 0）；README、build-system、ABI Profile 实施状态及 wiki 索引已同步。**未关闭的门禁：整个累积分支尚无独立 Reviewer；按用户要求使用现有 CMake 4.x，不再把恰好 4.0.x 的二进制测试列为门禁**；不得把 Build-Time STATIC `complete.json` 用于 SHARED Loader 放行。见 `.superpowers/sdd/2026-10-23-abi-collection/task-12-execute-and-review.md`。
- 审查细节与每轮命令见 `.superpowers/sdd/2026-10-23-abi-collection/task-9-execute-and-review.md`（本地忽略的审查记录）。

## 历史执行进度（2026-10-24）

- **Task 1—3 已完成**：私有请求、原生进程执行、精确 TU/响应文件处理；见 `.superpowers/sdd/2026-10-23-abi-collection/progress.md`（本地忽略的实施记录）。
- **Task 4 已完成第一阶段交接**：Guard、对象真实编译收据、内容哈希与失效、完成文件最后发布、故障注入/并发拒绝。Windows 完整 CTest 28/28、macOS 完整 CTest 20/20；WSL Linux 对应四项 Collection 测试 4/4（未宣称 Linux 全量测试）。macOS SMB 共享目录不支持硬链接，该单项被明确跳过，其余安全验证仍执行。
- **Task 5—6 已完成；Task 7—12 未完成**：Windows/Linux C++ Collector 的阶段验收已通过，Apple 后端正在调查；Android 后端、依赖闭包、Host Bootstrap、Module Target 强保证及五平台整体验收仍待实施。旧 Windows CMake 采集脚本暂时保留，按 Task 11 再切换 Root CMake。
- **临时文件清理待处理（2026-10-24 核查）**：`Tests/AbiProfile/CollectionStateTests.cpp.tmp0` 为编辑工具遗留文件，**不受 Git 跟踪、未被 CMake/源码/测试引用**；实际构建仅使用 `CollectionStateTests.cpp`。已尝试 Windows `Remove-Item`、单文件 ACL 授权/取得所有权及 macOS SMB `rm`/重命名，全部因权限拒绝而未能删除（WSL 挂载中不可见）；**不能标记为已删除**。需由仓库管理员仅针对该 `.tmp0` 文件修复权限并删除，随后核对 `git status`；不得扩大权限变更范围到目录或正式源码。

</details>

## Global Constraints

- 不改变 v1 Schema、Canonical 字节协议、Fingerprint 算法及原四参数编码接口。
- 一个 Module 一个实际 STATIC/SHARED Library Target；不拆额外 Module 库，不建立 BinaryImage、ThirdParty Wrapper 或通用进程框架。
- 本轮普通受管 Module 为 STATIC；Windows x64、WSL Ubuntu Linux x64、macOS x64、Android ARM64、iOS ARM64；使用现有 Ninja 单配置场景。
- 全可达源码 C++ STATIC 依赖严格检查，PRIVATE 不豁免；普通 IMPORTED C++ 库不因携带 Profile/摘要而获准接入。
- 受管 Module 构建图无环；普通技术 STATIC 内部归档环可分组；构建图与运行期声明图分开诊断。
- 普通工程宏允许并记录；已知 ABI 控制项严格核验；未知关键差异拒绝，公共 API/ODR 契约另行验收。
- 允许 STATIC 中仅由最终宿主提供的符号；最小运行库探针不强制链接全部 Module 对象，不运行 Android/iOS 目标程序。
- 每次执行轻量 Guard；ABI 输入变化后完成必要重建，或明确失败，禁止给旧对象套用新 Profile。
- 构建内内容关联不等于 Loader 信任；不实施签名、TOCTOU、防不可信 SDK 或生产 SHARED 装载。
- 构建工具自动准备、独立 Release 配置；并发构建同代单写，发布后只读；不复制引擎源码到 Mac 临时目录。
- 修改 C++ 前遵守仓库 C++ 规范；新改 CMakeLists 使用 `cmake_minimum_required(VERSION 4.0)`。临时探针放 `.probe/`，正式回归放 `Tests/`。
- Git 提交/推送仅在用户明确授权后执行，不为技能中的提交步骤自行操作 index 或创建提交。本次用户已授权提交并 amend 至原 `9561fe6`，未授权推送。

## Review Focus

1. 缓存命中仍须检测同路径内容替换，而编译器/SDK 漂移不能重新标记旧对象：任务 4、11 的原地替换与编译来源测试覆盖。
2. 同源多 Target、生成源、条件源和带引号的响应文件不能串入其他对象：任务 3、11 的精确 output 关联测试覆盖。
3. 技术归档环、合法最终 Host 符号不能被假想最终链接误拒绝：任务 5、9、11 的真实归档夹具覆盖。
4. Windows/Android、macOS/iOS 并行自举不能覆盖使用中的工具或继承 Target SDK：任务 10、12 的双配置进程测试覆盖。
5. 失败/取消/部分发布不能留下有效完成记录，普通编码不能伪装正式采集：任务 2、4、11 的失败注入覆盖。

## 文件与接口约定

以下是工具私有接口，不冻结 Public C++ ABI；不为本计划创建独立空壳库。

| 文件 | 负责内容 |
| --- | --- |
| `Private/CollectionRequest.h/.cpp` | 私有请求 v2 的校验与类型化；冻结 Profile 仍为 schema 1 |
| `Private/CompileCommands.h/.cpp` | 全 argv、Host quoting、响应文件、精确对象匹配；不在这里决定 SDK 版本 |
| `Private/Process.h`、`ProcessWindows.cpp`、`ProcessPosix.cpp` | 工具私有子进程生命周期、显式 argv、环境、输出与取消 |
| `Private/CollectionState.h/.cpp` | 输入快照、对象编译记录、失效、Guard、锁与最后完成记录 |
| `Private/Collection.h/.cpp` | 流程调度、CLI 操作及平台分派 |
| `Private/Encoding.h/.cpp` | 从 Main 提取原编码流程，保留现有校验/清理/退出分类 |
| `Private/WindowsCollector.h/.cpp` | Windows 观察与对象/SDK/CRT 核验，复用 `WindowsProfile` 的纯映射 |
| `Private/LinuxCollector.h/.cpp` | Linux libc++/Sysroot/ELF 证据 |
| `Private/AppleCollector.h/.cpp` | 共享 Apple 采集，区分 macOS 与 iOS Target |
| `Private/AndroidCollector.h/.cpp` | NDK/目标 API/Android libc++/ELF 证据 |
| `Private/DependencyGraph.h/.cpp` | 真实链接闭包、分类、SCC、严格相容与导入库拒绝 |
| `BuildTools/AbiProfile/Bootstrap/CMakeLists.txt`、`cmake/BuildTools.cmake` | 最小 Build tool bootstrap 与版本化工具发布接线 |
| `cmake/AbiProfile/AbiProfile.cmake` | 注册、元数据导出、Guard/编译记录/完成规则；取代原四份执行脚本 |

### 共同数据与签名

实现前在任务 1 固定下列头文件契约，后续任务引用同一声明。成员采用仓库 CamelCase，输入文本 UTF-8，文件内容/参数按值拥有。

- `CollectionRequest`：SourceRoot、BuildRoot、TargetName、Configuration、HostPlatform、TargetPlatform、CommandLineStyle、Compiler、Database、ObjectManifest、Library、OutputDirectory、SchemaFile、SignatureHeader、LinkGraph、PlatformContext、EnvironmentBaseline。PlatformContext 仅保留 CMake/Toolchain 声明的原始上下文，实际来源由后端核验。
- `CompileUnit`：Compiler、Directory、Source、Object、Arguments、ResponseFiles；Arguments 是展开后的完整真实 argv，不默默丢弃来源信息。
- `CompileObservation`：Facts（私有 JSON）、Headers、ObservedInputs；描述编译前可观察的事实，不宣称对象已经由此编译。
- `InputSnapshot`：文件路径/角色/内容摘要、有效相关环境、CompileDigest、DependencyDigest、Digest；签名头是派生输出，不递归计入自身输入摘要。
- `CollectedProfile`：Profile、Evidence、ObservedInputs；Profile 仍只含冻结字段。
- `ProcessRequest`：Program、Arguments、Directory、Environment、OutputMode、Timeout、CaptureLimitBytes；`ProcessResult` 拥有 ExitCode、Stdout、Stderr。实际编译使用转发模式和构建系统取消；探针使用有界捕获，不给正常编译强加短超时。
- `Result<CollectionRequest> ReadCollectionRequest(const std::filesystem::path&, FailureReport&)`。
- `Result<std::vector<CompileUnit>> LoadCompileUnits(const CollectionRequest&, FailureReport&)`。
- `Result<ProcessResult> RunProcess(const ProcessRequest&, FailureReport&)`。
- `Result<CompileObservation> InspectCompileInputs(const CollectionRequest&, std::span<const CompileUnit>, FailureReport&)`。
- `Result<CollectedProfile> CollectPlatformProfile(const CollectionRequest&, std::span<const CompileUnit>, const CompileObservation&, FailureReport&)`。
- `Result<GuardDisposition> GuardCollection(const CollectionRequest&, FailureReport&)`，状态为 `kCurrent` 或 `kBuildRequired`；无法可靠重建返回失败。
- `Status InvalidateCollection(const CollectionRequest&, FailureReport&)`、`Status Collect(const CollectionRequest&, FailureReport&)`。
- `Status PublishCollection(const CollectionRequest&, const CollectedProfile&, const InputSnapshot&, FailureReport&)`。
- `int RunCompilerInvocation(const CollectionRequest&, std::span<const std::string>, FailureReport&)`：真实编译参数直传；成功后才提交对象内容与编译上下文记录。

类型归属：任务 1 定义请求及平台/quoting 枚举；任务 2 定义进程类型；任务 3 定义 CompileUnit；任务 4 定义观察/产品/快照/Guard 类型与流程头，任务 9 定义图类型。缺失的后端只能明确返回 unsupported，不能提交成功结果；最终不得保留验收平台的 unsupported 分支。

CompileDigest 只描述影响对象生成的实际 argv、源/Header、编译器、SDK/运行库政策及相关环境，不包含自身对象、库、收据、派生签名头或 Profile 输出，防止摘要自引用。采集器/Schema 版本及依赖完成状态另外影响验证缓存；工具更新不等于自动改变目标 ABI，旧收据格式无法核验时才要求重新编译。

新增 CLI 为 `guard --request <v2.json>`、`compile --request <v2.json> -- <compiler argv...>`、`collect --request <v2.json>`、`invalidate --request <v2.json>`。这些私有构建命令与原四参数编码模式分派；手工编码不发布完成记录。

输出按 `Abi/<Config>/<Target>/` 隔离；稳定文件包括 profile.json、canonical.json、fingerprint.sha256、evidence.json、complete.json。最后发布 complete.json，关联库 SHA-256、canonical SHA-256、Fingerprint、输入快照和工具身份；读者核验全部引用，不能只看标记存在。

## 任务依赖

1 → 2/3 → 4 → 5 → 6/7/8；9 消费 1/3/4/5；10 消费已能独立运行的 构建工具；11 消费 5—10；12 验收全部。
后端可以交给独立实现者，但共享 Dispatcher/CMake 修改按任务交接，不能同时编辑同一文件。

### Task 1：私有协议与编码边界

**Files:** 新建 `Private/CollectionRequest.h/.cpp`、`Private/Encoding.h/.cpp`；修改 `Main.cpp`、工具和测试 CMakeLists；新增 `Tests/AbiProfile/CollectionRequestTests.cpp`。
**Interfaces:** 产出上述 CollectionRequest 与 `ReadCollectionRequest`；`int EncodeProfileFiles(const std::filesystem::path& schemaPath, const std::filesystem::path& profilePath, const std::filesystem::path& canonicalOutput, const std::filesystem::path& fingerprintOutput, FailureReport& report)` 保持原文件语义与退出分类。

- [x] **RED**：注册 `AbiCollectionRequest`。断言请求 schema=2 才接受；缺失 Compiler/ObjectManifest/Library/OutputDirectory、重复键、错误字段类型、输出别名覆盖输入均失败且不修改输入。原四参数 CLI、冻结向量仍作为兼容测试。
- [x] 运行 `cmake --build --preset windows --target PanguCollectionRequestTests`、`ctest --preset windows -R 'AbiCollectionRequest|AbiProfileVectors|AbiProfileCli|AbiProfileIo'`；先确认新校验失败，不能用构建错误冒充行为 RED。
- [x] **GREEN**：实现验证与类型化；提取原编码函数，Schema 固定摘要与 ParseUnique/有界报告/close 后提交行为不变。CMake 将 Source/Build、对象和依赖等事实传给请求；不在 JSON 中人工填写 Profile 叶子字段。
- [x] 冷构建请求中的 Library/Objects 尚不存在是合法状态；Reader 验证声明与输出边界，不误要求这些产物提前存在。实际 collect 阶段才要求对应文件及成功编译记录。
- [x] 相同命令 PASS；执行 Windows 完整 CTest，记录原 CLI 兼容结果。

### Task 2：显式进程执行与实际编译记录入口

**Files:** 新建 Process 三个文件、`Tests/AbiProfile/ProcessTests.cpp`、`ProcessFixture.cpp`；修改 Main/Collection 的 compile 分派和 CMake。
**Interfaces:** 消费 ProcessRequest；产出 RunProcess。Compiler wrapper 消费真实 argv，输出对象编译记录，格式在任务 4 完成。

- [x] **RED**：注册 `AbiCollectionProcess`，Host helper 返回 17 时断言 ExitCode=17；包含空格、引号、反斜杠、Unicode、`$()`/分号的参数逐字返回；stdout/stderr 同时各输出 1 MiB 无死锁；捕获超限、超时、启动失败和取消返回失败，子进程结束。
- [x] 在当前 Host 运行该 CTest，确认未实现的执行/引用/清理行为失败。
- [x] **GREEN**：Windows 使用宽字符原生进程 API，POSIX 使用 argv 执行；RAII 关闭资源并回收进程；不经 cmd/sh。捕获模式默认最多 16 MiB/流、探针超时 120 秒，允许私有请求调整；真实编译转发输出，不设这个短超时。
- [x] `compile` 保留编译器失败的非零结果和诊断，不发布成功对象记录；实际选定编译器和 argv 必须匹配请求。已有无法核验的 compiler launcher 不得被静默覆盖，先明确拒绝。
- [x] Windows 与 WSL/macOS 执行同名测试及现有完整 CTest；不是运行 Android/iOS helper。

### Task 3：精确 TU 与命令处理

**Files:** 新建 CompileCommands 两文件、`Tests/AbiProfile/CompileCommandsTests.cpp`；修改 WindowsProfile 的参数分类；复用现有 WindowsProfileTests。
**Interfaces:** `LoadCompileUnits` 接收请求；匹配 ObjectManifest 的每个 output，展开 argv；响应文件路径进入快照。

- [x] **RED**：注册 `AbiCollectionCompileCommands`。两个 Target 共用同一 Source 时，只返回选定两个 Objects；缺失/重复 output、空对象集、响应文件缺失/循环/超限拒绝。Windows/Posix quoting 各用字面期望 argv，嵌套响应文件按原编译工作目录解析。
- [x] 加入 `-DJSON_USE_GLOBAL_UDLS=0`、`-DPANGU_ENABLE_PROFILING=1` 的成功用例，并断言它们保留在证据；`-D_MSVC_STL_VERSION=145`、不支持的 ABI 改写参数拒绝；源码内 `_HAS_EXCEPTIONS=0` 继续拒绝。
- [x] **GREEN**：Host 决定 quoting，目标 ABI 决定后端解释；支持 command/arguments，限制响应文件 8 层和累计 1 MiB；规范化对象路径但不把 POSIX 路径一律转小写。保留原 argv 与实际响应文件身份。
- [x] 收据核对区分原始 argv 和解释后的 ABI argv；CMake 额外传入的依赖文件/输出控制参数不导致假冲突，但只能排除已理解的非 ABI 参数，不能抹掉未知选项。
- [x] 测试通过后跑完整 CTest；记录普通宏成功、ABI 控制项仍拒绝，避免以“接受所有 -D”替代分类。

### Task 4：Guard、对象来源记录与完成事务

**Files:** 新建 CollectionState 两文件、Collection 两文件、`Tests/AbiProfile/CollectionStateTests.cpp`；扩展 ToolReport/ToolIo；实现 compile/guard/invalidate/collect CLI 骨架。
**Interfaces:** 消费请求、CompileUnit、Process；产出 InputSnapshot、GuardDisposition、对象记录和 complete.json。

- [x] **RED**：注册 `AbiCollectionState`。成功记录关联库/Canonical/输入摘要；同路径同大小甚至保留 mtime 的库内容替换、Profile 替换、对象替换均拒绝缓存；删除对象编译记录后不能发布新 Profile。
- [x] 断言无变化 Guard 不调用编译器/链接探针；已配置相关环境变化后失效，要求重新配置或验证；无关环境变量变化不改变 ABI Fingerprint。首次、普通输入变化、缺失证明三种状态分别可观察。
- [x] 失败注入覆盖 write/flush/close、最终完成记录发布、清理权限、并发两写者；任一失败都没有有效完成记录。普通 encode 成功也不能生成 complete.json。
- [x] **GREEN**：真实 compile wrapper 成功后记录 object SHA、实际 argv/编译器身份、当前编译上下文与输入快照；失败/取消不写成功记录。源码/已知 Header 在执行期间变化则失败，不把可疑对象重新标记为当前。
- [x] Guard 区分 Own CompileDigest 与 DependencyDigest；仅依赖实现变化但 ABI 相容时可复用自身编译证据并刷新依赖完成状态。ABI 输入变化需触发新编译上下文；无可靠生产记录时要求重建，不能只刷新摘要。
- [x] 为任务 11 提供每 Target 的 comment-only `inputs.h`：内容仅为编译上下文/构建代际标识，不定义宏、包含 SDK 或改变类型。仅在需要实际重建时更新；不递归计入自身快照。
- [x] SHA 与记录路径使用现有工具能力，JSON/证据为按值数据；完成记录最后提交，在受锁保护的私有临时区处理。运行同名 CTest 及完整 Windows CTest。

### Task 5：迁移 Windows 到 C++ 并接入统一 CLI

**Files:** 新建 WindowsCollector 两文件；修改 Collection、WindowsProfile、Main；整理 Probe/Windows.cpp，新增 `Probe/RuntimeWitness.cpp`；新增 `Tests/AbiProfile/WindowsCollectorTests.cpp`。
**Interfaces:** 实现 Windows 的 InspectCompileInputs/CollectPlatformProfile，返回冻结 Profile 与独立证据；不在后端自己递归整个依赖图。

- [x] **RED**：注册 `AbiCollectorWindows`。将原 BASELINE/RTTI/UNKNOWN/MISSING/DUPLICATE/RESPONSE/STL_CONFIG 预期迁到 C++ 入口；真实 fixture 编译通过 compile wrapper，确认第二 TU 与同源其他 Target 不混淆。
- [x] 增加合法 Host 未解析符号用例：归档与采集成功，最终 consumer 提供符号后链接成功；不执行 EXE。缺失/冲突 SDK Header、Toolset/Platform.xml、COFF CRT 引用或实际运行库拒绝。
- [x] **GREEN**：迁移原 CollectWindows/WindowsRuntime 的逻辑并复用纯映射；最小运行库 witness 单独构建，不加入所有 Module 对象。真实对象的引用/指令与已确认运行库政策交叉检查；不把 SDK 标签当真实性证明。
- [x] 保留 `_HAS_EXCEPTIONS=1`、`_HAS_STATIC_RTTI=1`、`_CRT_PACKING=8` 的已支持范围和 CodeView 非 ABI 标志。API/ODR/最终消费者未验证的边界保持明确。
- [x] 通过统一 collect CLI 输出完整结果和完成记录；原四参数编码仍兼容。运行新回归、完整 Windows CTest，以及独立 Release MD/Debug MD/Release MT 三配置，断言两 Module 同配置一致、不同 CRT 配置摘要不同。

### Task 6：Linux 后端

**Files:** 新建 LinuxCollector 两文件、`Probe/Libcxx.cpp`、`Tests/AbiProfile/LinuxCollectorTests.cpp`；修改分派/CMake。
**Interfaces:** 消费公共 CompileObservation/Process/State；生成 Linux v1 字段。

- [x] **RED**：注册 `AbiCollectorLinux`。字面向量覆盖 libc++ namespace/version/布局配置差异、GLIBC 与目标 Sysroot 元数据不一致、未知 Sysroot 拒绝；不能从 Host uname 或普通目录名填 sdk_abi_id。
- [x] **GREEN**：在 WSL Ubuntu 的实际 LLVM23/libc++ 环境采集，核验前端、Header 和实际 ELF Runtime/provider；读取所选 Sysroot 的发行身份。当前验收为 Ubuntu/glibc，musl 未有真实环境不得声称已验收。
- [x] 独立最小运行库 witness 不执行；保留所有实际对象的 ABI 事实检查，不能以移除对象为由忽略它们。参数中遇到无法解释的 ABI 模式拒绝。
- [x] 在 WSL 运行后端及完整 CTest，记录真实 Profile 和 Schema/Canonical/Fingerprint 验证；其他 Host 的纯映射用例不能替代这一步。

### Task 7：Apple 后端（macOS/iOS）

**Files:** 新建 AppleCollector 两文件、`Tests/AbiProfile/AppleCollectorTests.cpp`；复用 Libcxx/RuntimeWitness probe，修改分派/CMake。
**Interfaces:** 一个 Apple 后端按 TargetPlatform 分派 macOS/iOS，Host 均为 macOS。

- [x] **RED**：注册 `AbiCollectorApple`。对换 macOS/iPhoneOS SDK、缺失 SDKSettings、部署 Target/前端 Triple 不符、STL Header 与 Runtime 来源不符、libc++ ABI 配置变异均拒绝。
- [x] **GREEN**：从真实编译命令及 SDK 元数据取值，保留 MacPorts 来源核验；不能把编译器版本当 SDK 或 libc++ 版本。区分系统运行库的声明与编译器安装目录。
- [x] 通过 `ssh -o BatchMode=yes moozen-macos`，在共享挂载源码上运行 macOS 后端及完整 CTest，并做 iOS ARM64 实际编译/采集；不复制源码、不执行 iOS witness、不要求安装 Xcode 替代现有独立 SDK。
- [x] macOS 与 iOS 产物通过冻结 Schema，目标平台/SDK 身份不同；库未解析 Host 符号仍允许，普通 SDK/ABI 未知仍失败。

### Task 8：Android 后端

**Files:** 新建 AndroidCollector 两文件、`Tests/AbiProfile/AndroidCollectorTests.cpp`；复用 libc++ probe，修改分派/CMake。
**Interfaces:** 支持 Windows Host 解析 Windows quoting、运行 NDK Clang，Target 为 Android ARM64。

- [x] **RED**：注册 `AbiCollectorAndroid`。NDK Clang 被桌面 Clang 替换、Pkg.ReleaseName/Revision 与实际库来源不符、API 宏别名解析不符、错误 namespace/ABI 版本、SDK/NDK 混用均拒绝。
- [x] **GREEN**：核验 NDK `source.properties`/实际 Header/ELF Runtime，按 ReleaseName 映射冻结 sdk_abi_id，完整 Revision 留在证据；不从数字主版本猜 r30，不要求 Android Clang23。
- [x] Windows native 工具处理真实 NDK21/ARM64/API29 编译与最小链接探针，原始 Target 仍用 NDK libc++/bionic；目标程序从不运行。
- [x] Schema 验证、真实 Profile 和拒绝回归通过；Android 其他 ABI 不因 Schema 支持自动算作验收。

### Task 9：真实链接闭包与技术归档 SCC

**Files:** 新建 DependencyGraph 两文件、`Tests/AbiProfile/DependencyGraphTests.cpp`、`Tests/AbiProfile/Fixtures/CMakeLists.txt` 和最小 Sources；新增 `cmake/AbiProfile/AbiProfile.cmake` 的元数据导出部分，暂不切换根接线。
**Interfaces:** `Result<DependencyGraph> ReadDependencyGraph(const CollectionRequest&, FailureReport&)`、`Result<DependencyPlan> PlanDependencies(const DependencyGraph&, std::string_view rootTarget, FailureReport&)`；DependencyPlan 保留选择后的直接关系、遍历目标、技术 SCC、原始链接项次序。

- [x] **RED**：注册 `AbiCollectionDependencyGraph`。覆盖 Alias、PRIVATE/PUBLIC/INTERFACE、`LINK_ONLY`、当前配置条件、菱形链、技术 A↔B 环、包含 managed Module 的环、普通 IMPORTED 即使附 Profile/SHA 也拒绝；构建工具执行依赖不误作 Target C++ 链接依赖。
- [x] CMake 4.0 兼容导出夹具验证真实链接关系。File API 的 dependencies 是 build graph，不能用它恢复全部 link graph；旧版本也不完整列出 INTERFACE/IMPORTED。CMake 导出原始/按配置求值的链接项、Target 类别、Alias/作用域及生成源事实，C++ 做分类/遍历/检查。
- [x] 不依赖 CMake4.1 起 LINK_LIBRARIES 自动传递的新行为；支持的表达式交由 CMake 求值，不能由 C++ 猜测。无法导出的表达式/外部裸库明确拒绝，不忽略后继续成功。
- [x] **GREEN**：技术 SCC 只分组采集，不为各技术库添加互等 Profile Targets；严格检查所有可达 C++ STATIC，平台 CRT/SDK 后端单列，未知 C/SHARED/外部 C++ 边界拒绝。
- [x] Runtime witness 不强拼所有归档或改变真实链接顺序；数据中仍保存必要重复项。使用各目标已验证 Profile 逐字段报告差异，不能只报两个 digest 不同。
- [x] 图单测及真实 CMake 夹具通过，明确证明相反配置下条件依赖不会被错误合并成环。

### Task 10：Build tool bootstrap 与并发只读发布

**Files:** 新建 `BuildTools/AbiProfile/Bootstrap/CMakeLists.txt`、`cmake/BuildTools.cmake`、`Tests/AbiProfile/BuildToolsTests.cpp`；按需要调整工具 CMake 以复用 Source 列表。
**Interfaces:** `pangu_prepare_build_tools(outExecutable)` 返回已完成且不可变的 native 工具路径；Host 阶段无 Target Profile 完成规则。

- [x] **RED**：注册 `AbiCollectionBuildTools`。从无工具目录开始准备；Root Target Debug/MT、Android NDK 或 iOS Sysroot 不泄漏到 Host Release；两个配置进程并行准备同代只构建一次；旧工具正在使用时新版本发布不覆盖它。
- [x] **GREEN**：使用同份 Source 和独立 CMake 项目；Windows 工具根目录为 `${sourceDir}/Build/BuildTools/Windows/`，Linux/macOS 使用宿主本地 `$HOME/PanguEngineBuild/BuildTools/<HostPlatform>/`。允许在根配置阶段完成增量构建工具准备并返回固定路径；为源/头文件/配置/宿主工具链登记正确的重新配置依赖。这属于构建自举，不在 CMake 实现 ABI 判断。
- [x] 锁保护构建区；验证成功后按身份/内容发布独立工具版本，之后只读。可以复制生成的工具二进制，不能复制引擎源码。构建工具由 Host Foundation 支持，不链接 Target Foundation。
- [x] Root native 的既有编码工具/辅助实现库可继续服务四参数 CLI 与 native 单测；正式采集统一使用发布的 构建工具，不因此形成 Root Foundation→Root Tool→Root Foundation 环。
- [x] Windows 验证 Windows/Android 并行场景；macOS 验证 macOS/iOS；缺少 Host 环境、构建失败、发布失败均明确诊断，没有可误用的新 ready 版本。

### Task 11：普通 Module Target 强保证与删除旧脚本

**Files:** 完成 `cmake/AbiProfile/AbiProfile.cmake`；修改根 CMake、`cmake/Platform/Common/Module.cmake`、工具/测试 CMake；重写 WindowsCollectionTests 为统一集成测试；新增 `Tests/AbiProfile/BuildContractTests.cpp`；删除原 `cmake/AbiProfile/{Windows,CollectWindows,WindowsRuntime,Prepare}.cmake`。
**Interfaces:** `pangu_register_abi_module(target)`、`pangu_finalize_abi_profiles()`；导出 schema2 请求。普通 Targets 保持真实 Library 类型和名字，不创建第二个 Module Library。

- [x] **RED**：注册 `AbiCollectionBuildContract`。只执行 `cmake --build <fixture> --target FixtureModule`；断言库、完成记录及所引用 Profile 同代有效。复现基线单 Target 只失效不采集的问题，再观察 GREEN。
- [x] 增加真实 DAG `FixtureModule→BaseModule`：Base 改动后，Fixture 未重新归档也必须检查新依赖状态；不相容时失败，不能因 Fixture 的 POST_BUILD 没运行而漏检。
- [x] **GREEN**：构建工具先就绪；每 Module 的轻量 Guard 在必要代码生成与选定依赖完成后运行，再允许本目标编译。依赖顺序使用 CMake 当前配置实际选择的 Target；不对可变 JSON 做 CMake ABI 判定。
- [x] Guard 对选定依赖的完成有真实的构建顺序保证，不能先读取旧的依赖记录再让依赖重建；条件依赖只选当前配置，不能把 Debug/Release 的边合并成伪环。技术库无独立互等 Guard，由消费 Module 分组验证。
- [x] 将 per-Target `inputs.h` 作为 PRIVATE 强制包含的纯注释依赖，不用全局 Source OBJECT_DEPENDS 造成同源多 Target 串扰；实际 compiler launcher 提交对象来源记录。输入变更时更新该依赖以触发重编译，collect 核对来源记录与实际对象；无法可靠重建则失败并提示 clean/reconfigure。
- [x] 每次真正编译须绑定当前编译代际，即使新旧 object 字节相同也以本次成功收据确认来源；不可由 object mtime 或 digest 变化单独推断“编译过”。异常替换/缺失收据可要求干净重建，不能暗中在采集步骤重编译或修补归档来绕过 CMake。
- [x] 冷构建、生成源/Header、无变化构建、编译器/SDK/相关环境变化、库/对象被替换、普通宏、技术归档环、Module 环、Imported 拒绝均执行。未变化只 Guard、不编译/归档/重跑探针；兼容依赖实现变化可重验闭包并复用自身证据。
- [x] 真实编译错误、对象记录写失败、采集失败、取消、清理失败时无有效 complete.json；修复后重建可恢复。同源不同 Target 的签名头、记录和错误互不污染。
- [x] 全部新回归通过后切换根入口，删除旧四脚本及其引用；原 `--collect-windows` 诊断入口迁移掉，保留四参数编码兼容。测试 harness 可调用 CMake，但不再实现或调用另一份采集脚本。
- [x] 在 Windows/macOS/Linux 运行单 Target/Tests OFF 和完整 CTest；Android/iOS 做正常 Root Preset 单 Module 构建，确认 Host 执行与 Target 产物角色分开。

### Task 12：五平台共同验收、规范同步与收尾

**Files:** 工具 README、wiki/foundation 的 ABI/构建实现状态、wiki 索引；`Tests/AbiProfile/` 的平台矩阵入口；本计划进展记录。
**Interfaces:** 统一 CLI/请求/完成记录和普通 Module Target 为正式外部验收面；无新的 Loader 承诺。

- [x] 五平台干净目录（或 fresh 配置）、普通单 Module、依赖 Module、`BUILD_TESTING=OFF`、正常增量构建验收；正式输出使用各平台 Build 树，临时变异在 `.probe/`。
- [x] Windows 保留 Release MD/Debug MD/Release MT；三桌面运行完整 CTest，不能只挑新测试。Android/iOS 运行宿主采集、验证目标对象/归档，绝不执行目标 witness。Mac 只使用共享挂载源码。
- [x] 使用现有 CMake 4.x 工具链验证（Windows/macOS 4.4.3、Linux 4.4.2），核对 `cmake_minimum_required(VERSION 4.0)` 与所用属性的最低支持版本；`TRANSITIVE_LINK_PROPERTIES` 自 3.30 起支持，不隐式提高 4.0 最低版本。不额外下载或安装恰好 4.0.x 的二进制，也不声称已在 4.0.x 上实测。
- [x] 对本轮 C++ 使用仓库 clang-format/clang-tidy，优先实际编译数据库；非独立 Probe 记录最小上下文检查限制；必要时选择支持的 ASan/UBSan 做本工具内存/解析检查，不扩展为全引擎验收。
- [x] 同路径内容替换、环境漂移、并发 Host 发布、最终 Host 未解析符号和技术环的结果逐项记录；说明 Profile 完整与 Loader 未完成的界限。
- [x] 旧 `cmake/AbiProfile/`、Windows 专用根入口和执行脚本引用均不存在；工具/普通库 Target 命名遵守既有边界，正式测试数量以本轮实际输出报告。
- [x] 同步稳定结论到 wiki，将已实施的 STATIC ABI Collection 与未实现的最终 Loader 范围分开；`git diff --check` 与 `git diff --cached --check` 均通过。复审补齐 构建工具 Probe 输入 SHA 跟踪，三宿主 BuildTools CTest 与五平台 fresh Module 构建已验证。没有新授权不提交或推送。
- [x] 累积变更集的**独立 Reviewer** 完成 Spec 与 Code Quality 双轴审查后交付最终 Approval；2026-10-26 R1—10 修复及独立复审通过，2026-10-27 追加修复及独立复审通过，最近一次完整三桌面 CTest 为 **104/104**，移动端/CRT 矩阵通过，Task 12 完成。最终验收汇总见本计划最新执行进度。

## 自审与交接

- 设计 §1—2 范围/版本：Global Constraints、任务 6—8/12。
- §3 强保证：任务 4/10/11，特别覆盖依赖更新而本库无需归档的场景。
- §4 C++ 职责与迁移：任务 1—8/11；旧脚本最终删除。
- §5 Host 与 Q7：任务 10/12。
- §6—7 全 TU、Q1/Q5 与平台：任务 3/5—8/11。
- §8 Q3/Q6/Q9：任务 5/9/11。
- §9 Q2/Q4/Q8：任务 4/11/12。
- 原 CLI/Schema/错误语义：任务 1/4/5/12。

自审必须逐项对照 Spec、接口名与任务顺序；发现平台事实与方案不符时记录最小修订与证据，不用 guessed/default 值掩盖。执行方法与本计划经用户确认后再实施，先读取对应执行技能。

### 2026-10-24 计划自审记录

- 范围：Q1—Q9、五平台矩阵、强保证与不实施 Loader 的边界均有任务/实际测试对应。
- 接口：共同类型归属和函数签名已统一；输出 schema2 为私有请求，冻结 Profile 始终 schema1。
- 次序：进程/argv/来源记录先于平台采集；Host 与闭包完成后才切换根构建；旧脚本最后一次性撤销。
- 平台事实：已对照安装版 CMake 文档，明确 File API build dependencies 不等于 link graph，4.1 起的自动传递行为不能被当作4.0前提。
- 特别补齐：冷构建产物尚未存在、同字节对象仍需真实新编译收据、依赖更新而消费者不归档、签名头摘要不自引用。
- 这是计划自审，不是代码/平台验收；实施时仍须逐步观察 RED→GREEN 和实际命令输出。未运行实现测试，未修改正式代码，未提交。
