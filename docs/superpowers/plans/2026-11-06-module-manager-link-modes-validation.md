# 统一链接模式与 ModuleManager 正式验收

> 状态：Completed（2026-11-07）。实现、逐任务评审、正式五平台矩阵、唯一全分支 Review 与一次统一修复的限定复评均已完成；I1/M1–M5 全部关闭，无新增问题。14条裁决已永久归档，本计划临时上下文已清理，原始验收产物和正式 Tests 保留。
> 实现基线：`b891f35aeec2a601ac3289d1501d555ed4f34609`。规范先提交为 `0c56cc237b209bc0ddebb7c4b3677671a28a65d6` 后完整 Git/子模块工作树干净，才启动正式矩阵。
> 契约：[ADR-0015](../../../wiki/decisions/0015-unified-link-modes-module-manager.md)、[批准规格](../specs/2026-11-06-module-manager-link-modes-design.md)、[实施计划](2026-11-06-module-manager-link-modes.md)、[覆盖迁移](../../../Tests/MIGRATION.md)。

## 提交与任务评审的永久结论

| Task | 实现/修复提交 | 任务评审与证据范围 |
| --- | --- | --- |
| 1 模式/导出/纯协议头 | `78f63ca` | Spec/质量通过，Critical/Important 0；真实 Modular StaticCatalog 初轮因类型地址比较失败，明确由 Task 2 接续，不删断言，不将此阶段称全绿 |
| 2 RTTI/常驻控制块 | `e304b55` | Spec/质量通过，Critical/Important 0；Task 1 原 StaticCatalog 断言已通过，Windows Modular Module58/58、Mono定向17/17；物理卸载后私有类型凭据由Task6接续 |
| 3 原生Loader | `4264fec` | Spec/质量通过，Critical/Important/Minor 0；Windows原生11/11及独立Modular入口11/11；其它宿主交Task8，未冒充跨平台完成 |
| 4 统一Manager/根 | `650d3a5` → `147c816` | 首轮三个Important，经一轮修复与限定复评全部关闭，无修复新增Critical/Important；最终Windows定向Modular21/21、Mono31/31。风格Minor延期 |
| 5 Pin/失败清理 | `d233c79` → `7ef2ac2` | 首轮一个Important，经一轮修复与限定复评关闭；最终Windows受影响Modular14/14、Mono30/30，无修复新增Critical/Important。尾逗号Minor延期 |
| 6 真实Scope/凭据 | `b111386` | Spec/质量通过，Critical/Important 0；实际新生产行为首轮已正确，无生产Runtime修改。Modular直接63/63、独立Mono75/75，父ModuleRegression首次沙箱错误后提升权限重试1/1；顺序检测力Minor延期 |
| 7 Project/移动链接 | `b891f35` | Spec/质量通过，Critical/Important 0；干净当前SHA/override OFF的Windows Project6脚本与移动四项最终链接均0；正式根全面矩阵由Task8接续，Android纯协议头既有3诊断保留 |

上述各阶段数量属于不同提交/配置/轮次，不能相加成最终版本总数。所有正式 Tests 长期保留，旧有效 Mono 场景和动态对应关系逐项记于 Tests/MIGRATION；只有 LinkedStaticModules/混合承载契约已正式废弃。

### Task 4/5 缺陷、修复与复评依据

Task4 I1：常驻 Consumer 依赖业务 Provider 时，原实现强制把常驻一律推迟，整体 Shutdown 自锁。修复允许必要的常驻 Consumer 按直接/传递实际资源DAG先排空，记录仍最后统一确认；外部Pin/接纳中计数未归零不得解除Provider有效边。正式回归 `ResidentConsumerDrainsBeforeBusinessProviderAndConfirmsLast`、`IndirectResidentConsumerDrainsBeforeBusinessProviderAndConfirmsLast`、独立常驻Busy覆盖。旧实现有效建立场景后子进程退出91；不是准备失败退出90。原始 `Build/Windows/task4-r1-red-mono-test.log` 与 `task4-r1-green-mono-test.log`。

Task4 I2：PollCleanup 先回收未交付失败Scope后才取原错，报告丢失OriginalError。修复在逐记录清理前拥有型快照PendingFailure，不把已完成历史失败永久导入新Shutdown。正式 `ModuleUnloadPollPreservesUnreturnedScopeFailureOnlyUntilCompletion` 真正销毁旧report后继续清理；RED为OriginalError/PrimaryError nullopt。Task4 I3：真实原生Release中的同report重入把外层成功报告污染成Busy；修复在外部代码返回后整体发布本地完整值。`BatchLoad.NativeUnloadReentryPreservesCompletedReport` 覆盖普通卸载和注册失败带原错卸载；原生DLL析构观察器触发重入，RED为Module/Handle/PrimaryError/OriginalError错误。`task4-r1-red-modular-test.log` 与 `task4-r1-green-modular-test.log` 保留真实证据。

Task4修复精确验证命令：`cmake --build Build/Windows --target PanguModuleIntegrationTests PanguNativeModuleLibraryTests`；`ctest --preset windows-precommit -R 'BatchLoad|ResidentModule|NativeModuleLibrary' --no-tests=error --output-on-failure`（21/21）；独立Mono根 `Build/Windows/Tests/Work/ModuleRegression/059f3540ef3a/Monolithic` 构建PanguModuleIntegrationTests后，以 `ctest --test-dir <该根> -R 'ScopeLifecycle|ResidentModule' --no-tests=error --output-on-failure`（31/31）。限定复评判I1/I2/I3 ADDRESSED且无新破坏，保留身份/资源/原生释放屏障及最终常驻确认约束。

