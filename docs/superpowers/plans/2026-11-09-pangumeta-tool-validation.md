# PanguMetaTool 正式验收

Task 1–13 实现与验证范围见下表；独立 Task13 review 和最终 whole-branch review 由 controller 后续执行。本记录区分实现、设计状态和实际平台覆盖；不把历史 PoC、部分定向或外层调度计数当作完整矩阵。

## 固定源码与复用边界

| 标记 | 提交 | Git tree | 用途 |
| --- | --- | --- | --- |
| R1 | e5c349cbe509734910ebb641b276575350e02807 | 3f7905cd6d7f72dba73459e14795a2398f5b2262 | 首轮冻结实现、移动矩阵、Unix Mono 完整验收 |
| R2 / R3 | 28b086ca8014785b284f852b863004be83374f22 | 44c3850a0c43d78604f444ccd4eb79f495b81ce5 | 仅增加协作测试 Host 部署调用及 ManualType POSIX visibility，2 文件 8 行 |

各轮开始前工作区与递归子模块干净，验收期间 tracked 输入冻结，PANGU_ENGINE_ALLOW_LOCAL_OVERRIDE=OFF。两次实现提交后另有文档归档提交，不能把文档 HEAD 当作所有命令执行时的 SHA。

controller 明确批准保留成功的 R1 Linux/macOS Monolithic Full：新增部署 helper 在非 Windows 立即 return；同路径协作目标的原生依赖查询、完整构建命令和 compile_commands.json 前后 SHA 完全一致，修后协作三例及 no-op 通过。ManualType 改动只修人工协议夹具的公共 RTTI 可见性，生产 RTTI 比较未变；R2 在 Unix 双模式各补 17 项 ModuleMetadata/HostMetadata/生成消费/卸载与 Lifetime，并检查 RTTI 外部符号和目标 no-op。失败模式在 R2 重跑 Full，原生退出码丢失的 Windows Mono 在 R3 单独补 Full。**不声明最终 tree 的六个桌面模式都重新跑过。**

移动矩阵使用 R1；R2 的两处测试接缝均不进入 BUILD_TESTING=OFF 的移动根或 Mobile 夹具，其生成协议、目标输入和工具未改变。

## 最终覆盖

| 平台 | 模式 | 实际被测提交/轮次 | 根 Full CTest | 工具内层 | 原生 exit |
| --- | --- | --- | ---: | ---: | ---: |
| Windows | Monolithic | 28b086c / R3补收据 | 172/172 | 67/67 | 0 |
| Windows | Modular | 28b086c / R2 | 164/164 | 67/67 | 0 |
| Linux | Monolithic | e5c349c / R1保留＋28b086c定向 | 171/171 | 68/68 | 0 |
| Linux | Modular | 28b086c / R2 | 163/163 | 68/68 | 0 |
| macOS | Monolithic | e5c349c / R1保留＋28b086c定向 | 171/171 | 68/68 | 0 |
| macOS | Modular | 28b086c / R2 | 163/163 | 68/68 | 0 |
| Android arm64-v8a | Monolithic / Modular | e5c349c / 移动冻结轮 | 不运行目标CTest；均最终链接/审计通过 | 宿主生成器 | 各0 |
| iOS Device ARM64 | Monolithic / Modular | e5c349c / 移动冻结轮 | 不运行目标CTest；均最终链接/审计通过 | 宿主生成器 | 各0 |

根 Full 的数字包含九个 Meta 工具调度项；其内层分别为 Windows 67、Linux/macOS 68 项，不能与根数累加。POSIX 比 Windows 多一个发布写入故障用例。每个正式内层用例恰好归属一个互斥组，空匹配/关键跳过不是成功；原始 JUnit 显示各组失败和跳过均为零。

| 根组 | Windows 内层 | Linux/macOS 内层 |
| --- | ---: | ---: |
| MetaSdk | 6 | 6 |
| MetaIR | 4 | 4 |
| MetaRequest | 6 | 6 |
| MetaContext | 8 | 8 |
| MetaOwnership | 7 | 7 |
| MetaAttr | 13 | 13 |
| MetaCodeGen | 7 | 7 |
| MetaPublish | 15 | 16 |
| MetaCli | 1 | 1 |

Modular Full 保留 ModuleRegression；SourcePluginProject、Ownership、Catalog、部署和增量测试仍在正式根集合中。R2 Unix 双模式定向均17/17，实际修复共享库RTTI从LOCAL变为WEAK DEFAULT/weak external。归属夹具3/3和no-op通过，未用人工表替代新生成消费/物理卸载测试。

## 可复现入口与证据

桌面各在约定共享源码目录串行运行，platform取windows/linux/macos，mode取Monolithic/Modular：

