# ABI Collection 独立评审修复计划

> 状态：Completed（R1—10，2026-10-26）。A—G 全部完成并独立复审通过，原计划 Task 12 已关闭。后续提交 `9561fe6` 的追加修复于 2026-10-27 完成，最近一次完整三桌面 CTest 为 **104/104**；当前统一结论见[原计划最新验收状态](2026-10-23-abi-collection.md)。下文授权、基线及工作区约束保留修复开始时的记录。
>
> 执行授权：用户要求修复 2026-10-26 独立评审问题。
> 依据：[批准 Spec](../specs/2026-10-23-abi-collection.md)、[原计划与验收状态](2026-10-23-abi-collection.md)中的 R1—10 独立评审结论。
> 工作区：现有 `D:/Git/PanguEngine`，基线 `dbfe7aa` 加用户未提交实现；不改变 index、HEAD，不提交、不推送，不清理既有产物。

## 约束与取舍

修复现有 STATIC 契约中的已证实缺陷，保留 v1 Schema、Canonical/Fingerprint 算法、四参数编码接口和 Host/Target 边界。不新增 Loader 信任要求。

归档关联采用核验实际 archive 成员与真实编译对象内容的方案，避免增加另一条需维护的 archiver launcher/收据管线。解析必须支持验收平台使用的普通 LLVM archive 格式，拒绝无法解释的格式和多余/缺失成员。

环境基线用私有 schema2 的 environment_baseline 保存相关值及 null（未设置），区分未设置和空值；由 C++ 验证，CMake 只导出原始配置事实。平台 ABI 控制项保持 fail closed，不能因修复而拒绝普通工程宏。

每组先补回归、观察行为 RED，再改实现并观察 GREEN；各组限定文件归属，构建使用不同 scratch BuildRoot。根协调者在各组稳定后整合诊断逻辑并执行集成验证。

## 修复任务

- [x] **A：R1/R2，状态与输入保护。** 修改 CollectionState/CollectionRequest 及状态、请求、组测试；核验真实 archive；统一辅助输出别名；保留请求文件身份。同时完成环境基线 null 的 C++ 读取/比较支持，供 D 的 Root 回归使用。
- [x] **B：R3/R4/R8，libc++ 后端。** 修改 Linux/Apple/Android Collector、HeaderTrace/Probe 及对应测试；覆盖源内 ABI 控制宏、未支持的 ABI 参数和实际关键 Header 路径。先核验 R8 的实际 overlay 行为再确定拒绝测试。不要修改 Windows Collector 或各后端 Execute 的诊断职责。
- [x] **C：R6/R7，Windows。** 分别传播 banner/resource 查询失败，拒绝实际 COFF 对象中冲突 CRT 指令；补真实资源查询失败和 CRT pragma 回归。不得改变合法最终 Host 未定义符号行为。
- [x] **D：R9/R10，生产环境与 构建工具 Bootstrap。** Root 请求捕获相关环境及 unset 状态；Host 清理目标 toolchain 环境并处理受污染缓存；补真实 Root/Host 回归，验证只改环境也能使完成状态失效。
- [x] **E：R5，失败诊断。** 等 B/C Collector 编辑结束后实现统一的工具私有有界探针诊断，保留失败阶段、真实退出码及输出；stdout 仍保留正式失败报告 JSON 的接口。以实际 compile wrapper 的 #error 回归验证四后端。
- [x] **F：macOS 进程测试。** 查明 1000ms 断言包含哪些调度/启动阶段；采用能验证取消、清理及有限上界的确定性同步，不能直接以盲目重跑或放宽阈值掩盖问题。
- [x] **G：复审与验收。** 每组独立复审，处理有效阻断项；最终三桌面完整 CTest、Android/iOS 宿主采集及目标归档验证；针对改动执行 format/tidy、diff 检查并更新原报告/计划。仅在实际通过后关闭 Task12。

## Review Focus

1. 真实重编译后跳过归档、首次使用不匹配归档、重复成员名和技术 SCC 仍不能错误发布。
2. 请求文件与辅助状态文件/保留目录、SignatureHeader 与源文件的别名都在写入前拒绝。
3. 源内 libc++ 宏、位域/ABI 模式及 Header overlay 不得同默认 Profile 混同，普通业务宏仍允许。
4. 环境 unset/空值/新值、Build toolchain 环境与旧缓存都需要真实生产接线覆盖。
5. 编译错误诊断不污染 stdout JSON；进程取消回归必须证明子进程已启动和被清理，避免调度时序假失败。

## 执行记录

执行期间的进度、逐组 RED/GREEN、命令及独立复审曾记录于 `.superpowers/sdd/2026-10-23-abi-collection/repair-progress.md`。计划完成并独立复审、验收通过后，必要结论已归档到本计划和[原计划验收摘要](2026-10-23-abi-collection.md)；临时工作区已于 2026-10-28 按用户要求清理，不再依赖该历史路径读取结论。