Task5 I1：失败批次原生释放未完成时，CreateScope/Activate内部回滚直接调用Runtime Cleanup，绕过Manager的潜在Provider屏障，可提前Stop/Destroy已Closing旧Scope并回收槽。修复在共同Cleanup的首次Scope退休/Stop/Poll/Destroy/Module退休前检查同一屏障；成功CreateScope及既有Active业务闸门不受影响。所有失败组真实Release成功后才解除，失败继续保留整批占位/剩余持有。

Task5正式回归 `ModuleUnload.FailedBatchProtectsClosingScopeFromCreateScopeFailureCleanup`（Factory/Start两分支）与 `ModuleUnload.FailedBatchProtectsEmptyScopeFromActivateFailureCleanup`，覆盖旧A Closing、新B失败、C仍可Borrow/Provider可Pin、空A记录保留、真实释放后继续清理。`ctest --preset windows-precommit -R 'FailedBatchProtects' --no-tests=error --output-on-failure` 的原始RED0/2、GREEN2/2；日志 `Build/Windows/task5-r1-{red,green}-test.log`。最终 `task5-r1-final-modular-test.log`14/14、`task5-r1-monolithic-test.log`30/30。限定复评I1 ADDRESSED，无新Critical/Important；新增尾逗号并入延期Minor。

Task5自审还实际复现常驻资源已排空但外部Pin尚在时，第二次Poll提前Destroy/NativeUnload Provider；收紧有效边解除条件后 `DrainedResidentWithPin` 回归通过。`Build/Windows/task5-resident-pin-{red,green}-test.log`。这不是Task6的生产修复。Task6 Busy子进程先前把Windows进程退出DLL回调混入Manager阶段，改为进入terminate前NativeUnload快照0且精确退出91，准备错误2–12不能冒充成功；真实动态寿命首轮已GREEN。

## 执行裁决、原因与错误代价

以下逐条永久保留控制器账本的所有Ruling，临时目录清理后仍可审计；不是新增架构决策。

1. 保留既有专用计划分支与固定共享源码工作区，不另建 worktree — 用户 AGENTS 与规格要求 Windows/WSL/macOS 共用固定源码路径和正式 preset，原树干净且不在 main/master — 若出现并行用户编辑需保护并重新评估隔离。

2. 本次明确执行请求替代计划 Status 中“执行方法待用户选择/不启动实施”的规划历史说明 — 用户已选 SDD — 无实现范围变化。

3. 需要执行既有构建、Git 提交、SDD Bash 脚本时可按需请求 sandbox escalation — 沙箱拒绝 MSYS IPC 与构建工具读取，首次自动审批已准许 — 仅本计划本地操作，无 push/merge。

4. Task 1 的 Modular StaticCatalog 完整 green 延至 Task 2 RTTI 交付后关闭，Task 1 保留原断言与失败证据，同时验收 Modular 其余定向和 Monolithic 完整 Module — 首次真实 DLL 正好暴露 Task 2 已锁定要替换的类型地址判等，提前复制 RTTI 修改会跨任务重复 — 若根因不止类型令牌，Task 2 须修复导出/模式接缝且不得以此豁免最终验收。Task 1 reviewer 仍独立评估此跨任务依赖。

5. Task 3 可在 ModuleError.h 提前添加本任务消费的原生错误枚举，严格使用公共接口约定的既定数值，Task 4 再补余项 — 独立 Loader 在 Task 4 之前必须可构建/测试，属于必要的头文件接线遗漏，不改错误协议 — 若下游假定连续枚举，应修正该假定，不能重新编号旧/新错误。

6. Task3可提前建立原列Task5的Private/ModuleTestAccess.h，集中唯一内部访问器定义与原生单次关闭故障接缝，Task5只扩充该头 — Release失败保留必须有可重复测试，TU各自定义同名访问器会产生后续ODR风险 — 若接缝超出最小per-instance测试用途，Task3 review须要求收窄，公共API不能出现mock/参数。

7. 整体Shutdown采用实际有效资源DAG＋同Module跨Scope后代资源顺序，并设“依赖方原生释放未确认前Provider资源不销毁”屏障，替代旧Scope规格的先清空全部Scope顺序 — 新规格§6.4与计划ScopeRuntime接缝要求逐记录依赖逆序清理/原生释放，旧全局HasChildren屏障会让consumer父资源被provider子资源阻住而自锁 — 若顺序实现错误可能提前销毁祖先/Provider，须由consumer父Scope+provider子Scope回归与Task5关闭失败保留Provider资源回归验证。独立BeginCloseScope/TryFinishCloseScope仍保留子先父及父有子记录不能完成关闭，整体Scope记录回收也不得破坏Owner句柄契约；Borrow保护、方向和合并环校验不减弱。Task8同步新旧规范替代边界。

8. Query/依赖信息未完整且批次原生释放失败时，允许State内有界私有清理组保守保留整批身份占位/未释放持有，并临时阻止既有潜在Provider被清理；已有Active业务闸门/固定绑定不改变，真实重试全部成功后解除保护 — 未知依赖无法安全用候选逆序猜Provider释放顺序，必须避免留下依赖方后释放其可能Provider — 代价是待清理失败批次期间部分无关Module的卸载可能保守地返回InUse/Busy，恢复后必须解除，不能形成永久全局阻塞；已知依赖路径仍按实际有效依赖逆序。不得新增公共Batch/Loader架构层，私有组/记录同受256预算。

