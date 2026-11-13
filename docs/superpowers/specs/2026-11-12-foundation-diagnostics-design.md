# 基础诊断首版：Foundation 失败现场与 Log Module

> 状态：Approved（设计）；四轮 grilling 的 30 项决定、共同理解摘要及本稿均已获用户确认，独立设计审阅已完成，尚未实施或验收产品代码。
> 日期：2026-11-12
> 工作分支：`feat/foundation-diagnostics`
> 实现基线：`78c830648d68bb19a5ee3b9481e3db4de00651bd`
> 权威语义：[错误与基础诊断](../../../wiki/foundation/error-diagnostics.md)、[ADR-0007](../../../wiki/decisions/0007-error-diagnostics.md)。

## 1. 意图与已确认决定

本轮在进入 ECS 集成前补齐基础诊断：引擎失败时，调用方能知道原始原因、失败现场和清理是否完成。报告在 Module 卸载和 Host 销毁后仍可读取，必要检查在所有配置下有效，普通日志不可用时仍能安全终止。

代表性验收场景：主动让业务 Module 初始化失败，应用取得原始原因、阶段、Module 和回滚结果，完成清理并观察真实物理卸载后，仍可展示报告。普通日志接入启动、运行、回滚、关闭等真实场景，并供后续 ECS 记录基本事件。

已确认的组织和行为：

- **B：Foundation + Log。** Foundation 保留 Error/Result、失败报告、Assert/Check/Fatal 和最小应急设施。普通日志独立为基础常驻 Log Module：`Source/Runtime/Log/`、`PanguLog` / `Pangu::Log`、`pg::log`。
- **fmt + spdlog。** 保持禁用 C++ Exception、初始化可恢复 OOM 和既有日志契约；优先扩展点，必要时维护受控补丁，不用库的默认行为暗中替换契约。
- **Host 独立拥有实例。** 业务经 Pangu 日志接口提交，Writer 可安全失效；首版公开模式仅异步，Log 消费者调用 spdlog 同步后端。
- **开发默认终端与文件。** 应用配置路径、开关和必需启动输出。实例独占文件分区，预算覆盖自身管理的当前与历史日志，支持轮转和有界回收。
- **有界、尽力输出。** 固定内存预算、高等级预留、拒收和损失可见；日志失败不改变原业务 Result，安全关闭与输出完整性分别表达。

后文行为均为本轮已确认决定。具体 C++ 名称、布局、容量与平台原语由实施计划细化；本稿已批准，后续按实施计划推进，不将设计批准视为实现或平台验收。

## 2. 边界与已有实现

交付拥有型现场、清理等待解释、独立应急终止、Log Module、第三方适配、双输出及 Host 接线，保留正式样例与回归。

不交付 ECS、完整错误字典、稳定 Error ABI/磁盘协议、Meta Schema、通用 Allocator、业务 Registry、Crash 上传、远程遥测、Editor 日志窗口、可独立卸载 Plugin Sink、公开同步模式或日志热重配。普通日志不承担可靠审计、崩溃后必然保存或持久落盘保证。

| 当前代码 | 已有能力 | 本轮增量 |
| --- | --- | --- |
| `Foundation/Public/Error.h`、`Result.h` | 值错误、Native Code、expected | 保留控制流，另外提供拥有型报告 |
| `EngineHost/Public/HostOperation.h` | 当前错误、阶段、历史 StartupFailure | 原始现场、Log 状态与排空结果 |
| `Module/Public/ModuleOperation.h` | 身份、原错/清理错与责任 | 现场和有界等待原因 |
| `EngineHost/Private/EngineHost.cpp` | 启动、回滚、可重试关闭 | 显式准备 Log，业务清理后最终排空 |
| `Module/Private/ScopeRuntime.cpp` | Borrow、依赖、Stop、销毁关卡 | 从原判定点采集现场和原因 |
| `Module/Private/NativeModuleLibrary.cpp` 及平台实现 | 原生错误、显式 Release、兜底终止 | 有界复制 Loader 现场，终止带明确原因 |

不新增另一套 Loader、资源 DAG 或 Service Scope。日志成功不决定业务成功，Error 不持有报告指针、字符串或 Module Pin。Foundation 的错误与应急能力不要求启动 Log。

## 3. 方案、依赖与驻留