```sh
cmake --preset <platform> -DPANGU_LINK_MODE=<mode> -DPANGU_BUILD_SAMPLES=ON -DPANGU_BUILD_BENCHMARKS=OFF -DPANGU_BUILD_META_BENCHMARKS=OFF -DBUILD_TESTING=ON -DPANGU_BUILD_ENGINE_TESTS=ON -DPANGU_ENGINE_ALLOW_LOCAL_OVERRIDE=OFF -DPANGU_META_SDK_CONFIG=<已准备配置绝对路径>
cmake --build --preset <platform>-full --parallel 4
ctest --preset <platform>-full --no-tests=error --output-on-failure
```

尖括号是说明占位，不原样执行。Linux通过WSL Ubuntu、源码/home/moozen/WindowsGit/PanguEngine、产物/home/moozen/PanguEngineBuild/Linux；Mac通过BatchMode SSH、源码/Users/moozen/WindowsGit/PanguEngine、产物/Users/moozen/PanguEngineBuild/macOS。未复制Engine源码树；SDK/缓存保持各宿主本地BuildRoot。

移动使用正式 CrossCompileTests.cmake，参数 PANGU_ENGINE_SOURCE_DIR、PANGU_TEST_ROOT、PANGU_TARGET_PRESET、PANGU_LINK_MODE、PANGU_META_HOST_TOOL、PANGU_META_SDK_CONFIG、PANGU_ENGINE_ALLOW_LOCAL_OVERRIDE 均显式给出。script先配置/构建根preset，再从实际cache转发目标工具链到正式Mobile fixture。目标 static_assert 检查 sizeof(void*)=8、__aarch64__=1、Android API29/iOS deployment180000参与求值的Key及ReadOnly；检查真实对象、正式根库和最终可执行文件的架构、SDK、iOS load command、动态依赖与no-op。移动没有运行GTest或设备程序。

各BuildRoot下 task-13-full-* 保存R1，task-13-r2-* 保存R2，Windows task-13-r3-* 保存补验；*-source.txt为SHA/tree，*-summary.txt保存准确命令、exit与耗时，*-results/保存每组独立JUnit。Windows R3每个命令返回后原子写*.exit，CTest stdout/stderr直接落盘，持久脚本独立于agent会话。移动证据为Build/Android及Mac iOS根下task-13-frozen-*，逐命令日志位于Tests/MetaMobile。大日志和临时diff不纳入Git，正式Tests永久保留。

## SDK 与工具链

现场二进制：Windows Clang23.1.5/MSVC STL（旧cache曾显示23.1.0）；Linux Clang23.1.0/libc++；MacPorts Clang23.1.2/libc++。Win/Linux SDK来自固定上游ea7d852a70e8bdfaf601d6626a760f9771b2c4b4，Release/-O3 -DNDEBUG，保留RTTI；Mac复用通过真实预检的23.1.2 SDK。未重建SDK、替换LLVM安装或让LLVM targets进入移动图。配置分别位于各桌面BuildRoot的Sdk/PanguMeta/PanguMetaSdk.cmake。

Android使用NDK30.0.16248370/Clang21.0.0、arm64-v8a/API29/c++_shared。iOS使用iPhoneOS18.2 SDK、deployment18.0、Device ARM64。宿主生成器资源头匹配宿主SDK，系统与标准库头来自实际目标SDK/NDK；不声称任意跨前端语言特性等价。

## 失败、修复及记录限制

| 平台/模式 | 轮次/提交 | 实际结果 | 原生 exit / 收据 | 处理 |
| --- | --- | --- | --- | --- |
| Windows Mono | R1 / e5c349c | 171/172 | 8 | 缺Physics DLL的正式失败，R2修复部署 |
| Linux Modular | R1 / e5c349c | 159/163 | 8 | ManualType跨DSO RTTI四项失败，R2补visibility |
| Windows Mono | R2 / 28b086c | LastTest完整172 Passed、0 Failed | 未捕获，不推断0 | 保留19:02–19:32记录，不作为最终原生成功收据 |
| Windows Mono | R3 / 28b086c | 172/172 | 原子*.exit=0 | 单独补收据轮，不抹去R2限制 |