9. 仅对从未成功接纳且从未Register/Activate的拒绝批次，先释放无可信完整描述项，可信无环图按逆拓扑；元数据环分量无合法业务拓扑时可逐项释放Manager自己的真实OS持有，不承诺非法Module图的业务析构顺序 — 否则一个应拒绝的环会导致清理永久无法启动；真实原生导入持有仍由OS维护且此批没有受管Factory/实例 — 若错误扩用到有效业务记录会破坏依赖清理，因此严格限定拒绝批次，首次Release失败即停、保留整批占位/剩余持有及既有潜在Provider保护，全部真实释放后解除。需正式回归元数据环拒绝/无注册/真实清理/身份复用，不构造CMake不支持的物理DLL链接环。

10. POSIX受控同配置Module改用RTLD_NOW|RTLD_GLOBAL，保留std::type_info值比较及逐Module最小显式导出/私有隐藏，替代计划Task3的LOCAL私有实现选择 — 规格冻结的是合法跨DSO类型值匹配，未冻结LOCAL；实测独立LOCAL组破坏libc++公共非多态RTTI弱符号合并，GLOBAL对照恢复且不需人工TypeId/trait/逐类型anchor或名字/hash比较 — 代价是公共符号进入进程作用域、可能产生名字冲突/符号插入，须受同配置ABI/ODR与显式Module身份/依赖治理，不能宣称插件隔离；错误时需重工Loader可见性策略。三桌面真实Module与移动两模式root/Project受影响重验。

11. 对必须观测consumer物理卸载先于Provider Destroy的NativeClose/Pin测试，固定Provider先显式装载再Consumer的可控前提，保留全部资源/Pin/故障/物理顺序断言；保留并新增另一装载顺序下自身持有释放成功/句柄失效与最终原生卸载的正式覆盖 — 无Manager的真实两库对照已证明Consumer先隐式Provider、再显式Provider时consumer dlclose=0可仍无NativeUnload，直到Provider最后引用释放才两库卸载；原断言把own-release误加强为任意OS立即移除 — 代价是特定物理顺序用例只代表受控前提，必须用另一顺序及额外OS持有用例覆盖可移植API语义，不能改Manager成功定义/ForceUnload或删原依赖顺序覆盖。

12. 仅Linux+Modular的PanguModuleUnloadTests设ENABLE_EXPORTS ON，文档明确ELF宿主若参与跨DSO公共类型RTTI，除了类型可见性还需动态导出相关公共符号（可用CMake ENABLE_EXPORTS） — 独立GLOBAL库无法与仅存在主程序静态符号表的公共RTTI合并，导出对照实证恢复；不引入逐类型anchor、不全局开放所有Targets，不改变库最小导出/私有隐藏 — 代价是该宿主的默认可见符号进入动态表、增大公共符号面与碰撞风险，未来Host须按同ABI/ODR及最小可见性策略使用；错误时需重新界定Host ABI可见性。Windows/macOS未据此无差别加flag。

13. 允许后续macOS模式仅在一个CTest会话内临时CTEST_PARALLEL_LEVEL=4，先确认PROCESSORS=2及独立WorkDir/可变夹具/宿主工具build目录隔离、内层构建jobs=2；实际共享项保持互斥/串行，当前已运行会话不打断，结束后才调整后续模式 — 单根仍仅一个验证会话，已有分类支持独立测试并行，可缩短每模式约25分钟的串行耗时 — 代价是CPU/内存争用和潜在时序flake，必须记录各轮并行级别、任何疑似并发失败定向串行复核且保留首轮；不改preset/timeout/断言/源码SHA，其它平台调度保持。

14. 现有CleanupOwner共享报告中，kCallerUnload同时表示已交付Scope显式关闭的调用方继续收尾责任：pending=kCallerUnload、完成/前置拒绝=None；未交付失败Scope pending=ManagerCleanup，整体Shutdown进行时pending=ManagerShutdown — 公开Scope BeginClose/TryFinishClose此前同样静默默认None，需要统一责任而不新增enum/改变未交付句柄拒绝契约；当前Public ABI仍Draft — 代价是旧枚举名语义需明确包括Scope，调用方必须根据原操作及ScopeHandle/ModuleHandle选择Finish，不能仅按枚举名误调TryFinishUnload；若解释错误会导致分派误用，故注释/规范与正式已交付/Busy/完成/Shutdown对照回归同步。

另有Task3原生诊断粒度约束：POSIX dlerror即时消费，公共Error只保存确定ModuleError，不长期借用错误文本；Windows可保留GetLastError数值。代价是POSIX详细原生诊断较粗，不修改错误值协议。

## 正式矩阵环境与隔离

