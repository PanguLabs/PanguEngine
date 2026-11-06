# 分层测试迁移记录

记录基于 `5e897dbee8f58a1f1d6fafecd1ae3b3e378c3a99` 的实际源码清单；不能把后续拆分后的用例数量与旧数量直接比较。
Task 1 迁移 Foundation，并建立 GoogleTest/GoogleMock 依赖和测试登记接口；Task 2 迁移 Module 单元与 JSON，
Task 3 迁移真实 Module 集成、Catalog 与析构夹具；Task 4 集中 BuildTools 测试及根调度。
下文分别记录已迁移映射和迁移前场景清单。
完整方案见 [批准规格](../docs/superpowers/specs/2026-11-05-layered-testing-design.md)。

## BuildTools：PanguBuildTool 已迁移

三份测试从 `BuildTools/PanguBuildTool/Tests/` 搬至 `Tests/BuildTools/PanguBuildTool/`。
独立工程目标保留 `PanguPluginManifestTests`、`PanguPluginDiscoveryTests`，通过固定
GoogleTestMain 逐例登记；CLI 保留 CMake 进程父检查。工具图保持无异常并保留 RTTI，
不链接 Runtime 的 `PanguBuildOptions`。

| 旧场景 | 新入口 | 保留判据 |
| --- | --- | --- |
| Manifest 的 23 项输入参数表 | `PanguPluginManifestTests.ManifestCases/PluginManifest.ParsesAndDiagnoses/<0..22>` | valid/minimal 字段、全部 ID/TOML/SemVer 合法非法输入、每项错误含实际完整路径 |
| Manifest 缺文件 | `PanguPluginManifestTests.PluginManifest.MissingInputIncludesFullDiagnosticPath` | 不存在文件必须失败，错误保留完整路径 |
| Discovery sorted 双根 | `PanguPluginDiscoveryTests.PluginDiscovery.SortsBothRootsAndIgnoresOrdinaryEntries` | Engine/Project 真实目录、普通文件/普通目录忽略、两项结果按 ID 排序 |
| Discovery empty / empty-valid / 同根 | `RejectsMissingEngineRoot`、`AllowsEngineWithoutPluginsDirectory`、`RejectsSameActualRootTwice` | 缺 root 失败、无 Plugins 返回空列表、同实际 root 两次拒绝且诊断非空 |
| Discovery duplicate / same-folder | `RejectsDuplicateDisabledPluginIds`、`AllowsSameFolderNameAcrossIndependentRoots` | 未启用重复 ID 仍拒绝、独立根同名目录可共存 |
| Discovery 缺文件/错误类型/非法 manifest | `RejectsMissingManifest`、`RejectsMissingCMake`、`RejectsDirectoryInPlaceOfCMakeFile`、`RejectsInvalidDisabledManifest` | 每项真实文件系统输入分别失败，诊断非空 |
| Discovery 目录链接 escape | `RejectsDirectoryLinkPathEscape` | 外部真实 Plugin 目录通过目录链接进入 Engine，先核对 canonical 外部目标，再要求发现拒绝；Windows 使用原生 Junction，Linux/macOS 使用 directory symlink；创建失败明确失败 |
| PluginCli 全部原脚本 | 宿主 `PluginCli` | stdout/stderr、退出码、精确空 JSON、ID 排序/字段/转义、全部 configure_inputs 原生绝对路径、中文/Emoji 路径及诊断、重复 ID 与未知命令 |
| 新工作目录契约 | `PanguPluginManifestTests.TestWorkspace.*` | 显式 root 内创建；拒绝 `..`、原生绝对路径及现有目录链接逃逸，不写越界目录；链接平台夹具同上 |

根调度 `BuildTools.PanguBuildTool.PluginManifest` 为 unit/build/fast；
`BuildTools.PanguBuildTool.PluginDiscovery` 为 integration/build/precommit；
`BuildTools.PanguBuildTool.PluginCli` 为 system/build/precommit，均为 owner.engine。
根准备入口使用独立宿主 BuildRoot，普通 bootstrap 的 OFF cache 不复用。
`TestingBuildToolsTests` 验证登记、实际发现、独立 cache、不同工具名隔离及同工具配置冲突；
`TestingBuildToolsFailurePropagation` 复用 Task 1 真实 fixture 的失败/跳过/空筛选/缺程序，
核对父状态和原始内层用例诊断。使用方式见 [BuildTools](../BuildTools/README.md)。

## 旧覆盖实测基线

2026-11-05，Windows / Clang 23.1.0 / Release，修改源码前的干净 `5e897db`：

| 范围 | 命令 | 首轮结果 |
| --- | --- | --- |
| 根配置 | `cmake --preset windows` | 普通沙箱的 Plugin 目录枚举报 `Access is denied`；同命令提权后成功，无源码修复 |
| 根构建 | `cmake --build --preset windows` | 成功，包括 Toolchain STATIC 探针与最终链接目标 |
| 根 CTest | `ctest --preset windows --output-on-failure` | 22/22 通过，146.32 秒，无失败/跳过 |
| 独立工具配置 | `cmake -S BuildTools/PanguBuildTool -B Build/Windows/Tests/BuildTools/PanguBuildTool -G Ninja -DCMAKE_CXX_COMPILER=D:/Developer/LLVM/bin/clang++.exe -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON` | 成功；没有复用普通 bootstrap 的 BuildRoot |
| 独立工具构建 | `cmake --build Build/Windows/Tests/BuildTools/PanguBuildTool` | 成功 |
| 独立工具 CTest | `ctest --test-dir Build/Windows/Tests/BuildTools/PanguBuildTool --output-on-failure` | PluginManifest、PluginDiscovery、PluginCli，3/3 通过，0.22 秒，无失败/跳过 |