| 方案 | 收益 | 代价 | 选择 |
| --- | --- | --- | --- |
| A：全部诊断在 Foundation | 依赖简单，启动早期易用 | Foundation 承担日志线程、输出和第三方适配 | 未采用 |
| B：Foundation 保留报告/Fatal，普通日志独立 Module | 基础错误路径精简，日志实现与适配集中 | 增加常驻 Module 和接线 | 采用，命名 Log |

保留 EngineHost → ModuleRuntime → Foundation，增加 ModuleRuntime → Log → Foundation。Log 不反向依赖 ModuleRuntime/EngineHost；Foundation 不依赖 Log、普通格式化器、日志线程或第三方日志状态。

ModuleDefinition/QueryModuleFn 已在 Foundation，RegistrationContext 可前向声明。Log 可提供空 Register 和合法 Query，无需反向调用 ModuleRuntime。Query/Register 不创建线程或实例；Host/独立工具显式初始化 LogSystem。

Log 为真实 Pangu Module，跟随 Monolithic/Modular 生成单一 STATIC/SHARED 库及公共身份。BuiltinResidentQueries 增加 Log，ModuleRuntime 定义显式依赖 Foundation 和 Log。保留 REQUIRED Foundation 直接边以满足现有 resident 验证，同时更新原生链接、ModuleDefinition 与 Catalog，不能只改其中一层。

fmt、spdlog 是普通 ThirdParty Target，不获得 ModuleId。spdlog 留在 Log 私有实现；fmt 因公开模板成为受控头依赖。spdlog 使用同一份外部 fmt，固定兼容版本/提交、配置和来源，遵守 [ThirdParty 规范](../../../wiki/foundation/third-party.md)，配置阶段不联网。

每个 Host 独立拥有资源和统计，不使用全局当前 Host、默认 logger、全局 registry 路由或共享普通日志线程池。ModuleManager 管理逻辑 Module，不拥有或提前销毁 LogSystem。

应用 → EngineHost → ModuleRuntime → Log 的原生依赖保持代码驻留。退休 resident 记录后，最终排空不访问已退休的管理对象；所有实例、消费者和残留 Writer 控制块结束前，应用不能卸载其承载代码。常驻名称不代替实际依赖/卸载证据。

## 4. 接口与所有权

以下为职责与命名方向，不冻结长期 C++ API/ABI。

| 归属 | 入口方向 | 职责与寿命 |
| --- | --- | --- |
| Foundation | `FailureReport` | 操作级拥有型现场，独立于 Host/Plugin |
| Foundation | `FailureContext` | 操作/回调局部采集视图，不逃逸 |
| Foundation | `PG_ASSERT`、`PG_CHECK`、`Fatal` | 可裁剪检查、必要不变量和独立终止 |
| Log | `LogSystem`、`LogConfig` | 拥有固定资源、后端和状态 |
| Log | `LogWriter` | 可复制、失效安全的句柄，不延长实例寿命 |
| Log | 提交/状态快照 | 可忽略的轻量接纳结果和累计状态 |
| Log | Flush/Stop 结果 | 有界拥有型排空快照，销毁后仍可读 |

使用 fmt 的丰富展示辅助归 Log 或应用，Foundation 报告本身只要求拥有型数据和独立可读的数值/文本，不为展示反向依赖 Log 或 fmt。

Host 显式向 ModuleManager 提供 Writer，经 RegistrationContext、ScopeBuildContext 提供所属 Module/Scope 标签。Factory 可将 Writer 注入业务对象，供 Start/Stop/Destroy 使用。独立工具和直接使用 Manager 的调用方也可显式提供，空句柄表示禁用。

LogWriter 只保留由常驻实现创建和释放的控制块。关闸后拒收新提交，已进入的提交结束前实例受保护；之后可释放队列和后端。残留 Writer 安全返回关闭/禁用，不访问已释放实例，句柄数量本身不阻止 Host 关闭。Host 销毁后的复制、提交拒收和最终释放仍须安全，控制块析构不能在业务 Plugin 中实现。

业务自身代码/对象仍遵守 CodePin/ScopeBorrow，Writer 不授予执行已卸载代码的权利。记录在提交返回前拥有消费所需数据，消费时不调用生产者代码。

Log 只依赖 Foundation 基础值。当前位于 ModuleRuntime 的 ScopeHandle、DeclarationId 由适配层复制为固定值标签；原领域报告保留强类型身份，标签不用于重建访问句柄。

## 5. 失败报告与清理现场

### 5.1 操作预算与原始原因

保留值错误、原始位置、必要摘要、领域身份及次要清理错误。主错优先，补充上下文、回滚错误和容量不足不能覆盖它；数值身份、清理责任和截断可判读，未知域仍可纯数值展示。