| 目标 | 宿主/共享源码 | 正式BuildRoot | 临时命令环境 |
| --- | --- | --- | --- |
| Windows | Windows，`D:/Git/PanguEngine` | `D:/Git/PanguEngine/Build/Windows` | LLVM_ROOT=`D:/Developer/LLVM` |
| Linux | WSL Ubuntu，`/home/moozen/WindowsGit/PanguEngine`（CIFS） | `/home/moozen/PanguEngineBuild/Linux`（ext4） | LLVM_ROOT=`/opt/llvm-23.1.0` |
| macOS | BatchMode SSH moozen-macos，`/Users/moozen/WindowsGit/PanguEngine` | `/Users/moozen/PanguEngineBuild/macOS`（APFS） | LLVM_ROOT=`/opt/local/libexec/llvm-23` |
| Android ARM64 | Windows同一源码 | `D:/Git/PanguEngine/Build/Android`（NTFS） | NDK30.0.16248370，Android preset |
| iOS ARM64 | Mac同一共享源码，不复制Engine | `/Users/moozen/PanguEngineBuild/iOS`（APFS） | LLVM23，Toolchain回退到已存在iPhoneOS18.2.sdk |

每个BuildRoot同一时刻一个验证会话，最终format/tidy/build/CTest同根串行；不同隔离平台根可在源码冻结时并行。可变Project夹具复制在相应BuildRoot，Engine始终指共享源码。原生退出码直接来自Process.ExitCode/subprocess.returncode或shell保存的命令退出值，不以PowerShell stderr包装或Get-Content的退出码替代。

实际编译器版本：Windows/Linux为Clang23.1.0，macOS/iOS为MacPorts Clang23.1.2，Android NDK30.0.16248370内置Clang21.0.0；各自使用平台规定的STL/SDK，不将桌面LLVM版本检查套用到NDK。最终编译器路径/版本由真实CMakeCXXCompiler.cmake快照补全在各根Task8/final-summary.json。

## 首轮原始结果与修复过程

## 0c56cc2 首轮与跨平台修复

Windows 规范冻结后完整 Modular full100/100、同根Mono Module75/75、Project+模式矩阵+Toolchain10/10、恢复Modular fast44/44，原生退出均0。Android/iOS两模式正式root构建和独立Project最终链接全0，恢复移动Mono/BUILD_TESTING OFF/Benchmark OFF。原始目录为各正式BuildRoot的 `Task8/`，Windows/Linux/macOS首轮文件另复制保存在 `Task8/Round1/`；本轮不是修复后版本成绩。

Linux 首次扫描报已迁移ModuleIdentity.h缺失，CIFS旧路径stat仍成功但open ENOENT、新路径可读；仅据此怀疑缓存，未证实serverino根因。独立扫描随后自然恢复，正式build通过，没有挂载、内核、源码或配置维护。首轮与数次同提交重试的扫描错误未分别保存成独立全量日志（早期驱动复用文件名）；命令清单、保留的失败日志、stat/open/挂载属性和最小扫描记录在 `Linux/Task8/Round1/`，不能将后次成功改写成首轮成功。

Linux受影响CTest74/79，失败为常驻Consumer/跨库合法RTTI、PartialStartFailure、物理卸载顺序及SourcePluginEnable。Mac受影响75/79，失败为合法TypedDependency、Enable及Matrix/Switch的空第三方归档。两桌面保持独立Scope库无原生互导；实际DB派生两DSO对照的type_info值比较LOCAL=0、GLOBAL=1。POSIX改GLOBAL并保持原生值比较，原因与公开符号碰撞/插入代价见[ADR-0015](../../../wiki/decisions/0015-unified-link-modes-module-manager.md)。

Linux GLOBAL定向首轮5/6，剩余宿主常驻Factory的公共RTTI未进EXE动态符号表；同实际link命令仅增加export-dynamic的独立产物1/1通过，再仅为Linux Modular的PanguModuleUnloadTests设ENABLE_EXPORTS，定向6/6。Mac GLOBAL定向6/6。Linux原LOCAL正式回归RED为1/5、退出8，新增双顺序NativeLoader已通过，其余合法RTTI失败；日志 `Task8/fix-red-tests.log`、`fix-green-before-host-exports.log`、`host-exports-probe.log`、`fix-green-tests.log`。这些是提交前定向证据，正式新SHA矩阵另记。

无Manager双顺序Loader对照表明：Provider先显式装载时Consumer dlclose=0即NativeUnload1；Consumer先隐式装载Provider、再显式持有Provider时，Consumer dlclose=0但NativeUnload0，双方持有最后释放才到2。因此仅物理顺序用例固定Provider先，全部资源/Pin/故障/物理断言保留，新增两顺序自身Release后wrapper失效及最终真实卸载的正式回归，不加强Manager成功定义。

Enable脚本原来只检查Lib，POSIX Modular库在Bin，Windows import.lib掩盖缺口。修复同时检查Lib/Bin，正向段在Selection-Monolithic/Modular fresh目录显式配置内层模式，负例完整保留；不以外层CTest模式替代独立Project实际模式。

Mac模式夹具ThirdParty空TU的Mach-O object无符号，实际AR=`/opt/local/bin/ar`、RANLIB=`/opt/local/libexec/llvm-23/bin/llvm-ranlib`，ld拒archive member '/'。相同Clang `-stdlib=libc++ -O3 -DNDEBUG -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk` 和相同AR/ranlib，只改实际返回23函数后编译/归档/最终链接/运行全部0（`macOS/Task8/archive-empty-nonempty.log`）。正式夹具增加真实ThirdPartyValue调用，保留STATIC和策略隔离，不改全局工具链。

首轮Windows全分支现存改动C++格式退出0，正式根及保留Mono子图实际DB共37TU tidy均原生0，用户诊断未清零；既有/新增延期项在最终归档汇总。iOS MobileHost首次tidy未继承仓库配置而no checks，不计通过；显式config-file后0，和Android均保留ModuleIdentity.h既有3诊断。


