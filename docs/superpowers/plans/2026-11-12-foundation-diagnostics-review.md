# Foundation 诊断与 Log：最终评审与执行裁定

> 状态：源码修复、必要平台定向、配置恢复和独立复审完成。必要结论在 bad1dc3 归档并核对无运行依赖后，本计划临时工作区已精确删除；正式 Tests 及 BuildRoot 缓存保留。

关联：[实施计划](2026-11-12-foundation-diagnostics.md)、[正式验收](2026-11-12-foundation-diagnostics-validation.md)。

## 评审与修复结论

全分支范围 `78c8306..0b7a097` 的独立评审提出两项 Important：Windows Debug logger空容器iterator proxy必经隐式分配，以及异步测试假定首条必被接纳。唯一修复波次为 `5e23385` 与 `aa827b2`；独立复审范围 `0b7a097..aa827b2`，2/2 ADDRESSED，新增 Critical/Important/Minor 为0/0/0，开放修复项0。

修复包为28文件、116998字节，SHA256 `8D5491B6036A3F213809B5395139AD9951CD86EE89078FEDCCDEBD9D7C330756`。复审没有重新运行构建、suite、format或tidy，也未扩大为未触及代码的再次广审；它核对完整修复包、对应原始证据和锁顺序。

- I1：消费者直接调用两个内联 `RawSink : spdlog::sinks::sink`，不再构造logger的字符串、sinks vector和backtracer vector；使用批准规格允许的同步Sink路径，保留异步前端及双路Outcome。无上游补丁、无全局STL ABI宏调整、无生产allocator替换。真实Debug构造与Initialize均先观察到fixture91/CTest8，再经分配门与四配置长期回归通过。
- I2：精确计数的准备段先取得Writer，再用短ConsumerPause执行一次真实提交，解锁后才管理；真实TryPop竞争另保留合法Dropped、序号和损失账目回归。Host补漏用确定性首轮Stop取锁Busy证明原纯Wait失败，再在原两秒内持续管理推进，保留Closing/Busy/WaitingForLog及未释放断言；原Mac Mono7/8和强制RED0/1均未被后续GREEN覆盖。

5e的三桌面Release Modular相关结果为Windows144/144、Linux/macOS各140/140；ModuleDiagnostics各13/13、正式Release/Debug×Assert ON/OFF门各0。aa只改Host正式测试及说明，三桌面两模式Host各8/8、恢复0；其他产品/Log/四配置图按真实diff复用。Android/iOS四组正式根构建、versioned CrossCompile入口及恢复各0；仅编译/最终链接/宿主检查，未设备运行。精确版本、命令和轮次见正式验收，不把这些定向结果拼为当前完整Full。

本轮默认实例预算Windows771568/Linux762344/macOS762352B，均低于1MiB。公开报告及必要副本未变，共享峰值65456/62384B；Windows仅余80B的约束继续适用。旧性能数据属于BackendLogger历史实现，本轮没有新RawSink性能实测。

## 执行裁定

按发生顺序原文归档。临时实验方案被后续裁定替代时，以后续结论为最终实现；保留原决定、依据和错误代价，避免倒写成功后的因果。
- Ruling: 在当前独立功能分支原位实施 — 用户规定优先使用 D:/Git/PanguEngine，五平台正式构建依赖同一共享源码固定路径，且不存在其他工作树或未提交修改 — 错误裁定的代价是后续需迁移功能分支；不触碰 main。

- Ruling: 每任务执行相关定向测试，最终统一完整矩阵 — Tests/README.md 的分层契约与本计划的正式验收顺序 — 若影响判断遗漏，最终全量及分支评审检出后补测。

- Task 4 Ruling: 将计划中直接接受fmt::format_string的前端参数收紧为仅接受consteval字符数组的LogFormatString，内部仍以fmt::format_string<归一化类型...>检查语法/类型 — 正式compile-fail已证明fmt::runtime能隐式构造fmt::format_string，原计划签名不能满足规格拒绝运行期格式串；规格为绑定权威 — 若裁定错误，代价是下游Task5调用模板/编译测试需调整；不放宽支持语义，不改第三方/异常策略。

