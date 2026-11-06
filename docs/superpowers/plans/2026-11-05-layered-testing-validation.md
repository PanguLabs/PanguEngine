# 分层测试验收记录

状态：已完成（2026-11-06）。Windows/Linux/Android 的源码基线为 `28398f78c7eee1be4e8f901445b6eaff6376d94f`，macOS/iOS 为 `bf6657f32476c7c5b336ebbebac2974cb69fc6d8`；此后至收尾仅有文档改动。Task 1–9、整分支评审、限定复评及本计划临时目录清理均完成，四项非阻断维护项如下保留。各任务评审见[实施计划](2026-11-05-layered-testing.md)。

## 桌面与移动端

Windows 和 Linux 正式配置显式启用 `BUILD_TESTING`、`PANGU_BUILD_ENGINE_TESTS`、`PANGU_VALIDATE_TOOLCHAIN`，关闭 `PANGU_BUILD_BENCHMARKS`，分别执行 `cmake --workflow --preset <platform>-fast`、`<platform>-precommit`、`<platform>-full`。fast 均 59/59，precommit 均 66/66。Windows full 重跑 86/86、Linux full 重跑 85/85，首尾 SHA 一致；包含三组 BuildTools 和 Plugin 来源、开关及消费者矩阵。独立宿主 CTest 各 40/40，单会话 `-j 2` 各 40/40；Manifest/Discovery 直接批量、乱序各重复两轮通过，独立结果不与根 full 合并计数。

Windows/Linux full 首轮各有六个 Project 用例因 Google Benchmark 子模块索引损坏失败；Android Cross 首轮同因失败。索引原件已备份，基于固定 HEAD 重建并通过对象及工作区检查。共享仓库根及三个子模块的本地 `core.autocrlf=true` 与原 Windows 系统配置对齐，修正 WSL 对同一 CRLF 工作树的误报；未改源码、refs 或全局配置。两平台 full 和 Android Cross 随后完整重跑。恢复审计及原始配置备份位于 Windows `Build/Windows/ValidationResults/task-9-20261106/git-recovery/`；首轮与重跑日志分别在同级 `logs/`。Linux 重跑的非登录 shell 首次缺少 `LLVM_ROOT`，未进入测试；改用登录环境后通过。

Android NDK 30.0.16248370 / Clang 21，`cmake --preset android-arm64 -DBUILD_TESTING=ON` 和对应 build 通过；独立 LinkSmoke 最终链接为 ELF64 AArch64。显式运行 `CrossCompileTests.cmake`（绝对 Engine 源路径、Android BuildRoot 内独立测试根、`PANGU_TARGET_PRESET=android-arm64`）通过 Game/Editor 最终链接、SHARED 部署和宿主工具检查，目标文件头均为 AArch64。交叉配置未发现或运行目标 GTest；没有设备运行结论。

macOS 使用共享源码 `/Users/moozen/WindowsGit/PanguEngine` 和本地 `$HOME/PanguEngineBuild/macOS`，首尾 SHA 均为 `bf6657f32476c7c5b336ebbebac2974cb69fc6d8`，工作树干净且固定依赖匹配。显式启用三项正式测试开关、关闭 benchmark 后，`cmake --workflow --preset macos-fast` / `macos-precommit` / `macos-full` 首轮分别 59/59、66/66、85/85，均 exit 0；测试耗时 4.05、3.92、990.13 秒，无跳过。独立宿主 CTest 串行及 `-j 2` 各 40/40；Manifest 和 Discovery 分别直接乱序两轮，28/28 和 11/11 均通过，不与根 full 合并计数。