- Windows Debug真实扫描曾拒绝CMake标准-Xclang -gcodeview；新增严格warning Context用例实际RED后，仅剥离该已知调试输出参数，Context8/8转绿。不泛放不透明cc1参数。初次CTest空匹配不计RED。
- Mac旧宿主cache以含../..和规范路径重复登记同一toolchain，Ninja重复输出失败；只规范化输入清单，正式根构建恢复。空符号Bridge被Apple ar/LLVM ranlib写成不兼容索引；正式Unrelated.cpp保留外部函数符号后Registration/Incremental/FailureRecovery通过，不改机器ar/ranlib。
- Mobile接线先因缺Foundation导出头失败，再由目标static_assert捕获CMAKE_SYSTEM_VERSION=1与实际API29不一致；改用NDK ANDROID_PLATFORM_LEVEL，正确目标常量通过。早期审计把宿主依赖清单误认成目标LLVM链接，改为真实LINK_LIBRARIES/动态依赖检查；这些夹具问题不伪装产品RED。
- R1 Windows Mono171/172、exit8：协作测试Host漏部署PanguPhysics.dll，GTest discovery退出0xc0000135。R2加现有stage_host_runtime调用，正式Ownership回归1/1通过，内部三例实际执行。此前阶段汇报只看tail漏过中段Failed，完整失败记录保留。
- R1 Linux Modular159/163、exit8：人工ManualType的RTTI被隐藏，三项ModuleMetadata和一项HostMetadata失败；生成夹具正例通过。R2只补POSIX公共类型可见性，不改变生产类型比较。
- Windows R2 Mono保存完整172条Test Passed、零Test Failed和19:02–19:32的Start/End testing，但agent父进程中断导致aggregate exit未保留。该行只证明逐例覆盖，不推断exit0；R3补验单列，不覆盖旧记录。额外成本及裁决原文见后文。
- Mac首轮定向CTest曾误在根build未退出时启动，出现缺可执行文件，已停止其完整子树，构建exit0后串行重跑。该中断轮不计通过。
- Windows python别名命中PyManager，输出声称自动更新至26.0后因runtime/network不可用退出1；没有执行文档脚本或安装Python runtime。后改用现成WSL python3，未改LLVM/SDK安装。

## 静态检查、性能与限制

任务C++与9个移动生成文件clang-format --dry-run --Werror通过。clang-tidy使用实际工具、Graph、Android及Linux目标数据库；任务新增诊断已修正。仍有既有ModuleIdentity.h三项qualified-auto/指针算术提示及被抑制的系统头统计，不宣称全工程lint-clean。前序Minor保留供最终全分支评审，benchmark旧版本标注已修正。

Windows正式Consumer夹具（已准备宿主工具、Clang23.1.5、Release）测得configure2.125s、首次目标构建17.267s、无关TU增量0.222s、普通no-op0.049s，均exit0。5个生成文件3708bytes，Consumer.exe9216bytes（不含依赖DLL）。生产宿主工具33,254,400bytes；Mono生成Fixture/Service分别4文件11,233bytes/4文件4,008bytes。它们是本机有限输入测量，不是SDK首次构建或完整内存/启动成本保证。

Task12提交e718a2b的三次wall-time均值基线复用：Modular直接/生成/描述冷查询1.07/1.04/45.0ns，Mono1.05/1.01/36.8ns。描述包含Gate/RTTI/Pin，冷查询指不缓存借用结果；不把噪声差异宣称为加速。完整复现入口见Tests/Meta/Benchmarks/README.md。

仅支持有对应CMakeCache的Ninja单配置，跨配置切换有正式回归，多配置生成器/不可还原PCH及C++ Modules明确拒绝。通用Attribute框架、磁盘Schema、完整序列化/编辑器、SDK安装包、Apple Silicon宿主、移动签名/安装/启动、模拟器与真机运行未由本轮验收。

## 恢复与文档归档

三桌面已恢复 Modular、PANGU_BUILD_SAMPLES=OFF、PANGU_BUILD_BENCHMARKS=OFF、PANGU_BUILD_META_BENCHMARKS=OFF、BUILD_TESTING/PANGU_BUILD_ENGINE_TESTS=ON、override=OFF；各自恢复配置/必要构建/17项定向/Runtime目标no-op均exit0。Android/iOS已恢复Monolithic、BUILD_TESTING=OFF、Samples/Bench OFF、override=OFF，恢复构建exit0。恢复不重复完整CTest。

最后只更新文档，不改已测C++/CMake输入，未再次运行完整CTest。原任务实现/被测SHA与文档归档HEAD分开记录；最终HEAD/tree见Task13报告及提交历史。计划workspace仍供独立Task13与全分支评审，禁止实现者删除；controller在评审/验收通过、必要结论归档且无活跃依赖后精确清理。全部长期Tests保留。

## Tasks 1–12 完成记录与待复审 Minor

以下原清单保留顺序；Task12 的编译器版本标注已在 Task13 校正，其余 Minor 交最终全分支评审，不静默删除。

Task 1: complete (commits bc05c5f..e9891b3, review clean)

Task 2: minor (deferred): 补充noexcept(false)赋值类型断言覆盖；当前实现表达式已按真实is_nothrow_assignable传播，最终全分支评审需审阅建议。

