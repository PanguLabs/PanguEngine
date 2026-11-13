# Log 正式验收

最终评审修复将 LogInit / Lifecycle / Drain、初始化/排空监督夹具、私有零分配场景和
EngineHost Factory/Destroy 的精确接纳准备统一接入短段 `ConsumerPause`。
`SubmitPaused` 先取得传入 Writer，再暂停消费者，仅执行一次真实提交，解锁后才进行
Inspect/Flush/Stop；它不循环重试。已在输出/序号屏障后的天然静止阶段继续保持原断言。
初始化在途生产者仅在管理调用返回后保护释放与提交，原固定 H 不变。

Host 的单次 `TryFinishShutdown` 返回 Busy 可能发生在 Log.Stop 发布 StopRequested
之前；Closing/WaitingForLog 也不能证明消费者已收到停止请求。正式 Host 用例在业务
析构后用现有 SnapshotLock 确定强制该路径，先解除屏障，再于原两秒内持续推进管理，
逐轮检查 Busy、Closing、WaitingForLog 和未释放，最后保留原生退出及安全关闭断言。

`EmptyConsumerContentionDoesNotAdvanceAcceptedPrefix` 把真实消费者 `TryPop` 停在已取得
队列锁的空轮询窗口，验证首条合法 Dropped、竞争计数、H 不因拒收增长，随后受控
Accepted/Delivered=1，累计拒收仍使 OutputComplete=false。屏障只由 Cold 接入的实例
Hooks 启用，有两秒上限及 Owner RAII 解除；没有改变普通提交协议或队列数据布局。

`LogNoAllocation` 额外在禁止隐式 new 的域内执行真实 Initialize/Stop，并验证实际
Data/Control/worker 的取得与释放；构造/Emit/Flush/析构关卡保持。Windows Debug 使用
独立测试 EXE 的 Debug CRT allocation hook，先验证真正 malloc 被拒绝，再分别拒绝
Initialize 的 Data/Control 两次 Try 分配。恢复原 hook 后检查 kOutOfMemory、Closed、
资源计数归零；生产目标没有这个 hook。Linux 保留直接 malloc 引用的 --wrap OOM 用例。

`LogAssertConfig` 在四个隔离 Release/Debug × Assert ON/OFF 子图构建并执行同一
NoAllocationFixture；Windows 严格核对真实 `_ITERATOR_DEBUG_LEVEL=0/2`，Debug 必须
交付 malloc 自验证及两例初始化恢复计数。分配门不将单个 DLL 改为 iterator ABI=0。
编译等级为 Error 的子图通过真实 Error 提交执行同一正式夹具，不依赖被裁剪的 Info。

`LogLifetime` 通过正式 PanguMetaTool Query 加载 `PanguLogProducer`，Host EXE 在 Modular 不原生导入该业务 DLL。同一 ScopeBorrow 覆盖两个显式生产阶段：先同步复制 Plugin 栈消息、分类、source_location 和失败报告，等待 `BlockSequence=1` 在首次 EncodeRecord 前冻结，再提交静态消息/分类。这样第二次提交不与首次 TryPop 竞争，不把合法 Dropped 误判为所有权错误；不修改产品拒收契约或无限重试。释放 Borrow/Scope 访问后启动 Host Shutdown，确认 DLL 原生静态析构文件标记、HighestProcessed=0 和 Host Closing，再放行消费者并核对实际文件文本、最终报告。记录用例先释放外部 Writer，另一个用例保存 Writer 到 Host 对象析构后复制、WithTags、提交 Closed 和最后释放。Monolithic 同时覆盖静态调用链和存活，明确不声称静态归档物理卸载。

`LogRuntimeNoAllocation` 链接真实 Pangu::Log，不编入替代实现。九个实例私有初始化故障阶段验证回滚、报告、资源计数和未发布 Writer；真实初始化完成后，武装 EXE new 探针执行 Disabled 初始化、GetWriter/WithTags、格式化提交、消费者、Inspect、FailureReport、Flush、Stop、Closed 拒收和 Writer 释放。探针以 throwing/nothrow/aligned new 的退出码 91 自验证。Windows EXE 的替换不覆盖全部 DLL、CRT 或 OS 分配；ELF/Mach-O 符号插入同样不保证覆盖动态库内部入口。原生线程/文件准备发生在武装前。该结果与 `LogNoAllocation` 私有源码探针（Linux 另有直接 malloc 引用 --wrap）、Try 分配故障及锁定第三方源码审计互补，不能泛化为所有第三方路径无分配。

首条也必须受控：`ConsumerPause` 复用现有私有 SnapshotLock，消费者在同锁内尝试 TryPop。先获取 Writer，再短段持锁仅提交，解锁后才能 Inspect/Flush/Stop；有限等待取锁不改变产品拒收契约。Lifetime 首条、Runtime 成功分支和基准预热都用它。Runtime 监督另启动独立 `first-drop` 进程，在同一真实 LogSystem 的空队列锁被确定持有时提交，验证 Dropped、H=0、Info 竞争拒收=1、两路用户 Delivered=0、ResourcesReleased=true、OutputComplete=false；成功进程独立验证 H=1、两路 Delivered=1、完整且真实文件文本存在。两分支共用实际 Game/Editor 的 ValidateLogOutcome，还验证伪 H/完整性/单路送达/释放状态以及 Closed/Disabled/Filtered 不能通过，不以随机重跑或丢弃累计 loss 获得 GREEN。

实际应用对单次正常 Info 提交如实交付两个互斥结果：接受时读取真实文件并输出完整交付；合法首条竞争拒收时输出不完整和累计 dropped=1，但业务关闭仍可成功。双路故障、异常 Closed/Disabled/Filtered 或统计不一致依旧失败。Project 的统一 CheckLogOutcome 精确检查唯一一行 H/accepted/dropped/两路送达/资源释放/完整性，不把拒收写成 complete。

桌面先构建 `PanguLogLifetimeFixture PanguLogRuntimeNoAllocationFixture`，再以根 preset 运行 `ctest --preset windows -R '^LogLifetime$|^LogRuntimeNoAllocation$' --no-tests=error --output-on-failure`；Linux/macOS 使用对应 preset 与隔离 BuildRoot。受监督进程均有明确退出码和超时，错误退出或崩溃不能充当成功。

基准显式 `-DPANGU_BUILD_BENCHMARKS=ON` 后构建运行 `PanguLogBenchmarks`，完成恢复 OFF。程序输出每场景样本数、均值、p50/p95/p99，单位 ns，不设性能门槛。Disabled 10000 次、Accepted 2000 次（队列 4096、32 预留、16 MiB 预算）、FullDrop/ContentionDrop 各 10000 次（队列 8、1 预留）、报告构造复制 10000 次、真实双输出提交加 Flush 100 次。提交全部经过真实 Writer 存活/接纳 Gate；私有屏障只控制消费者调度，竞争场景明确持有真实队列锁以稳定量测拒收分支。Accepted 不包含随后异步 I/O，FullDrop/ContentionDrop 包含同步格式化；双输出包含消费者调度与原生刷新，目录准备/初始化/最终 Stop 排除于计时。计时包含两次 steady_clock 读取，不做空操作扣除；样本缓存、排序和输出在计时外。

R1 后预热的短段 SnapshotLock 在样本计时外；DualOutputAndFlush 每次样本为确保真正接受，计时内还包含 ConsumerPause 的有界协调与解锁，然后才是真实 Flush。这个私有协调成本没有被扣除，不把它宣传为自然竞争条件下的纯提交成本；与旧轮次分别记录。
