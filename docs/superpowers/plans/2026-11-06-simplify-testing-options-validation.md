# 测试开关精简验收记录

2026-11-06，验证源码为实现提交 `c40e90d`（基础 `8eb57033f61559177ed2cd6b30fbca56e6c780d3`）。旧 Toolchain 开关的 OFF 不再阻止 Engine full；两个保留开关分别关闭仍被拒绝，Project 自有 full 保持独立。Toolchain 跟随 Engine 测试接入，预算默认 2、接受正整数且保留显式值。

| 平台与配置 | 首轮结果 |
| --- | --- |
| Windows / Clang 23.1.0 / Release | `windows-full` 构建成功，86/86 CTest 通过，包含 SourcePluginProject、文件/目录链接及镜像回归；先行受影响回归 5/5。旧 OFF cache 已清除，两座 Toolchain 库及链接目标构建成功。显式传入已删除的 `-DPANGU_VALIDATE_TOOLCHAIN:BOOL=OFF` 时，CMake 会给出 `unused-cli` 提示；这是旧命令行参数已不再消费的预期输出，不影响配置、旧 cache 清理或 full 验收。当前配置命令应移除该参数。 |
| Linux / Clang 23.1.0 / Release | 真实 Toolchain 两库及链接目标构建成功，受影响回归与链接测试 6/6，恢复后 fast 59/59。 |
| macOS / Clang 23.1.2 / Release | 真实 Toolchain 两库及链接目标构建成功，受影响回归与链接测试 6/6，恢复后 fast 59/59。 |
| Android / NDK Clang 21 / arm64 | 显式开启两项正确性开关，两座 Toolchain 编译探针构建成功，CTest 发现 0 项。 |
| iOS / Clang 23.1.2 / arm64 | 显式开启两项正确性开关，两座 Toolchain 编译探针构建成功，CTest 发现 0 项。 |

Windows 新契约在旧实现上的红灯是 `Engine full requires PANGU_VALIDATE_TOOLCHAIN=ON`，实现后通过。预算独立检查默认 2、显式 1/3/7，拒绝 0、负数和非整数。各正式 BuildRoot 最终均为正确性 ON/ON、Benchmark OFF、预算 2，旧 cache 项不存在。Linux 和 macOS 使用约定共享源码路径及各自本地 BuildRoot；Android 和 iOS 未执行设备部署或运行，本次未重复性能测量。原始日志保留在各宿主 BuildRoot 的 `simplify-*.log`；本次范围与历史三开关验收分开记录，历史结果见 [分层测试验收](2026-11-05-layered-testing-validation.md)。

## 评审与收尾

状态：已完成（2026-11-06）。任务规格及质量双门通过；最终整体评审范围 `e7c2663..ef75b6b`，结论 Ready to merge: Yes，Critical 0 / Important 0 / Minor 1。唯一 Minor 的旧命令行提示说明由 `007c812` 补齐；限定复评确认 ADDRESSED，修复新增 Critical 0 / Important 0 / Minor 0，所有发现关闭。旧 cache 迁移、真实 Toolchain 接入、两个剩余开关的 full 拒绝、Project 自有 full 及预算传播均有实现与正式回归支持。

补充隔离核验使用实际 helper 与实际 Engine：同进程重复 include 保留默认 2、显式 1/3，后设 0 再 include 实际拒绝；父工程普通变量 `parent-sentinel` 在接入 Engine 后保留，旧 cache 被删除。精确命令和原始输出位于 `Build/Windows/Tests/Work/SimplifyOptionsAudit/{reinclude,parent}.log`。这些 Probe 为补充人工核验，正式验收入口仍为版本化 Tests、workflow 和真实 Toolchain 目标；不依赖已清理的临时输入来运行正式测试。输入及完整报告已归档到 `Build/Windows/ValidationResults/simplify-testing-options-20261106/review/`。

验证源码统一为 `c40e90d`，后续变更仅 Markdown，未将不同代码版本的结果拼为一次 full。Windows full 首轮 86/86（310.09 秒）；Linux 定向 6/6（11.38 秒）、fast 59/59（0.49 秒）；macOS 定向 6/6（37.71 秒）、fast 59/59（2.33 秒）。移动端本轮为正式构建树的增量探针构建，未声称全部 TU 重编译；CTest 0 项与构建日志共同支撑其验证范围。首次普通沙箱拒绝和首次 fixture UNINITIALIZED/BOOL 类型诊断仅在执行会话保存，未独立归档其原文；该审计限制保留，不补造历史。实际目标 RED/GREEN 和正式平台原件均可复核。

确认 Windows、WSL、macOS 无残余验证进程且所有子代理结束后，仅清理 `.superpowers/sdd/2026-11-06-simplify-testing-options/`，保留正式 Tests、其它计划、各 BuildRoot、历史结果和本轮证据。当前实现与已验收版本一致，未修改或重新采集性能基线；未合并或推送。

## 执行裁决
- Ruling: 沿用用户已授权的专用分支、共享源码与各平台固定BuildRoot，不新建/复制工作树 — 本次为同分支后续精简，原隔离偏好持续有效 — 判断错误需按本次提交回退，串行实施和独立review约束范围。
- Ruling: 原规格full三开关条款按用户本轮明确同意修订为两开关，直接执行已说明方案并同步规范 — 需求/方案已明确，无需再次等待文档审批 — 代价是旧接口与负例要同步迁移，不能把旧验收事实重写。
- Ruling: 将ProjectTestHelpers的第三处同预算默认/校验一并收敛到按需include的PanguTestBuildJobs helper — 代码核对发现它与前两处维护同一参数，符合用户收敛重复的意图 — 代价是增加一个最小共享文件，并须验证脚本模式/缺输入重试/镜像包含链。
- Ruling: 保留首次两次诊断仅会话保存的审计边界，不补造历史原文；旧九项/Benchmark、未改C++债务、移动端部署/设备运行和新性能测量不在本精简范围 — 现有目标RED/GREEN、正式五平台与补充行为Probe已独立核验，最终review未发现影响这些范围的变化 — 判断错误的代价是将来追溯丢失诊断或扩展验证范围时需补新的专项证据，不能外推本次结论。