预算按整次操作核算当前现场、历史 StartupFailure、嵌套 Module 报告、必要副本和峰值，不为每层各配默认 8 KiB。8 KiB 仅为候选容量；计划明确角色分区、对外交付大小、持久历史与采集工作峰值，记录副本预算来源。收集/归并不扩容。

复制文件名、函数名和错误文本，不保留 string_view、source_location 字符指针、内部自引用、Plugin formatter、Host Arena 或依赖 Plugin 的析构机制。快照复制/移动后可在卸载及 Host 销毁后读取、展示和销毁。

领域操作选择主错，PrimaryError、OriginalError、CleanupError、StartupFailure 和清理责任保持含义。现场匹配对应领域错误，不产生另一种控制流结论。历史启动首错独立保存，当前清理结果可更新但不覆盖它，返回清理错误不自动成为启动起因。

FailureContext 显式下传，起源层记录位置，传播层追加上下文；未知起源不以打印位置冒充。报告可选或截断不影响 Error 独立处理。操作局部构造报告，结束后整体发布，重入 Busy 不污染外层同一 report。并发子操作预分总预算、分别采集，完成/排空后按声明的稳定顺序归并，不共写一个 Context。

### 5.2 回调与原生现场

RegistrationContext、ScopeBuildContext 提供局部 FailureContext，与可保存的 Writer 分开。Start/PollStopped 增加 FailureContext，由 Runtime 每次调用提供；一次迁移自有消费点，不长期并存等价签名。

Factory、对象和异步工作不能保存局部 Context。RequestStop/Destroy 沿用无可恢复返回值语义，可用已有 Writer 记录，不能让析构通过隐藏报告伪装成成功关闭。

Win32 数值先于日志捕获；POSIX Loader 借出文本在下一次 Loader 调用前有界复制。保留原生来源和路径，不要求读取报告时查询原 Plugin 字典。

### 5.3 等待原因

从现有 Gate、DAG、Stop、原生释放判定点采集快照，区分 Borrow、Module Pin、依赖/子 Scope、Stop/PollStopped、库释放及 Log 排空。附身份、必要计数、已有错误和责任，不追踪每次借用的持有者调用栈。

列表有固定上限、剩余数量/截断和稳定选择规则，候选上限 8 项，最终值由实施验证。并发计数为采样，不承诺时间全序；未知原因不推断。诊断只解释状态，不替代 Gate、Pin 或资源图。

## 6. Assert、Check 与 Fatal

统一引擎 Assert 编译开关默认开启，Release/NDEBUG 不自动关闭，裁剪时显式关闭，自有消费点保持受控配置一致。第三方的断言/异常选项分别管理，不修改全局 NDEBUG 来混合策略。

PG_ASSERT 启用时条件一次、消息只在失败分支构造，关闭时不求值，不承担业务副作用。PG_CHECK/Fatal 始终保留，外部输入错误继续返回 Result。

Fatal 不返回，不能通过调试器继续、回调、异常或 longjmp 恢复。仅依赖 Foundation 和已验证少量平台原语，不依赖 fmt、spdlog、普通日志锁、Scheduler、Host Shutdown 或 Plugin Sink，不 Flush 普通日志。

原因、Error 和安全位置使用固定上限存储。并发/递归 Fatal 不等其他失败线程输出。应急尽力而为，普通 stderr/管道/文件/调试输出未经验证不能当成有界后端；无合格后端可无可见文字直接终止，不因外部写入无限延迟。

仅覆盖普通线程上下文，不宣称完整 signal/SEH/损坏进程处理或必然 dump。子进程验证终止；先迁移本轮触及的兜底终止点，不全仓库机械替换。

## 7. fmt/spdlog 复用与无异常适配

### 7.1 复用范围

前端使用 fmt 编译期格式串类型，公开模板可引入受控 fmt 头。默认集合仅为验证过的文本、标量、枚举和身份类型，并限制长度、宽度、精度等；任意用户 formatter、运行期格式串和 locale 扩展等不进入首版默认路径，具体子集与检查由实施验证。

过滤先于昂贵参数求值与格式化，提交期间同步生成拥有型记录，不排队闭包、format_args 或借用参数。输出长度限制不等于任意格式化都无分配/有界，受支持子集须验证实际路径。可检测构造失败退化为带标记最小记录，不覆盖业务错误。