- Task 4 Ruling: 拆分ValidateLogConfig（实例级矛盾/队列内存）与ValidateLogFileConfig（文件能力） — 规格§9.1要求默认双输出缺目录/不可用时可选File降级仍保留Console，原单一校验无法表达 — 若裁定错误，代价是Task6/7调用接口需调整；不改变LogConfig公共字段/默认值，不提前实现Initialize。

- Task 5 Ruling: 接纳门一次CAS，最多一条在途构造记录，其他并发构造竞争可拒收任意等级 — 规格明确允许竞争拒收且要求固定预算，避免未限定并发构造内存 — 若错误，代价是吞吐/公平需求需后续重新设计；本轮不承诺公平或多条并行格式化。

- Task 5 Ruling: CountLogEvent单次CAS，算术饱和或竞争导致未记入均置CounterOverflow/统计不可信，不循环争用统计 — 规格要求统计不可信不能报完整，且提交拒收路径不应循环阻塞 — 代价是竞争时计数不精确/保守OutputComplete=false，即使未发生算术溢出；必须公开注释和Task8交接。序号/HighestAccepted/接纳关闭线性化仍须精确，禁止套用近似计数策略。

- Task 6 Ruling: FileStore::Open需要B中的LogInitReport/文件快照等共享值，若尚无定义则按B最小建立并供Task7原位扩展 — 原任务依赖需要这些类型但此前任务尚未产出，不能造重复报告或提前实现Initialize — 代价是Task7需继续补齐公共报告字段；不放宽原始接口/所有权/预算契约。

- Task 6 Ruling: 用新的Sol代理接手R1，不恢复旧Astra实施者 — 用户最新明确模型约束优先于Skill原代理复用/模型升级建议，followup工具不能改变旧代理模型 — 代价是Sol需从原报告恢复上下文，允许耗时更长。

- Task9 Ruling: Foundation增加FailureContext::AddSecondary(const FailureSite&)最小拥有型复制重载 — 既有接口只接受source_location，不能重建已拥有Site的原始位置，而返回Error必须权威、错配现场又须作为次项保留 — 若错误，代价是Foundation公开API增加一个后续需维护的重载；不改容量/RuleZero/无分配/主错保护，不建通用报告框架。已要求Foundation复制/容量回归与Module错配回归。

- Task9 Ruling: 不要求256个并存失败全量文本历史永保；允许8个Poll拥有型Site+专属完整当前主报告+Runtime统一8个历史Report — 规格要求固定预算/主错优先并允许补充截断，原值错误/身份/清理责任仍逐项准确 — 代价是超额历史文本Unknown/显式截断，须正式覆盖满缓存/跨操作/>8历史，不能误匹配旧现场。去除Record/Scope逐实例完整报告，每Manager唯一不跨Host共享，整体并存预算与Host留量共同核算。

- Task9 Ruling: 稳定源/已有矩阵通过后可先提交Task9代码，再以override OFF跑需clean Git锁的Project消费，必要时增量重配刷新revision；后续代码修复作本任务追加commit — 正式锁验证需要不可变已提交源码，不用ON开发开关替代 — 代价是单任务可能多提交，review-package从Task9 BASE收集全部；不因SHA变重复同tree Module矩阵，不清SDK/构建树。

- Task9 Ruling: I6按精确平台Fatal终止状态+测试准备标记验证，保持现有无文字产品Fatal — 规格允许省略输出，“对应应急消息”不能变成新增阻塞输出要求 — 代价是Windows监督仍仅状态码层，不宣称reason子参数可见。

- Task10 Ruling: 允许长期StartupFailure.ModuleReport由完整ModuleOperationReport改为拥有型Module启动摘要，当前Host完整Failure/ModuleReport仍保留，原始Origin独立Site — 规格§5授权整次报告角色分区，48016B模块峰值后仅17520B给Host，不能堆两份大HostReport及full历史缓存 — 代价是该未冻结C++字段类型调整，相关消费者需迁移；错误/ModuleHandle/Scope/Declaration/责任/Progress/FailureOrigin/历史不足语义保留，省略上下文/等待等须明确标识，不冒充完整报告，不改64KiB或Host16384上限。