## 正式第二轮实际矩阵（冻结提交 f9349df）

执行日期为2026-11-06至2026-11-07（Asia/Shanghai）；Windows命令清单带+08:00，Linux/Apple命令清单使用UTC。下表全部属于完整干净树 `f9349df2d2ce97e8ad0953db1724a141decf2d55`，不拼0c56首轮或提交前定向GREEN。

| 宿主/模式 | 正式结果 | 原始记录（相对本平台BuildRoot） |
| --- | --- | --- |
| Windows Modular full | 101/101，777.9s；相关configure/build均原生0 | Task8/Round2/modular-full-workflow.log |
| Windows Monolithic Module | 75/75，3.01s；相关configure/build均原生0 | Task8/Round2/mono-module.log |
| Windows Project/Matrix/Switch/Toolchain | 10/10，580.08s；相关configure/build均原生0 | Task8/Round2/mono-project-matrix.log |
| Windows恢复Modular fast，基准前 | 44/44，3.71s；相关configure/build均原生0 | Task8/Round2/restore-fast-test.log |
| Linux Modular Module/Toolchain/Plugins | 80/80，562.19s；full build原生0 | Task8/Round2/Modular-{configure,build,tests}.log |
| Linux Monolithic Module/Toolchain/Plugins | 90/90，457.44s；full build原生0 | Task8/Round2/Monolithic-{configure,build,tests}.log |
| Linux 独立Matrix/Switch | 2/2，68.77s | Task8/Round2/mode-matrix.log |
| macOS Modular Module/Toolchain/Plugins | 80/80，1744.21s；full build原生0 | Task8/Round2/Modular-{configure,build,tests}.log |
| macOS Monolithic Module/Toolchain/Plugins | 90/90，1093.51s；full build原生0 | Task8/Round2/Monolithic-{configure,build,tests}.log |
| macOS 独立Matrix/Switch | 2/2，168.29s | Task8/Round2/mode-matrix.log |
| Android Monolithic/Modular | 两模式root configure/build、独立Project最终链接均原生0 | Task8/Round2/{Monolithic,Modular}-{configure,build,project}.log |
| iOS Monolithic/Modular | 两模式root configure/build、独立Project最终链接均原生0 | Task8/Round2/{Monolithic,Modular}-{configure,build,project}.log |

每根 `Task8/Round2/commands.txt` 保存精确原生命令与退出码；`version.log` 为上述完整SHA。Windows三个本轮受影响脚本另以fresh目录、当前SHA和显式 `PANGU_ENGINE_ALLOW_LOCAL_OVERRIDE=OFF` 串行补验：PluginEnableTests、StaticCatalogIntegrationTests、SharedDeployTests全部原生0；9个实际cache均OFF，内层实际模式覆盖Monolithic/Modular，Engine.commit均为f934。精确命令/日志在 `Build/Windows/Task8/StrictClean/commands.txt`、同名脚本log与 `actual-locks.json`。

常规root CTest的helper默认override=ON，本轮实际源树干净且Engine.commit匹配，不伪称OFF。各桌面 `Task8/ChecksRound2/enable-inner-modes.json` 记录fresh内层cache/锁；外层Mono标签不替代内层实际模式。Task7旧SHA严格锁六脚本只保留历史范围。

### 实际符号、装卸与移动边界

Windows用llvm-readobj、Linux用 `llvm-readobj --dyn-symbols --needed-libs`、macOS用 `nm -gU`/`otool -L` 核对Foundation/Module/ScopeProvider/ScopeConsumer的真实固定Query及Runtime依赖，日志在各根 `Task8/ChecksRound2/*-symbols.log`（Windows为 `*-exports.log`）。三桌面Modular集合内真实执行NativeModule、ModuleUnload/Concurrency、SharedScopeLifecycle、ModuleLifetime；合法/错误类型、私有类型、Pin/Borrow/失败清理及物理卸载后凭据通过，Scope模块保持无原生互导，新NativeLoader双顺序回归正式接线，不依赖临时Probe。

移动独立Project在正式根 `Tests/ModuleLinkModes/<模式>/<随机runId>/`，保留TargetCommands.txt、MobileHostSymbols.txt、ModuleQueryExports.txt、ModuleHeaders.txt及Architecture.txt等。Monolithic的真Runtime/业务归档进入Android AArch64 ELF Host.so或iOS ARM64 Mach-O executable/platform2；Modular四个真动态库有Query，业务依赖Runtime、Runtime依赖Foundation，宿主不直接导入业务且无Module Query。脚本断言CTest testCount=0/无目标GTest，宿主PanguBuildTool实际执行。补查在Android/iOS的 `Task8/ChecksRound2/`。这是编译、最终链接、宿主文件检查；未进行设备RTTI/执行/卸载、APK/IPA、签名、发布或跨宿主ABI/性能等价验收。

### 可复现正式入口

各根同一时刻一个验证会话；最终format/tidy/build/CTest同根串行。内层 `PANGU_TEST_BUILD_JOBS=2`。可选单CTest `CTEST_PARALLEL_LEVEL=4` 已获允许但本轮未应用，未改preset/timeout/测试判据。

Windows从D:/Git/PanguEngine执行：