Log 自管队列、序号和排空，消费者调用 spdlog 同步 logger/Sink，不叠加默认 async_logger/thread_pool。首版对外仅异步，工具显式 Flush/Stop，内部同步后端不等于公开同步提交。

### 7.2 上游差距与处理原则

| 对上游 v1.x 的源码核查 | 本轮要求 |
| --- | --- |
| 禁异常时 SPDLOG_THROW 进入 abort，文件失败可能走此路径 | 可失败 Sink/文件适配，预期 I/O 失败保存状态并停用，不直接 abort |
| logger 字符串/Sink 容器默认分配没有 OOM 返回 | 审计并适配全部必经初始化分配，自定义 Sink 不自动解决 OOM |
| 默认格式化/消息缓冲可能增长 | 受限 fmt、定长拥有型记录，消费者路径也计入固定预算 |
| 默认异步 Flush 排队，线程池析构等待 | Log 自有请求、完成快照和可重试停止 |

这是源码证据，不是锁定版本或本项目验收。计划须明确版本、扩展点、补丁和失败传播路径，优先上游接口，必要补丁按 ThirdParty 规范维护并测试。不把 SPDLOG_THROW 改为空/return，不通过启用异常、替换全局分配器或吞错绕过契约。

普通库分配/隐藏构造不能被当作已可恢复 OOM。需要大幅重写、放宽预算或改变已确认语义时先回到设计讨论。