- Task10 Ruling: HostOperationReport取消独立1992B Failure存储，用Failure() const只读访问器返回当前拥有型ModuleReport.Failure；无Module现场为空/Unknown，PrimaryError/Log摘要/Startup Origin仍独立保留 — 完整数据已有同一ModuleReport持有，访问时借用自身而不存自指针可消重复并满足硬预算 — 代价是未冻结C++字段改方法，消费者需迁移、明确返回语义/借用寿命；补复制后指向自身/源清空后独立、E1/E2/Log对应回归，不丢完整ModuleFailure或放宽预算。

- Task10 Ruling: 允许最小Log报告副本修正：保留FileStore拥有型Inspect()，新增私有out填充供LogSystem.Working.File；控制摘要改Queue/Writer小统计独立采样而非复制完整Snapshot — 命中整次报告硬预算且不改Flush/Stop/native协议 — 代价为多几处私有调用/对应Log定向回归和栈证据，更新真实峰值。

- Task11 Ruling: 允许为正式OFF Git锁在当前功能分支保留中间RED测试/stub提交，最终从Task11 BASE收集全部review — clean Git消费者无法验证dirty测试树，TDD历史需可审计 — 代价为额外本地提交，不推送；Root可运行RED先做，产品未实现与环境/SDK/dirty拒绝分开，最终真实夹具/应用完成后OFF GREEN，stub早退不算卸载验收。

- Task12 Ruling: Android最小分支改公开pthread_gettid_np(Handle)==0后pthread_join — NDK header/libc导出无tryjoin，并非旧Task7假设API26 guard；AOSP android10/current bionic getter/join与内核CLONE_CHILD_CLEARTID支持native退出证据 — 代价是Android特定源码推理依赖与平台定向编译/链接/DB检查，不新增字段/预算，不用业务完成标志，DesktopLinux/Apple路径保留。新依据/失败轮次正式归档，明确未设备运行，稳定后再最终Full。

- Task12 Ruling: 最小补Consumer独立CMake的Log/Fmt/Spdlog原生接线并查同类手工ModuleBuiltin夹具 — Runtime新增Required Log后该独立consumer未同步，正式Full暴露 — 代价是新固定commit重跑尚未完成三桌面双模式Full；先ConsumerPipeline实际定向RED→GREEN，利用缓存增量不清构建树，不以重跑遮首轮失败。

- Task12 Ruling: Mac SourcePluginProject优先拆Base(Enabled/Disabled/ServiceHosts)与EngineOptIn Full为独立正式CTest，各保留原900s、冷根、原断言，核耗时并同步分类/Ownership/选择契约 — 原单case串行多工程冷准备已证持续进度且总预算不足，独立验收场景可隔离 — 若错误代价是测试注册/分类迁移与实际Full新计数、需定向和新固定源全量；不热跑掩失败、不无依据增时，保留R1Timeout。所有锁退出后才实施。

- Task12 Ruling: 原caller flags等6Full后独立提交时序现调整为当前R1全部进程/锁结束后、R2固定提交前统一落地 — R1 Mono已真实失败，原成功预期失效且反正需新源Full，避免R2后再次解冻 — 代价是R2包含移动脚本但不增加desktop依赖；保留原独立影响分析、Android/iOS实际DB及4最终组验证，不把旧R1结果伪称通过。

- Task12 Ruling: 给Fmt/Spdlog增加最小私有CurrentContext编译依赖，先不改SCAN — WSL三步Debug信息恢复正确但Windows实际A Debug→B Release→旧A链接RED exit1，Debug ModuleQuery.obj ITERATOR_DEBUG_LEVEL2 vs fmt format.cc.obj0；旧A命令Debug但实际对象仍Release，风险已证 — 代价是第三方随上下文变化必要重建与跨图定向/Full成本；不引入Pangu警告/宏/Module身份，不改预算，配置恢复和no-op单独证明，只有仍失败才SCAN单变量。首次Windows.debug，以CRT/linker为权威。