Task 2: complete (commits e9891b3..49836b3, review clean)

Task 3: minor (deferred): 六条既有Manager/Catalog静态检查提示已逐项披露，最终全分支评审审阅；无新增代码诊断，不为本任务改动无关逻辑。

Task 3: complete (commits 49836b3..22d4754, review clean)

Task 4: minor (deferred): clang-tidy抑制的系统头诊断计数属于输出统计而非显示自有警告，最终摘要继续区分。

Task 4: complete (commits 22d4754..cc4f733, review clean)

Task 5: minor (deferred): M1确定性测试仅比较部分列表/位置，建议完整逻辑序列与canonical重复乱序；当前实现排序正确，留最终全分支评审。

Task 5: minor (deferred): M2第三方警告计数噪声，摘要区分无用户诊断与完全静默，保留原日志。

Task 5: complete (commits cc4f733..f853b7f, review clean)

Task 6: minor (deferred): MSVC xfilesystem_abi.h两条EnumCastOutOfRange值10/11已披露，为第三方诊断，摘要不称全部静态零告警。

Task 6: complete (commits f853b7f..a690dad, review clean)

Task 7: minor (deferred): PowerShell将tidy stderr进度包装NativeCommandError，实际原生exit0且无诊断；后续摘要/日志捕获改善，不误写分析失败。

Task 7: complete (commits a690dad..d3903a6, review clean)

Task 8: minor (deferred): MSVC xfilesystem_abi EnumCast值33系统头诊断单列，不扩豁免。

Task 8: complete (commits d3903a6..3a97db0, review clean)

Task 9: minor (deferred): CliTests.cmake语义失败后当前只复查Actor.gen.h+stamp，建议最终复审考虑全部生成文件未变断言（正常稳定性已全SHA/mtime）。

Task 9: complete (commits 3a97db0..c33b5d7, review clean)

Task 10: minor (deferred): 无任务fixture成功日志unused-cli PANGU_META_HOST_BUILD_ROOT/SDK_CONFIG未使用，声明准备输入可消噪但不改变不bootstrap契约。

Task 10: complete (commits c33b5d7..23c2c2c, review clean)

Task 11: minor (deferred): CodeGenTests字符串顺序/去重断言先ASSERT存在，避免npos相互比较误通过，最终复审关注。

Task 11: complete (commits 23c2c2c..3c902e6, review clean)

Task 12: minor (deferred): benchmark基线compiler应实际23.1.5而非旧cache23.1.0；负例先断言失败再error()；第三方/非用户warnings摘要保留。

Task 12: complete (commits 3c902e6..e718a2b, review clean)

## Whole-branch review 收尾定向验证（M1–M4）

基于 `0a0a638` 完成 M1–M4 修复，提交 `42067dc35294f677e6020ef6ad7b1b66505abb82`，tree `af9ad3b9ca40001fadcb88279d6a2687981204d8`；本轮仅修改使用文档和正式测试，没有修改生产实现。原 Full/移动矩阵仍按上文实际 SHA 与复用边界保留，不能将本轮收尾提交称为重新通过全量矩阵。whole-branch review 无 Critical/Important；本轮修复的一次 scoped review 已通过。

- M1：更新五参数 Query 登记、双模式 binding、工具 generate CLI/退出码，以及现行增量接线/目标警告策略状态；历史 PoC 仍保留历史语气。
- M2：Meta/Module/EngineHost 新增 metadata 负例通过局部辅助函数绑定一次操作结果，先断言失败再读取 error；致命断言只退出辅助函数，调用者继续 Release/Shutdown。
- M3：CodeGen 顺序与去重断言先逐项确认查找成功，避免 npos 误通过。
- M4：保留 17→29→17，新增单配置 Debug 的 A=17/B=17 往返，检查 CurrentProject、request.project_source/context.database、两次切换后的 stamp 更新和恢复后 no-op/stamp 不变。

Windows 默认 Modular、Samples/Benchmarks=OFF、Tests=ON、override=OFF 保持不变，复用已有宿主 SDK。定向命令均在仓库根运行：