```powershell
cmake --preset windows -DPANGU_LINK_MODE=Modular -DBUILD_TESTING=ON -DPANGU_BUILD_ENGINE_TESTS=ON -DPANGU_BUILD_BENCHMARKS=OFF -DPANGU_TEST_BUILD_JOBS=2
cmake --workflow --preset windows-full
cmake --preset windows -DPANGU_LINK_MODE=Monolithic -DBUILD_TESTING=ON -DPANGU_BUILD_ENGINE_TESTS=ON -DPANGU_BUILD_BENCHMARKS=OFF -DPANGU_TEST_BUILD_JOBS=2
cmake --build --preset windows-precommit
ctest --preset windows-precommit -L '^area\.module$' --no-tests=error
ctest --preset windows-full -R '^(SourcePlugin(Project|StaticCatalog|SharedDeploy|Incremental|TestOwnership|Enable|EngineRevision)|ModuleLinkMode(Matrix|Switch)|ToolchainLibraryLink)$' --no-tests=error --parallel 1
cmake --preset windows -DPANGU_LINK_MODE=Modular -DBUILD_TESTING=ON -DPANGU_BUILD_ENGINE_TESTS=ON -DPANGU_BUILD_BENCHMARKS=OFF -DPANGU_TEST_BUILD_JOBS=2
cmake --build --preset windows-fast
ctest --preset windows-fast --no-tests=error
```

Linux在/home/moozen/WindowsGit/PanguEngine以LLVM_ROOT=/opt/llvm-23.1.0；macOS经 `ssh -o BatchMode=yes moozen-macos` 在/Users/moozen/WindowsGit/PanguEngine以LLVM_ROOT=/opt/local/libexec/llvm-23执行（platform分别linux/macos）：

```sh
for mode in Modular Monolithic; do
    cmake --preset "$platform" -DPANGU_LINK_MODE="$mode" -DBUILD_TESTING=ON -DPANGU_BUILD_ENGINE_TESTS=ON -DPANGU_BUILD_BENCHMARKS=OFF -DPANGU_TEST_BUILD_JOBS=2
    cmake --build --preset "$platform-full"
    ctest --preset "$platform-full" -L '^area\.(module|toolchain|plugins)$' --no-tests=error
done
ctest --preset "$platform-full" -R '^ModuleLinkMode(Matrix|Switch)$' --no-tests=error
cmake --preset "$platform" -DPANGU_LINK_MODE=Modular -DBUILD_TESTING=ON -DPANGU_BUILD_ENGINE_TESTS=ON -DPANGU_BUILD_BENCHMARKS=OFF -DPANGU_TEST_BUILD_JOBS=2
```