iOS 显式 `cmake --preset ios-arm64 -DBUILD_TESTING=ON`、对应 build、Toolchain README 中独立 LinkSmoke、以及传入绝对 Engine 路径、iOS BuildRoot 内独立测试根和 `PANGU_TARGET_PRESET=ios-arm64` 的 `CrossCompileTests.cmake` 均 exit 0。交叉配置跳过 CTest 可执行程序发现，`ctest -N` 为零项；Game/Editor 最终链接、SHARED 部署及宿主工具执行通过。LinkSmoke、Game/Editor 与构建及部署的 dylib 均为 Mach-O ARM64，`LC_BUILD_VERSION` 为 iOS platform 2、最低 18.0、SDK 18.2，不表示设备运行。系统默认 `xcrun --sdk iphoneos` 因仅选择 Command Line Tools 而失败；项目现有工具链回退到 `$HOME/Library/Developer/SDKs/iPhoneOS18.2.sdk`，实际缓存及编译命令确认该路径，未改系统设置。首次诊断保留在 `environment.log`。

本轮 Mac 原始命令、首尾 SHA、exit、日志及 JSON 位于 `$HOME/PanguEngineBuild/macOS/task-9-mac-resume/`；iOS 交叉夹具及其子命令日志位于 `$HOME/PanguEngineBuild/iOS/task-9-cross/`。宿主 macOS 14.8.9 x86_64，CMake 4.4.3、Ninja 1.13.2、MacPorts Clang 23.1.2。构建状态与先前运行的验证隔离为单会话，日志记录首轮结果。

## 基准与耗时

Windows Release、Clang 23.1.0 / MSVC STL，Ryzen 5 7500F（6 核 12 线程），Google Benchmark v1.9.5 `192ef10025eb2c4cdd392bc502f0c852196baa48`。空闲机器运行 `PanguModuleBenchmarks --benchmark_repetitions=5 --benchmark_out_format=json`；六场景各五个原始样本检查迭代次数、单位、repetition index 和错误字段。原始 JSON、宿主信息位于 `Build/Windows/BenchmarkResults/task-9-20261106/`；此前可能与索引检查重叠的一轮另存，未用于基线。Catalog 测量包括 Host 析构。

| 场景 | CPU 中位数 ns | CPU 变异系数 |
| --- | ---: | ---: |
| ModuleIdentityParse | 24.31 | 1.10% |
| ModuleIdentityFormat | 3.07 | 1.38% |
| ScopeAccessBorrowRelease | 21.00 | 0.00% |
| CatalogCreate/1 | 465.26 | 0.97% |
| CatalogCreate/8 | 2905.28 | 1.13% |
| CatalogCreate/32 | 24065.02 | 0.98% |

Linux 和 macOS 基准均已编译、链接并对六场景 dry run；macOS JSON 逐项核对名称、迭代、单位及无错误，原始 JSON 与检查日志位于上述 Mac 结果目录。dry run 不作为性能基线或与 Windows 绝对值比较。Windows fast/precommit 暖缓存 build 分别 0.44/0.91 秒、test 2.24/3.11 秒；Linux 分别 build 0.33/0.17 秒、test 0.51/0.66 秒；macOS 分别 build 0.85/0.93 秒、test 2.46/3.56 秒。这是另行计时轮，非首次 workflow 构建耗时。

## 静态检查与已知限制

分支自有 C++ 变更 `clang-format --dry-run --Werror` exit 0；Runtime/基准以根编译数据库、工具及测试以独立宿主数据库、Plugin 以实际复制夹具数据库运行定向 `clang-tidy`，均 exit 0。工具生产文件仍有既有 include-cleaner、const、optional、数组与位运算诊断；根数据库检查抑制非用户代码警告及 5 处 NOLINT。上游 Google Benchmark 配置有无异常 `std::regex` 支持提示；这些不记作零诊断。普通 Windows Ninja 二次构建显示 `no work to do`。

已初始化固定依赖后，以仅进程有效的 `GIT_ALLOW_PROTOCOL=file` 及指向确认未监听的回环端口的代理变量，分别对根及独立宿主 configure 执行 `--trace-expand --trace-format=json-v1`，两者 exit 0。审计展开事件未发现 `file(DOWNLOAD/UPLOAD)`、FetchContent 获取或 Git fetch/clone/submodule update；根工程 Git 调用为本地版本查询。这证明本次已初始化依赖后的配置没有获取或更新依赖，不代表操作系统级物理断网。此前 `FETCHCONTENT_FULLY_DISCONNECTED` 被报告未使用，以及初次选中的代理端口已有监听，均未作为此证据。