| 命令 | 结果 / 原生 exit |
| --- | --- |
| `cmake --build Build/Windows --target PanguMetaIntegrationTests PanguEngineHostTests PanguModuleIntegrationTests --parallel 2` | 构建通过 / 0 |
| `cmake --build Build/Windows/Tests/BuildTools/PanguMetaTool --target PanguMetaCodeGenTests --parallel 2` | 构建通过 / 0 |
| metadata 定向 CTest（命令见下） | 16/16 / 0 |
| `ctest --test-dir Build/Windows -R '^BuildTools\.PanguMetaTool\.MetaCodeGen$' --output-on-failure` | 根组 1/1、内层 7/7 / 0，不合并调度计数 |
| `ctest --test-dir Build/Windows -R '^PanguMetaProjectSwitch$' --output-on-failure` | 1/1，146.09 秒 / 0 |
| 五个修改的 C++ 文件 `clang-format --dry-run --Werror` | 通过 / 0 |
| 四个 Runtime 测试以 `clang-tidy -p Build/Windows`，CodeGenTests 以 `clang-tidy -p Build/Windows/Tests/BuildTools/PanguMetaTool` | 均为实际编译数据库 / 各0，无显示的自有源码诊断 |

metadata 定向命令：

```powershell
ctest --test-dir Build/Windows -R '\.(ModuleMetadata|EngineHostMetadata|MetaIntegration|MetaUnload)\.' --output-on-failure
```

失败记录：首次构建误写不存在的 `PanguModuleTests` 目标，exit1，未启动编译；纠正目标后通过。首次组合 CTest 正则漏 GTest 目标前缀，只运行 CodeGen 和 ProjectSwitch；后者在 sandbox 路径规范化阶段 Access denied，整体 exit8，不是产品断言失败。提升权限重跑 ProjectSwitch 通过，随后按真实完整名称补跑 16 项 metadata，并单独取得 CodeGen 组 native exit0。原日志保留于本计划临时目录的 `final-fix-*`，测试内层日志/JUnit 仍在 `Build/Windows/Tests/Results/BuildTools.PanguMetaTool.MetaCodeGen/`。

静态检查保留原统计：Runtime suppressed 22234（22152 non-user code、82 NOLINT），CodeGen suppressed 5544（non-user code）；未新增豁免，也不称全工程零 warning。PowerShell 的 stderr NativeCommandError 包装不覆盖已捕获的 clang-tidy 原生 exit0。

M5（noexcept(false) 赋值契约断言）与 M6（Normalize 完整确定性序列断言）明确继续 deferred；不扩生产功能或既有/第三方告警治理。Linux/macOS、移动端和 Full 没有因文档/测试补强重复执行。下方原 29 条 Ruling 内容与顺序保留，末尾追加最终范围裁决。

## 最终评审与收尾

Tasks 1–13 均通过独立规格与质量评审。最终整分支审阅覆盖 `bc05c5f..0a0a638` 的全部 219 个变更文件，结论无 Critical/Important；随后仅一次统一修复 M1–M4。限定复审覆盖 `0a0a638..42067dc` 的 12 个文档/测试文件，M1–M4 全部 ADDRESSED，无新增问题。复审核对已有定向命令、日志与原生退出码，没有重复运行套件。

最终评审确认：负例的辅助函数在失败时记录测试失败并退出辅助函数，调用者继续释放借用/关闭；字符串断言先排除 npos；同参数切换验证真正的 Project source/build、request 数据库、stamp 失效与恢复后 no-op。CLI 失败后完整输出不变已由正式 FailureRecovery 用例覆盖。M5/M6 与既有诊断继续披露，不作为当前实现缺陷或阻塞项。

SDK 预编译分发、任意跨前端/多配置产品支持、额外 ABI/宿主及移动设备运行、动态 Registry/Private 自动访问/序列化仍为首版范围外；本验收不声称交付这些能力。正式规格、计划、长期 Tests 及本记录保留；临时评审包、执行报告与探针的必要结论已归档，清理对象严格限定本计划 `.superpowers/sdd/2026-11-09-pangumeta-tool/`。各平台构建与 SDK 缓存不随计划清理。

收尾核对：`docs/pangumeta-standalone` 分支保留，`feat/pangumeta-tool` 本地引用不存在；最终修复提交后递归子模块与工作区干净，Windows 默认仍为 Modular / Samples及Benchmarks OFF / Tests ON / override OFF。此后的归档提交仅更新计划与验收状态，不改变被测源码或补验结果。

## 执行裁决原文（按顺序保留错误代价）

Ruling: 按用户指定在现有 docs/pangumeta-standalone 分支和共享源码目录原地执行，不创建 worktree 或新分支 — 最新用户指令且三平台需访问同一源码 — 错误代价为本分支承载全部实现，可用逐任务提交审查和回退。

Ruling: 计划中的“本文只编制计划/批准后执行”是编制时状态，用户显式调用执行技能已授权实现 — 执行请求优先于历史交接措辞 — 错误代价为需要回退本地提交。

Ruling: 基线与正式 SDK 等前置条件失败单独记录，不伪装 RED；优先推进可独立工作，平台受限明确报告 — 规格要求真实覆盖 — 错误代价为最终验收仍需补齐环境。

