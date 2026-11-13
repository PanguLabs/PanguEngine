# Foundation 失败现场与 Log Module Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 交付可卸载后独立读取的失败现场、Foundation 必要检查/应急终止，以及基于 fmt/spdlog、Host 独立拥有的有界异步 Log Module。

**Architecture:** 保留 ModuleRuntime → Foundation，增加 ModuleRuntime → Log → Foundation。Log 自管队列、失效安全 Writer、文件分区和排空协议，消费者调用私有 spdlog 同步后端。报告和 Fatal 不依赖 Log；Host 在业务清理后完成日志排空，再发布 Closed。

**Tech Stack:** C++23、项目五平台 Clang/标准库、RTTI ON、C++ Exception OFF、CMake 4.0+、Ninja、CTest/GoogleTest；fmt 12.2.1、spdlog v1.17.0，使用同一外部 fmt。

**Spec:** [已批准设计](../specs/2026-11-12-foundation-diagnostics-design.md)。输入为 `a9069a7` 的行为规格及用户设计批准；代码基线 `78c8306`。必须同时读 AGENTS.md、Tests/README.md、wiki/foundation/cpp-guidelines.md。实现与实测范围见文末归档和正式验收，具体 Public C++ API、布局及跨平台 ABI 仍未冻结。

**实施状态（2026-11-13）：** Task1–12 的实施、任务评审与平台验收已完成。全分支评审的两项 Important 已由 `5e23385`、`aa827b2` 修复，唯一范围复审通过，新增 Critical/Important/Minor 为 0/0/0。原 Full 与本轮必要定向按真实版本分别归档；最终配置和无写者已核，必要结论在 bad1dc3 归档并核对无运行依赖后，临时工作区已精确清理。详见[最终评审与执行裁定](2026-11-12-foundation-diagnostics-review.md)。

## Global Constraints

- “B：Foundation + Log”；源码 `Source/Runtime/Log/`，Target `PanguLog` / `Pangu::Log`，命名空间 `pg::log`。一个 Module 一个真实 STATIC/SHARED 库，产物名不含平台名。
- “fmt + spdlog”；“保持禁用 C++ Exception、初始化可恢复 OOM”；不得开启异常、替换生产全局分配器或吞掉错误绕过契约。第三方是普通 Target，不取得 ModuleId。
- “首版公开模式仅异步，Log 消费者调用 spdlog 同步后端”；不使用全局当前 Host、默认 logger、全局 registry 路由和默认异步线程池。
- “固定内存预算、高等级预留、拒收和损失可见”；满队列或竞争可以拒收任意等级，不驱逐已接受记录。
- “实例独占文件分区，预算覆盖自身管理的当前与历史日志”；轮转保留完整记录，只回收最旧已关闭受管理文件，保留活跃/外部文件。
- “失败输出不自动重开、恢复或热重配”；运行故障主动查询，Log 不回调业务或反向关闭 Host；RequiredOutputs 只约束启动。
- “LogWriter”安全失效；残留句柄不阻止关闭，在途提交保护实例；报告/队列不借用 Plugin 文本、格式化器或析构代码。
- Flush 固定累计前缀、一个未完成管理请求，Stop 保留旧 Flush 并交付两份结果；超时保留资源，未完即销毁才 Fatal。“安全关闭与输出完整性分别表达”。
- Assert 显式开关默认 ON，与 Release/NDEBUG 解耦；Check/Fatal 全配置保留。应急不依赖 fmt/spdlog，外部文本可缺失，不能无限等待。
- 新/改 CMakeLists.txt 使用 `cmake_minimum_required(VERSION 4.0)`，按现有上下文校验 Clang；自有代码遵守 format/tidy，提交/注释/文档中文，不附工具署名。
- Windows/Android BuildRoot 为 `Build/Windows` / `Build/Android`；WSL 源码 `/home/moozen/WindowsGit/PanguEngine`、构建 `$HOME/PanguEngineBuild/Linux`；Mac 源码 `/Users/moozen/WindowsGit/PanguEngine`、构建 `$HOME/PanguEngineBuild/macOS` / `iOS`，用 `ssh -o BatchMode=yes moozen-macos`，不复制引擎源码到临时目录。
- 桌面两模式实际运行，移动仅 Android/iOS ARM64 编译/最终链接和宿主检查，不运行交叉 GTest，不宣称真机日志/线程/Fatal 验收。
- 正式测试留 Tests；执行临时材料只放 `.superpowers/sdd/2026-11-12-foundation-diagnostics/`，验收/评审后归档必要结论再精确清理。相同已测 tree 整合按 AGENTS.md 复用全量。

## Review Focus

1. 已有默认 Host 测试遇到新增消费者，单次 TryFinish 不再必然完成：Task 10 将非日志测试显式 Disabled，真实日志用例保持异步并验证 Closing/重试。
2. 错误目录、分区被另一实例占用、RequiredOutputs 未包含于 Enabled：Task 6/7 验证不接管、不清理外部文件；配置矛盾显式失败，初始化残留仍有 Owner。
3. Flush 超时后持续生产并立即 Stop：Task 8 验证旧 H、最终 H、两个快照及后续错误不混入旧前缀。
4. 名字像日志的外部文件、上次进程异常退出、轮转 Close/删除失败：Task 6 验证归属凭据、锁自动释放、保留外部文件和预算失败降级。
5. Plugin 消息/位置已入队，Writer 仍被外部保存，Host 和 Plugin 随后结束：Task 11 在物理卸载标记后消费并读取；残留 Writer 不阻止关闭、不解引用旧实例。

---

## 文件职责与执行顺序

本计划交付一条失败现场、普通日志和 Host 关闭的闭环，按独立可验收成果分成 12 个任务。Task 1 是第三方可行性门：失败时保留证据，按规格回到适配设计，不能跳过后声称整体兼容。Task 2/3 的 Foundation 能力不依赖 Log 初始化。其余按接口顺序推进，不整体重写现有 Manager/Runtime。文件表/Task 的 `Foundation/`、`Log/`、`Module/`、`EngineHost/` 均指 `Source/Runtime/` 下对应目录，其余从仓库根起算；简写 Private/Public 跟随同组 Runtime Module。

| 文件组 | 职责 |
| --- | --- |
| `Source/ThirdParty/{Fmt,Spdlog}/{CMakeLists.txt,README.md,Repo/}`、`.gitmodules` | 固定源码、目标配置、版本/许可证/已审计路径 |
| `Foundation/Public/{FailureReport,FailureContext,Check}.h`、`Private/{FailureReport,Fatal}.cpp`、`Private/Platform/{Windows,Posix}/Emergency.cpp` | 无日志依赖的报告、必要检查和应急终止 |
| `Log/Public/{LogModule,LogTypes,LogConfig,LogRecord,LogWriter,LogFormat,LogSystem}.h` | 身份、配置、固定值、失效安全前端和生命周期接口 |
| `Log/Private/{SpdlogBackend,WriterControl,LogQueue,LogSystem,LogDrain}.{h,cpp}` | 第三方接缝、接纳/存活门、FIFO、消费者和累计快照 |
| `Log/Private/{LogMemory,NativeLogIo,NativeLogThread,LogFileStore}.{h,cpp}` 及 `Platform/{Windows,Posix}/` | Try 分配、原生资源、专属文件分区、轮转和故障传播 |
| `Module/Public/{RegistrationContext,ScopeBuildContext,ModuleOperation,ModuleManager}.h` 及对应 Private 文件 | Writer/Context 接线、回调迁移、等待快照和日志基础依赖 |
| `EngineHost/Public/{HostStartupConfig,HostOperation,EngineHost}.h`、`Private/{EngineHost,EngineHostState}` | 初始化、历史首错、最终关闭和只读日志快照 |
| `Tests/{Foundation,Log,Module,EngineHost}`、`Samples/MinimalHost/` | 正式单元、子进程、竞争/卸载/应用验证 |

### A. 固定来源、初始适配路径与身份