基线的根 22 项为下文全部普通运行入口；独立工具的 3 项另记，不能声称根已覆盖它们。
Linux/macOS 未重跑此旧版本的完整基线；移动端未在 Task 1 执行。当时三桌面完整验收与移动端验收尚待进行，最终结果见[验收记录](../docs/superpowers/plans/2026-11-05-layered-testing-validation.md)。

## Foundation：已迁移

旧 `FoundationError` / `PanguFoundationErrorTests` 被下列独立 CTest 用例替代；统一前缀 `PanguFoundationUnitTests.`，
标签为 `boundary.unit;area.foundation;owner.engine;run.fast;run.precommit;run.full;platform.<当前平台>`。
默认超时 30 秒，保留直接批量运行可执行程序的能力。

| 原函数或约束 | 新 suite.case | 关键判据 |
| --- | --- | --- |
| `TestErrorPropagation` 的 `MakeError` 比较，及 Error 值语义 | `Error.PreservesDomainCodeAndNativeValue` | Domain、code=42、无原生值、有原生值 Source=7/Value=-17；不同 Domain/原生值不相等 |
| `TestExpectedValues` 的 Result 成功分支 | `Result.SuccessPreservesExactValue` | `has_value()` 且值精确为 17 |
| `PropagateDummy(false)` | `Result.SuccessfulPropagationReturns124` | 检查成功后解引用，值精确为 124 |
| `PropagateDummy(true)` / `ParseDummy(true)` | `Result.FailedPropagationPreservesOriginalError` | 失败；完整原始 `kWithNative` 相等；Source/Value 原样保留 |
| `CheckDummy(false)` | `Status.SuccessHasNoError` | `has_value()`，成功 Status 不进入错误分支 |
| `CheckDummy(true)` | `Status.FailurePreservesOriginalError` | 失败且原始 `kSimple` 相等 |
| 全部原 `static_assert`，包括 `TestExpectedValues()` | 仍在同一 `ErrorTests.cpp`，随目标编译验证 | Domain 的受限构造/零值、Error 布局/拷贝/析构/大小/constexpr、Result/Status 类型别名和 constexpr 成功失败均保留 |

旧布尔函数没有作为一个 `EXPECT_TRUE` 包装；`ParseDummy`、`PropagateDummy`、`CheckDummy` 的触发逻辑保留。

## Module 单元：已迁移

Task 2 迁移前检查：新前缀 `PanguModuleUnitTests.`、`PanguModuleConcurrencyTests.`、
`PanguScopeBuildMockTests.`、`PanguJsonUnitTests.` 尚无入口；使用 `ctest --preset windows -R
'^(PanguModuleUnitTests|PanguModuleConcurrencyTests|PanguScopeBuildMockTests|PanguJsonUnitTests)[.]'
--no-tests=error --output-on-failure` 明确报 `No tests were found` 并失败。
迁移须逐项保留 Identity 非法长度/大写/非 hex/零值、Scope 三道门与 move-only、
所有权与类型错误及 Fake Sink 粘性错误、JSON 显式 UDL 与 discarded 断言；
JSON 编译宏、版本及所有已有布局/constexpr 断言仍通过目标编译验证。

旧 `ModuleIdentity`、`ScopeAccess`、`ScopeBuild` 入口分别由下列逐例测试替代。
`PanguModuleUnitTests.` 包含 Identity、普通 ScopeAccess 与 ScopeBuild，标签为
`boundary.unit;area.module;owner.engine;run.fast;run.precommit;run.full;platform.<当前平台>`。
并发用例前缀为 `PanguModuleConcurrencyTests.`，从 precommit 起执行，并增加 `requires.threads`。
新增 Mock 用例前缀为 `PanguScopeBuildMockTests.`，使用 GoogleMockMain，标签与 Module 普通单元相同。

