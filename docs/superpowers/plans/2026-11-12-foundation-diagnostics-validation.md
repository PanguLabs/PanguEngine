# 基础失败现场与 Log 正式验收

> 状态：实现、任务评审、全分支评审及唯一修复复审完成；原 Full 与最终修复的必要定向分别归档，必要结论归档并核对无运行依赖后，临时材料已精确清理。
> 设计：[批准规格](../specs/2026-11-12-foundation-diagnostics-design.md)；实施：[Task 1–12](2026-11-12-foundation-diagnostics.md)。

本轮实现 Foundation 拥有型失败现场、显式 Assert/Check/Fatal，以及 Host 独立拥有的有界异步 Log Module。wiki 是长期契约权威；Implemented 不代表 Public C++ API、布局和跨平台 ABI 全部 Frozen。正式 Tests 保留。

下列原 Task12 章节保留截至 `0b7a097` 的实际版本与历史结果。最终 RawSink/异步测试修复的版本、定向验证及当前配置另见本文末节与[最终评审归档](2026-11-12-foundation-diagnostics-review.md)，不能将旧 Full、旧工具版本或历史性能表外推为新实现的实测。

## 版本、环境与依赖

执行基线 `fb631f6912acd29cfebc6acc4b262a8a0087a813`，计划代码基线 `78c8306`；Task12 起点 `32e53382d4beee66c5ff35e916461bcea5661968`，tree `d3635f9921a8b9a0e7a4495e9a350581fdb2c06c`。

| 验收范围 | 实际被测 commit | Git tree |
| --- | --- | --- |
| Windows/Linux 两模式 Full、Android/iOS 两模式根构建及显式 Cross | `d324763b9fd40d56f20d3e5e1c610cff4aa5bc2c` | `273e596b160102254bdd998544287575affa77f5` |
| 最终 macOS 两模式 Full | `9d70aae21f60eec6e40bfa0f50c1ddd8f407f215` | `c27430c12ea760cf3c204aee64562770583193ab` |
| macOS fresh Owner Base/Project/Provider | `dc4a5e411ff50b17174e091061aa6ff02a6ff834` | `5adc92d5270ed7b466826db1ad6efa400bccfe14` |
| macOS fresh Owner Engine，在1200秒内通过 | `11492bf00e69841064a5d44e93b2f337ce1762a9` | `acfb8627a0c87864e030239ca251ae814859a075` |

本文件及计划状态的最后提交为纯文档归档，不追认为该提交原样执行过全矩阵。源码、测试、有效命令改变按真实影响补验；SHA 改变本身不重复 Full。

| 实现阶段 | 关键提交 |
| --- | --- |
| 固定后端、拥有型报告、独立Fatal | `efcac2d`、`63d9a69`、`a2e70cf`、`fe7b143` |
| 有界值/格式、Writer/队列、文件分区 | `054ec92`、`a7c22e1`、`de731c3`、`c15172e`、`e1d6bd1` |
| 实例/原生消费者、Flush/Stop | `7669b28`、`60906d9`、`911a6cb` |
| Module与Host接线、清理现场与直接依赖修正 | `b6d2b21`、`9644846`、`37c113b`、`d99fb42`、`6466214`、`c6b29cd` |
| 卸载、真实应用、公有分配探针及受控交错 | `549b42d`、`dae4366`、`cfdf550`、`3806fc0`、`8b5138d`、`32e5338` |
| Task12移动/独立Consumer/最终定向修正 | `3e41e78`、`c914f57`、`cd5f52b`、`d324763` |
| Mac发现、独立Owner及实测准备预算 | `deffce3`、`dc4a5e4`、`11492bf`、`9d70aae` |

Windows 源码 `D:/Git/PanguEngine`、BuildRoot `Build/Windows`，Android 为 `Build/Android`；WSL 源 `/home/moozen/WindowsGit/PanguEngine`、构建 `$HOME/PanguEngineBuild/Linux`（ext4）；Mac 源 `/Users/moozen/WindowsGit/PanguEngine`、构建 `$HOME/PanguEngineBuild/macOS` / `iOS`（APFS）。未经 `/mnt/d` 混树，正式引擎构建直接使用共享源；独立测试仅按原契约准备夹具和工具输入副本。桌面 Release、RTTI ON、异常 OFF；Windows/Linux Clang23.1.0、macOS23.1.2；Android NDK30.0.16248370/Clang21.0.0/API29。标准库使用各平台正式 preset 的目标配置。