Ruling: Task1 允许 pangu_prepare_build_tool 接受显式 PANGU_BUILD_TOOL_SOURCE_DIR，既有 Incremental 夹具在添加 Engine 前指向其可变工具镜像，layout/discover 共用唯一工具准备路径；目录规范化并验证工程存在，默认正式源码不变 — 新 layout 提前准备工具会抢占既有镜像发现路径，必须保留原工具源码失效回归 — 错误代价为新增一个需维护的工具准备输入，相关生产/夹具定向验证必须覆盖。

Ruling: 允许先备份Mac唯一正式根CMakeCache到本计划临时目录，再以cmake --fresh --preset macos定点重建顶层配置元数据，保留目标产物/Tests/Work/嵌套工具和其他宿主状态 — 已核实home路径字节完全相同且源/生成器可读，但旧cache持续触发source身份不匹配；Task1明确要求旧缓存迁移，配置元数据可再生 — 错误代价为该根重配成本；仍失败则继续诊断而不扩大清理。

Ruling: Task1部署生产者还需追踪已部署Runtime DLL的存在性，补删除目的DLL后普通build恢复的RED和最小修复；先结束正在跑的冻结提交验证，再另建修复提交 — 已有stamp但目的DLL丢失时普通build成功却Host无法直接运行，违背部署产物的实际可用契约 — 错误代价为新增输出追踪与回归维护；必须保留no-op及DLL变化不强迫Host重链接。优先原生OUTPUT/BYPRODUCTS，受genex限制时可用经验证depfile。

Ruling: Task1 I1采用当前配置OUTPUT_DIRECTORY属性优先、基础/BINARY_DIR回退，实际复制/depfile沿原生TARGET_FILE_DIR；支持的标准配置表达式需归一化用于目的地去重，不引入通用genex解释器；无法安全确定身份的target-dependent表达式应明确拒绝并记录限制 — CMake配置期不能通用求值任意target-dependent生成表达式，本轮Ninja单配置契约需要可证明唯一生产者 — 错误代价为部分自定义Host输出表达式暂不支持，后续有实际消费者时再扩展；不能静默错误部署或重复生产者。

Ruling: Task3的ModuleMetadataAccess声明头放Module/Public/，实现仍Private/；保留detail命名和Manager私有AcquireMetadata — EngineHost需要一个可声明的跨Module桥，不应把Module整个Private包含目录暴露给上层；规格未冻结文件拆分，该声明不是提供者回调或Registry — 错误代价为新增一个可见detail声明头，后续可按内部接口策略收窄。

Ruling: Task4可使用本地已有Git参考提交ea7d852a70e8bdfaf601d6626a760f9771b2c4b4准备独立LLVM23.1.0源码检出，保留现有LLVM main/22.x和安装不变、不联网；Prepare.cmake自身仍只验证源码HEAD、绝不checkout — 当前主源码是24.x、现有Win/Linux SDK无RTTI，而本地23.1.0对象已存在，计划明确要求显式准备兼容SDK — 错误代价为额外独立源码目录和构建存储/时间，不改变其他工作区；独立源码需可供约定共享路径访问，不能放将被计划收尾删除的临时目录。

Ruling: 仅显式Linux SDK初始化flags抑制unused-command-line-argument，清理本任务SDK根受污染特性探测缓存后重配，由原生依赖重编必要对象；不关闭LTO/PCH绕过PIC，不改RTTI/EH/stdlib/优化，不削弱自有工具warning — 已有具体探测误判和PCH模式不一致证据，需要修复真实PIC配置 — 错误代价为SDK额外重编成本及一项SDK专属诊断抑制，保持源码/配置/失败日志可审查。

Ruling: 若Linux SDK非即将结束，允许一次受控终止本任务Prepare/子Ninja后改为纯cmake --build同SDK根增量后台续跑，固定PID/日志/退出码文件；代码提交与Win/Mac验收完成后Task4以环境concern交controller，后续单一实现仅Win、不使用Linux BuildRoot，SDK结束于稳定提交边界显式Check，Task13前补齐Linux全部验收 — 避免Prepare后置预检读取正在被Task5修改的工具源码，同时不空等长SDK原生重编 — 错误代价为额外后台作业交接与稍后发现Linux问题的返工；必须在task-4-linux-sdk-handoff.md留完整命令/状态/路径，绝不宣称Linux已通过。

Ruling: IR/FieldInfo的语言IsConst与属性ReadOnly/IsReadOnly保持独立，Normalize不得OR合并；写能力仍同时检查const与政策 — 接口B与Task2已经明确分离语言事实和规范化属性政策，规格的实际只读状态由二者共同约束而非覆写字段 — 错误代价为若消费者错误只检查政策会需修正；Task7/9/12 brief已同步两个标志与实际写能力的合同。