| 原函数或约束 | 新 suite.case | 关键判据 |
| --- | --- | --- |
| `TestParseAndFormat` | `ModuleIdentity.ParseAndFormat` | 成功且有效；格式精确等于原 32 字符串；首尾 byte=00/ff；重复解析结果相同 |
| `TestInvalidIdentities` | `ModuleIdentity.RejectsInvalidLength`、`RejectsUppercase`、`RejectsNonHexCharacter`、`RejectsZero` | 保留原四个输入，分别匹配 `kInvalidLength`、`kInvalidCharacter`、`kInvalidCharacter`、`kZeroId` |
| `TestRoundTrip` | `ModuleIdentity.RoundTrip` | 原 `0123456789abcdef0123456789abcdef` 解析后格式化完全相等 |
| Identity 全部布局/constexpr 断言 | 原 TU 编译验证 | ModuleId 拷贝/布局/16 字节、ModuleVersion 比较、零值无效与 constexpr 格式化原样保留 |
| `BorrowPinsAndReleasesOnce` | `ScopeAccess.BorrowPinsAndReleasesOnce` | borrow 值 42；三层计数都为 1；move 后重复 Release 仍只释放一次，三层计数归零 |
| `EveryGateRejectsNewBorrowAndPreservesOldBorrow` | `ScopeAccess.EveryGateRejectsNewBorrowAndPreservesOldBorrow` | 保留 Module/Scope/Object 三种关闭；新 borrow 精确 `kNotReady`；旧值 42 与计数 1 保留；释放后三层归零，清空 Object 后仍拒绝 |
| `RejectsEmptyAndWrongType` | `ScopeAccess.RejectsEmptyAndWrongType` | 空 token 为 `kNotReady`，错误类型为 `kTypeMismatch` |
| `AcquisitionAndCloseAreLinearized` | `ScopeAccessConcurrency.AcquisitionAndCloseAreLinearized` | 保留 200 轮、start/closed 先后关系与 reader；关闭后不得新借用；线程只收集 failed，join 后检查 failed 和三层计数归零 |
| `FactoryOwnsMoveOnlyCapture` 及 move-only/handle 静态断言 | `ScopeAccess.FactoryOwnsMoveOnlyCapture` 与原 TU 编译验证 | 捕获 unique_ptr 后原配置为空；factory 可移动且移出为空；ScopeFactory/ScopeBorrow 不可拷贝且 nothrow move，默认 handle 无效 |
| `OwnershipPrecedesStartAndTokensAreInitiallyClosed` | `ScopeBuild.OwnershipPrecedesStartAndTokensAreInitiallyClosed` | Own 转移所有权且未启动/未析构；初始不可借用；Scope slot=2；显式 Start 一次、打开三道门后值 17；关闭并销毁一次，旧 token 不可借用 |
| `InvalidOwnershipAndTypeErrorsAreSticky` | `ScopeBuild.InvalidOwnershipAndTypeErrorsAreSticky` | 原 Fake Sink 保留：空 Own 后下次合法 Own 同错并销毁对象；不成对 hooks 拒绝并销毁；double 依赖类型错误锁存到后续 int 依赖；未声明依赖精确报错 |
| `FactoryIsReusableAndCreatesSeparateObjects` | `ScopeBuild.FactoryIsReusableAndCreatesSeparateObjects` | 同一个捕获 config=33 的 move-only factory 调用两次；两个 Sink 都拥有对象且地址不同，started=0 |
| `NullFunctionPointerIsAnEmptyFactory` | `ScopeBuild.NullFunctionPointerIsAnEmptyFactory` | null noexcept 函数指针创建的 factory 为空 |
| 新增真实 seam 交互 | `ScopeBuildMock.InvalidHooksRejectBeforeStage` | 现有 `ScopeBuildSink` 的 StrictMock；只设置 RequestStop；Reject 恰一次接收/返回 `kInvalidDeclaration`，Stage 零次，Own 返回同错；未修改生产接口 |

所有 expected 在解引用或读取 error 前先 ASSERT 检查；旧布尔整例未包成单个 EXPECT。
普通及并发测试共用 Module 私有 `Fixtures/ScopeAccessFixture.h`，每例本地构造独立实例，无跨例静态状态。

## Module 集成：已迁移

下表保留迁移前的场景清单。`StaticModuleHostTests.cpp`、`RegistrationTests.cpp`、`ScopeLifecycleTests.cpp`
的原行为函数分别映射到 `PanguModuleIntegrationTests.StaticModuleHost.<原函数名>`、
`PanguModuleIntegrationTests.Registration.<原函数名>`、`PanguModuleIntegrationTests.ScopeLifecycle.<原函数名>`。
唯一拆分是 `ModuleDependencyKeepsProviderWorkAliveDuringBusyCleanup`：直接/传递依赖分别独立登记为
`ScopeLifecycle.DirectModuleDependencyKeepsProviderWorkAliveDuringBusyCleanup` 与
`ScopeLifecycle.TransitiveModuleDependencyKeepsProviderWorkAliveDuringBusyCleanup`；共 34 例。
原布尔整例及自定义 main 已移除，短路条件按原顺序展开为逐项断言，回调和真实管理调用保持。

以上用例为 `boundary.integration;area.module;owner.engine;run.fast;run.precommit;run.full;platform.<当前平台>`。
Catalog 独立入口为 `PanguStaticCatalogTests.StaticCatalog.RetainsArchiveRootsAndRunsRealFactories`，
析构父入口仍为 `ScopeDestructor`；二者从 precommit 起运行，后者增加 `requires.subprocess`。

