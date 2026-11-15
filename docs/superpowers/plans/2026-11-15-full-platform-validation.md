# 五平台全量验证与测试基础设施修复

状态：验证已完成；完整结果、版本和边界见文末验收摘要。

> 执行方式：在当前共享源码上逐项验证；Full/Project 运行期间固定 HEAD、保持 tracked 工作区干净，使用独立宿主 BuildRoot。

**目标：** 按用户要求先完成 Windows 全量，修复有证据的问题，再完成其他平台验证。

**架构：** 三桌面使用根正式 preset，按 Monolithic→Modular 完成 full 构建和完整 CTest；
Android/iOS ARM64 完成两模式根构建及真实 MobileHost 最终链接、架构和动态依赖检查。

**技术：** C++23、Clang、CMake/CTest、WSL Ubuntu、macOS SSH。

**规格：** [测试维护规范](../../../Tests/README.md)、[测试分类与运行契约](../../../wiki/foundation/testing.md)及用户本轮验证要求。

## 全局约束

- Windows 完成后才启动其他平台；Linux/macOS 使用不同宿主和 BuildRoot，可并行，但各自模式切换串行。
- Windows/Android 使用 NTFS，Linux 使用 ext4，macOS/iOS 使用 APFS；Engine 源码始终使用规定的共享挂载，不复制 Engine 到临时目录。
- Samples/Benchmarks/override 保持 OFF；桌面 BUILD_TESTING/EngineTests ON，移动编译验收时显式 ON，结束后恢复默认 BUILD_TESTING OFF、Monolithic。
- 不削弱断言、负例、恢复、分类、运行集合或业务期限。失败、诊断、重跑及被测版本分别记录，不拼接为一轮 Full。
- 修复提交仅为正式 Git 锁验收提供干净快照；不推送或合并。

## Task 1：原始基线验证

- [x] 保存基线 `abc9f8e67296b242c5616231203a706da0d2954a`，tree `d0c90e8288157f93b9d4168006884cd6da05967e`。
- [x] Windows 两模式正式配置、full 构建和完整 CTest。
- [x] Linux 两模式正式配置、full 构建和完整 CTest。
- [x] Android 两模式根构建、正式 CrossCompileTests 最终链接和默认配置恢复。
- [x] 保留 macOS 首轮失败/中断记录并定位，不把部分通过计入完整验收。

| 基线配置 | 结果 |
| --- | --- |
| Windows Monolithic | 319/319，2415.10秒 |
| Windows Modular | 315/315，2082.75秒 |
| Linux Monolithic | 314/314，1847.99秒 |
| Linux Modular | 310/310，1565.57秒 |
| Android ARM64 Modular / Monolithic | 根配置、构建、真实 Runtime/业务宿主最终链接、架构/Query/动态依赖检查均退出0；恢复退出0；无设备运行 |
| macOS Monolithic 首轮 | EmbeddedEngine 在900.13秒被监督截断；其他已完成项通过，随后有意中断Full，CTest退出130；未执行该轮Modular |

## Task 2：定位与最小修复

- [x] 原始日志显示 Full 构建/发现、有效分类和未分类反例检查均已完成，恢复OFF日志尚未落盘即被900秒监督截断。
- [x] 中断后核对并终止本次 Full 的孤立后代，确认无其他构建写者，再开始独立冷目录测量。
- [x] 在固定基线、全新 APFS 夹具目录上运行同一正式 Engine-stage 脚本，override OFF、预算2、外层聚合构建串行；所有断言保持不变。
- [x] 完整冷测量退出0：real 903.98秒、user 626.88秒、sys 178.16秒；valid PASS、unclassified FAIL、restored PASS 全部符合预期。
- [x] 仅将 Darwin 的 ProjectIntegration.EmbeddedEngine 准备监督由900秒改为1200秒，约33%余量；Windows/Linux保持900，原Owner.Engine的1500秒不变。
- [x] 核对三宿主登记差异并完成独立只读评审，修复提交以Git历史为准；正式验收使用该干净快照。

预算修复 `eb85306` 中，Windows315项、Linux310项的全部命令和属性均与基线完全一致；macOS317项仅EmbeddedEngine的
TIMEOUT由900变为1200，标签、运行集合、预算、锁及其他属性不变。Source/BuildTools/cmake产品实现树无改动。
独立只读评审无Critical、Important或Minor。登记比较器首轮只有Decimal标度序列化差异，
核对原始值后保留数值类型重新比较，未放宽任何其他属性检查。

## Task 3：修复后验收

- [x] 归档首轮及预算修复轮 EmbeddedEngine 生成目录，最终代码的正式Monolithic复验使用冷夹具。
- [x] macOS Monolithic 正式 full 构建及完整 CTest，一轮非空全量通过。
- [x] macOS Modular 正式 full 构建及完整 CTest，一轮非空全量通过。
- [x] iOS ARM64 两模式根构建与 CrossCompileTests 最终链接、架构/Query/动态依赖检查；恢复默认配置。
- [x] 核对Darwin预算登记差异和未变的产品C++输入；只读SCM变更用三桌面完整版本锁矩阵补验，保留原始Full被测SHA，不声称基线Full在修复SHA重新运行。
- [x] 归档完整结果及版本、确认无运行任务依赖后清理本计划临时工作区。

## Task 2b：版本锁只读索引回归

`eb85306` 上的正式冷 EmbeddedEngine 已通过，934.83秒，证明1200秒预算覆盖原场景而没有移除断言。
同轮 IncrementalBuild 在547.03秒因 Git 锁拒绝而失败：expected/actual均为 `eb85306`，子模块完整，
但macOS当时将大量tracked文件报告为D；Windows和Mac随后无可选写锁复读均为干净，文件存在且长度/mtime未变。
Mac系统日志同一时段记录 SMB `<index>` Open 的 lease重复错误37，SMB3.1.1重连计数0。