本轮 full 的 `TestingBenchmarkIsolation` 验证同一构建树在 benchmark-only、正确性测试、组合、关闭和恢复之间切换，检查零正确性条目、无 GTest/GMock、目标恢复以及缺失依赖负例；`SourcePluginProject` 验证嵌入默认关闭 Engine 测试时保留父 Project 测试及分类反例/恢复；`SourcePluginTestOwnership` 核对未启用、Project-only、Engine+Project、Provider-only、全部关闭等配置、标签与真实消费者。前者的 `commands.log`、后者五组 cache、分类日志和两桌面 full 输出已归档。CTest 空图清理限 standalone Engine，不能清除父 Project 图。

按 [MIGRATION](../../../Tests/MIGRATION.md) 的逐项映射核对旧触发、失败路径与当前入口；分组审计和本轮结果在 `ValidationResults/task-9-20261106/logs/task-9-migration-audit.md`，三项独立工具测试均单独列入。历史 macOS 重配前缓存文本未独立保存的审计缺口无法追溯补造，当前源码验收已有独立完整证据。

## 整分支评审与后续维护

最终评审范围为 `4733a21287305d32aa3765463f83567b9321a7f1..3aa2f10596b8619b8d3d74c6335027ab928af63b`，29 个提交、99 个文件；结论 Ready to merge: Yes，Critical 0 / Important 0 / Minor 6。单轮文档修复 `b77ace3` 解决 M1 迁移状态及 M2 筛选、三桌面基准命令；限定复评确认两项已解决、无新增 Critical/Important/Minor，M3–M6 仍为非阻断维护项。文档检查、只读 CTest 列举与各平台基准程序路径核验通过，未重复平台全量或正式性能测量。

必要结论及 28 条裁决已归档；完整任务和最终评审报告、修复报告、账本保存在 `Build/Windows/ValidationResults/task-9-20261106/review/`。确认 Windows、WSL、macOS 均无残余验证进程且子代理结束后，仅删除 `.superpowers/sdd/2026-11-05-layered-testing/`，保留正式 Tests、其它计划目录、各平台 BuildRoot 和原始结果。当前分支保留，未合并或推送。

- M3：未来逐项解析 benchmark JSON，核对名称、正迭代、单位、错误及 context，现有正式样本已逐项核对。
- M4：未来在清理后分别断言 retained 快照各字段，保留 Busy 清理顺序和覆盖。
- M5：生产工具既有 include、const、optional、数组及位运算 tidy 诊断留待针对性维护；exit 0 不表示零 warning。
- M6：注册夹具 Windows `getenv` 弃用告警留待局部处理，保留 pass/fail/skip 传播。

评审保留的边界：历史 Mac 重配前 cache 文本未保存；任意 Unicode BuildRoot 的上游文件打开行为、跨 CMake 目录分散登记同一工具、无异常模式下任意非法 benchmark 正则均未判定。Android/iOS 设备运行、真实动态 Loader/CodePin、额外压力规模和跨宿主性能退化门禁不属本次范围。Windows Junction 是目录越界等效夹具，文件 symlink 与多级改指另有成功验收；中文/Emoji 原生输入保留，binary 使用 ASCII 含空格路径。原始失败、索引/锁恢复、换行配置及日志保留，旧无 RTTI patch 与 UAC 路径已被用户后续政策覆盖。上游 regex/format 提示和非用户 tidy 抑制不等于零诊断；分阶段桌面、ownership 和移动端延期由 Task 9 的当前源码收据关闭，未拼接局部数量冒充 full。

## 历史执行裁决归档

下列裁决保留执行时的依据和代价；历史无 RTTI 与 UAC 路径已被后续用户的保留 RTTI、Developer Mode 普通令牌要求覆盖，不构成当前政策。