| 旧入口 / 文件 | 原函数清单 |
| --- | --- |
| StaticModuleHost / `StaticModuleHostTests.cpp` | `CopiesMetadataBeforeSourceDestruction`；`RejectsInvalidDefinitionsAndIdentity`；`EnforcesLimitsBeforeCopying`；`ValidatesDependencies`；`OptionalSelectionIsExplicitAndRejectsCycles`；`NestedQueryCannotPolluteOuterReport` |
| ModuleRegistration / `RegistrationTests.cpp` | `RegistersOnceInDependencyOrderAndPublishesAtomically`；`RollsBackAndPreservesOriginalErrorAndProgress`；`IgnoredDeclarationFailureCannotCommit`；`RejectsInvalidAndOversizedDeclarations`；`ConcreteScopesOwnIndependentCopies`；`RejectsCrossHostScopeReentry`；`CrossScopeNestedReportBelongsToOuterOperation` |
| ScopeLifecycle / `ScopeLifecycleTests.cpp` | `ScopeFailureIsLocalAndFactoryRepeats`；`BorrowAndWorkDelayDestructionIndependently`；`PartialStartFailureStaysCleanupOnlyAndReloadUsesNewTokens`；`DependenciesPreserveParentAndReverseDestruction`；`WrongDependencyTypeCannotCommit`；`HandlesAndMovingHostPreserveOwnership`；`ManagementReentryAndAliasedReportDoNotCorruptOuterOperation`；`FailedScopeWithoutHandleRemainsTrackedUntilWorkStops`；`RejectsCombinedAndLocalCyclesBeforeFactories`；`ScopeBudgetIsCheckedBeforeCallbacksAndSlotsCanBeReused` |
| ScopeLifecycle（续） | `ModuleDependencyKeepsProviderWorkAliveDuringBusyCleanup`（包含 `ProviderWorkSurvivesBusyCleanup` 的直接/间接依赖两种参数）；`ShutdownRetainsUnreturnedScopeFailure`；`FactoryRetirementBlocksSharedDeclarationReentry`；`FailedRegistrationRetiresFactoriesBeforeReporting`；`ReportSeparatesOriginalFailureFromCleanupResult`；`ActivationReportSeparatesCleanupError`；`ShutdownCauseSurvivesRetiringFailedScopeAcrossPolls`；`ShutdownDoesNotImportCompletedHistoricalFailure`；`InactiveDeclarationCycleDoesNotBlockUnrelatedActiveScope`；`FactoryMustOwnExactlyOneInactiveObject` |
| ScopeDestructor / `ScopeDestructorTests.cmake` + 原 `DestroyBusy` | 迁至独立 `PanguScopeDestructorFixture <marker>`，父脚本用目标路径传入 BuildRoot 内的 marker；已有 borrow 存活时析构 Host 触发 terminate；退出码精确为 91；存在 `.ready`，对象析构 marker 不存在；子进程超时仍为 15 秒 |
| StaticModuleCatalog / `StaticCatalogTests.cpp` | 真实 First/Second 两座 STATIC 库；生成 Catalog 保留两个 Query 引用根；依赖顺序 Second→First；声明 Id/Owner/DisplayName；真实工厂创建两个独立 Scope，关闭及 shutdown 清空声明 |

迁移逐项保留的参数表和循环如下：

| 位置 | 全部原触发条件与断言 |
| --- | --- |
| Identity 非法输入 | 长度不足、大写、非 hex、全零分别匹配 `kInvalidLength`、`kInvalidCharacter`、`kZeroId`；大小写不得放宽 |
| StaticHost `kWrongVersions` | `(0,2,3)`、`(1,0,3)`、`(1,2,0)`，每项 `kVersionMismatch` |
| StaticHost 依赖参数 | 零 Id、自依赖、非法 Kind=255、重复依赖；缺失提供方及 report 的 Module/RelatedModule；超预算 257 项 |
| StaticHost 可选绑定 | 未选/缺失/版本不符原因；显式选中后的依赖顺序；不存在的 dependent/dependency、反向非法 binding、重复 binding；仅显式选择时出现的环 |
| Registration 忽略错误 | `existing=false/true` 两种分支都不能 commit；保留原始 Error 与 progress |
| Registration 非法声明回调表 | 全部三个 callback，包括超额声明的 1…257 循环；失败回滚不发布半成品 |
| Registration 跨 Host / 嵌套 report | `fail=false/true`、`ready=false/true` 分别验证外层状态/report 不被内层污染 |
| ScopeAccess gate / race | Module、Scope、Object 三种 gate 关闭；已有 borrow 保留且新借用拒绝；200 轮 acquisition/close 并发，关闭后无新借用且三层计数回零 |
| ScopeLifecycle 容量 | 255 个 context slots 后拒绝且不进入 callback；释放后 slot 可重用 |
| ScopeLifecycle 注册失败参数表 | callback 返回 `kFailure`；忽略重复声明后返回成功但锁存 `kDeclarationConflict`；两项都先退休 factory，再发布原始错误/cleanup progress |
| ScopeLifecycle report 参数 | `mode=0/1/2` 的原始失败与 cleanup result 分离；不导入已经完成的历史失败 |
| ScopeLifecycle owning 参数表 | 工厂不 Own 对象；工厂 Own 两个对象并忽略第二次错误；均 `kInvalidDeclaration`、未 Start、全部已构造对象被销毁、导出的 token 不可 borrow |
| Catalog 编译/链接 | 保留两个真实 STATIC 目标、Catalog 生成、局部 `CXX_SCAN_FOR_MODULES=OFF`、`-ffunction-sections -fdata-sections` 及 Windows `/OPT:REF`、Linux `--gc-sections`、macOS `-dead_strip`；不能换成普通源文件或 whole-archive |

## JSON：已迁移；其它第三方与 Toolchain 保留并分类

旧 `JsonExtras` / `PanguJsonExtrasTests` 替换为 `PanguJsonUnitTests.` 前缀的三个独立 CTest 用例，
标签为 `boundary.unit;area.thirdparty;owner.engine;run.fast;run.precommit;run.full;platform.<当前平台>`。