- Task12 Ruling: 最终第三方修正仅保留CXX_SCAN_FOR_MODULES=OFF，移除先前bd369私有Ctx依赖 — 621(有Ctx+SCAN_OFF)三桌面对象/配置/链接/no-op全0，Windows独立消融无Ctx且无modmap实际三步CRT/链接/no-op也全0，Ctx因果非必要而SCAN为真实变量 — 代价是生产精简新提交后正式跨配置回归/Full，保留先前Ctx-only全RED和消融范围，不将隔离probe替代正式Full，不因先前裁定固守多余接线。正式ProjectSwitch增加有效Debug/Release回切真实消费/链接与no-op契约。

- Task12 Ruling: 等当前全部Full/SDK写者退出后，为失败MyGamePluginCollaborationTests仅macOS显式DISCOVERY_TIMEOUT30秒(业务60/外层900不变)，helper仅必要透传、其余目标/平台默认5不变 — 真实5秒超时与XProtect事件至日志4.925秒吻合，cwd本机APFS、库存在、热发现正常、业务未运行，属于启动发现预算鲁棒性；OS具体延迟因果未完全证明，不宣称XProtect已彻底定因 — 错误代价为该目标发现故障最多延迟至30秒，不改安全/签名/产品协议。精确生成命令前后比对、新fresh冷Ownership验收，再Mac真实Mono→Mod Full原生0，不拼旧失败Full；Win/WSL已过Full按C++/默认有效路径/命令不变真实影响复用、仅必要定向，不因SHA全量重复；如有确切产品/路径证据优先修真因。

- Task12 Ruling: 仅Mac将Ownership多冷图拆4个正式case：Base(Origin负例+NoPlugins+AllDisabled)、ProjectOnly、EngineAndProject、ProviderOnly，各原900s — 真实两个冷图各5m编译加配置即耗尽单case900，独立验收有明确成本边界 — 错误代价为Mac注册/分类/Full计数增加与4冷门+真双模式Full成本，不放宽业务/产品期限。先核跨场景比较依赖，不删断言/漏执行；必要共享只能显式正式CTest fixtures、当前源/模式产物和自动依赖，不能历史热根或未版本化probe；Win/WSL默认All原序/原块/命令保持、iOS测试OFF有效图不变。稳定新commit后4fresh冷case完整0再Mac真实Mono→Mod Full；其他已过4Full+移动4组按真实影响复用、分SHA，不无依据重全量。

- Task12 Ruling: 仅Darwin Ownership Engine场景TIMEOUT1200秒，不引入receipt/fixtures — 单冷图编译真实估约978秒超过任意900，1200容纳发现/3测试并留约20%余量，局部参数比新增阶段共享状态最小 — 错误代价为挂起编译最多额外300秒才检出，不改变产品/业务期限；其余3场景900、业务60、目标发现30/其他5、其他平台All不变。稳定新commit/有效属性比较后仅Engine新fresh冷验收，dc4其余3场景已0且输入/属性未变复用，不因SHA重复；再Mac真实Mono→Mod Full非空/native0，不拼旧失败Full；Win/WSL4Full+移动4组按有效图/代码未变证据复用分SHA。

- Task12 Ruling: Mac Full前仅Engine准备预算1500秒，其他3场景900/业务60/发现30默认5不变 — 实际新冷1183.60令旧估978下的约20%余量不成立，仅余16.4秒，1500相对实测约26%可维护余量 — 错误代价为挂起编译最多再多300秒检出，不改产品/业务期限。先确认/结束当前自有Full prepare/SDK写者且CTest开始前安全暂停，不能在写者中改源；若已进CTest先报告。已在更严1200下真实0的同输入/断言冷编译不重复，仅有效属性1200→1500对比与新稳定commit，然后Mac真实双模式Full新HEAD；11492冷结果与新1500明确分SHA，其他平台按影响继续复用。

- Final: Ruling: Declined1 移动真机线程/终端/文件/Fatal/动态卸载未认证 — 批准验收只要求ARM64编译、最终链接和宿主检查；保留运行限制，Android公开实现推断不升级为设备证据 — 错误代价是若产品需要设备保证须另设真机验收。

- Final: Ruling: Declined2 完整signal/SEH、损坏进程与必然crash dump不纳入本次承诺 — 批准Fatal仅普通线程，允许无文字有界终止，现有独立终止契约仍必须成立 — 错误代价是相关场景需要独立Crash系统设计与认证。