- fmt 12.2.1 固定 `208219daff58f2a7438f6931cc2ddc358acf33ef`，spdlog v1.17.0 固定 `79524ddd08a4ec981b7fea76afd08ee05f83755d`；Task 1 以实际 gitlink 再核对 tag 对应关系，不用浮动分支。依据：[fmt 发布](https://github.com/fmtlib/fmt/releases/tag/12.2.1)、[spdlog 发布](https://github.com/gabime/spdlog/releases/tag/v1.17.0)。
- `ThirdParty::Fmt` 指向编译型静态 fmt，`FMT_EXCEPTIONS=0`；`ThirdParty::Spdlog` 使用编译型静态库、`SPDLOG_FMT_EXTERNAL`、`SPDLOG_NO_EXCEPTIONS`、`SPDLOG_NO_THREAD_ID`、`SPDLOG_DISABLE_DEFAULT_LOGGER`，保持 RTTI，开启 PIC。关闭上游 tests/examples/install、禁止捆绑 fmt 或全局改 BUILD_SHARED_LIBS；选项在适配子目录作用域设置。先定义本树 `fmt::fmt`，阻止 spdlog 回落到系统 find_package；两库 DEBUG_POSTFIX 清空，编译选项不继承 PanguBuildOptions 警告策略。
- **先走零补丁扩展路径。** 私有 `BackendLogger final : spdlog::logger` 用空名字、空 Sink vector 构造，覆盖受保护 `sink_it_`/`flush_`，持有两个内联 RawSink，调用非格式化的 `log(time_point{}, source_loc{}, level, string_view)`；不设置 pattern、不启用 backtrace、不 clone、不注册 logger。RawSink 直接实现 `spdlog::sinks::sink`，不使用会创建 formatter 的 base_sink；固定为已编码文本，封闭的配置函数若被误调用则记录 InvalidConfig 并拒绝该路，不进入库 throw/格式化路径。
- 上游 [logger 扩展点](https://github.com/gabime/spdlog/blob/v1.17.0/include/spdlog/logger.h)、[默认禁用 backtrace](https://github.com/gabime/spdlog/blob/v1.17.0/include/spdlog/details/backtracer.h)、[空 circular_q](https://github.com/gabime/spdlog/blob/v1.17.0/include/spdlog/details/circular_q.h) 支持此源码推断，**不等于无分配已实测**。Task 1 在所选标准库下验证构造/提交/刷新/析构，任何隐藏分配或库 abort 都阻止进入 Task 4。
- 无补丁则不创建空 Patch。确需小补丁时固定文件与目的、按 ThirdParty 规范在 BuildRoot 准备补丁副本并测试匹配失败/重配；大幅改库或契约回设计，不在执行时静默换版本/开启异常。
- Log ModuleId=`b6ddc3fdc5b34a2e908c566d90db1e21`，版本 1.0.0、协议 1、Required Foundation 1.0.0，Query=`pg::log::QueryLogModule`，常量=`kLogModuleId`。计划前已查无此身份命中，实施时正式重复核查。
- Log Error Domain=`0x70676c6f67000001ULL`，代码显式编号：InvalidConfig=1、OutOfMemory=2、NoOutputs=3、FileOpenFailed=4、IoFailed=5、PartitionBusy=6、BudgetExceeded=7、ThreadStartFailed=8、Busy=9、InvalidState=10、SequenceExhausted=11。命名采用 `kCamelCase`，不把 ModuleId 当错误域。

### B. 全任务共用的接口与预算

- Foundation：`pg::FailureSite` 为独立拥有型主现场；`FailureReport` 为固定字节/偏移数据，`PrimarySite() const noexcept -> std::optional<FailureSite>`、`Truncated() const noexcept -> bool`、`Clear() noexcept`。`FailureContext(FailureReport&) noexcept` 借用采集，提供 `SetPrimary(Error, std::string_view, std::source_location=std::source_location::current()) noexcept`、`AddContext(std::string_view key, std::string_view value) noexcept`、`AddSecondary(Error, std::string_view, std::source_location=std::source_location::current()) noexcept`，三个采集方法返回 void，全部不分配。
- 报告以 offset/length 引用自身内联字节，不存自身地址。主现场保留 512 B，其余条目上限 8、文本总候选 1024 B；`sizeof(FailureSite)<=512`、`FailureReport<=2048`、`ModuleOperationReport<=4096`、`HostOperationReport<=16384`。每次操作的报告及必要副本工作预算 64 KiB，Task 2/9/10 列实际对象峰值，不能按每层叠加额度。16 KiB Host 上限考虑同时保存当前/历史 Module 报告和 Log 结果，是规格授权下的计划预算，未改长期 ABI。
- LogTypes：`Level` 为 `kTrace/kDebug/kInfo/kWarning/kError/kOff`；`OutputMask` 为 `kNone/kConsole/kFile` 位集合；`LogState` 为 `kCold/kRunning/kClosing/kClosed`；`SubmitResult` 为 `kAccepted/kFiltered/kDropped/kClosed/kDisabled`，不标 nodiscard。`LogTags` 只含 ModuleId 和固定 Scope/Declaration/Operation 值，缺失显式表示。
- `LogRecord` 固定容量，消息 1024 B、分类 64 B、文件/函数文本合计 512 B；`Failure` 为 optional<Error>，Tags、Truncated/构造失败标志为值。编码行缓冲 12 KiB，必需数值身份先于可截断文本写入；完整 UTF-8/转义边界输出。单队列默认 256、预留 32，最大 4096/512；配置验证预留小于总量及尺寸乘加不溢出。
- `NativePathView` 为 `std::basic_string_view<std::filesystem::path::value_type>`，仅初始化调用期间借用，内部有界复制。`LogConfig` 含 `Directory`（绝对专属分区）、`PartitionId`（非零 16 字节稳定值）、Enabled=Console|File、Required=None、Minimum=Info、QueueCapacity=256、PriorityReserve=32、MemoryBudgetBytes=1 MiB、WorkerStackBytes=256 KiB、PartitionBytes=64 MiB、FileBytes=4 MiB、DefaultWait=100 ms；路径上限 1024 native code units，超限显式失败。禁文件时不要求目录/Id；Required 必须为 Enabled 子集；文件阈值至少容纳头和一条最大编码记录，总预算至少一个文件，队列数据必须装入 MemoryBudgetBytes。
- `LogSnapshot` 含 State、HighestAccepted/HighestProcessed、Enabled/Ready/Failed masks、两路各自 Error/送达/未送达/刷新状态、按等级拒收/原因、截断/构造失败、计数溢出、回收文件/字节等；快照拥有数据。`LogInitReport` 另含初始化 disposition、实际拥有型路径及 CleanupPending/清理责任，不依赖 Writer 分配成功。
- `LogSystem` 默认无分配 Cold、不可复制/移动；`Initialize(const LogConfig&, LogInitReport&) noexcept -> Status` 只接受 Cold。成功发布 Running；Disabled 成功但不启动消费者；全部不可用或必需失败返回错误，安全清完则 Closed，清理未完则保留 Closing。这样失败不会因 Result 只返回 Error 而丢失部分资源 Owner。
- `LogSystem::GetState() const noexcept -> LogState`、`GetWriter(LogTags={}) const noexcept -> LogWriter`、`Inspect() const noexcept -> LogSnapshot`、`Flush(std::chrono::steady_clock::time_point deadline, FlushReport&) noexcept -> Status`、`Stop(std::chrono::steady_clock::time_point deadline, StopReport&) noexcept -> Status`；`LogWriter::WithTags(LogTags) const noexcept -> LogWriter` 不分配。完成成功仅表示请求处理/安全停止，完整性另看报告；未完 Log Busy、非法状态 InvalidState；Closed Stop 幂等返回最终结果。
- Cold 的 Writer 为 Disabled，Stop 无资源时成功转 Closed；Cold/Closed 析构不启动线程或输出。Initialize 只调用一次，Disabled/失败且清理完成时也保存对应零范围停止快照，初始化失败不能被 Stop 的安全成功抹掉。Flush 在 Cold/已关闭且无已有请求时 InvalidState；Running/Closing 析构只尝试 DefaultWait 的停止，未完成才 Fatal。
- `FlushReport` 含 H、Finished、OutputComplete、两路累计结果和拒收/截断/构造失败/溢出。过滤和正常历史回收不算损失；请求输出初始化失败、未送达、刷新失败、拒收、截断/构造失败或统计不可信均不能报 OutputComplete。`StopReport` 含可选 PreviousFlush 和 FinalFlush、资源清理状态；最多一份旧结果，不保存无限账本。
- 默认前端为 `PG_LOG(level, writer, category, formatLiteral, ...)` 及固定等级便捷宏：writer/level 各求值一次，过滤和提交保护成功后才求值格式参数；返回 SubmitResult。内部 `detail::FormatRecord(LogRecord&, fmt::format_string<Args...>, Args&&...) noexcept` 仅同步执行，不保存参数。
- 首版格式子集：自动 `{}` 与转义括号、bool/整数/默认浮点/有界文本/枚举及引擎身份的已知转换；不接裸未知长度指针、用户 formatter、locale、动态宽度/精度或运行期格式串。fmt 编译期检查语法/类型，Pangu 子集检查在调用 fmt 前完成，拒绝项生成标记最小记录；输入/输出长度均有上限。最终浮点/库分配证据属于 Task 4 的门，不泛化 format_to_n 的保证。
- private `OutputOutcome` 含 `std::uint64_t BytesWritten`、`std::optional<Error> Failure`、`bool Complete`；`NativeLogIo` 提供 Write/Flush/Close 和故障接缝，I/O 可阻塞但输入/资源有界，partial write 不计整条送达。`SpdlogBackend::Emit(Level,std::string_view) noexcept`/`Flush() noexcept` 返回 `std::array<OutputOutcome,2>`，保持串行；Sink 不递归日志。
- private `LogQueue::TryPush(LogRecord&&) noexcept -> SubmitResult`、`TryPop(LogRecord&) noexcept -> bool`、`Inspect() const noexcept -> QueueSnapshot`；记录的 Sequence 由 TryPush 成功接纳时写入，QueueSnapshot 提供 HighestAccepted 和按等级拒收原因。Writer 的 `detail::SubmitLazy` 仅同步调用受控构造函数，先持提交保护，再构造和 TryPush，出站必定释放保护；非模板控制块工厂与最后释放实现在 Log 库。

### C. 正式验证入口

配置先沿用本机已验证的 `PANGU_META_SDK_CONFIG`，不覆盖成空值；Root Query/Catalog 仍需正式宿主 SDK。每次定向测试先构建真实目标。Windows 共用形态：`cmake --build --preset windows --target <target>`，然后 `ctest --preset windows -R '<下述完整前缀>' --no-tests=error --output-on-failure`。不能把空匹配、崩溃负例或仅配置成功当 GREEN。

新目标：`PanguLogBackendTests`（unit/precommit）、`PanguLogUnitTests`（unit/fast）、`PanguLogConcurrencyTests`（integration/precommit，threads）、`PanguLogFileTests`（integration/precommit，subprocess）、`PanguLogHostTests`（integration/precommit）、`PanguLogNoAllocationFixture` / `PanguLogLifetimeFixture`（受监督程序）。既有 Foundation/Module/Host 目标保持原集合。

## Task 1: 固定第三方、Log 身份骨架与无分配后端可行性门

**Files:** Create `Source/ThirdParty/Fmt/{CMakeLists.txt,README.md,Repo}`、`Source/ThirdParty/Spdlog/{CMakeLists.txt,README.md,Repo}`；Create `Source/Runtime/Log/{CMakeLists.txt,Public/LogModule.h,Public/LogTypes.h,Private/LogModule.cpp,Private/SpdlogBackend.h,Private/SpdlogBackend.cpp}`；Create `Tests/Log/{CMakeLists.txt,BackendTests.cpp,NoAllocationFixture.cpp,RunNoAllocation.cmake}`；Modify `.gitmodules`、根 `CMakeLists.txt`、`Tests/CMakeLists.txt`。

**Interfaces:** Produces A 的固定目标和身份；private 后端接收两个不拥有的输出接缝，不执行文件打开或分配。测试私有源码可直接编入 backend 单元目标，避免仅为测试扩大 DLL 公共导出；该目标不同时链接另一份 Log 实现，真实 LogSystem 路径在 Task 7/11 再验证。

- [x] **Step 1: RED 用例。** `Backend.EmptyConstructionNeedsNoHeap`、`Backend.RawPayloadPreservesBraces`、`Backend.FileFailureDoesNotAbortConsole`、`Backend.NoGlobalRegistry`；子进程开启仅测试进程的分配计数/失败注入后构造、Emit、Flush、销毁，期望正常 exit0、分配请求 0、文件失败时终端次数 1。
  ```cpp
  EXPECT_EQ(result.ExitCode, 0);
  EXPECT_EQ(result.AllocationRequests, 0U);
  EXPECT_EQ(result.ConsoleWrites, 1U);
  ```
- [x] **Step 2: 运行 RED。** 构建 `PanguLogBackendTests PanguLogNoAllocationFixture`，运行 `^PanguLogBackendTests[.]Backend[.]|^LogNoAllocation$`，预期接口/目标缺失或默认文件路径终止。记录原生失败，不把依赖缺失当产品 RED。
- [x] **Step 3: 接入固定 submodule、目标、私有派生 logger/RawSink 和 no-op Query。** 新目录缺失时先 `git submodule add` 再在 Repo 检出 A 的 SHA，已存在先检查其修改；核对 HEAD、tag、许可证。生产只用 Try 分配，测试进程的故障注入不能成为生产绕过机制。此任务仅把 Log 加入根构建顺序，不把它接为 ModuleRuntime 依赖。
- [x] **Step 4: GREEN 与关卡。** 上述 Windows 用例通过，并在 Linux/macOS 针对 backend 运行；确认三标准库空构造不隐式分配、格式化缓冲/registry/backtrace 未进入。分别记录 new/C 分配的探针捕获范围，标准库/CRT未被捕获的构造路径须补对应源码依据，不能把“未观察到”写成覆盖全部分配。失败则先界定必要补丁并更新证据，范围大改回设计，不能开始 Task 4。
- [x] **Step 5: 提交。** `git commit -m "feat: 接入固定 fmt 与 spdlog 并验证无异常日志后端"`。

## Task 2: Foundation 拥有型报告与操作总预算

**Files:** Create `Foundation/Public/FailureReport.h`、`FailureContext.h`、`Private/FailureReport.cpp`；Create `Tests/Foundation/FailureReportTests.cpp`；Modify 两处 Foundation CMake。

**Interfaces:** Produces B 的报告/Context/Site，固定容量、offset 索引、Rule of Zero。Context 同一主错幂等，次错只能追加；回调返回 Error 与填报主错不一致时，Runtime 后续以返回错误为权威、原始位置 Unknown，保留不匹配信息为次项，不篡改业务 Result。

- [x] **Step 1: RED。** `FailureReport.PrimarySurvivesOverflow`、`CopiesOwnTheirText`、`UnknownOriginIsNotBoundary`、`SecondaryCannotReplacePrimary`：填满非主区后仍写入主错；销毁源/覆写原字符数组后复制件仍可读；核对全部预算而不只条目数。
  ```cpp
  ASSERT_TRUE(copy.PrimarySite().has_value());
  EXPECT_EQ(copy.PrimarySite()->Cause, originalError);
  EXPECT_TRUE(copy.Truncated());
  EXPECT_LE(sizeof(pg::FailureReport), 2048U);
  ```
- [x] **Step 2: 运行 RED。** 构建 `PanguFoundationUnitTests`，筛选 `^PanguFoundationUnitTests[.]FailureReport[.]`。
- [x] **Step 3: 实现固定预算采集和拥有型快照。** 初值清零、内部只有偏移，复制不留下自指针；至少给主现场单独保留 512 B。登记报告/历史/副本预算来源，Context 不接受并发共写或跨回调保存。
- [x] **Step 4: GREEN。** 正常、满预算、移动/复制、自赋值及未知域测试通过；无 Log/fmt/spdlog 链接，Report/Site 的构造、复制、记录、析构无分配。Host/Module 总预算在 Task 9/10 补齐 static_assert 和峰值清单。
- [x] **Step 5: 提交。** `git commit -m "feat: 建立有界拥有型失败报告与显式上下文"`。

## Task 3: 显式 Assert 配置、Check/Fatal 与应急终止

**Files:** Create `Foundation/Public/Check.h`、`Private/Fatal.cpp`、`Private/Platform/{Windows,Posix}/Emergency.cpp`；Create `Tests/Foundation/{FatalFixture.cpp,FatalTests.cmake}`、`Tests/CMake/LogConfig/{CMakeLists.txt,AssertConfigTests.cmake}`；Modify 根/ Foundation/Tests CMake 及 `cmake/Platform/Common/BuildOptions.cmake`。

**Interfaces:** `pg::Fatal(std::string_view reason, std::optional<Error> error={}, std::source_location=std::source_location::current()) noexcept` 标记 noreturn；PG_ASSERT/PG_CHECK 条件一次、消息惰性。选项 `PANGU_ENABLE_ASSERTS=ON` 导出 Foundation 公共配置 `PG_ENABLE_ASSERTS=1|0`，不改 NDEBUG；`PANGU_LOG_COMPILED_MIN_LEVEL=0..4` 默认 0，公开前端同一受控配置。

- [x] **Step 1: RED。** 子进程场景 AssertOn、AssertOff、CheckOffStillFatal、NoLogInstance、ConcurrentFatal、RecursiveFatal、BlockedOutput。测试关闭 Assert 不求值，Check 在 OFF/Release+NDEBUG 仍终止；Fatal 不调用测试析构标记/普通日志。
  ```cpp
  EXPECT_EQ(offConditionCalls, 0);
  EXPECT_EQ(checkConditionCalls, 1);
  EXPECT_FALSE(child.ReturnedFromFatal);
  ```
- [x] **Step 2: 运行 RED。** 构建 `PanguFoundationFatalFixture`，运行 `^FoundationFatal$|^LogAssertConfig$`，外层超时杀死卡住的测试子进程，不能让整个 CTest 被阻塞。
- [x] **Step 3: 实现独立应急与配置接线。** 首个 Fatal 只处理固定原因/数值，递归/并发不等首线程；无可证明有界输出时直接终止。平台原语查官方契约后选定并记录，不把普通 fprintf/文件 Flush 引入 Fatal。
- [x] **Step 4: GREEN。** Release+Assert ON/OFF、Debug+ON/OFF 定向配置验证，使用正式 BuildRoot 下隔离子图与父 preset/toolchain/SDK 参数，独立 EngineBuildRoot，不能覆盖主构建 CurrentContext；Consumer 同时得到一致宏，第三方 NDEBUG 未被改写。三桌面终止实际运行，移动只编译链接。
- [x] **Step 5: 提交。** `git commit -m "feat: 实现独立致命路径与显式断言配置"`。

## Task 4: 固定 Log 值类型、配置与受限 fmt 前端

**Files:** Create `Log/Public/{LogConfig,LogRecord,LogFormat}.h`、`Private/LogFormat.cpp`；Create `Tests/Log/{ConfigTests,FormatTests}.cpp`、`Tests/Log/FormatCompileTests.cmake`、`Tests/Log/Fixtures/FormatCompile/{CMakeLists.txt,Positive.cpp,UnsupportedFormatter.cpp,RuntimeFormat.cpp}`；Modify Log/Tests Log CMake。

**Interfaces:** Produces B 的配置、记录和 FormatRecord；路径输入只借用 Initialize 调用，待复制上限明确。支持子集先限制为自动 `{}`/转义括号、已知标量/文本/身份转换；动态格式及格式说明项拒绝或留到有实测证据的后续扩展，不让 fmt 的无异常错误路径变成预期输入的 abort。

- [x] **Step 1: RED。** `LogConfig.RequiredMustBeEnabled`、`CapacityArithmeticOverflow`、`LogFormat.OwnsAllInput`、`LiteralBracesSurviveBackend`、`UnsupportedSpecNeverEntersFmt`、`Utf8TruncationKeepsErrorIdentity`；不支持类型作为正式 compile-fail fixture。用最多长度及内置浮点极值验证分配 0。
  ```cpp
  EXPECT_TRUE(record.Truncated);
  EXPECT_EQ(record.Failure, suppliedError);
  EXPECT_EQ(probe.AllocationRequests, 0U);
  ```
- [x] **Step 2: 运行 RED。** 构建 `PanguLogUnitTests`，筛选 `^PanguLogUnitTests[.](LogConfig|LogFormat)[.]|^LogFormatCompile$`；compile-fail 子图先编过 Positive，再验证“有合法 fmt formatter 的自定义类型仍被 Pangu 前端拒绝”和运行期格式串拒绝，不能因缺头/SDK失败算通过。
- [x] **Step 3: 实现值类型/校验/格式化。** 全部有界复制、UTF-8/控制字符处理，必需错误身份先编码；无裸未知长度字符串、用户 formatter 或隐式 locale；格式参数不外泄到后端。使用直接 fmt 输出迭代器，不用会增长的 memory_buffer。
- [x] **Step 4: GREEN。** Windows/Linux/macOS 的全部支持类型极值/长文本验证无隐藏分配；明确每个支持项及拒绝项，当前路径失败则收窄受支持集合或回适配设计，不能仅通过增大 inline buffer 宣称零分配。
- [x] **Step 5: 提交。** `git commit -m "feat: 定义有界日志值与受限 fmt 格式化入口"`。

## Task 5: 失效安全 Writer、接纳门与 FIFO

**Files:** Create `Log/Public/{LogWriter,Log}.h`、`Private/{WriterControl,LogQueue,LogMemory}.{h,cpp}`；Create `Tests/Log/{WriterTests,QueueTests,WriterConcurrencyTests}.cpp`；Modify Log/Tests Log CMake。

**Interfaces:** Produces PG_LOG/Writer 和队列；Writer 控制块由 Log 的 Try 工厂单块分配、侵入引用计数，避免 shared_ptr 隐藏第二次分配。控制块保存关闭门/必要只读配置，实例指针只在成功提交保护内访问；队列短临界区只尝试接纳一次，竞争失败计数后返回。

- [x] **Step 1: RED。** `LogWriter.FilterBeforeArgumentEvaluation`、`SurvivesInstanceOwner`、`CopyDoesNotPreventClose`、`InFlightBuildPreventsRelease`；`LogQueue.ReserveCannotBeConsumedByInfo`、`ContendedErrorMayDrop`、`NoEvictionOrSequenceReuse`、`CountersExposeOverflow`。
  ```cpp
  EXPECT_EQ(expensiveCalls, 0);
  EXPECT_EQ(staleResult, pg::log::SubmitResult::kClosed);
  EXPECT_EQ(after.HighestAccepted, before.HighestAccepted);
  ```
- [x] **Step 2: 运行 RED。** 构建 `PanguLogUnitTests PanguLogConcurrencyTests`，筛选 `^PanguLog(Unit|Concurrency)Tests[.](LogWriter|LogQueue)[.]`。
- [x] **Step 3: 实现公共惰性宏和控制块/队列。** Writer/level 各求值一次，运行期过滤和存活接纳成功后才执行构造；构造函数指针/闭包只同步借用。低等级只占非预留区域，只有入队成功发序号。计数溢出可见且完整性不能误报，引用/门计数不回绕。
- [x] **Step 4: GREEN。** 用生产控制块/队列及受控内部 DrainOne 测试，不造替代队列；压力下验证每次结果、关闭与释放，保留令牌后销毁 Owner 再操作仍安全；单次 Submit 无分配、无 I/O、不等待空位。真实线程/公开实例集成在 Task 7/11 验证。
- [x] **Step 5: 提交。** `git commit -m "feat: 实现失效安全日志句柄与有界接纳队列"`。

## Task 6: 原生双输出、文件独占与配额轮转

**Files:** Create `Log/Private/{NativeLogIo,LogFileStore}.{h,cpp}`、`Platform/{Windows,Posix}/{NativeLogIo,LogFileStore}.cpp`；Create `Tests/Log/{FileStoreTests,OutputTests}.cpp`、`FileStoreFixture.cpp`、`RunFileStore.cmake`；Modify Log/Tests Log CMake。

**Interfaces:** Produces B 的 OutputOutcome、两个槽位和有界 FileStore。`FileStore::Open(const LogConfig&, LogInitReport&) noexcept -> Status`、`Write(string_view) noexcept -> OutputOutcome`、`Flush() noexcept -> OutputOutcome`、`Close() noexcept -> Status`、`Inspect() const noexcept -> FileStoreSnapshot`；快照至少含 ManagedBytes/ReclaimedBytes/ReclaimedFiles/LeaseHeld/原生错误。保留原始 errno/Win32 和原生进度，规范化逻辑字节，partial write 不计整条送达。

- [x] **Step 1: RED。** `LogFiles.ExclusiveAcrossProcessesAndHosts`、`StaleProcessLeaseCanBeReacquired`、`ForeignMatchingNamePreserved`、`NoDeleteOfLiveFile`、`QuotaIncludesHistory`、`RotateAtWholeRecord`、`CloseOrDeleteFailureDegrades`、`NoReopenAfterFailure`；同名外部文件/链接不因扩展名被认领。
  ```cpp
  EXPECT_TRUE(std::filesystem::exists(foreignPath));
  EXPECT_LE(state.ManagedBytes, config.PartitionBytes);
  EXPECT_EQ(state.ReclaimedBytes, expectedReclaimedBytes);
  ```
- [x] **Step 2: 运行 RED。** 构建 `PanguLogFileTests PanguLogFileStoreFixture`，筛选 `^PanguLogFileTests[.]Log(Files|Output)[.]|^LogFileStoreProcesses$`，工作根限定对应 BuildRoot，测试退出释放全部 lease。
- [x] **Step 3: 实现平台文件/终端接缝与 FileStore。** Windows/POSIX 各以真正跨进程的非等待独占租约锁管理分区，锁文件遗留不等于活跃占用；新文件 exclusive create。文件头保存版本/PartitionId/会话和段号作为归属凭据，未知或损坏归属保留。遍历流式选最旧关闭项，不为全目录建立增长容器。
- [x] **Step 4: GREEN。** 终端失败/文件失败分别停用；轮转前核算完整行及头，预算不足仅回收自身最旧关闭项，不能回收则失败降级。正常回收另计数不改送达账目；原生关闭/刷新错误按实际平台资源是否已释放处理，不用错误返回简单推定 handle 仍有效。
- [x] **Step 5: 提交。** `git commit -m "feat: 建立可失败双输出与独占日志分区轮转"`。

## Task 7: 真实实例、原生消费者与可恢复初始化

**Files:** Create `Log/Public/LogSystem.h`、`Private/{LogSystem,NativeLogThread}.{h,cpp}`、`Platform/{Windows,Posix}/NativeLogThread.cpp`、`Private/LogTestAccess.h`；Create `Tests/Log/{InitializationTests,LifecycleTests}.cpp`、`InitializationFixture.cpp`、`RunInitialization.cmake`；Modify Log/Tests Log CMake。

**Interfaces:** Produces B 的默认 Cold LogSystem、Initialize/GetWriter/Inspect/Stop。private `NativeLogThread::Start(void (*entry)(void*) noexcept, void*, size_t stackBytes) noexcept -> Status`、`TryJoin(deadline) noexcept -> Status`。显式实例内数据预算默认 1 MiB、消费者栈请求 256 KiB，队列增大必须同时满足 MemoryBudgetBytes，OS/CRT保留量另列证据。

- [x] **Step 1: RED。** `LogInit.EveryFailurePointRetainsCleanupOwner`、`RequiredFailureNeverPublishesWriter`、`DisabledNeedsNoHeapOrThread`、`OptionalFileFailureKeepsConsole`、`BothUnavailableIsNotRunning`；`LogLifecycle.StopWaitsForActiveSubmission`、`WriterAfterHostIndependentOwnerDeath`。逐点注入配置复制/控制块/队列/同步/后端/输出/线程失败，检查实际资源计数回到初值或明确 Closing。
  ```cpp
  EXPECT_NE(system.GetState(), pg::log::LogState::kRunning);
  EXPECT_EQ(report.CleanupPending, expectedPending);
  EXPECT_EQ(staleResult, pg::log::SubmitResult::kClosed);
  ```
- [x] **Step 2: 运行 RED。** 构建 `PanguLogUnitTests PanguLogInitializationFixture`，筛选 `^PanguLogUnitTests[.]Log(Init|Lifecycle)[.]|^LogInitializationFailures$`。
- [x] **Step 3: 实现 Initialize、消费者和无既有 Flush 的 Stop。** 配置错误在资源获取前失败；准备全部固定资源后再启动消费者，门最后开放。单块 Try 数据存储和单独 Writer 控制块同源释放，公开对象默认构造不分配。失败保留原始 Error、部分 Owner 和有效空 Writer；不可移动实例不丢失清理责任。
- [x] **Step 4: GREEN。** 真实消费者处理两路，再显式关闸/等提交/排空/等待安全退出。等待 deadline 不能落入无上限 join；明确 native 完成判定与最后资源释放。私有 LogTestAccess 只给正式测试提供实例内故障/阻塞接缝，沿用仓库私有 test-access 模式，不把测试回调开放为 Plugin Sink 产品接口。
- [x] **Step 5: 提交。** `git commit -m "feat: 接通日志实例消费者与可恢复初始化"`。

## Task 8: 固定前缀 Flush、Stop 交接与完整性账目

**Files:** Create `Log/Private/LogDrain.{h,cpp}`、`Tests/Log/{DrainTests,DrainConcurrencyTests,DrainFixture}.cpp`、`Tests/Log/DrainTests.cmake`；Modify `Log/Public/{LogSystem,LogTypes}.h`、`Log/Private/LogSystem.{h,cpp}`、`Log/Private/LogTestAccess.h`、`Tests/Log/CMakeLists.txt`。

**Interfaces:** Completes B 的 FlushReport/StopReport；`LogSnapshot` 明确包含 HighestAccepted/HighestProcessed。Initialize 的 Stop 子集扩展为同一请求协调器，不保留另一套停止实现。逐输出 outcome 归到当前序号，内部降级摘要另计控制输出，不额外领取用户接纳序号。

- [x] **Step 1: RED。** `LogDrain.FlushDoesNotFollowLaterRecords`、`CapturesAlreadyProcessedPrefix`、`TimeoutRetriesSameH`、`StopRetainsTimedOutFlush`、`LaterFailureDoesNotPolluteEarlierPrefix`、`CompletedWithRejectedRecordsIsIncomplete`、`ControlNoticeDoesNotAdvanceHOrRepeat`、`OverflowNeverReportsComplete`；子进程 `OwnerDestroyedWithBlockedSinkTerminates`/`StopCallerReturnsWhileSinkBlocked`。
  ```cpp
  EXPECT_EQ(retried.HighestAccepted, first.HighestAccepted);
  ASSERT_TRUE(stopped.PreviousFlush.has_value());
  EXPECT_LE(stopped.PreviousFlush->HighestAccepted, stopped.FinalFlush.HighestAccepted);
  EXPECT_FALSE(stopped.FinalFlush.OutputComplete);
  ```
- [x] **Step 2: 运行 RED。** 构建 `PanguLogUnitTests PanguLogConcurrencyTests PanguLogDrainFixture`，筛选 `^PanguLog(Unit|Concurrency)Tests[.]LogDrain[.]|^LogDrainProcesses$`。用明确屏障冻结第 H 条和后续失败，不依赖 sleep；所有竞争结果都检查，子进程有外层监督超时。
- [x] **Step 3: 实现前缀检查点。** 接纳锁内固定 H；消费者越过 H 前保存累计结果并刷新两路，再继续。一个管理请求，超时不覆盖；Stop 关门固定最终 H，保存旧结果后推进最终范围。完成但有损失只返回处理成功，OutputComplete 必须为 false。
- [x] **Step 4: GREEN。** 持续生产、H=0、全部已处理、输出初始化失败/停用、partial write/Flush失败、被过滤、文本截断、正常历史回收分别有正确账目。窗口后拒收只在其采集快照中可见；Closed 后残留 Writer 不修改已交付结果。有限析构兜底仍未安全完成的场景用子进程验证 Fatal。
- [x] **Step 5: 提交。** `git commit -m "feat: 实现累计日志排空与可重试停止协议"`。

## Task 9: ModuleRuntime 的 Log 依赖、失败上下文与等待解释

**Files:** Modify `Module/CMakeLists.txt`、`Private/{ModuleBuiltin,ModuleCatalog,ModuleManager,ScopeRuntime,ScopeBuildContext,RegistrationContext,NativeModuleLibrary}.cpp`、`Private/{ScopeRuntime,ModuleRecord,NativeModuleLibrary}.h`、`Private/Platform/{Windows,Posix}/NativeModuleLibrary.cpp`、`Public/{ModuleOperation,ModuleManager,RegistrationContext,ScopeBuildContext}.h`；Create `Public/ModuleDiagnostics.h`、`Tests/Module/{ModuleDiagnosticsTests,ModuleWaitTests}.cpp`；Modify `Tests/Module/{ResidentModuleTests,ModuleMetadataTests,ModuleManagerTests,BatchLoadTests,ScopeResourceAccessTests,ScopeBuildTests,ScopeBuildMockTests,ScopeTypeTests,ScopeLifecycleTests,ModuleUnloadTests,ModuleUnloadConcurrencyTests}.cpp`、`Tests/Module/Support/ScopeTestSupport.{h,cpp}`、`Tests/Module/Fixtures/ScopeAccessFixture.h`、本任务 Step 4 的回调夹具和 `Tests/Module/CMakeLists.txt`。

**Interfaces:** `ModuleManager::Create(..., ModuleOperationReport&, log::LogWriter writer={}) noexcept -> Result<ModuleManager>` 尾部增加可选参数。Context 的 `Log() const noexcept -> log::LogWriter`、`Failure() noexcept -> FailureContext&`；`ScopeHooks<T>::Start/PollStopped` 变为 `Status (*)(T&, FailureContext&) noexcept`，type-erased virtual 同步变更，RequestStop 保持 `void(T&) noexcept`。`ModuleOperationReport` 增加拥有型 Failure 与固定 WaitReasons，保持原有 Error/身份/责任含义。

构造接缝明确为 `ScopeBuildContext(detail::ScopeBuildSink&, FailureContext&, log::LogWriter={}) noexcept`；私有 `RegistrationContext(RegistrationScope&, ModuleId, std::uint64_t hostDomain, FailureContext&, log::LogWriter) noexcept`。原私有 optional<Error> Failure 更名 RegistrationError，避免与新 Failure() 重名，友元读取同步更新，保留原粘滞声明错误语义。

- [x] **Step 1: RED。** `ModuleDiagnostics.CoreIncludesLogAndBothRequiredEdges`、`RegisterAndStartOriginSurvivesRollback`、`CallbackContextCannotOverrideReturnedError`、`PollErrorKeepsOriginalCause`；`ModuleWait.BorrowPinDependencyAndNativeCloseAreDistinct`、`SnapshotTruncationPreservesOwner`。记录实际 sizeof 和报告峰值来源。
  ```cpp
  EXPECT_EQ(pg::module::BuiltinResidentQueries().size(), 3U);
  EXPECT_EQ(report.OriginalError, originalError);
  EXPECT_EQ(report.CleanupOwner, expectedOwner);
  EXPECT_LE(sizeof(pg::module::ModuleOperationReport), 4096U);
  ```
- [x] **Step 2: 运行 RED。** 构建 `PanguModuleUnitTests PanguModuleIntegrationTests PanguModuleUnloadTests PanguScopeBuildMockTests`，筛选 `^(PanguModuleUnitTests|PanguModuleIntegrationTests|PanguModuleUnloadTests|PanguScopeBuildMockTests)[.]`。两个模式都跑，另保持 ModuleRegression 的 Mono-only 原覆盖，不用猜测旧 GTest suite 名称。
- [x] **Step 3: 接线与迁移。** PUBLIC 链接 Log，Builtin 添加 Query，验证 Log/Foundation/Runtime required 边。把旧手写 `{Foundation,Runtime,...}` 夹具改为完整核心集合，保留缺核心/错版本负例；不能只改 size 断言。局部 Report 在回调结束发布，保存首次原始现场、标注当前清理现场，不把重入 Busy 写入外层 Context。
- [x] **Step 4: GREEN。** 明确迁移 `Tests/EngineHost/Fixtures/HostServiceModule.cpp`、`Tests/EngineHost/EngineHostFailureTests.cpp`、`Tests/Module/Fixtures/{ScopeLifecycleFixture.h,First.cpp}`、`Fixtures/Shared/{Provider,ScopeProvider,ScopeConsumer,Consumer}.cpp`、`Fixtures/ResourceAccess/ResourceAccessFixture.cpp` 的签名，包含 Task 9 列出的 Support/ScopeBuild 本地回调。只读采样原 Gate/DAG 关卡，不改变停止顺序；Win32先抓值、POSIX先复制 Loader 文本。用新的 PG_CHECK/Fatal 替换触及的兜底 terminate，显式 Release 仍返回失败。
- [x] **Step 5: 提交。** `git commit -m "feat: 将日志与失败现场接入模块生命周期"`。

## Task 10: Host 日志初始化、首错保留与最终关闭

**Files:** Modify `EngineHost/Public/{HostStartupConfig,HostOperation,EngineHost}.h`、`Private/{EngineHost.cpp,EngineHostState.h}`；Create `Tests/EngineHost/EngineHostLogTests.cpp`；Modify `Tests/EngineHost/{EngineHostTestFixture.h,EngineHostStartupTests.cpp,EngineHostFailureTests.cpp,EngineHostShutdownTests.cpp,EngineHostServiceTests.cpp,EngineHostLifetimeTests.cpp,EngineHostLifetimeFixture.cpp,EngineHostDestructorFixture.cpp,EngineHostConcurrencyTests.cpp,CMakeLists.txt}`、`Tests/Meta/MetaTestFixture.h`、`Tests/CMake/ModuleLinkModes/Fixtures/Consumer.cpp`、`Tests/CMake/SourcePluginProject/MyGame/Source/HostCheck.h`、`Samples/MinimalHost/{Main.cpp,README.md}`。

**Interfaces:** `HostStartupConfig::Logging` 为 `log::LogConfig`；`HostOperationReport` 追加当前失败现场、Log 状态/初始化摘要/停止结果，StartupFailure 保存首次拥有型 Origin。增加 `HostOperationStage::kLogInitialization=9`，原枚举值 0..8 不改。`EngineHost::GetLogWriter(log::LogTags={}) const noexcept -> log::LogWriter`、`InspectLog() const noexcept -> log::LogSnapshot`；Starting/业务清理期间经显式持有的 LogSystem 访问，不套用只允许 Ready 的 Scope 服务查询 Gate。

- [x] **Step 1: RED。** `EngineHostLog.OptionalInitFailureDoesNotOwnStartupCause`、`RequiredInitFailureRollsBack`、`FactoryAndDestroyUseCorrectHost`、`ManagerDoneButLogBusyIsClosing`、`FailedStartRetainsCauseWhileLogDrains`、`ClosedCanHaveIncompleteLog`、`TwoHostsHaveIndependentPartitions`。对 E1 原错/E2 回滚错/Log 错三者分别断言。
  ```cpp
  EXPECT_EQ(report.StartupFailure->Cause, startupError);
  EXPECT_EQ(host.GetState(), pg::engine::HostState::kClosing);
  EXPECT_FALSE(report.LogStop->FinalFlush.OutputComplete);
  EXPECT_LE(sizeof(pg::engine::HostOperationReport), 16384U);
  ```
- [x] **Step 2: 运行 RED。** 构建 `PanguLogHostTests PanguEngineHostTests PanguEngineHostConcurrencyTests`，筛选 `^PanguLogHostTests[.]EngineHostLog[.]` 及原 Host 组；样例启用后运行 `^MinimalHostSmoke$`。
- [x] **Step 3: 实现 Host 协调器。** State 的无分配 LogSystem 成员在 Manager 之前声明，使逆序析构先处理 Manager，再处理 Log；在 Manager/回调前 Initialize 并记录 disposition，任意失败复用最终关闭流程。Manager 成功后不直接 Closed，保留 Log 原生依赖、请求 Stop、发布当前报告；TryFinishShutdown 使用非等待/明确 deadline 的推进，不把一次 Busy 当错误恢复完成。GetLogWriter 不触发 Manager 管理或隐式启动，按 Host 的串行查询约束读取 Log；内部回调优先使用已注入 Writer。
- [x] **Step 4: GREEN 和旧用例迁移。** 不测试日志的原 Host/Meta/Project 夹具显式 Enabled=None，保持其原有无后台日志假设；专门新测试使用真实异步实例和受控屏障验证重试，不能全部 Disabled 来回避集成。样例配置真实专属目录/PartitionId，关闭 helper 有界轮询，未结束不能离开作用域假装成功。记录总体报告工作峰值不超 64 KiB。
- [x] **Step 5: 提交。** `git commit -m "feat: 建立 Host 日志初始化与最终排空闭环"`。

## Task 11: 真实 DLL 卸载、跨 Host 句柄与完整应用回归

**Files:** Create `Tests/Log/{LifetimeFixture.cpp,LifetimeTests.cmake,RuntimeNoAllocationFixture.cpp,README.md}`、`Tests/Log/Fixtures/{LogProducer.h,LogProducer.cpp,CMakeLists.txt}`、`Tests/Log/Benchmarks/{LogBenchmarks.cpp,CMakeLists.txt}`；Modify `Tests/Log/CMakeLists.txt`、根 CMake benchmark 接线、`Tests/EngineHost/EngineHostLogTests.cpp`、`Tests/CMake/SourcePluginProject/MyGame/Source/{GameMain,EditorMain,MobileHost}.cpp` 及 `Tests/CMake/SourcePluginProject/{StaticCatalogIntegrationTests,SharedDeployTests,IncrementalBuildTests}.cmake`。

**Interfaces:** 生产者 ModuleId=`b6ddc3fdc5b34a2e908c566d90db1e22`，声明 Id=`b6ddc3fdc5b34a2e908c566d90db1e23`；Query 复用正式 PanguMetaTool 接线。Host 不原生导入业务库，私有卸载标记确认真正释放，不只检查逻辑状态。

- [x] **Step 1: RED。** `LogLifetime.OwnedRecordSurvivesPhysicalUnload` 在消费者屏障前入队 Plugin 栈/静态消息与位置，释放 Pin/Scope 后观察物理卸载，再放行消费；`WriterOutlivesHostAndDoesNotPinProducer` 保留 Writer 和报告直到 Host 对象析构后操作。真实公有 LogSystem 路径另做无分配/故障注入验证。
  ```cpp
  ASSERT_TRUE(physicalUnloadObserved);
  EXPECT_EQ(savedText, expectedText);
  EXPECT_EQ(afterHostResult, pg::log::SubmitResult::kClosed);
  ```
- [x] **Step 2: 运行 RED。** 构建 `PanguLogLifetimeFixture PanguLogRuntimeNoAllocationFixture` 和 Project Game/Editor，运行 `^LogLifetime$|^LogRuntimeNoAllocation$|^SourcePlugin(StaticCatalog|SharedDeploy)$`，核对实际匹配名称，不拼接不同源码轮次计数。
- [x] **Step 3: 接通生产 Fixture 与实际应用。** Plugin 只在受保护同步调用内格式化，队列无其回调/视图；Core Log 库由应用链接链持有，残留控制块最后释放仍执行常驻实现。验证 Log DLL/第三方部署由原生图推导，不手工复制 Windows DLL 假装跨平台。
- [x] **Step 4: GREEN 与成本记录。** 两模式运行，Modular 有真实卸载标记；记录私有源码无分配探针与真正 DLL 调用探针各自可捕获的分配范围，Windows EXE 的 new 计数不能被宣传成能拦截全部 DLL/OS 分配。保留源码审计、Log Try 分配故障点与三标准库实测证据。opt-in 基准测禁用、接受、满队列/竞争拒收、报告构造和双输出，不绕过存活 Gate；记录均值/分位数和配置，不设未经依据的性能门槛。
- [x] **Step 5: 提交。** `git commit -m "test: 验收日志跨卸载存活与实际应用消费"`。

## Task 12: 五平台双模式、文档和最终归档

**Files:** Modify `Tests/CMake/SourcePluginProject/CrossCompileTests.cmake`、`Tests/CMake/SourcePluginProject/MyGame/Source/{MobileHost.cpp,MobileHost.h}`、`Tests/README.md`、`wiki/foundation/{error-diagnostics,third-party,testing,build-system}.md`、`wiki/core/{engine-host,module-api}.md`、`wiki/README.md`；Create `docs/superpowers/plans/2026-11-12-foundation-diagnostics-validation.md`，更新本计划勾选和验收摘要。

**Interfaces:** MobileHost 最终链接实际引用 `LogSystem::Initialize/GetWriter/Stop`、FailureReport 和 Check，使用编译期限定的 UTF-8/标量 fmt 消费，不在构建宿主执行交叉目标。既有平台前置条件和 PanguMeta SDK 接线保持。

- [x] **Step 1: 最终定向契约。** 桌面分别运行新后端/报告/Fatal/队列/文件/排空/Host/卸载组；移动 Consumer 真正调用而非只 include，检查目标架构与动态依赖，禁带宿主 STL/LLVM。Assert ON/OFF 两个配置的对应子进程组均有证据。
- [x] **Step 2: 三桌面正式全量。** 在固定已提交源码、override=OFF 上，各按 Monolithic→Modular 配置、build/full CTest，保留 ModuleRegression 与 Project 消费。命令见下表，所有结果原生 exit0 且选择非空；失败/重跑分别记。
- [x] **Step 3: 两移动双模式最终链接。** 正式 Android/iOS ARM64 根构建及 SourcePluginProject/CrossCompileTests.cmake 两模式运行，检查真实 MobileHost/Log/Runtime 库，记录仅编译/链接/宿主检查，不称设备文件、线程、Fatal 运行验收。
- [x] **Step 4: 静态检查、恢复与归档。** 自有变更 `.h/.cpp` 使用仓库 clang-format，tidy 用对应实际数据库；第三方按独立边界记录。桌面恢复 Modular、Samples/Benchmarks OFF、Assert ON；移动恢复 Monolithic/BUILD_TESTING OFF。归档 SHA/tree、子模块、宏、sizeof/内存/性能、日志/命令、限制、补丁和可恢复 OOM 证据；测试保留。全分支 Review/验收后确认无活跃依赖，再精确清理本计划 workspace。

Step4 已全部完成。必要证据与 39 条裁定在 bad1dc3 正式归档，确认无运行依赖后已精确删除 `.superpowers/sdd/2026-11-12-foundation-diagnostics/`，并核对路径不存在。正式 Tests 及各平台 BuildRoot 缓存保留。
- [x] **Step 5: 提交。** `git commit -m "docs: 归档基础诊断与 Log 正式验收"`，满足 AGENTS 条件的同 tree 整合复用结果。

## 验证命令与记录

| 场景 | 正式命令或判据 |
| --- | --- |
| Windows 定向 | `cmake --preset windows`（保留有效 SDK cache）→ `cmake --build --preset windows --target <任务目标>` → `ctest --preset windows -R '<任务前缀>' --no-tests=error --output-on-failure` |
| Windows Full | `cmake --preset windows -DPANGU_LINK_MODE=Monolithic -DPANGU_ENABLE_ASSERTS=ON` → `cmake --build --preset windows-full` → `ctest --preset windows-full --output-on-failure`；Modular 重复必要完整矩阵 |
| Linux/macOS | 在规定源码挂载路径替换 preset 为 linux/macos，使用各自 ext4/APFS BuildRoot，不能经 /mnt/d 或临时源码副本复用 |
| Assert 裁剪 | 正式 BuildRoot 内 LogConfig 隔离子图分别 Release/Debug、ON/OFF；恢复主配置 ON，禁止修改第三方 NDEBUG 代替自有选项 |
| Android/iOS | `cmake --preset android-arm64 -DPANGU_LINK_MODE=Monolithic -DBUILD_TESTING=OFF`、对应 build preset，再 Modular；Mac 用 ios-arm64 重复。最终链接额外显式执行现有 CrossCompileTests.cmake |
| 移动最终消费 | `cmake -DPANGU_ENGINE_SOURCE_DIR=<规定共享源码> -DPANGU_TEST_ROOT=<本地BuildRoot>/Tests/LogMobile -DPANGU_TARGET_PRESET=android-arm64 -DPANGU_LINK_MODE=Monolithic -DPANGU_META_SDK_CONFIG=<有效宿主配置> -DPANGU_ENGINE_ALLOW_LOCAL_OVERRIDE=OFF -P Tests/CMake/SourcePluginProject/CrossCompileTests.cmake`；iOS/另一模式替换相应值 |
| format/tidy | `clang-format --dry-run --Werror <自有变更文件>`；`clang-tidy -p <真实数据库目录> <受影响.cpp>`；头通过真实测试 TU，SDK/Project 子图使用自身数据库 |

实施过程中每任务记录 RED/GREEN、实际命令和返回码、关键提交；最终 validation 留必要结论，不把大量终端日志写入计划。未完成项不勾选，未执行平台明确写“未验证”。首个可行性门成功后才进入依赖它的任务，不以只读源码推断替代实测。被测 SHA/tree 与后续纯文档归档提交分别记录，不能把前者说成后者的完整矩阵；Full 后若有代码/测试改动，按真实影响补验证并独立归档轮次。

## 计划自查与规格覆盖

| 规格范围 | 任务 |
| --- | --- |
| §1–4 组织、固定库、接口、驻留 | 1、5、9、10、11 |
| §5 主现场、预算、回调、等待原因 | 2、9、10、11 |
| §6 Assert/Check/Fatal | 3、9、12 |
| §7 fmt/spdlog、无异常差距 | 1、4、7、11 |
| §8 固定记录、FIFO、初始化 | 4、5、7 |
| §9 分区、预算、轮转、故障 | 6、7、8 |
| §10 累计 Flush、交接、停止 | 7、8、10 |
| §11 Host 初始化、首错、关闭 | 9、10、11 |
| §12 验收、平台、证据 | 每任务 GREEN、11、12 |
| §13 授权参数与超范围回讨论 | B、Task 1/4 的门、最终验收 |

本计划按 writing-plans 自查覆盖、步骤可判定性、接口名称、Review Focus 和篇幅。原计划的输入/断言与实际运行结果分别保留。计划已按用户授权执行；实现、测试与平台结果按真实版本归档，Review/清理状态独立记录，不由模板推定已完成。
## 实施与验收归档（2026-11-13）

本节保留截至 `0b7a097` 的原 Task12 记录；后续修复与当前收尾状态见下一节，不将旧被测版本追认为新实现的 Full。

- Task1–11 的后端、拥有型报告、Fatal、日志值/Writer/文件/排空、Module/Host接线和卸载/应用验收均已完成并独立审阅；关键提交、预算、可恢复OOM与分配探针边界统一见[正式验收](2026-11-12-foundation-diagnostics-validation.md)。
- Windows Monolithic/Modular Full 317/317、313/313，WSL Linux 312/312、308/308；原生配置、Full构建、非空选择及CTest均0。实际被测`d324763b9fd40d56f20d3e5e1c610cff4aa5bc2c`/tree`273e596b160102254bdd998544287575affa77f5`。
- macOS两模式Full 315/315、311/311，原生CTest0，分别4490.48/4607.81秒；ModuleRegression与全部Project/Owner实际执行。实际被测`9d70aae21f60eec6e40bfa0f50c1ddd8f407f215`/tree`c27430c12ea760cf3c204aee64562770583193ab`。outer98998实际1与原进程继续native全0分开保留，不伪记外层0。
- Android/iOS ARM64两模式根构建+显式Cross均原生0，真实类型caller、LogSystem/FailureReport/Check消费、架构与动态依赖已检查；实际被测d324/tree273，仅编译/最终链接/宿主检查，未设备运行。后续仅Darwin的测试接线未改变其有效图，按真实影响复用，不追认新HEAD原样全矩阵。
- dc4三个fresh冷Owner Base/Project/Provider各0，11492的新fresh Engine在1200秒下1183.60秒真实0；后续只单调放宽该准备上限至1500，不重复冷编译，不把旧失败4项门拼成一轮通过。原失败、热续建成本、30秒局部发现及OS因果限制分别归档。
- 必要修正包括Android公开gettid/join分支与有类型移动caller、独立MetaCli真实Log链、第三方普通TU关闭模块扫描、Fatal声明契约测试和独立冷场景。共享报告峰值Windows65456/POSIX62384B（Win余80B），默认Log三桌面小于1MiB；完整阶段计账和基准范围见正式验收。
- 最终桌面Modular/Samples和Benchmarks OFF/Assert及测试ON，移动Monolithic/BUILD_TESTING OFF；全部override显式OFF并保留有效SDK。三宿主无构建/测试/Git写者、全子模块clean、无index.lock后才写本次文档。
- `0b7a097` 为该阶段纯文档归档，与被测代码SHA/tree分开；当时Review和精确清理尚待执行，当前状态见下一节。不因提交身份改变重跑无影响Full，全程未merge、push或删除正式Tests。

## 最终评审修复与收尾（2026-11-13）

- `5e23385` 移除 Windows Debug logger 空容器隐式分配，按批准规格直接使用同步 RawSink；精确接纳准备段增加消费屏障，同时保留真实竞争拒收及其累计损失回归。`aa827b2` 仅补 Host 正式测试在首次停止请求尚未发布时的有界管理推进，时限仍为两秒。
- 唯一修复范围复审为 `0b7a097..aa827b2`，I1/I2 均 ADDRESSED，新增 Critical/Important/Minor 为 0/0/0。原 Mac Mono Host 7/8 与两组确定性 RED 均保留为实际失败轮次，未被后续通过结果覆盖。
- 必要定向包括 Debug 22/22，Release Modular Windows 144/144、Linux/macOS 各 140/140，三桌面 ModuleDiagnostics 各 13/13、四配置门各 0；最终三桌面两模式 Host 各 8/8。Android/iOS 两模式正式根构建和完整 CrossCompile 入口均 0，未设备运行。精确被测版本与复用见[正式验收](2026-11-12-foundation-diagnostics-validation.md)。
- 三桌面默认实例预算为 771568/762344/762352B，报告峰值仍为 Windows65456/POSIX62384B；本轮未重测性能，旧表仅为历史 BackendLogger 成本。
- 三桌面恢复 Release/Modular、Samples/Benchmarks OFF、Assert/测试 ON；移动恢复 Monolithic/测试 OFF，全部 override OFF、SDK 有效。三宿主全子模块干净、无构建写者、无 index.lock；必要结论及全部裁定见[评审归档](2026-11-12-foundation-diagnostics-review.md)。必要记录在 bad1dc3 归档后，本计划临时工作区已精确删除并核对路径不存在，正式 Tests 保留，未合并或推送。