Ruling: Task6为prospective输出身份必要时可在Frontend私有平台适配用原生API，std值接口与MetaIR/Normalize/Model无Windows/Clang依赖边界保持；不采用locale宽字符lower模拟 — std::filesystem不提供尚不存在路径的完整原生大小写能力，Task5通用层已明确将此检查交Task6 — 错误代价为新增小平台适配与支持模式诊断维护；必须真实existing等价与prospective判断分开测试，不能静默覆盖。

Ruling: Task6支持邻接CMakeCache证明的单配置Ninja数据库，严格匹配CMAKE_BUILD_TYPE/request.Config并拒绝多配置/未知生成器；object显式精确source/object绑定，空object只允许OutputRoot/Private/MetaContext.cpp唯一命令 — 编译数据库自身无target/config权威字段，本轮规格限定Ninja单配置，不能选第一条或从对象名猜目标 — 错误代价为外部手写数据库缺CMake元数据时暂不支持；fixtures需提供元数据。Task10专属Source禁CXX_SCAN_FOR_MODULES/PCH，避免modmap/生成依赖循环。

Ruling: Task6的纯BuildOutputMapping计算/集合校验归Model，原生身份验证归Frontend私有，CodeGen只复用纯映射 — 接口D要求纯CodeGen/Model不反向依赖Frontend/LLVM，不能以复用便利破坏边界 — 错误代价为多一个小验证接缝；Task9/10 brief已同步映射和专属Context精确路径。

Ruling: 每Target/Config/OutputRoot唯一发布批次/状态/原生producer；有Meta头的Module用scan并在Task11把已知ModuleQuery槽位合入同一outputs/Generate/Publish，无Meta头用query、Host用catalog；装配仍在Request不进IR — 各kind共享同OutputRoot与Meta.outputs/index/stamp，独立scan/query任务会互相覆盖状态/删除对方产物，是跨任务load-bearing计划缺口 — 错误代价为scan支持一个显式已知装配槽位与Task11联合发射接线，避免另增协议字段/状态根；6/8/9/10/11 brief已同步，CMake按link mode选择槽位，Tool不猜模式。

Ruling: 对无法可靠分析的项目内未选择前端条件include，Task7明确unsupported_condition；已选择不含反射的普通include、无关探测、普通平台条件与系统头仍保持允许 — 该分支未由SDK真实预处理，不能凭源码猜测目标侧是否引入反射而静默漏生成 — 错误代价为部分与反射无关但未选择的版本条件include也需调整或未来扩展分析；这是明确的首版保守输入边界，报告与正式正/负例必须保留。

Ruling: Task8可将纯FS OutputMappingValidation从Frontend编译归属下沉PanguMetaOutput并提取ValidateOutputPaths，Frontend单向链接Output，Model/CodeGen边界不变 — 避免复制原生Unicode/目录能力验证，符合纯Output不依赖Frontend — 错误代价为一次目标依赖接线迁移，原Context/Ownership回归须保留。

Ruling: Publish可先提交旧∪新ownership恢复index，再文件/depfile/废弃删除，末尾精简index+stamp；全部预检/候选准备和撤旧stamp在任何索引/目标替换前 — 既能保留失败删除的旧ownership，也能追踪部分写入的新文件，下一轮可完整协调 — 错误代价为失败期间index是并集而非当前成功集合，消费者只能以最终stamp判断批次成功，需正式故障恢复测试。

Ruling: Task9保持登记HeaderInput Root/Path/Prefix与输出槽位不变，在Scan/Driver真实FS边界先把当前Target Type.Source唯一绑定到登记Path，供纯Generate仅词法匹配；canonical依赖集合不改 — canonicalize私有Request的Root/Path可能改变别名/根内映射，而纯CodeGen不能做FS探测或依赖Frontend — 错误代价为SourceFile登记路径归一/一个私有Model副本绑定接缝，需别名/同basename/Unicode共同消费回归；不能随意取第一个或折叠symlink/..改语义。

Ruling: Task10允许显式PANGU_META_TOOL_SOURCE_DIR及真正Engine源根输入，源失效fixture仅复制独立Tool子树到正式Root/Tests工作根，Runtime/支持头/cmake始终原共享源，Tool缓存隔离且固定身份 — 正式增量回归不能修改共享源码，又必须验证宿主源码依赖重建，沿用Task1同类可复用准备接缝 — 错误代价为一个source准备输入与测试缓存身份维护；不得覆盖生产缓存、不能复制Engine树或变成长期功能过渡开关。