| 原 `Run` 场景或约束 | 新 suite.case / 编译入口 | 关键判据 |
| --- | --- | --- |
| JSON 配置与版本 | 原 TU 编译验证 | 保留 include 前 `JSON_USE_GLOBAL_UDLS=0` 编译检查与 JSON 3.12.0 static_assert |
| 显式 literals 和 pointer | `Json.ExplicitLiteralsAndPointer` | 只在局部引入 `nlohmann::literals`；原 fixture 的 `/value` 精确为 9 |
| 正常无异常解析 | `Json.ParsesObjectWithoutExceptions` | 原 JSON 输入解析不 discarded，确为 object 且含 target；triple 原样匹配 `x86_64-pc-windows-msvc`，count=3 |
| 非法无异常解析 | `Json.InvalidInputIsDiscarded` | 原输入 `{`、allow_exceptions=false，结果 discarded |

以下脚本和真实编译/链接探针仍保留：

| 入口 / 目标 | 原断言与后续映射边界 |
| --- | --- |
| NlohmannSubmoduleRequired / `MissingHeaders.cmake` | 复制适配层但缺少 Repo 头文件时配置明确失败，不在 configure 联网 |
| ThirdPartyCompiledProbe / PanguToolchainProbe | 两个真实 STATIC 编译探针；相同平台工具链，ThirdParty 不链接 PanguBuildOptions，Pangu 目标沿用自有策略 |
| ToolchainLibraryLink / `LinkProbe.cpp` | 两座探针库进入桌面最终链接并真实运行；不能以 STATIC 归档代替最终链接 |
| Toolchain LinkSmoke | Android/iOS 显式最终链接入口保留，Task 1 未运行，不算设备覆盖 |

## 根 CMake 脚本：保留原契约并统一分类

| 旧 CTest 名称 | 脚本断言清单（全部仍在原脚本） |
| --- | --- |
| BuildEnvironment | 未设置 LLVM_ROOT、ANDROID_HOME 时子进程失败，保留 Pangu 专用诊断 |
| SourcePluginBuildTool | 空插件根；首次配置构建独立宿主工具；无变化 Ninja 不做工作、warm configure 成功；Linux 选定 libc++ 头/链接而非 libstdc++ |
| SourcePluginBuildToolInvalidation | manifest 内容非法、缺少 CMakeLists 的原生增量失败与修复恢复；拆分后的两个链接入口保留原触发条件 |
| SourcePluginBuildToolFileLinks | manifest 文件 symlink 删除/恢复、预先创建目标的直接改指、中间文件链接改指/删除/恢复、未启用 Plugin 的 CMakeLists 多级链接越界与恢复；私有源码变化仍不触发 configure；缺权限明确失败 |
| SourcePluginBuildToolDirectoryLinks | Windows 真实 Junction / Linux、macOS 真实目录 symlink；插件根入口、Engine 根参数祖先入口的多级改指、失败诊断与恢复；新增 Plugin 仍要求显式 configure |
| SourcePluginEngineRevision | `ExpectCheck` 对全部既有 Git revision/锁文件/local override 参数表判断接受或拒绝，并匹配对应诊断 |
| SourcePluginProject | `Enabled(ON,OFF)` 仅 MyGameProbe；`Disabled(OFF,OFF)` 无测试；`EngineOptIn(ON,ON)` 显式构建 PanguTestsFull 后从 JSON 检查实际 Engine 用例及 owner.engine，不接受 NOT_BUILT；默认归属按实际名称逐项核对，无固定总数假设；父 MyGame 拥有顶层 project；修改 Engine.commit 后原生 build 重新校验锁 |
| SourcePluginBuildToolSourceChange（Windows） | 工具源码变动触发重配置及可执行程序时间变化；工具失败阻止 configure；修复后重试恢复 |
| SourcePluginEnable | 仅显式启用 Plugin 执行、按 Id 排序；未消费 Module 不进入 ALL；Probe 只构建传递依赖；显式 target 可独立构建；既有非法启用/定义配置拒绝路径保留 |
| SourcePluginStaticCatalog | MyGame/MyEditor 各自 Catalog、真实执行及依赖顺序；不构建另一 Host 的无关库；assets-only/仅启用但未消费 Plugin 不构建；删除必需 manifest 后 build 失败 |
| SourcePluginSharedDeploy | 未消费 SHARED 不构建/部署；显式部署产物存在且非空、SHA256 一致；PE/ELF/Mach-O magic；产物名不含平台名；无变化部署不重写 |
| SourcePluginIncremental | 既有生产源码与配置输入变更、失败后修复重试；禁用 Plugin 的 CMake 不得执行；新建未启用 Plugin 不应执行；宿主工具变动后必须重建 |

`PluginScaleTests.cmake` 与 `CrossCompileTests.cmake` 是保留的显式专项，不属于上述根 22 项统计。
后者仍覆盖 Android/iOS 独立 Project 的 Game/Editor 最终链接、SHARED 部署及宿主工具运行；Task 1 未执行这些专项。

## 独立宿主工具：迁移前场景清单