| 固定子模块 | Gitlink |
| --- | --- |
| fmt 12.2.1 | `208219daff58f2a7438f6931cc2ddc358acf33ef` |
| spdlog v1.17.0 | `79524ddd08a4ec981b7fea76afd08ee05f83755d` |
| GoogleBenchmark v1.10.0 | `a6b4a9f00b36c19af9997d68a1cf6e0b5b3d032c` |
| GoogleTest v1.18.0 | `063de7e9578f82b369302001269680b4b1553359` |
| NlohmannJson v3.12.0 | `55f93686c01528224f448c19128836e7df245f72` |
| TomlPlusPlus 固定来源 | `30172438cee64926dc41fdd9c11fb3ba5b2ba9de` |

fmt/spdlog 使用普通 STATIC/PIC Target、同一外部 fmt，保持 RTTI，关闭上游 tests/examples/install。实际宏为 `FMT_EXCEPTIONS=0`、`SPDLOG_COMPILED_LIB`、`SPDLOG_FMT_EXTERNAL`、`SPDLOG_NO_EXCEPTIONS`、`SPDLOG_NO_THREAD_ID`、`SPDLOG_DISABLE_DEFAULT_LOGGER`；不走默认 logger、registry、thread pool。第三方不继承 Pangu 警告、上下文或身份策略；普通翻译单元 `CXX_SCAN_FOR_MODULES=OFF`，无上游补丁。

## 正式入口和结果

桌面在固定已提交源码上按 Monolithic → Modular 串行执行下列根入口；p 为 windows/linux/macos，mode 为对应模式，保留有效宿主 `PANGU_META_SDK_CONFIG`。每阶段等前一步真实退出，各 BuildRoot 独立；Full/Project Git 锁期间不改 tracked 源码、文档或 HEAD。

```text
cmake --preset p -DPANGU_LINK_MODE=mode -DPANGU_ENABLE_ASSERTS=ON -DBUILD_TESTING=ON -DPANGU_BUILD_ENGINE_TESTS=ON -DPANGU_ENGINE_ALLOW_LOCAL_OVERRIDE=OFF -DPANGU_BUILD_SAMPLES=ON -DPANGU_BUILD_BENCHMARKS=OFF
cmake --build --preset p-full
ctest --preset p-full --show-only=json-v1
ctest --preset p-full --no-tests=error --output-on-failure
```

| 桌面 | Monolithic 完整 Full | Modular 完整 Full | 原生 configure/build/select/CTest |
| --- | --- | --- | --- |
| Windows | 317/317，3396.60s | 313/313，2853.16s | 两模式全部0 |
| WSL Linux | 312/312，2616.66s | 308/308 | 两模式全部0 |
| macOS | 315/315，4490.48s | 311/311，4607.81s | 两模式全部0 |

根计数不重复累加内层 BuildTools/ModuleRegression/Assert 子图。Windows/Linux 模式差集为 Mono 独有45项、Modular 新增41项：静态 Integration/Scope/StaticCatalog 与真实 NativeLibrary/Unload/Host/ModuleLifetime/ModuleRegression 等按既有模式条件交换，净差4；独立 Project 和 ModuleRegression 保留。macOS差集每个名字与Linux相同，另外3个Owner共同场景保留，实际315/311；其ModuleRegression真实准备和执行193.85s通过。

Foundation/Log/Module/EngineHost 的后端、报告、拒收/队列、文件、Flush/Stop、真实卸载及应用消费均在 Full。LogAssertConfig 在正式隔离子图覆盖 Release/Debug × Assert ON/OFF、跨TU/Consumer、第三方 NDEBUG 隔离与 Check/Fatal 精确退出。Modular 的 ModuleRegression 真实准备并运行 Mono Module 与 EngineHost 子图。

移动各 mode 先运行对应 `android-arm64` / `ios-arm64` 根 preset，显式设置 `PANGU_LINK_MODE=mode`、Assert ON、BUILD_TESTING OFF、override OFF、Samples/Benchmarks OFF，再构建对应 preset（`--parallel 2`），额外显式运行：