- Ruling: 在现有干净专用分支就地执行，所有 worker 共享 D:/Git/PanguEngine — 用户先前明确将设计移到此新分支，仓库要求三平台读取唯一共享源码和固定 BuildRoot，原生工具未列出可复用工作树；不再复制源码或切换分支 — 判断错误的代价是分支变更需回退，因此每任务提交并禁止并行实现。
- Ruling: 使用 Git Bash 运行技能脚本时按沙箱实际需要提权；只操作本计划 scratch — 普通 Git Bash 在命名对象创建处被拒绝，提权后两个脚本已成功 — 只增加本计划忽略文件，不更改产品。
- Ruling: baseline 若失败先保留证据并诊断；可继续与失败无关的迁移，不把旧失败写成成功 — SDD 持续执行规则及 spec 平台完成门槛共同约束 — 最终仍须补齐全部必要验收，不能绕过失败结项。
- Ruling: 新登记接口使用公开 CMake 4.0+ API，并验证当前安装的 4.4；不得依赖私有 GoogleTestAddTests 实现 — spec 明确最低版本，当前宿主已安装较新版本 — 可能增加少量适配与配置回归。
- Ruling: 普通工具 bootstrap 始终关闭测试；宿主测试仅由测试专用构建准备目标调度，不挂普通 ALL — spec 与计划对 no-op、独立宿主图的共同要求 — 直接旧版 ALL 后运行 full 需按新文档先执行对应测试构建入口。
- Ruling: Task 1 完成 Windows 旧完整基线/工具三项及新框架聚焦验收，Linux/macOS 仅做新框架/Foundation聚焦 — Task 9 才承担当前最终源码的三桌面完整矩阵，避免每任务重复整计划验收 — 代价为其它平台旧完整基线不在本任务重新测量，报告须明确范围。
- User steering after Task 1: questioned reappearance of HostTool. Verified build-system.md forbids a generic Bootstrap framework while ADR-0014 retains concrete native PanguBuildTool execution. Neither planned HostToolTesting file exists in implemented code. Ruling: pending Task4 is PanguBuildTool-only wiring, not a tool registry/category/framework; rename its paths, function, targets, CTest names and variables from HostTool to BuildTool, and explicitly bind this constraint in spec/plan. Refresh only pending briefs; historical task reports stay unchanged. Product implementation Task1 is unaffected. Cost: a documentation/API naming change before those APIs are implemented.
- Task 2 initial review: Spec Compliance pass, Task quality Needs fixes. Important I1: ScopeAccessConcurrencyTests.cpp:12 duplicates ScopeAccessTests.cpp:22 member/gate/type initialization and Access method (~20 lines). Read fix-loop.md for this round; no plan-mandated duplication. Ruling: permit a Module-private Fixtures/ScopeAccessFixture.h used by both TUs, preserving per-case fresh instances, exact setup and public production interfaces; this is a small test-local common definition, not a general test framework. Cost: one additional private header outside original Files list.
- Ruling: 继续现有 docs/layered-testing 干净专用分支及原宿主 BuildRoot — 延用已记录工作区裁决与共享源码约束 — 每任务提交、串行实施与单独评审控制回退边界。
- Ruling: 以当前 b51ffec 版本的 spec/plan 命名为准；PanguBuildOptions 仅有解释讨论，未批准改变其策略 — 避免把讨论当设计变更 — 后续需要改变则另记明确授权。
- Ruling: 允许Task4为固定GoogleTest v1.18 Windows/MSVC STL无RTTI组合添加最小第三方Patch，独立兼容修复提交；只在MS STL且无static RTTI时屏蔽不可用std::any printer，Repo保持干净，BuildRoot副本按wiki Patch标准应用/校验，覆盖不匹配与重复配置 — 实际gtest-printers.h:916–937无条件引用类型，MS STL在-fno-rtti时不定义any；Spec要求沿用工具无RTTI、允许最小兼容配置，优先于计划默认不复制源码的假设 — 判断错误的代价是多维护一份固定基线最小补丁，可随上游修复移除；不改变生产ABI或编译策略。
- Ruling: Task4目录链接逃逸场景在Windows使用原生Junction，Linux/macOS仍使用真实目录symlink；测试先确认canonical链接目标/越界，再检查生产路径拒绝，命名/迁移说明明确平台差异，构造失败仍FAIL — 原Discovery行为是目录链接canonical越界，现有BuildToolInvalidation也使用Windows Junction，符合Tests维护规范按平台等效行为保留；Windows普通账号缺symlink权限 — 判断错误的代价是仍需另补Windows目录符号链接专门验收；此裁决不覆盖文件symlink/多级文件改指，不能据此降低Task5关键场景。
- Ruling: Task4通用夹具binary采用ASCII带空格路径，source副本及CLI/真实可变root继续使用中文/Emoji/空格，文档不声称任意Unicode BuildRoot支持 — 正式BuildRoot为固定ASCII路径，原需求是原生输入与可变夹具路径；新加Unicode binary目录触发上游GTest窄字符JSON文件打开限制，非原功能退化 — 判断错误的代价是任意Unicode构建目录仍需后续专项适配；不减少原CLI路径判据。
- Ruling: 将此次RTTI策略修订作为独立实施任务/提交处理，再对Task4全范围及修订一起评审 — 用户直接覆盖旧决定，需移除已无必要的复杂兼容分支而保留其它已验迁移 — 判断错误的代价是额外一次受影响构建与文档修订，不改变无异常策略。
- Ruling: Task5将额外发现的正式脚本NlohmannSubmoduleRequired纳入统一metadata/full登记，并允许最小修改Testing辅助与BuildTools共享预算接线，让嵌套build/CTest和外层PROCESSORS从PANGU_TEST_BUILD_JOBS一致消费（默认2保持），无需新增调度接口；测试脚本通过cmake -E env向bootstrap继承CMAKE_BUILD_PARALLEL_LEVEL，不改普通生产bootstrap — 计划明确要求全部原脚本分类和嵌套预算，原Files列表未枚举该正式脚本及部分公共预算点 — 判断错误的代价是扩展少量共享接线文件与定向回归，需Task5独立评审；不重做Task4迁移、不改变普通ALL行为。
- Ruling: 已获用户仅提升必要测试进程授权的固定helper，本次仅使用进程启动参数-ExecutionPolicy Bypass来执行已审阅脚本，不调用Set-ExecutionPolicy、不改任何持久设置；已启动的唯一验收继续，不为参数形式中断重跑 — 原入口仅被默认脚本策略阻止，实际动作仍为用户授权的固定CTest；机器/用户策略不变 — 判断错误的代价是该短暂进程的脚本限制比默认宽，终止进程后不保留；后续优先使用原生CMake/CTest提升helper避免此参数，不扩大到其它操作。
- Ruling: Task6 Windows执行新增Project/Plugin归属矩阵及计划列出的现有回归，Linux/macOS本任务聚焦改动的宿主C++/CLI生产路径，最终Project/Plugin全矩阵由Task9当前源码全量统一覆盖 — 避免每项反复构建昂贵独立Project图，保留全局三桌面完成门槛不变 — 判断错误的代价是其它宿主的CMake整合缺陷到Task9才发现，Task9未通过不得结项。
- Ruling: 用户“允许仅提升测试进程”授权涵盖一次固定的完整CTest测试进程及必要子进程，可在普通configure/build准备后UAC提升其单一完整会话，不要求先重跑一套已知1314失败的普通全量；不提升Git/普通准备、不建服务/不改持久设置 — 正式完整验收需要该进程继承文件symlink能力，单case提权会拆成多轮而不再是一轮全绿；用户已授权同类Task5执行 — 判断错误的代价是该短暂CTest下其它用例也拥有管理员令牌，测试结束退出，范围不扩到其它操作。优先原生CMake/CTest固定helper，避免PowerShell执行策略参数。
- Ruling: Task7允许在Files清单外对根CMakeLists.txt添加最小full前提调用/接线，使顶层Engine在测试开关关闭时仍能对正式full给出明确拒绝，同时保持嵌入Project仅自有full不继承Engine三开关 — 计划要求的行为不能只放在被BUILD_TESTING gate跳过的Tests目录中实现，属于必要producer调用点 — 判断错误的代价是多改一个根接线文件，须通过关闭开关/Project-only回归及任务评审；不改变普通ALL或强改父cache。
- Ruling: 原Task7 gpt-6.1-sol代理容量不可用，替换为同级gpt-6.1-terra/high继续同一任务 — 服务明确报capacity，角色为多文件CMake集成且标准编码模型足够，避免等待/重做 — 判断错误的代价是部分实现需再理解，仍用独立评审门确保质量；不增加并行实现。
- Ruling: 允许仅standalone顶层Engine在BUILD_TESTING=OFF时将其自有BuildRoot生成CTestTestfile.cmake写为空关闭图，恢复ON由CMake正常生成，并补同树ON→OFF/benchmark-only→ON往返回归 — 新开关隔离契约要求无旧注册，CMake默认不清理旧CTest文件造成可运行旧产物；这是已实际复现问题 — 判断错误的代价是自有生成根图在关闭配置时被清空，故严格禁止触碰嵌入Project父图/程序/缓存或强改父cache，独立评审检查边界。
- Ruling: Task9的Step8整分支评审/临时目录清理由控制器在Task9实现、平台验收和局部任务评审之后执行，worker不得自己派评审/提前清理或宣称整计划完成；Task9先交付步骤1–7及对应文档提交，最后由控制器补Step8最终状态 — 原计划把全局收尾列在最后任务内，而SDD要求全部实现任务后再全局评审，需划分责任避免循环门禁 — 判断错误的代价是最后多一次纯文档进度提交；全局review/cleanup与验收要求均不省略。
- Ruling: 在完整备份及临时索引证明工作树无改动后，按Git index.lock协议原子替换仅GoogleBenchmark派生index，不改源码/对象/refs — 中断后实际索引损坏阻断所有clean-state验收，固定树内容完整 — 判断错误的代价是原暂存状态需从已保留的原index/对象取回，故保留完整备份且无用户工作树差异才操作。
- Ruling: 将当前共享PanguEngine及GoogleBenchmark/GoogleTest/NlohmannJson仅本仓库core.autocrlf=true，保留各config备份，不改全局配置或工作文件 — 原Windows系统Git为true，Linux默认不同把同一CRLF文本误报modified；跨宿主共享源码需一致归一化 — 判断错误的代价是这些本地仓库后续checkout/提交的文本归一化行为变化，可用备份恢复；提交树与源码字节未改。
- Ruling: 对与00:29中断同时间的本Root零字节index.lock，在Windows/WSL无Git写进程、时间/长度未变并独占读写打开成功后，将确切原件移动到git-recovery归档再恢复正常暂存 — 已确认无活动持有者的残留锁阻断提交，保留原件可审计而非删除未知锁 — 判断错误的代价是一个停滞Git事务需手工恢复，因此仅该锁且完整保存收据，不触碰其它锁。
- Ruling: 仅续Task9剩余Mac/iOS与全局评审，保留已通过且无源码变化的非Mac证据 — 账本及Git证实Task1–8完成、Task9非Mac阶段已评审，不重做已完成工作 — 若遗漏隐含跨平台影响，最终整分支评审再要求有依据的针对复验。
- Ruling: M3–M6保留为非阻断维护项，本计划完成但不宣称零债务 — 最终广审及限定复评确认本轮逐项性能样本有效、旧生命周期覆盖保留、工具诊断为既有建议、getenv无并发写入缺陷 — 判断错误的代价是未来缺字段的基准条目可能漏报、生命周期失败定位较慢及新告警易被旧噪声掩盖，后续改动相关位置时须定向改善。
- Ruling: 最终评审未判定的五类边界继续显式保留：历史Mac cache文本不可恢复；任意Unicode BuildRoot；跨目录分散工具登记；任意无异常非法benchmark正则；设备运行/动态Loader/额外压力/跨机性能门禁 — 前四类为已披露证据或支持范围，最后一类为spec非目标，当前已交付行为已独立核查 — 判断错误的代价是将来扩展这些使用方式时需另补实现与验证，不能沿用本次完成结论。

后续版本验收见 [Google Benchmark v1.10.0 定向验收](2026-11-06-google-benchmark-1.10.0-validation.md)；本记录及其原始结果仍为 v1.9.5 历史。