Ruling: Host Catalog采用固定pg::module::GeneratedStaticModuleCatalog() noexcept返回既有StaticModuleCatalog，声明新增Module/Public/GeneratedModuleCatalog.h，Host生成CPP持静态表并定义，不改Foundation/JSON、不暴露escapedTarget手写声明 — v1 catalog无导出函数字段且每Host唯一Catalog，固定API便于实际消费并保持Foundation轻量 — 错误代价为新增一个仅登记Monolithic Host提供定义的声明接口，未登记误调用会链接失败，文档需明确。

Ruling: PanguNullQuery无Definition/既有Id，作为恶意ABI fixture保留手写extern C包装并退出正常Query登记，不伪造Id — 正常Module身份协议不适用于故意返回nullptr负例，原覆盖必须保留 — 错误代价为一处明确测试特例，需证明没有同Target重复包装；其他有效Module全生成迁移。

Ruling: 未Active负例可真实生成候选经Manager Load/Register再生产detail::ModuleMetadataAccess.Acquire断言kNotReady，仅此失败路径走下层；成功描述/Pin/物理卸载/实例仍全走Host高层与Scope — Host成功Start已Active且回调HostBusy优先，无可达公开未Active窗口，不能为测试伪造Host状态 — 错误代价为该一个error顺序用例属于生产下层契约而非Host端到端，报告须明确且不使用TestAccess/人工表。

Ruling: 让当前Unix冻结full正常完成后统一加协作Host现有部署调用并新提交；Windows定向RED→GREEN及Mono/Modular full必须补齐。Unix可复用旧固定e5 Full，但需helper非Windows立即return及新旧有效图/编译链接命令相同证据，新固定提交补Ownership相关fixture/noop；若有Unix图/行为变化补相应full，每行真实SHA记录，绝不称最终SHA六轮均重跑 — 已证修复仅Windows DLL部署，强行中断/重复完全等价Unix full无新增覆盖 — 错误代价为若等价判断遗漏Unix影响需补全量，保留源差异/命令证据供最终审阅。

Ruling: 以当前模式为冻结结束边界（Linux当前Modular/Mac当前Mono），不再故意进入已知旧MacModular；统一仅修Win协作Host部署和ManualType POSIX visibility后新固定SHA。新SHA跑WinMono/Modular、LinuxModular、MacModular Full；旧e5成功LinuxMono/MacMono Full保留，并新SHA双mode相关ModuleMetadata/HostMetadata/生成MetaIntegration-Unload定向、Rtti符号/可见性及no-op，正式表每行真实SHA；若修复或证据影响更广则补相应Full — 两项均既有fixture接缝，避免无意义中断/重跑其他完全未改生产路径，但失败模式必须新Full — 错误代价为若fixture影响判定遗漏需补全量，不能宣称最终tree同SHA六轮均已跑。

Ruling: 复用28b086c Windows Mono完整172/172 CTest自身逐测试成功记录及JUnit，wrapper原生exit明确记为未捕获，结合本轮前正式Ownership exit0补验，不为重获包装退出码重复约30分钟全量；Modular和恢复轮须持久化原生exit — 原会话失联只丢父包装状态，CTest自有完整起止、逐项Passed及固定源码均已核实；重跑同tree无新增功能覆盖 — 错误代价为无法排除包装程序在所有测试完成后的异常退出，最终报告必须保留此证据限制。

Ruling: 保留Windows R2Mono逐例172Passed/aggregate exit未知记录；当前WinModular完成后只补一次WinMonolithic Full，用独立持久脚本CTest返回立即原子落native exit，再恢复Modular必要定向，已有完整receipt Unix Full不重复 — 正式计划要求成功具备原生exit0，日志功能覆盖不能替代丢失退出码，这不是因为SHA/分支变化重测 — 错误代价为一次已运行模式的额外Full时间/计算成本，换取可复核完整验收；不得伪造0或抹去丢失轮次。

Ruling: 保留原controller唯一执行链及已经启动的R3 native receipt补验，后启动恢复controller撤出调度，不再按自己“无需补R3”的较早裁决另行恢复或评审 — 已证实两独立controller并发，而原链已有持久native退出码方案；统一所有权优先于各自继续 — 错误代价为R3额外全量成本仍存在，后启动窗口需等原controller完成，但避免重复实现/提交/评审和BuildRoot冲突。

Ruling: 最终M5（noexcept(false)断言）和M6（Normalize完整序列断言）保留为非阻塞改进；SDK分发/任意前端或多配置、额外平台/设备运行、动态Registry/Private自动访问/序列化及既有第三方告警治理继续遵循已批准首版范围 — 当前实现契约与有效覆盖已通过独立评审，未发现需扩大交付的生产缺陷 — 错误代价为两类未来回归的定向断言较弱，范围外能力仍需后续独立工作；不得将现有验收扩大为这些能力已交付。