上游依据：[fmt API](https://fmt.dev/latest/api/)、[logger](https://github.com/gabime/spdlog/blob/v1.x/include/spdlog/logger.h)、[错误处理](https://github.com/gabime/spdlog/blob/v1.x/include/spdlog/common.h)、[文件实现](https://github.com/gabime/spdlog/blob/v1.x/include/spdlog/details/file_helper-inl.h)、[线程池](https://github.com/gabime/spdlog/blob/v1.x/include/spdlog/details/thread_pool-inl.h)。接入时核对具体提交，不依赖浮动分支构建。

## 8. 记录、队列与初始化

等级 Trace/Debug/Info/Warning/Error，Fatal 独立。候选默认 Info、消息 1 KiB、分类 64 B、位置文本合计 512 B。包含可选 Error 和 Module/Scope/操作值标签，缺失与截断分开。UTF-8 文本有界处理多行/控制字符，不冻结为业务协议。

有界 FIFO、多生产者、一个消费者，候选 256 条/高等级预留 32 条。预算包含字段、对齐、在途记录、输出缓冲、控制块、统计和管理快照。最终数值可配置，验证溢出、最小值和相互约束，初始化后不增长。

低等级只占非预留容量，高等级可用全部空位。不驱逐已接受记录，按序号消费。容量或接纳竞争均可拒收任意等级，按等级/原因统计。不等空位、不循环等待接纳锁、不在生产者执行 I/O、不扩容，不宣称无锁或硬实时。

提交可返回可忽略轻量枚举：接受、过滤、丢弃、关闭等，不逐条构造 Error/报告。接受不保证送达，只有接受成功才分配递增序号。序号耗尽停止接纳并报告；计数饱和或等效可见溢出，不静默回绕。

初始化支持可恢复 OOM：配置、控制块、缓冲、同步、后端、输出、线程逐项可失败，准备完成才发布实例。失败反向清理，不发布半初始化 Writer；残留原生责任保留实例与 Closing，默认降级仍须持有并最终清理，必需失败正常回滚。

可用 Log 内最小 Try 分配和线程接缝，同源释放，不先建设通用 Allocator。全部初始化/运行分配在实际配置验证，空/禁用 Writer 不为报告初始化失败再分配。

## 9. 双输出、文件分区与故障

### 9.1 配置与独占分区

开发默认请求终端和文件，可分别关闭并指定目录。目录缺失、权限或分区占用作为文件能力失败；默认明确降级，健康终端继续，不能猜 cwd 或换目录写入。RequiredOutputs 指定启动必需能力，失败正常回滚；均关闭为 Disabled，均不可用保留应急，不发布伪造运行实例。

每个 Log 独占应用分配的日志分区，多 Host 共用根目录时分别使用独立分区，稳定归属由应用配置。跨进程/实例占用可靠检测，未取得独占权不得清理或接管；冲突按可选/必需策略处理。

每次运行使用独立会话文件，独占创建、有限命名重试、不覆盖。实际路径以拥有型有界文本交付，超限失败而不截为另一条路径。样例写应用运行/产物目录，测试写本宿主 BuildRoot，移动应用提供可写目录。

### 9.2 预算与轮转

总预算覆盖分区内本 Log 管理的当前与历史日志。单文件达阈值按完整记录轮转；需要空间优先回收最旧已关闭受管理文件。保留活跃、归属不明和外部文件，不仅凭扩展名判断归属。

预算不足且无法安全回收，或轮转/删除/创建失败时，文件输出明确降级，不继续突破预算。枚举/回收工作存储也有界，不为文件清单隐式扩容。字节口径、默认限额、阈值、文件数/枚举资源约束及校验由实施者确定记录，不声称限制其他主体写入或整个文件系统空间。

正常按保留策略回收已送达的历史文件，不回改为提交拒收或 Sink 未送达；回收文件数/字节等状态另行可见。累计 Flush 的完整性描述处理/送达结果，不承诺此前文件永久存在。

### 9.3 处理与运行故障

两个常驻输出槽位分别记账，消费者在队列/路由锁外依次调用。允许共享阻塞：慢文件可拖慢终端并导致拒收。某路返回失败即停用，健康路继续；停用槽位后续计未送达，另一槽位成功不能代表两路完整。

错误不递归写回自身，最多向健康路发一次有界降级摘要，保留数值原因与累计统计。业务主动查询状态/计数，不在消费者线程回调业务。消费者中的提交/管理重入不能自激、等待自身或死锁。

失败输出不自动重开、恢复或热重配，新 Log 实例恢复，Host 不提供运行时替换。正常轮转属于正常流程。RequiredOutputs 只约束启动，运行失效交 Application 决定是否停 Host，Log 不反向调用 Host 或自动 Fatal。

普通 I/O 可阻塞，具体行为须声明，Stop deadline 不强制取消任意原生写入。Flush 只承诺约定缓冲刷新，不默认持久落盘；安全关闭前保留输出状态和代码。

## 10. Flush、Stop 与完整性

状态为未启动 → Running → Closing → Closed，初始化失败可留下待清理 Closing。拥有者串行管理，提交可并发。关闸与接纳线性化，拒收后续记录，在途提交结束前不释放实例资源。

Flush 固定最高已接受序号 H，返回实例起点至 H 的累计前缀，不重置历史。包含逐输出送达/未送达、刷新结果、完成状态及采集时的累计拒收。完成与完整分开，过滤不等于丢失；禁用、初始化失败和停用须区分。

消费者越过 H 前保存前缀并完成约定刷新，再处理后续。H 后错误不能混入，也不能漏掉调用前已处理结果。仅一个未完成管理请求，超时保留原 H/进度，重试同请求；管理重入不等待消费者自身。

Stop 先关接纳、等在途提交并固定最终 H。已有未完 Flush 时保留原目标，完成并保存结果，再最终排空；Stop 交付原 Flush 和最终结果，原前缀完成不代表停止完成。Closing 不创建新 Flush，只推进/取得已有结果。

只维护一活动请求及一旧前缀摘要，不保存无限历史。未完时保留队列、线程、输出、控制块和代码，不 detach、强杀或泄漏伪造完成；输出处理、资源清理和消费者安全退出后才完成。

超时返回未完成，由应用重试，不自动 Fatal。仍未安全完成即销毁拥有者时，有限析构兜底后 Fatal，不隐式无限等待。永久阻塞可持续 Closing，不能虚报成功释放资源。

最终快照保留截止采集点的拒收/损失，残留 Writer 后续拒收不追写已交付快照；Closed 后读取不依赖 Sink/线程。安全停止但损失日志为“完成但不完整”。

## 11. Host 接入

HostStartupConfig 增加日志配置，在常驻注册/业务回调前准备 Log，保持原有六个 Module 阶段的相对顺序与意义。Core 原生依赖在 Manager 装配前已持有，不通过 Register 隐式创建实例。

Disabled、可选降级、必需失败和残留责任分别报告。可选失败不抢占后续业务首错；必需失败成为对应启动错误并回滚。每次操作携带当前 Log 状态，查询不隐式初始化，Closed Host 不重启。

正常关闭沿原 Module/Scope 协议排空并销毁业务，清理中已有 Writer 可用，外部生产者由应用协调。然后关 Log 并最终排空；Manager 已完但 Log 未完仍 Closing，TryFinishShutdown 返回可重试 Busy 并解释等待来源。

Start 失败共用最终关闭协调流程：返回首次启动原因，StartupFailure 拥有原始现场；Manager/Log 未清理完保持 Closing，后续继续推进。清理错、输出失败和超时不覆盖启动首错。

Log 不注册为 Manager 会提前销毁的资源，最终排空不依赖退休对象。应用原生依赖持有到全部工作/控制块结束，Modular 核对实际驻留。Host Status 表达业务/生命周期，停止报告表达输出完整性；Closed 可以伴随日志不完整，应用同时检查，Sink 失败不替换原 Result。

最终失败摘要由处置边界输出一次，底层采集不逐层打印，正常 Busy 不刷 Error。展示/提交是后续诊断操作，不修改原报告和 Result。

## 12. 验收与覆盖

以下为实施后要求，当前不宣称构建/运行/平台通过。

| 场景 | 必须检查的结果 |
| --- | --- |
| Module 接线 | Log 单库、Query/Catalog/required 依赖与双模式正确，无反向环；第三方无 Module 身份 |
| 第三方适配 | 固定版本/配置；分配可恢复，无禁异常预期 I/O abort；补丁匹配/重配边界可验证 |
| 初始化故障 | 各准备阶段逐点注入，不发布半实例，降级/必需失败/残留责任正确 |
| 回调现场 | Register/Factory/Start/PollStopped 原因/位置/身份正确，回滚保首错，Context 不逃逸 |
| 报告/重入 | 总预算和副本峰值明确，复制无自引用、截断可见，同一 report 重入不污染 |
| Writer | Host 销毁后复制/拒收/释放安全；残留句柄不阻止关闭，在途提交阻止过早释放 |
| 真实卸载 | 文本/位置已复制，释放 Pin 并观察物理卸载后仍可消费/销毁报告及记录 |
| 等待原因 | 各关卡可区分，列表有界，不改变原 Gate/DAG |
| 格式化 | 过滤前不求值昂贵参数，支持子集无隐藏增长，构造失败/截断可见，不接任意 formatter |
| 队列压力 | 提交无分配/I/O/空位等待，预留/FIFO/竞争拒收/序号和计数溢出正确 |
| 文件分区 | 两 Host 独立，占用不接管，目录/权限/路径/命名故障遵守可选/必需策略 |
| 轮转回收 | 按整条记录，覆盖自身历史预算，仅回收最旧已关闭受管理文件，保留活跃/外部文件，枚举有界 |
| 输出故障 | 一路失败另路继续，共享阻塞可复现；状态可查询，无自动恢复、业务回调或递归风暴 |
| Flush/Stop | 固定累计范围、旧请求交接、两份结果、超时重试和损失快照准确 |
| Host 关闭 | Destroy 可记录，Manager 完成不提前 Closed，历史首错保留，安全与完整分开 |
| Assert/Fatal | 明确开关与 NDEBUG 解耦；Check/Fatal 不裁剪；子进程验证无实例、并发/递归、受阻输出终止 |
| 成本 | 报告/队列/历史管理内存与过滤/接纳/拒收成本有基线，不移除保护美化结果 |

正式测试放 Tests/Foundation、Tests/Log、Tests/EngineHost 和必要 Module 夹具，遵守 [Tests 维护规范](../../../Tests/README.md)。死锁/超时/Fatal 用受监督子进程，不以未版本化 Probe 为验收依据。format/tidy 用实际目标和数据库。

三桌面正式 presets 验证双模式与真实卸载，WSL/macOS 遵守共享源码和本地 BuildRoot。Android/iOS ARM64 双模式验证编译/最终链接/架构/依赖，不称为设备线程、终端、文件或 Fatal 运行验收。

归档提交/tree、命令、退出码、失败/重跑和限制，完成后同步 wiki。相同已测 tree 整合按 AGENTS.md 复用测试，不因分支/SHA 改变重复全量。

## 13. 实施授权与设计状态

用户授权实施者在上述契约内验证并确定容量、预留比例、报告分区、磁盘限额/阈值、默认等待预算、fmt 子集、私有文件布局、平台原语和故障注入。当前数值为候选，最终配置和依据写入计划/验收，不暗中成为持久 ABI。

不能借此放宽无异常、可恢复初始化 OOM、固定预算、失效安全、输出降级和可重试关闭。显著扩大补丁、改变已确认语义或范围时返回设计讨论。

四轮 grilling 的 30 项及共同理解摘要已确认，本稿据此取代 A。旧 A 稿审阅结论不覆盖本稿。本稿独立可实施性审阅及后续规范/实施契约两路审阅均未发现有效问题；没有运行构建或测试，不表示实现验收。用户于 2026-11-12 明确批准本稿，进入实施计划阶段；产品代码尚未实施。