```text
cmake -DPANGU_ENGINE_SOURCE_DIR=<规定共享源码> -DPANGU_TEST_ROOT=<本地BuildRoot>/Tests/LogMobileFinal/<mode> -DPANGU_TARGET_PRESET=<android-arm64或ios-arm64> -DPANGU_LINK_MODE=<mode> -DPANGU_META_SDK_CONFIG=<有效宿主SDK> -DPANGU_ENGINE_ALLOW_LOCAL_OVERRIDE=OFF -P Tests/CMake/SourcePluginProject/CrossCompileTests.cmake
```

| 移动目标 | Monolithic 根 configure/build/Cross | Modular 根 configure/build/Cross | 最终消费和宿主检查 |
| --- | --- | --- | --- |
| Android ARM64 | 0/0/0 | 0/0/0 | ELF64 AArch64、真实 MobileHost/caller/Log/Runtime Query 和动态依赖通过 |
| iOS ARM64 | 0/0/0 | 0/0/0 | Mach-O arm64/平台2、真实 MobileHost/Log/Runtime Query 和动态依赖通过 |

MobileHost 真正消费 `LogSystem::Initialize/GetWriter/Stop`、FailureReport、Check/Fatal 和受控 UTF-8/标量格式化。Android caller 与实现共用 `MobileHost.h` 的 `const char*` 声明，实际传绝对目录，PRIVATE 接 PanguBuildOptions；实际命令含 `-fno-exceptions` 且保留 RTTI。C linkage 符号没有被当作 caller 类型正确的证据。Android 应用仍须传自身可写绝对沙箱目录，iOS 使用 HOME/Library/Logs/PanguMobileHost。检查最终 exe/SHARED 依赖而非只检查 STATIC 归档，禁带宿主 STL/LLVM；以上均未运行交叉目标。

d324→9d70 变更严格限 macOS 测试发现、Owner 拆分和准备时限，产品、caller、交叉脚本、平台工具链 diff 为0。Windows/Linux 默认 All 的7个原块及顺序保持；两平台轻量配置后的完整 name+command 选择与已测 Modular 逐字相同，生成发现 include 保持默认5/业务60。移动测试 OFF 图不调用这些注册。因此按真实无影响证据复用 d324 四个 Full 和四组移动结果，保留原被测SHA，不称9d70原样跑过它们。

## 失败轮次与最小修正

| 轮次/版本 | 真实失败或中止 | 修正及后续证据 |
| --- | --- | --- |
| 初始 Android | NDK pthread_tryjoin_np 未声明，pthread.h/libc 无此入口 | 最小 gettid/join 分支定向编译及最终链接通过，未设备运行 |
| 3e41e78 移动预检 | 链接成功，旧脚本硬编码顶层 Bin 导致 inspect1 | c914f57 改 TARGET_FILE 后原子图重配置/构建/正式检查0，仅预检 |
| c914f57 桌面 R0 | Win/Linux已选316/311，MetaCli ConsumerPipeline缺Log链；Full中止（Win外层1/Linux143），Mac build143、未运行Full CTest | fixture 接实际 Fmt/Spdlog/Log；后续 Main 缺Logging初始化的真实编译RED也保留，显式配置后MetaCli1/1/0 |
| cd5f52b R1 | Win315/316、2405.26s；Linux309/311、2191.41s；Mac308/311、5410.34s，均nativeCTest8 | Win ProjectSwitch；Linux另有LogAssertConfig；Mac另有复合Project Timeout900.09s，失败后不切Modular |
| d324 R2 Mac | 311/312、native8、5720.20s，Owner Failed853.07s | 指定目标默认5秒 PRE_TEST 发现超时，非外900超时 |
| deff fresh Owner | 外Timeout900.86s、native8，未进入Engine目标发现 | 多个冷图累计准备超预算，Darwin拆4个独立case，7原块逐字保持且无跨块数据依赖 |
| dc4 四冷门 | 3/4、native8；Base378.80/Project257.81/Provider331.12s各0，Engine Timeout900.84s仍在编译 | 同失败根仅一次有界热续建76.726s作成本诊断，不当冷GREEN |
| 11492 fresh Engine | 新冷根1/1、native0、1183.60s，在1200内完成 | 输入/断言不变，最终只单调放宽准备上限至1500；有效属性比较仅该项变化，不重冷编译 |
| 11492 prebudget Full | configure/build/select0、315已选；余量裁定时安全中止，外层实际1，无完整CTest结果 | 精确停止该CTest/所属树，待SDK/Git写者退出才改源，不伪补退出码或完整结果 |