- Final: Ruling: Declined3 不承诺强持久化、可靠审计或永久历史 — 批准尽力日志与历史回收不等于审计日志，接纳/送达/损失账目仍准确可见 — 错误代价是强可靠需求需另立持久化协议。

- Final: Ruling: Declined4 分区lease不认证同权限恶意目录替换 — 当前协议检查拥有权/链接/活跃文件，但没有安全沙箱承诺 — 错误代价是敌对TOCTOU环境需额外安全边界，不能将现有lease宣传为ACL。

- Final: Ruling: Declined5 普通非Try STL及Module/Host其他分配不扩为通用可恢复OOM — 冻结Memory规范允许非Try分配失败终止；本次FailureReport无分配及Log Initialize可恢复OOM仍为硬契约，I1必须修复 — 错误代价是未来全引擎可恢复需求须单独Allocator改造。

- Final: Ruling: Declined6 不认证用户formatter/运行期格式串/locale/宽字符/long double — 首版封闭格式子集明确拒绝这些入口，支持子集的同步有界格式化仍受验证 — 错误代价是扩展入口需要重新审计预算、分配及编译拒绝边界。

- Final: Ruling: Declined7 不宣称Sanitizer全覆盖或隔离性能SLO — 批准要求真实成本基线，未设性能阈值；现有未隔离负载与工具限制保留 — 错误代价是正式性能承诺需额外可复现场景和动态分析。

- Final: Ruling: Declined8 不认证大历史分区收缩响应SLO — 当前空间/枚举有界且归属与预算正确，普通I/O可阻塞、Initialize无deadline；没有对应负载性能实测 — 错误代价是若需响应上限，应增加可复现负载及算法性能验收。

- Final: Ruling: I1由BackendLogger扩展改为直接调用两个内联RawSink的spdlog同步Sink路径 — 批准spec §7.1原文允许同步logger/Sink，原计划logger派生仅为最初零补丁路径；Debug事实推翻空容器无分配假设，而optional补丁约50处访问更侵入。保留真实spdlog::sinks::sink/log_msg、等级映射、独立Outcome、显式time/source、误配置拒绝、异步公开接口及格式库；不改全局ABI/支持范围、不改upstream — 错误代价是若未来要求logger附加功能需重新审计适配；本轮须Debug真实构造/Emit/Flush/析构RED→GREEN、正常配置跨平台回归、Data预算重算及一次复审。

- Final: Ruling: 对Linux/macOS Mono直跑Host组补本地BuildRoot cwd定向，不重复其余组/四配置/Full — 原wrapper current_path将HostLogPartitions产物落到共享源码CIFS/SMB，Tests/README规定运行产物在本地BuildRoot，不能据此宣称ext4/APFS该组输出行为；无写者后将本轮产物精确可逆移存ignored目录 — 错误代价是必要模式切换和Host目标增量构建成本；Windows同属NTFS不因cwd全组重跑但保留本轮cwd限制与产物清理证据，最终各宿主恢复Modular。

- Final: Ruling: 复审Declined1的整体完成门独立收口 — 唯一源码复审只裁定固定0b..aa的I1/I2及修复新风险，当前2/2 ADDRESSED、C/I/M0；控制器仍须核原生wrapper退出、全部配置、无写者、正式归档和清理后才宣称Plan完成 — 错误代价是漏掉运行状态或档案收尾，不能用代码Approved代替。

- Final: Ruling: 复审Declined2沿用真实版本/运行/成本范围 — 旧六Full和旧四移动不拼为新RawSink或Windows23.1.5的Full，本轮不重测性能，移动仍只编译/最终链接/宿主检查 — 错误代价是未来若要求当前Full、性能SLO或设备运行认证须单独补验；本轮必要定向及四移动正式入口结果独立归档。

- Final: Ruling: 复审Declined3不外推分配探针覆盖 — new/DebugCRT/ELF wrap只覆盖已实现的具体路径，不认证所有DLL/系统/任意STL/Debug/Sanitizer；I1结论同时基于当前后端对象已删除三容器及真实Debug构造/初始化门通过 — 错误代价是新增必经分配或平台策略时需重新审计与扩展探针，不能引用本轮零观察泛化。