移动命令逐模式完整见[计划Task8 Step5/6](2026-11-06-module-manager-link-modes.md#task-8-权威规范同步跨平台验收与收尾)，实际展开路径/退出码见Android/iOS Round2/commands.txt。入口是对应root preset显式模式/BUILD_TESTING=ON/PANGU_BUILD_ENGINE_TESTS=ON配置、同名build preset，再以本平台共享源码根/正式测试根、target preset和模式调用 `Tests/CMake/SourcePluginProject/CrossCompileTests.cmake`。最后显式恢复移动Monolithic、BUILD_TESTING=OFF、PANGU_BUILD_ENGINE_TESTS=ON、Benchmark OFF。

## 静态检查、基准与恢复

0c56首轮全分支现存改动C++格式0；Windows正式root/保留Mono子图37TU实际DB tidy全原生0。f934 fix六文件格式0，Linux实际root/ModeFixture五TU tidy0；Windows新改四TU实际DB tidy0；Mac POSIX Loader和四个新改TU实际DB tidy0。Android/iOS MobileHost实际DB以仓库配置tidy0；iOS最初no checks不计通过，显式config-file后0。临时Probe只在实际DB派生最小上下文验证，不是正式验收入口。默认tidy退出0不等于零用户诊断。

| 诊断/Minor | 新旧与处理 |
| --- | --- |
| Task1 First.cpp尾逗号、旧dirty override提示 | 既有样式/联调提示；当前干净树和OFF严格锁另记 |
| Task2 ScopeRuntime五项const/range-for/参数命名 | 既有方法提示，不能称整份tidy零诊断 |
| Task4 ManagerFixture/benchmark大括号、contains/直接include | 本计划新增延期Minor；最终fixwave修复，定向验证另列，限定复评已通过 |
| Task5聚合尾逗号，含fix新增回归 | 本计划新增延期Minor；最终fixwave人工补真实聚合尾逗号，未批量应用函数参数误判修复，限定复评已通过 |
| Task6多实例最后Destroy/Factory事件检测力 | 最终fixwave增强既有真实DLL用例，限定复评已通过；M2原生退出码证据补齐已关闭 |
| ModuleIdentity.h:40/44/45 qualified-auto及两处pointer arithmetic | 既有迭代器算法在libc++下的头诊断，Android/iOS各3条；Linux/macOS的Task8/ChecksRound2/native-loader-tidy.log亦各3条，归并保留，不盲改auto*破坏MSVC迭代器 |
| POSIX NativeModuleLibrary.cpp:20/26/37/39/48/51 dlerror，concurrency-mt-unsafe | Linux/macOS各自Task8/ChecksRound2/native-loader-tidy.log各6条，原生退出0仍有诊断。Loader及这些调用由本分支Task3引入；相对Task8只改flags的工作起点已存在，不是分支基线调用。保留为待复核静态诊断，非已证实运行竞态或平台误报；当前管理操作与查询由调用方串行协调，原生错误即取即消费且不借用返回字符串，不据此宣称所有平台线程安全，不增加全局豁免 |
| Benchmark std::regex/无异常配置提示 | 上游v1.10.0边界，不改变引擎无异常政策 |

精确日志：Windows `Task8/tidy-*.log`/tidy-exitcodes.json（37TU）、`Task8/ChecksRound2/tidy-*.log`/tidy-exitcodes.json（4TU）；Linux `Task8/fix-static.log` 与 `ChecksRound2/native-loader-tidy.log`；Mac `ChecksRound2/native-loader-tidy.log`/fix-targets-tidy.log；Android/iOS `ChecksRound2/mobile-tidy*.log`。Task6 M2为阶段 `Build/Windows/task6-native-tidy.log`/task6-native-tidy-exitcodes.txt，5TU逐项0，不再开放。

三桌面每模式显式打开PANGU_BUILD_BENCHMARKS，构建PanguModuleBenchmarks，以 `--benchmark_dry_run --benchmark_out=<本根>/Task8/<模式>-benchmark.json --benchmark_out_format=json` 执行，两模式均六场景6/6、无error_occurred。原始configure/build/dryrun同名前缀日志、JSON与benchmark-version.txt均在各根Task8，锁定f934。依赖GoogleBenchmark v1.10.0、固定submodule `a6b4a9f00b36c19af9997d68a1cf6e0b5b3d032c`。

六场景保持ModuleIdentityParse、ModuleIdentityFormat、ScopeAccessBorrowRelease、CatalogCreate/1/8/32。身份/借用输入不变，Catalog的1/8/32业务项外另加两个必要基础常驻根，Create/Manager同步关闭与析构仍在原计时区间；不测试动态Load吞吐，不移出计时改善结果。只做dryrun，不混旧STATIC基线、不重采无关性能基线、不建立跨宿主门禁。

基准结束后各桌面显式恢复Modular、BUILD_TESTING/PANGU_BUILD_ENGINE_TESTS ON、Benchmark OFF并实际重建/运行fast：Windows44/44（StrictClean/restore-fast-{build,test}.log）、Linux44/44、Mac44/44（Task8/benchmark-restore-fast-{build,test}.log），与基准前fast分开记录，避免只改cache留下上一模式产物。移动最终为Monolithic、BUILD_TESTING OFF、PANGU_BUILD_ENGINE_TESTS ON、Benchmark OFF。各根Task8/final-summary.json保存最终cache/编译器/版本快照。



## 最终fixwave（限定复评已通过）

日期2026-11-07。修复代码与规范提交为 `2dd769dbee8de5e90c33d730b706b4ba6c877137`，基线 `f567f4e311764a012a6b107db2d829f16d8e3b22`。I1在Manager的三个Scope公共返回边界按Progress统一映射责任，不从Error推断阶段；失败Scope未交付且pending为ManagerCleanup，已交付Scope显式关闭pending为CallerUnload，Shutdown期间pending为ManagerShutdown，完成/前置拒绝为None。M1/M2仅清新增明确样式与真实聚合，M3增强ExistingOwners全部对象/释放状态先于首个工厂、最后工厂先于NativeUnload，M4/M5分别补上表诊断与ADR索引。未新增测试目标、公共enum或开放失败Scope关闭句柄。

本轮运行的是既有正式GTest目标的定向选择，分别记录，不拼接为全矩阵：

| 配置与命令（从D:/Git/PanguEngine执行） | 结果 |
| --- | --- |
| `cmake --build Build/Windows/Tests/Work/ModuleRegression/cda260d613d3/Monolithic --target PanguModuleIntegrationTests -j 2`，随后该根`Bin/PanguModuleIntegrationTests.exe --gtest_filter=ScopeLifecycle.*` | 构建0；27/27，运行0，含原report销毁、原错跨Poll、同步回滚/前置拒绝和显式关闭/Shutdown责任 |
| `cmake --build Build/Windows --target PanguModuleUnloadTests PanguModuleIntegrationTests -j 2`，随后`Build/Windows/Bin/PanguModuleUnloadTests.exe --gtest_filter=ModuleUnload.FailedBatchProtectsClosingScopeFromCreateScopeFailureCleanup:ModuleUnload.FailedBatchRetainsUnreleasedReservation` | 构建0；2/2，运行0，前者保留Factory/Start两分支 |
| `Build/Windows/Bin/PanguModuleIntegrationTests.exe --gtest_filter=SharedScopeLifecycle.*:BatchLoad.*` | 10/10，运行0，含增强的真实DLL ExistingOwners及同report原生重入报告 |

RED先只补正式断言：未修生产代码时，Mono未交付Scope及Modular失败批次Scope两项均原生退出1，均明确为CleanupOwner的00与02不等；后者Factory/Start各命中。上述GREEN与RED原始输出分别保留在 `Build/Windows/FinalFix/{red-mono-tests,red-modular-tests,green-mono-tests,green-modular-unload-tests,green-modular-integration-tests}.{stdout,stderr}.log`。

九个改动C++文件执行仓库clang-format dry-run/Werror退出0；七个相关TU用实际DB执行仓库clang-tidy均退出0，stdout/stderr和原生退出分别保留。Root DB用于ModuleManager、BatchLoad、ModuleUnload、ModuleUnloadConcurrency、SharedScopeLifecycle；上述Mono子图DB用于ScopeLifecycle，也覆盖ManagerFixture。benchmark TU复用既有 `Build/Windows/Tests/Work/Task8Green/benchmark-only`，源码仍为本仓库，临时配置Modular/Benchmark ON并构建真实PanguModuleBenchmarks取得DB；只tidy、不采样，随后恢复OFF。首次配置受沙箱插件发现Access denied影响退出1，提升权限同命令退出0，非契约RED。最终benchmark TU未显示用户诊断；Manager/Scope/Batch保留既有const/range/optional/条件表达式、preprocessor/designated-initializer/成员初始化等诊断，不称零诊断。两项中间静态读取曾重叠，最终Scope/Batch按同BuildRoot串行重验；原始轮次均保留。

精确原生命令/退出码为 `Build/Windows/FinalFix/commands.jsonl`；实际DB命令快照为 `actual-commands.json`，最终缓存为 `final-caches.json`。文档相对文件链接与ADR0014/0015表连续性检查、git diff --check均0（不把文件存在检查称为全部锚点验证）。Windows正式根保持Modular、BUILD_TESTING/PANGU_BUILD_ENGINE_TESTS ON、Benchmark OFF；独立benchmark树恢复OFF。其它平台正式默认未改。五平台完整矩阵和两模式六场景dryrun仍只属于f9349df，f567f4e为原文档归档；本轮未重复这些验收，也不将其改写为2dd769d的矩阵通过。限定复评已完成且全部发现关闭；本计划临时上下文已由控制器按精确路径清理。

## Review与收尾

Task1–8任务评审、Task4/5各一次限定复评、唯一全分支Review和唯一统一fixwave的限定复评均已完成。最终修复全部 ADDRESSED，无新增 Critical/Important/Minor 或范围外缺陷；本计划临时目录已由控制器完成清理。正式 Tests、各平台 BuildRoot 与原始日志保留。

### 最终整体 Review 与限定复评结论（2026-11-07）

唯一整体评审范围为 `5be751f70ef93f64f2d9505138e4d8c602d2fc70..f567f4e311764a012a6b107db2d829f16d8e3b22`：Critical 0、Important 1、Minor 5，最初结论 With fixes。统一修复为 `2dd769dbee8de5e90c33d730b706b4ba6c877137`，定向验收/第14条裁决归档为 `d73e83321a1a548e7441a13c9372294e062049b6`。唯一限定复评检查完整两提交修复范围，以下六项全部 ADDRESSED，未发现修复新增 Critical、Important、Minor 或范围外缺陷。

| 发现 | 最终处理与复评结论 |
| --- | --- |
| I1 未交付失败 Scope 的 CleanupOwner 默认 None | 三个 Scope 公共边界按 Progress/真实 Shutdown 状态补责任；未交付待清理由 Manager 接管，显式关闭由调用方继续，完成/前置拒绝 None，未交付句柄仍拒绝普通 Finish。正式字段 RED→GREEN 与责任对照见上一节 |
| M1 新增 braces/contains/直接 include | 仅清理本分支新增明确项；保留旧及第三方背景提示，不借机重构 |
| M2 新增聚合尾逗号 | 真实多行聚合人工补齐，函数实参分隔符未误改；实际 DB 检查目标诊断已消 |
| M3 多实例最后销毁顺序检测力 | 原计数和首事件断言保留；新增最后对象/Provider/Deleter/PrivateDestroy 早于首 Factory、最后 Factory 早于 NativeUnload，真实 DLL 用例通过 |
| M4 POSIX 原生 Loader 静态诊断遗漏 | 两 POSIX 各6条 dlerror 及各3条 ModuleIdentity 完整归档，准确区分分支新旧，待复核不等于已证实竞态/误报，无全局豁免 |
| M5 ADR-0015 索引表空行 | 表格恢复连续，后续标题分隔保留 |

最终评审逐项未扩大的范围均与批准契约一致：移动设备/打包签名；Hot Reload及完整Host/Registry/调度/GPU/Meta；跨配置或跨宿主ABI与不可信二进制隔离；额外原生持有下强制物理移除/任意初始化器副作用回滚；dryrun之外性能门禁；未变旧样式及第三方全面治理；释放最后保护后继续执行DLL或操作/回调进行时移动销毁Manager的调用方误用。真实受控物理卸载、正常跨线程Release和已承诺管理操作仍在验收范围，未以这些范围排除掩盖缺陷。

控制器在 `d73e833`（代码仍为 `2dd769d`）上追加收尾快速检查：`cmake --build --preset windows-fast` 和 `ctest --preset windows-fast --no-tests=error` 原生均0，44/44（2.02秒）；原始 `Build/Windows/FinalFix/controller-fast-build.log`、`controller-fast-test.log`。该结果与五平台 `f9349df` 全矩阵、最终修复27/27、2/2、10/10分轮记录，不宣称重跑过当前修复的完整跨平台矩阵。后续状态归档仅改文档，未改代码/构建/测试输入。
### 临时上下文清理确认

2026-11-07，最终整体评审及限定复评结论已先以 `e4b503c` 永久提交，14条裁决及原因/代价已保存在本文件。全部实现/评审代理和验证会话已结束，无运行任务依赖该临时目录。控制器解析并严格校验 `D:/Git/PanguEngine/.superpowers/sdd/2026-11-06-module-manager-link-modes` 为预期绝对路径、位于本计划父目录且非ReparsePoint后，仅递归删除该目录，随后存在性检查为false。正式 `Tests/Module`、`Build/Windows`、`Build/Android` 仍存在，其它计划目录与远程平台BuildRoot未触及。计划Step8–10现全部完成。