最终9d70 Mac Modular的外层观察会话98998在15:39附近实际返回1、此次无新增stdout，当时没有原生CTest结束结果；未能从日志证明具体外层原因。只读重连确认Mac未重启、原wrapper80828及CTest26753仍存活，原轮继续通过Matrix470.86s、ModeSwitch203.84s与真实ModuleRegression，最终完整311/311、nativeCTest0。恢复原生命令0（16:09:14）、summary到达MATRIX_EXIT=0标记，随后wrapper和所有写者退出。原生wrapper最终OS退出码未单独捕获，不补造；外层98998实际1始终保留，未为此重启CTest、切配置或改源码。

ProjectSwitch 分别验证配置、链接和 no-op。原 SCAN_ON 在 Windows 真实出现旧 Debug 消费者 iterator2 与 fmt0 的 CRT 错配；Linux 对象恢复正确但 no-op 失败。`bd36956`只加私有上下文仍RED；`621e9fa`加SCAN_OFF后三桌面三步全过。独立Windows夹具仅消融上下文，实际命令无上下文/无modmap仍全过，故最终删除该不必要依赖，只保留 SCAN_OFF。正式回归读取真实 fmt/spdlog 对象 CRT/Debug 信息，保留运行与原no-op断言。

Fatal 测试原表达式把所选 libc++ 的 literal→string_view 隐式构造纳入 noexcept；改以已构造 `std::string_view{}` 检查原入口。产品 noexcept、禁异常、ON/OFF/NDEBUG/Consumer 契约不变。旧复合 Project 分 Base 与 EngineOptIn Full 两个独立冷场景，各保留900秒和原断言；三桌面定向门全0，Mac Engine 冷门854.55s。Android caller此前实际命令缺 `-fno-exceptions` 的断言RED独立保留；d324 PRIVATE接BuildOptions后，两移动两模式真实编译/最终链接及Android实际flags全部通过。

仅 `CMAKE_SYSTEM_NAME=Darwin` 的 MyGamePluginCollaborationTests 显式发现30秒；helper最小可选透传，其余目标/平台默认5，业务60。原失败 cwd 在APFS本机目录，库存在且RPATH正确，热精确发现正常，业务只在TEST body执行。失败窗口与XProtect事件差4.925秒；另一次fresh启动观测约7.79秒（含采样扰动），栈在pre-main dyld/fcntl。以上支持启动加载/系统校验参与，未完全证明全部OS耗时因果；未关闭/绕过安全扫描或签名。

Darwin Owner Base/Project/Provider 各900秒、Engine完整准备1500秒；业务60、目标发现30/其他5不变。约978秒编译估计低于真实冷1183.60，最终按实测留约26%准备余量；900→1200→1500每步增加300秒挂起准备检测窗口，产品/业务deadline未动。dc4三个已0冷场景输入/属性未变；11492 Engine在更小1200下已0，分别按影响复用，不把旧失败4项门拼成新一轮4/4。