| 旧入口 / 函数或脚本 | 参数表与关键断言 |
| --- | --- |
| PluginManifest / `Check` | `valid`、`minimal`、`missing-id`、`uppercase-id`、`empty-segment`、`bad-hyphen`、`leading-zero-version`、`leading-zero-prerelease`、`unknown-key`、`unknown-section`、`missing-section`、`wrong-id-type`、`wrong-name-type`、`wrong-version-type`、`damaged-toml`、`valid-build-zero`、`valid-prerelease-zero`、`missing-patch`、`extra-version-part`、`empty-prerelease`、`empty-build`、`bad-build`、`bad-id-digit`；逐项 shouldPass 与失败诊断完整路径；有效项精确 Id/中文 Name/SemVer、minimal 默认空字段；缺失输入必须带完整路径 |
| PluginDiscovery / `AddPlugin`、`ExpectFail` | Engine+Project 双根排序，忽略普通文件/无描述目录；缺失根拒绝、存在空根返回空；同实际根拒绝；未启用重复 Id 也拒绝；不同根同目录名允许；缺 manifest/缺 CMake/目录代文件/未启用非法 manifest 均拒绝；符号链接越界拒绝，不能建链接时旧程序显式输出 skipped |
| PluginCli / `PluginCliTests.cmake` | JSON array=2、按 Id 排序、中文/引号/反斜线与可选字段、SemVer、source/manifest 路径存在；configure_inputs 为数组且至少两项、每项绝对且存在；空根精确输出 `{"plugins":[]}`；`UnicodeRoot` 与 `引擎😀` 两种原生路径及 `插件😀` 诊断往返；未启用重复 Id 与未知子命令非零、stdout 空、stderr 有诊断 |

三项继续使用工具的无异常、保留 RTTI 契约，不继承 Runtime 的 PanguBuildOptions；集中迁移与根调度已由 Task 4 完成，入口为 `BuildTools.PanguBuildTool.{PluginManifest,PluginDiscovery,PluginCli}`。

## Task 1 新接线验收

- 新回归先在接口不存在时失败，两个 fixture 的 Clang 检测均成功；原因是缺少依赖适配与登记接口，不是工具链或路径误配。
- `TestingRegistration` 检查两例真实发现、稳定名称、累计层级、标签、默认超时、每例 SHA256 工作目录、每例 XML、重复运行/并行、无用例程序明确失败，以及重复登记/非法 Tier/缺 OWNER 诊断。普通命令测试另检查 project/plugin 归属与最低层级的累计依赖。
- `TestingDependencyIsolation` 检查仅 include 无目标、关闭上下文、父 BUILD_SHARED_LIBS 不变、缺源码拒绝、同图 ON/OFF 一致请求与冲突、未知父目标冲突。只读核对固定 submodule SHA，配置期间不联网。
- Windows 使用根 preset 构建 Foundation 与四个框架 STATIC 库；Linux/macOS 从正式共享源码挂载目录构建、链接并运行相同聚焦测试。macOS 旧缓存源码身份检查失败后在已确认空闲的 APFS BuildRoot 使用 `--fresh` 重新配置。
- `BUILD_TESTING=OFF` 的隔离 Engine 配置不产生 GTest/GMock 或普通测试目标；移动端仍沿用现有跳过可运行目标策略。

本任务实现工作区（父提交 `5e897db`）的实际结果：Windows 正式完整构建与根 CTest 29/29 通过（134.74 秒）；
新接线及 Foundation 八项以 `--repeat until-fail:2 --parallel 4` 重复通过（6.40 秒），直接运行 Foundation 为 6/6。
最终 Linux/macOS 聚焦构建与八项测试分别 8/8（3.15 秒）、8/8（11.36 秒）；它们不代表旧测试完整矩阵已验收。
迁移源码和注册夹具通过仓库 clang-format/clang-tidy；tidy 没有可见的自有代码诊断，仍报告被抑制的非用户代码 warning 汇总。

此处记录 Task 1 的验证边界；后续任务仍需完成所有旧入口的分类、迁移、正式 full 前提、三桌面完整正确性矩阵和移动端专项。

## Task 2 迁移验收

本任务基于 `51a358b` 的工作区版本。Windows / Clang 23.1.0 正式根 preset 配置、四个新目标编译及最终链接成功；
新前缀定向 19/19 通过，并以 `--repeat until-fail:2` 两轮通过（1.35 秒）。
四程序直接 `--gtest_shuffle --gtest_repeat=3` 各轮均通过（分别 14、1、1、3 例），没有跨例状态残留。
六个自有 TU 通过仓库 clang-format；真实根编译数据库下 clang-tidy 无可见自有诊断，仍输出非用户代码 warning 抑制汇总。

Windows 完整构建成功；根 CTest 首轮 43/44，130.84 秒，唯一失败是 SourcePluginProject 仍按旧 ModuleIdentity 名字检查 EngineOptIn。
修复为先显式构建 PanguModuleUnitTests，再检查实际发现的明确新入口和 owner.engine，拒绝 NOT_BUILT 占位；
默认关闭 Engine 测试、关闭 BUILD_TESTING、顶层 Project 所有权及 Engine.commit 重验证契约保留。
该专项在 Windows/Linux/macOS 分别 1/1 通过（7.26 / 36.66 / 109.97 秒）。
修复后重新完成 Windows 当前版本完整构建与根 CTest：44/44 通过，143.16 秒，无失败/跳过；首轮失败与复验结果分别记录。