独立小仓库诊断确认：当前校验对内容未变但stat缓存过期的文件执行status时会刷新索引；
允许optional-locks时索引SHA变化，禁用后索引SHA保持不变且校验通过。本机Git官方文档明确说明该标志
禁用的是可选索引刷新副作用，不会跳过请求的状态计算。本轮修复只保证校验自身不写共享索引，
不放宽Git锁，也不据此声称修改了macOS SMB实现。

- [x] 保留第二轮失败/中断；精确中断Root CTest并按PID/命令/进程组快照清理仅本轮后代，未修改共享挂载或安全设置。
- [x] 先新增正式EngineRevisionTests回归：强制调用方允许optional locks，触摸内容未变的文件后，校验必须接受clean且源索引SHA不变。旧实现RED退出1，诊断为索引被刷新。
- [x] 所有版本锁Git查询增加 `--no-optional-locks`，其余参数及错误判定保持不变。
- [x] Windows/Linux/macOS既有完整EngineRevision矩阵和新只读回归通过；标准与需求两轴只读评审均0项问题，修复提交以Git历史为准。
- [x] 在最终修复快照上重新执行完整macOS双模式和iOS双模式；此前两次中断不当作全量通过。

## 原始产物与诊断边界

各宿主 `<BuildRoot>/Tests/FullPlatformValidation/2026-11-14-abc9f8e/` 保存原配置、构建、CTest JSON/JUnit和日志。
macOS 的 `macos-EmbeddedEngine-cold-timing.log` 保存完整成功测量；其独立冷目录仅作诊断，不能代替修复后的正式 CTest。
首轮Full中断留下的Owner.Engine后代按精确命令、进程组及后代快照清理，不涉及其他任务；未删除其他测试或平台缓存。
Android/iOS 不运行设备程序，不宣称设备、模拟器、打包或签名验收。

## 验收摘要

两个修复均已提交，不推送或合并：

- `eb8530674f6031e8918bf4044028bac5dd00975c`：仅macOS EmbeddedEngine准备预算900→1200秒。
- `317f88276f851112837d13c799202903e4854985`：版本锁Git查询保持只读，新增索引SHA不变回归，接受/拒绝规则不变。

| 平台 / 模式 | 被测提交 | 完整结果 | CTest耗时 |
| --- | --- | --- | --- |
| Windows Monolithic | `abc9f8e` | 319/319 | 2415.10秒 |
| Windows Modular | `abc9f8e` | 315/315 | 2082.75秒 |
| WSL Linux Monolithic | `abc9f8e` | 314/314 | 1847.99秒 |
| WSL Linux Modular | `abc9f8e` | 310/310 | 1565.57秒 |
| macOS Monolithic | `317f882` | 317/317 | 6373.54秒 |
| macOS Modular | `317f882` | 313/313 | 5541.48秒 |

六份完整JUnit逐项核对用例数量一致，failure/error/skipped均为0；内层BuildTools用例不与根调度数量累加。
桌面均为根Release preset，Samples/Benchmarks/override OFF。macOS两模式均独立执行一次完整非空CTest，未拼接此前失败或中断轮次。

| 移动验证 | 被测提交 | 结果 |
| --- | --- | --- |
| Android ARM64 Modular / Monolithic | `abc9f8e` | 两模式根configure/build、真实Runtime/业务MobileHost最终链接及架构/Query/动态依赖检查全部退出0；恢复配置/构建退出0 |
| iOS Device ARM64 Modular / Monolithic | `317f882` | 同上全部退出0；恢复配置/构建退出0；真实共享源码Git锁为当前提交且override OFF |

Windows/Linux/Android的Full与链接结果保留原始提交，不重写为最新SHA的执行记录。后续没有改动Source或BuildTools的C++实现；
Darwin预算的三宿主登记对比证明其他平台完全不变，只读SCM查询改动的全部既有版本锁矩阵和新增行为回归又在三桌面各执行通过。
无需仅为提交SHA或验收文档变化重复这些未受影响的全量，实际补验范围如实保留。

最终macOS Monolithic中的增量回归711.53秒、正式冷EmbeddedEngine913.24秒全部通过；
Modular中的对应回归708.74秒、129.63秒也通过，未再出现本轮所见共享索引假删除报告。
Owner.Engine两模式分别514.89秒、489.06秒，仍使用原1500秒；业务、GTest与发现期限没有放宽。

最终缓存已核对：三桌面Modular、BUILD_TESTING/EngineTests ON；Android/iOS Monolithic、BUILD_TESTING OFF；
全部Samples/Benchmarks/override OFF。所有本任务构建/测试会话均已结束，写验收记录前tracked工作区与子模块状态干净。
没有修改C++，因此不重复执行clang-format/clang-tidy；移动仅验证编译、最终链接和宿主检查，不表示设备运行。

基线原始产物位于各宿主BuildRoot的 `Tests/FullPlatformValidation/2026-11-14-abc9f8e/`；
预算修复后的失败/中断、只读Git红绿回归与SMB诊断位于 `2026-11-15-eb85306/`；
最终macOS/iOS完整记录位于 `2026-11-15-317f882/`。Windows BuildRoot同名最终目录下的 `Reports/`
归档六份完整JUnit、移动最终链接日志和 `final-result-audit.txt`，执行上下文另归档到 `ExecutionArtifacts/`。
本轮临时上下文统一使用 `.superpowers/sdd/2026-11-14-full-platform-validation/`；全部执行资料逐文件SHA256核对后，已精确清理该目录。