Android修正纠正Task7“tryjoin自API26引入”的未验证假设。[Android10 gettid](https://github.com/aosp-mirror/platform_bionic/blob/android-10.0.0_r1/libc/bionic/pthread_internal.cpp)读取tid，[同版join](https://github.com/aosp-mirror/platform_bionic/blob/android-10.0.0_r1/libc/bionic/pthread_join.cpp)仅在tid非零时futex等待。当前分支查询仍拥有的joinable线程，tid0后才join，采用内核退出证据，不以业务返回代替原生完成；无新增字段，Linux/Apple不变。证据限公开源码及NDK编译/最终链接，未设备运行。

## 布局、内存与无异常

以下尺寸与工作预算采用三桌面实测布局及必要对象的共存关系；不外推为未经认证的移动 Public ABI。

| 实际对象或预算 | 字节 |
| --- | --- |
| FailureSite / FailureReport | 464 / 1992 |
| ModuleOperationReport / HostOperationReport | 3296 / 5744 |
| Manager唯一Runtime报告区 / Module最大报告操作 | 24528 / 48016 |
| 共享单次报告工作总峰值 Windows / POSIX | 65456 / 62384 |
| LogSnapshot Windows / POSIX | 2808 / 1784 |
| FileStoreSnapshot Windows / POSIX | 2192 / 1168 |
| FlushReport / StopReport | 608 / 1232 |
| HostStartupFailure / optional / LogInit摘要 | 800 / 808 / 320 |
| 默认Log实例 Windows / Linux / macOS | 771848 / 762584 / 762616 |

报告峰值计入同时存活的必要对象，满足单次64KiB，Windows余80B，Host不另领64KiB。本任务无生产字段/必要报告副本增长。Task10最终阶段账目如下，Log报告即使也位于实例/栈预算内，仍纳入共享报告交集：

| 生命周期阶段 | 必要报告共存关系 | 保守峰值 |
| --- | --- | --- |
| Host嵌套Module，包括StartBatch→FailStartup→原生回滚Close | Module48016（已含Manager24528、三份Module、四份Failure、路径及小值）+caller Host5744+缓存Startup808+Init摘要320+Log持久报告9672+Host标量512+worker小统计384 | Windows65456B |
| 同阶段POSIX | 上式Log持久报告改为6600，其余相同 | 62384B |
| Manager仍在但Module已返回，组装Host报告 | 24528+两份Host11488+Module out3296+Startup808+摘要320+Log9672+小值896 | Windows51008B |
| Manager已释放，Log停止/失败初始化 | 两份Host11488+Module out3296+Startup808+摘要320+Log9672+Task8管理工作12040+额外保守Init4584+小值896 | Windows43104B |

完整local Host只在嵌套Module返回后构造；worker最终H的完整快照仅在StopRequested后创建，此时Host已销毁Manager。控制摘要改为小统计、FileStore直接填写生产快照，必要副本不靠NRVO排除。编码/输出工作区与操作报告分别计账。

默认Log的自身管理预算小于1MiB，分项如下：

| 分项（B） | Windows | Linux | macOS |
| --- | ---: | ---: | ---: |
| 内联LogSystem | 4072 | 3048 | 3048 |
| Data+256槽 | 486000 | 481856 | 481888 |
| Writer控制块 | 88 | 88 | 88 |
| 单在途生产者工作区 | 7504 | 7504 | 7504 |
| 管理工作预算 | 12040 | 7944 | 7944 |
| 请求worker栈 | 262144 | 262144 | 262144 |
| 总计 | 771848 | 762584 | 762616 |

最低支持栈128KiB；OS/CRT guard、TLS、内核句柄、pthread缓存与残留Writer另列边界，不宣称进程总内存1MiB。

栈测量按轮次保留：Task7初始实例的Consume/Encode/WriteNative/Scan单帧分别为Windows2456/392/24712/3224、Linux1288/584/456/8680、Mac1304/600/88/8728B；这些历史Consume值不当作后续实现的最终帧。Task8实际Linux DB追加-fstack-usage后Consume2456、EmitControlNotice4072、WriteOutput2104、Flush2456、Stop56B；Task10预算修正后同类实际命令测得Consume2456、EmitControlNotice1304、RecordOutcomes72，Host FinishShutdown5864/FailStartup3720/StartBatch9448B。辅助输出仅改-o并追加该选项，未改变正式flags。单帧不机械相加、不重复计内联，也不代替对象生命周期账目；正式最低128KiB夹具在最终Full继续真实处理21条1KiB消息、双路输出并轮转至少3文件。

初始化9故障阶段逐点验证rollback与owner计数，保留Runtime私有探针、public DLL探针和分配负对照。Windows EXE new不拦全部DLL/CRT/OS；Linux --wrap只拦直接引用，不覆盖动态库内部；Mac malloc-zone有观测盲区。无分配结论限所选标准库、已审计fmt子集/BackendLogger/RawSink和探针覆盖，不启异常、不替换生产全局分配器、不把库abort当可恢复OOM。

Fatal监督要求Windows `0xc0000409` 或POSIX `SIGKILL` 且校验 `FATAL_READY`，exit42/非法指令/timeout等负对照不能误报成功。应用可诚实Dropped：H0/drop1、双路用户0、ResourcesReleased1、OutputComplete0；成功路径H1、双路1、完整1且有真实文件正文。安全退出依据ResourcesReleased，H已处理/文件关闭/业务返回均不足。

## 性能范围

复用Task11 R1六场景，后续仅构建扫描和测试注册变化，产品计时代码与有效Release/Modular codegen flags语义不变。Windows/WSL为Ryzen7500F/12逻辑CPU，Mac为iMacPro1,1/Intel Skylake/2逻辑CPU；未固定affinity、未隔离后台负载，不设阈值。

Disabled10000次；Accepted2000次（队列4096/预留32/16MiB/消费者冻结），计时后才输出；FullDrop/ContentionDrop各10000次（队列8/预留1，含同步格式化），ContentionDrop持有真实队列锁测确定拒收分支，不冒充多线程吞吐；FailureReport构造/复制/读取10000次；Dual100次（默认队列256/栈256KiB/文件4MiB/分区64MiB）。Dual含私有短段协调/解锁、真实双输出和Flush，不是纯Submit。初始化/目录准备/样本存储/排序/打印/最终Stop在计时外，steady_clock未扣空操作。Windows终端重定向NTFS原生句柄，POSIX终端文件在SMB，日志NTFS/ext4/APFS。

每格为mean / p50 / p95 / p99，单位ns；不混旧样本，Windows时钟量化p50=0不代表零开销。

| 场景 | Windows | WSL | macOS |
| --- | --- | --- | --- |
| Disabled | 18.93 / 0 / 100 / 100 | 19.20 / 20 / 20 / 30 | 40.83 / 40 / 40 / 50 |
| Accepted | 543.35 / 500 / 600 / 800 | 661.51 / 550 / 930 / 1220 | 1441.71 / 1170 / 1490 / 1720 |
| FullDrop | 511.58 / 500 / 600 / 600 | 481.39 / 420 / 730 / 840 | 1155.69 / 1000 / 1230 / 1430 |
| ContentionDrop | 443.32 / 400 / 500 / 600 | 469.92 / 420 / 620 / 750 | 1175.77 / 980 / 1200 / 1370 |
| FailureReport | 101.33 / 100 / 200 / 200 | 93.44 / 80 / 100 / 140 | 127.57 / 120 / 140 / 160 |
| DualOutputAndFlush | 44048758 / 41674800 / 50012800 / 116540100 | 6014192.01 / 5995480 / 6131110 / 7955199 | 6982404.94 / 2806517 / 18897799 / 55318971 |

## 静态检查、恢复与限制

d324分支自有168个.h/.cpp按根clang-format `--dry-run --Werror`全0，之后仅CMake/文档变化。Task1–11 tidy按最后hash/有效flags/真实DB复用；Task12实际检查MetaCli三消费者、M8 ModeConsumer、三桌面两个Fatal Consumer、两移动NativeLogThread/实际复制MobileHost及Android caller，均exit0。移动保留既有ModuleIdentity qualified-auto/pointer-arithmetic三条提示；MSVC/部分libc++参数名、GTest optional/trailing等既有minor如实保留，未借保留名、改SDK、共享头批量--fix或全局禁检查。第三方按独立边界，未批量改upstream。

最终缓存已核：桌面Modular、Samples/Benchmarks OFF、Assert ON、BUILD_TESTING/PANGU_BUILD_ENGINE_TESTS ON；移动Monolithic、BUILD_TESTING OFF、Assert ON；各平台override显式OFF，保留有效SDK。三宿主构建/测试/Git写者与原Mac wrapper均已退出，全子模块clean、无index.lock，并再次确认9d70/tree c274后才写本次正式文档；未清理本计划workspace。

移动未验收设备文件/线程/Fatal、RTTI动态卸载、打包/签名；未跑交叉GTest、Sanitizer或负载隔离性能门槛。Fatal运行验收限普通线程，未认证信号处理或损坏进程上下文。OS启动校验具体耗时因果未完全证明；报告预算余量很小，后续必要字段/副本必须重算。原始命令/native exit/选择JSON/LastTest/cache/产物路径/失败与采样在本计划workspace的task-12-report.md及task12-*；主控Review与验收后确认无依赖再精确清理，不将rawlogs长期灌入计划。

## 最终评审修复轮次（5e23385 / aa827b2）

本节是全分支评审之后的独立补验，不能把旧六份Full及旧四组移动结果拼为新RawSink实现或Windows23.1.5的Full。完整评审结论、全部执行裁定与遗留项分诊见[最终评审归档](2026-11-12-foundation-diagnostics-review.md)。

| 范围 | 实际被测 commit | Git tree |
| --- | --- | --- |
| I1/I2产品修复、Debug与Release相关门、四配置、实例布局 | `5e233852b647286a253cac693896b54eaee887d5` | `31206673025a9b135067910a1151a19e643fe689` |
| Host管理推进补漏、三桌面两模式Host、Mac未执行余门、Android/iOS正式两模式 | `aa827b252a9594dafdaeb548d0172501e9a71685` | `9b368372b60c894077696897d2c0741630a54e66` |

5e移除私有BackendLogger，直接以两个内联RawSink消费显式time/source的借用log_msg；符合批准规格§7.1的同步logger/Sink路径，无上游补丁、无全局ABI宏或生产allocator调整。精确接纳段复用短ConsumerPause，独立保留真正TryPop持锁的竞争拒收及累计损失。aa仅修改Host正式测试与说明，`Source/`、`cmake/`对5e差异为空，其产品、非Host、四配置与布局图按真实输入不变复用。

| RED轮次 | 实际失败 | 后续验证 |
| --- | --- | --- |
| 原Debug后端与新增真实Initialize | 构建0，正式LogNoAllocation各0/1，fixture91、CTest8 | RawSink后原门6/6；最终Debug22/22、CTest0，含真实Initialize、CRT OOM、后端、消费竞争、13项ModuleDiagnostics及四配置 |
| 实际空TryPop竞争 | 正式EmptyConsumerContentionDoesNotAdvanceAcceptedPrefix直接GTest0/1、exit1，真实Dropped与旧Accepted假设不符 | 屏障控制精确准备，独立断言Dropped、Contention=1、H不推进，后续Accepted/Delivered=1且OutputComplete=false |
| Mac Mono Host原失败 | 7/8、exit1；唯一ManagerDoneButLogBusyIsClosing纯等ExitEntered两秒失败 | 强制首次Stop取SnapshotLock失败，旧纯Wait确定性0/1、exit1、2009ms；原两秒内持续管理推进后，三桌面两模式Host各8/8、exit0 |

首次I2 CTest另因未同步的旧Concurrency EXE在发现阶段缺私有DLL符号0xc0000139，原生CTest8；它不是产品RED，正式依赖图重建后再验证。Windows Mono wrapper的stderr问题、源目录运行产物和最终状态脚本缺rg均单独保留，未覆盖真实失败或伪造退出码，详见评审归档。

正常Release Modular沿用有效SDK cache，配置显式设置`PANGU_LINK_MODE=Modular`、Assert/BUILD_TESTING/EngineTests ON、Samples/Benchmarks/override OFF，构建对应`<platform>-precommit`后执行：

```text
ctest --preset <platform> -L '^area[.](log|engine-host)$' --no-tests=error --output-on-failure
ctest --preset <platform> -R '[.]ModuleDiagnostics[.]' --no-tests=error --output-on-failure
ctest --preset <platform> -R '^LogAssertConfig$' --no-tests=error --output-on-failure
```

| 平台 | Modular相关CTest | ModuleDiagnostics | Release/Debug × Assert ON/OFF |
| --- | ---: | ---: | --- |
| Windows | 144/144，0 | 13/13，0 | 最终Debug22项中的正式LogAssertConfig0，四子配置实际执行0 |
| WSL Linux | 140/140，0 | 13/13，0 | 正式LogAssertConfig1/1，0 |
| macOS | 140/140，0 | 13/13，0 | 正式LogAssertConfig1/1，0 |

Mono构建十二相关正式目标后，直接运行Unit57、Concurrency11、Backend6、File Windows27/POSIX23，均全部0；ModuleDiagnostics各13/13、NoAllocation/Initialization/Drain/Runtime/Lifetime五类正式监督程序均0。Mac原轮在Host失败后未执行的部分在aa的未变产品图补齐，不倒写原轮。最终aa三桌面两模式Host各8/8，通过`cmake -E chdir <本地BuildRoot>/Tests/Work/FinalHostBothModes <BuildRoot>/Bin/PanguLogHostTests`执行；各模式内构建/运行串行，最后恢复Modular，配置及相关目标构建均0。

Android/iOS在固定aa、共享原源码和规定隔离BuildRoot上，各按Modular→Monolithic执行正式根preset/build，再直接运行完整版本化入口：

```text
cmake -DPANGU_ENGINE_SOURCE_DIR=<规定共享源码> -DPANGU_TEST_ROOT=<本地BuildRoot>/Tests/LogMobileFinalFix/<mode> -DPANGU_TARGET_PRESET=<android-arm64或ios-arm64> -DPANGU_LINK_MODE=<mode> -DPANGU_META_SDK_CONFIG=<有效宿主SDK> -DPANGU_ENGINE_ALLOW_LOCAL_OVERRIDE=OFF -P Tests/CMake/SourcePluginProject/CrossCompileTests.cmake
```

两移动两模式的根configure/build/Cross全部0，恢复0，iOS原会话实际IOS_EXIT=0；最终链接、ARM64/Query与动态依赖检查通过。ignored缓存尾段helper没有执行，不作为正式依据。Android使用NDK r30/30.0.16248370、Clang21.0.0；iOS采用有效显式`/Users/moozen/Library/Developer/SDKs/iPhoneOS18.2.sdk`，路径存在且SDKSettings版本18.2，未修改系统xcrun选择。未运行交叉GTest或设备，不宣称真机文件、线程、Fatal、动态卸载、打包/签名。

静态检查：20个变更自有cpp/h的clang-format23.1.5 `--dry-run --Werror`全部0；14个变更TU用真实Windows Debug DB检查，逐个clang-tidy退出0。定点消除本轮include等提示后，生产Queue/LogSystem/SpdlogBackend均0发出告警；准确保留47条既有基线（Host34、NoAllocation CRT命名12、Initialization SDK枚举1）。aa单个Host文件再以实际Release DB检查及format，均0，未全局禁检查或修改SDK。Windows本轮实际Clang/format/tidy23.1.5，旧CMake识别cache仍23.1.0；以实际binary为准，不将其伪称旧工具链Full。Linux23.1.0、macOS23.1.2。

三桌面两模式真实`PanguLogDrainFixture memory`均0，新默认预算如下：

| 字节 | Windows | Linux | macOS |
| --- | ---: | ---: | ---: |
| LogSystem内联 | 4072 | 3048 | 3048 |
| Data+256槽 | 485720 | 481616 | 481624 |
| 控制块 | 88 | 88 | 88 |
| 单在途生产者工作区 | 7504 | 7504 | 7504 |
| 管理工作 | 12040 | 7944 | 7944 |
| 请求worker栈 | 262144 | 262144 | 262144 |
| 总计 | 771568 | 762344 | 762352 |

均小于默认1MiB；OS/CRT/TLS/guard/缓存及残留Writer继续另列。队列对象没有新增字段；公开报告和必要副本未变，共享报告峰值仍Windows65456/POSIX62384B，不能把Data减少折算成报告栈余量。

本轮未重测性能，前述六场景表只作为原BackendLogger历史成本。WindowsDebug独立EXE的new/CRT hook先自验证，再拒绝真正Data/Control malloc，恢复hook后核kOutOfMemory、Closed、无残留及资源归零；Linux保留原ELF wrap路径。覆盖不外推到全部DLL/系统分配或任意STL/Sanitizer，结论同时依据后端对象已删除三容器和真实Debug构造/初始化门。

最终三桌面Release/Modular、Samples/Benchmarks OFF、Assert/测试 ON，移动Monolithic/测试 OFF，全部override OFF并保留有效SDK。三宿主全部子模块干净、无构建/测试写者；Windows无index.lock。POSIX首轮缺rg的cache/SDK/进程结论无效，改用系统grep逐步检查后ALL_STATE_CHECKS_EXIT=0。源码验收与复审均完成，必要记录已归档；必要记录在bad1dc3归档、确认无运行依赖后，临时工作区已精确删除并核对路径不存在；正式Tests和各平台BuildRoot缓存保留，未合并或推送。