Linux / Clang 23.1.0 与 macOS / Clang 23.1.2 使用规定的共享源码挂载和独立本地 BuildRoot，
四个新程序编译、最终链接与定向运行分别 19/19（0.21 秒）、19/19（1.90 秒）通过；补齐 Mock 直接头包含后，该例两平台重编译与 1/1 重跑均通过。
这些定向结果不替代后续三桌面完整正确性矩阵；本任务未执行移动端专项，不声称设备运行覆盖。

## Task 3 迁移验收

基于 `b51ffec`。迁移前新集成前缀无入口，CTest 明确以 exit 8 失败；旧 Catalog/析构 2/2 通过。
迁移后 Windows 首轮定向 36/36；临时将析构父脚本预期退出码从 91 改为 92，精确观察实际 91 后失败，恢复后通过。
原三个布尔程序由 34 个具名集成用例替代，Catalog 为独立 1 例，析构保留父脚本和独立夹具。

Windows 初轮完整构建与根 CTest 为 75/75（138.12 秒）。自审补齐每例回调状态复位和析构 Work 路径后，
曾因守卫变量与原参数表重名发生编译失败；仅修正测试局部变量名，未改原回调或判据。
最终源码重新完成 Windows 正式完整构建、34 例随机顺序直接运行三轮、根 CTest 75/75（148.05 秒）以及定向 36/36（1.25 秒），无失败/跳过。
Linux/macOS 从规定的共享源码目录和隔离宿主 BuildRoot 重编译、最终链接与定向运行均为 36/36（0.32 / 1.75 秒）。
五个消费 TU 在真实编译数据库下 clang-tidy 无可见自有代码诊断，六个 C++ 源码/头文件通过仓库 clang-format。

三平台实际链接命令均保留两个独立 STATIC 归档和生成 Catalog 对象，分别使用 `/OPT:REF`、`--gc-sections`、`-dead_strip`；
Catalog 编译仍带 function/data sections，无 whole-archive。CTest 元数据核对 34 例 fast 集成、Catalog/析构 precommit、
默认 integration 超时 60 秒、析构 requires.subprocess 和独立 Work 目录；析构子进程仍为 15 秒，34 例 XML 均独立产生。
上述为 Task 3 范围的实际证据；当时 Linux/macOS 完整正确性矩阵留待 Task 9，移动端专项未在本任务执行。最终结果见[验收记录](../docs/superpowers/plans/2026-11-05-layered-testing-validation.md)。

## Task 5 分类、隔离与资源预算

所有 Engine 原生/脚本入口使用统一登记接口；`TestingScriptClassification` 从实际 CTest JSON 逐项检查
boundary、area、owner.engine、run.full、分层和工作目录，不以固定总数代表迁移完整性。

| 入口 | 边界 / 领域 / 最低层级 | 构建与隔离 |
| --- | --- | --- |
| BuildEnvironment | system / toolchain / precommit | 子进程检查原环境诊断；独立 Tests/Work 工作目录 |
| ToolchainLibraryLink | integration / toolchain / precommit | 原最小链接目标及两个真实 STATIC 库；生产链接依赖不变 |
| ThirdPartyCompiledProbe / PanguToolchainProbe | full 编译依赖 | 交叉构建保留编译验证，不登记设备运行 |
| NlohmannSubmoduleRequired | system / thirdparty / full | 缺失任一上游头的独立配置失败；保留离线诊断和随机夹具 |
| SourcePlugin* | system / plugins / full | 每测试名独立 Tests/Work 根；既有随机夹具保留；文件/目录链接能力分别可定位 |
| Testing* | system / testing / full | 每测试名独立根；保留注册、依赖隔离、多工具组及失败传播契约 |

`PANGU_TEST_BUILD_JOBS` 为正整数，默认 2。脚本以 `cmake -E env` 将同值传给
`CMAKE_BUILD_PARALLEL_LEVEL`，覆盖 configure 内 bootstrap 的子构建；显式构建同样传该预算。
外层 CTest PROCESSORS 与内层构建、BuildTools 内层 CTest 一致；包含嵌套工具准备的聚合构建外层串行，
避免两层并行相乘。原固定 `--parallel 8` 已由预算替换。通用 BuildTools 回归另以预算 1 实际构建并执行两个独立工具组。

分类回归还先用错误 Engine 路径生成输入并观察配置失败，再在同一工作根以正确 Engine 重跑，
验证输入重新生成与恢复；所有原插件脚本的失败修复序列保留。真实子 configure 将继承的预算写到 BuildRoot 内，父测试核对该值。
Windows 文件 symlink 若权限不足，正式 FileLinks 测试明确失败，不能以目录 Junction 通过替代文件链接验收。

### Task 5 实际验收