## 遗留项分诊

| 原条目 | 最终全分支评审判断 |
| --- | --- |
| M1/M2/M3/M9 | 测试进程替换new/delete与MSVC或libc++外部声明参数名不同的静态提示，非生产分配器或配对错误；不采用SDK保留名、不修改SDK、不全局屏蔽。下次维护相关夹具时处理。 |
| M4 | 接受的Truncated/ConstructionFailed累计和拒收标志不累计已有两项直接回归；其首条接纳交错另纳入I2修复。 |
| M5/M12-1 | ModuleIdentity固定边界循环的qualified-auto/指针运算提示及MSVC SDK位枚举诊断；未确认功能缺陷，保留基线。 |
| M6 | 最低栈夹具已按当前session前缀计数，避免历史文件混入；首次接纳交错另纳入I2。 |
| M7 | 整数比较已迁移std::cmp_equal，其他空初始化/trailing/default提示属风格；optional提示已按具体保护复核，未确认新的空访问。不将tidy退出0表述为零告警。 |
| M8 | ModeConsumer已直接include LogTypes.h，实际编译数据库tidy证据已闭环。 |
| M12-2 | POSIX Git untracked-cache-disabled stderr伴随原生退出0及clean状态，属于环境噪声；不改全局Git配置消音。 |

全分支评审没有新增Minor行为问题。历史Deferred记录均经上表分诊，已闭环项不再冒称未修复。

## 验收边界

移动只确认ARM64编译、最终链接及宿主产物检查；没有真机线程、文件、Fatal或卸载认证。Fatal验收限普通线程，没有signal/SEH、损坏进程或必然crash dump承诺。日志采用尽力输出与可回收历史，没有强持久化、可靠审计或永久保存承诺。分区lease不是对恶意同权限进程的ACL沙箱。可恢复OOM限本次明确Try/Log初始化契约，未扩张为全引擎分配器改造。格式只认证封闭支持子集。Sanitizer、隔离性能SLO及大历史收缩响应SLO均未认证。

公开报告与必要副本未变的核对和新默认实例布局实测均已完成；旧默认Data数字只作为原Task12历史记录保留。

## 执行异常与证据处理

- Windows 原 Mono wrapper 将合法原生 stderr 升为 NativeCommandError，外层实际1、该次 GTest 原生最终退出未知；不算产品RED。无残留后只续尚无原生结果的部分，保留stderr并显式捕获LASTEXITCODE，后续全部0。混编码原件保留至收尾，UTF8观察件与原件SHA256账本经独立PS/.NET字节核对，未用观察件覆盖原件。
- 原直跑Host的工作目录落到共享源，生成8份运行文件；无写者后精确可逆移存，前后哈希一致，删除0。最终三桌面两模式Host均在各自本地NTFS/ext4/APFS BuildRoot内运行通过，源树无遗留产物。
- 最终POSIX状态脚本首轮缺少rg，cache/SDK/进程过滤未执行，末尾常量0不构成通过；该轮只采实际Git状态0。改用系统grep逐步核证后，全部子模块、配置、无写者与iOS显式18.2 SDK路径存在均实际0。未为此重跑测试或构建，未修改全局Git/SDK/系统工具选择。
- 本轮已识别3次PATH python调用命中PyManager：实施者早期拟提取夹具一次、控制器打包一次、独立评审解码一次。三次均报告管理器自更新至26.0；后两次明确出现运行时索引解析失败，早期一次只观察到更新提示并核对目标文件不存在、原CMake未改。预期Python脚本结果均未产生，随后用现有PowerShell/.NET完成实际工作，不重试安装、不将启动器外层返回值当作验证成功，也不声称外部环境未发生自动变化。原始日志的PS/.NET字节和哈希核验已完成。

必要结论、失败轮次、源码版本和裁定已在本文件及正式验收记录归档；原始日志、临时脚本和diff仅属于执行工作区，不长期灌入计划。必要记录在bad1dc3归档并核对无依赖后，本计划执行工作区已精确删除且路径不存在；长期源码、正式Tests、各平台BuildRoot缓存及关键提交继续保留。