基于 `9f67159` 工作区。分类回归先在旧登记下由 BuildEnvironment 缺少元数据明确报红；
统一接线后，Windows 完整原生构建和聚合准备成功，根完整 CTest 为 82/83（156.68 秒），
唯一失败为 `SourcePluginBuildToolFileLinks` 缺少实际文件 symlink 权限，CMake 与原生 CreateSymbolicLinkW 均确认权限错误 1314；
无跳过，不把真实 Junction 通过等同于文件 symlink 验收。其余原脚本、CLI、插件增量/失败修复/部署通过。
Linux/macOS 使用正式共享源码和宿主本地 BuildRoot，完成全部正式测试构建准备、Toolchain 两库与最终链接；
本任务定向运行分别 21/21（188.57 / 661.92 秒），包含文件 symlink、多级文件改指、目录链接及独立 Discovery 场景，无失败/跳过。
自审补强后的分类测试在 Windows/Linux 定向复验分别 1/1（1.50 / 0.55 秒）；macOS 的上述 21 项运行包含该最终分类断言。
没有修改 C++，未执行无关 clang-format/clang-tidy；CMake 经三宿主真实配置/运行验证，暂存差异通过 `git diff --check`。
当时 Windows 文件 symlink 关键权限验收尚未完成；Linux/macOS 的根完整原生运行矩阵与移动端专项留待 Task 9。最终结果见[验收记录](../docs/superpowers/plans/2026-11-05-layered-testing-validation.md)。

提交前预算末次搜查补齐了失败传播回归自身两处原固定内层 `PANGU_BUILD_TOOLS_TEST_JOBS=2`，
改为同一预算变量；默认行为不变。仅该受影响回归在 Windows/Linux/macOS 各复验 1/1（1.04 / 0.33 / 1.99 秒），
并在 Windows 的独立 Tests/Work 根直接以预算 1 执行正式失败传播脚本，核对 pass/fail/skip/empty/NOT_BUILT 原判据。

### Task 5 fix1：嵌入 Project 的分类范围

分类回归现在以 Engine 的 `PROJECT_BINARY_DIR` 查询其 CTest 子树，顶层 `CMAKE_BINARY_DIR` 仍决定产物与工作根。
Project 自有 `MyGameProbe` 不进入 Engine 分类图；Engine 子树内每项仍完整检查 owner、boundary、area、累计层级、预算及隔离，
没有按已有 `owner.engine` 标签过滤。`SourcePluginProject` 在同次生成的 EngineOptIn 工程内实际执行分类入口，
并在 Engine 子树注入无标签 `UnclassifiedEngineProbe`，要求其明确失败；关闭注入后再次要求通过。
版本化夹具保留完整创建、配置、构建和恢复过程，不依赖预存 fixture，不递归运行整套 Engine 系统测试。

### Task 6：Project / Plugin 测试上下文

发现工具在扫描 Engine / Project 参数时保存 `PluginOrigin`，JSON 的 `origin` 只允许 `engine` / `project`。
该字段不来自 Manifest、路径前缀或当前 CMake 调用栈。`pangu_add_enabled_plugins` 保持原接口，
在函数内为各已启用 Plugin 设置局部上下文：Engine 跟随 `PANGU_BUILD_ENGINE_TESTS`，Project 跟随调用方显式的 Project 测试策略。
MyGame 在加入 Engine 后设置 Project 上下文；子目录 Plugin ID 和 owner 不泄漏给 Project 自有测试。

`SourcePluginTestOwnership` 验证未启用、仅 Provider、Engine 测试关闭/开启、全局测试关闭及非法来源诊断。
Physics / Gameplay 的两个 unit 用例链接各自真实 Module，Project 的 integration 用例用两个真实 Query 创建 Host，
验证 Provider 优先顺序并实际注册。它们均为 `area.build` / `fast`，Plugin 用例带自身 Plugin ID；
Project 协作用例只有 `owner.project`。原 `MyGameProbe` 保留零 Plugin 行为，按 `integration/build/precommit` 显式登记。
矩阵实际验证普通 `MyGameProbe` 不编译未消费 Module 或测试，聚合 `PanguTestsFast/Precommit/Full` 按依赖拉入生产 Module 与测试，
并通过运行行为和 CTest JSON 标签验收，保留 `EXCLUDE_FROM_ALL` 语义和普通 no-op 构建。

本阶段 Windows 执行 Project / Plugin 矩阵和现有系统回归；Linux/macOS 仅重建、运行改动的宿主 Discovery / CLI 路径。
其它桌面的完整 ownership 矩阵随 Task 9 当前源码 full 统一执行，本阶段不宣称已完成该范围；最终结果见[验收记录](../docs/superpowers/plans/2026-11-05-layered-testing-validation.md)。

## Task 7：分层入口

桌面 fast、precommit、full workflow 依次配置、准备累计构建依赖并运行对应测试；空筛选失败。
当时 Engine 正式 full 校验三个测试开关，Project 自有 full 不强制修改父工程开关。后续已精简为两个开关，见[本次验收](../docs/superpowers/plans/2026-11-06-simplify-testing-options-validation.md)。直接 test preset 前须先执行同集合 build preset。
当时三桌面完整矩阵及移动端专项留待后续验收；最终结果见[验收记录](../docs/superpowers/plans/2026-11-05-layered-testing-validation.md)。

Task 9 的三桌面完整矩阵及 Android/iOS 编译、最终链接和部署等对应层次已验收，结果见[验收记录](../docs/superpowers/plans/2026-11-05-layered-testing-validation.md)；移动端未进行设备运行。整分支评审、限定复评及本计划临时目录清理均已完成，四项非阻断维护项已归档。此前各任务的局部结果不合并冒充三桌面完整通过。
