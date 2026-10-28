# Windows 宿主并行 ABI 输出校验修复计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.
> 状态：Completed。Android 并行采集遗留已关闭，最终验收见文末；沿用 `feature/module-static-catalog`，基线 `4392b2e` 加此前 STATIC 切片。不提交、推送或改写既有 index。

**Goal:** Windows 宿主的正式并行编译等待受管输出删除结束，保留全部路径校验、有限等待和原始错误，不因瞬时 DeletePending 失败。

**Architecture:** 保留现有 C++ Collection 事务和锁，不把安全检查改为接受无法访问的路径，不在 CMake 串行化编译。先通过真实 Windows DeletePending 文件句柄复现与 Android 首轮一致的 `output_symlink` / native 5，再按证据修复等待范围。

**Tech Stack:** C++23、Clang、Win32 文件句柄、正式 Presets/CTest。

**Spec:** [STATIC 切片遗留](2026-10-28-module-static-catalog.md#遗留与边界)、[ABI Collection Spec](../specs/2026-10-23-abi-collection.md)、`wiki/foundation/abi-profile-fingerprint.md`；不改变 Schema、Fingerprint、Loader 或安全边界。

## Global Constraints

- 仅 Windows 宿主的已有 native ACCESS_DENIED 重试；不能将 Linux/macOS 文件系统行为等同于 Windows。
- 只对受管、明确枚举的生成输出和锁路径重试；输入路径、未知路径及其他错误仍失败。
- 每次完整重新校验，成功后才取得/使用事务；不跳过 symlink/hardlink/输入别名校验。
- 沿用现有 30 秒截止时间；永久拒绝必须保留真实 Error 和路径，不能生成收据或完成记录。
- 所有探针和日志写本计划 `.superpowers/sdd/`，正式回归写 `Tests/`；不改变前一切片源码/index。

## Review Focus

1. 错误来自输出的父目录或输入文件时，不能因 basename 相似被当成受管输出。
2. 已满调用报告不能使重试丢失原始错误或污染成功报告。
3. 重试后路径变为链接或输入别名时必须重新拒绝。
4. 非等待 Guard 仍立即失败；永久 denied 的等待须有限且不发布结果。
5. 正式 Launcher 的请求读取、失效与完成收据三个阶段均应在实际并行构建中验证。

### Task 1: 确定性复现

**Files:** `Tests/AbiProfile/CollectionStateTests.cpp`，复用已有暂停编译器和 native DeletePending 夹具。

**Interfaces:** 真实 `RunObservedCompilerInvocation`，`GuardCollection`；不新增生产测试钩子。

- [x] 将锁目录回归扩展为明确路径参数，增加 `canonical.json` 的瞬时/永久拒绝用例。
- [x] 构建并运行 `AbiCollectionState`，确认新增用例在同一待删输出的 native 5 失败（确定性持有句柄时提前在 `resolve_request_path` 被拒），旧锁用例保持原行为。

### Task 2: 最小修复与复验

**Files:** `BuildTools/AbiProfile/Private/CollectionState.cpp`；若正式 Launcher 揭示其他阶段的同类缺口，先补对应回归再限定修改。

**Interfaces:** 保留 `Transaction::Begin(bool wait)`、`CheckOutputs` 和 `CollectionOutputFiles`。

独立评审确认正式 Launcher 在事务前还有请求读取、SCC 成员解析及 Observation 输入路径检查三个入口。增加工具私有 `CollectionOutputWait`（默认不等待、显式绝对 Deadline）；只读校验共用精确路径/native 5 分类与独立 attempt 报告，整段重验。SCC 解析与紧接着的事务取锁共享同一截止点；实际子编译器运行前后重新建立等待阶段，不把编译时长算入文件删除等待预算。Guard 调用链保留默认不等待。对应 Windows 原生文件回归位于 `Tests/AbiProfile/CollectionOutputWaitTests.cpp`。

- [x] 按真实复现结果限定重试路径，保留完整验证及截止时间。
- [x] 运行上述定向回归至 GREEN；检查非等待、永久拒绝及报告预算。独立评审指出的三个锁外入口追加 RED/GREEN 回归通过。
- [x] 用正式 Android Preset 重新编译多个 TU，启用并行度而非串行回避；保留每轮结果。

### Task 3: 验收与评审

**Files:** 本计划及前一 STATIC 切片遗留记录；必要测试说明。

- [x] 执行受影响 C++ 的 clang-format、实际数据库 clang-tidy、Windows 完整 CTest；Linux/macOS 定向回归。
- [x] 独立只读评审并处理有效问题；记录 Android/桌面实测边界，不将单次增量通过作为并行重编译证明。
- [x] 归档根因、RED/GREEN、命令、遗留及所有未验证范围。

## 根因与修复

`RunCompilerInvocation` 的失效阶段会在锁内删除旧 Profile/Canonical 等生成文件；另一个 Launcher 的请求、输入或事务前置校验可能恰好读取仍处于 Windows DeletePending 的文件，得到 system error 5。原等待只覆盖 `.collection-lock`，生成文件及事务之前的只读校验会立即失败。

修复保留完整别名、symlink/hardlink、边界与真实身份校验，仅对枚举生成文件/锁的精确路径及 Windows system/5 重试。`CollectionOutputWait` 不进入 Request JSON 或 ABI Profile；只读校验默认不等待，正式编译明确启用。SCC 解析和取锁共用阶段截止时间，实际编译前后分别建立等待阶段。临时错误隔离在 attempt 报告中；成功不截断调用方已有报告，截止后返回真实 native Error。

## 验收（2026-10-28）

验收源码为 `4392b2e` 加前一 STATIC 切片及本修复，未提交；代码冻结后执行最终矩阵，之后只更新文档。

| 检查 | 实际结果 |
| --- | --- |
| Windows x64 Release | 正式 `windows` Preset 全量构建；完整 `ctest --preset windows --parallel 2 --output-on-failure` **41/41 PASS**，446.67 秒，无失败重跑 |
| Windows 确定性回归 | `AbiCollectionState` 的真实暂停编译器 + DeletePending 锁/Canonical 文件；`AbiCollectionOutputWait` 的请求读取、输入校验和 SCC 解析全部通过。覆盖瞬时释放、永久拒绝、默认 no-wait、满报告、共享 200ms deadline 和输入错误不重试 |
| Android ARM64 | 最终工具版本 `70ac9999eb176301a368ca4d92a1d99989118eb373a1a153dd50f185a92dc01f`，正式 `android-arm64` Preset，已有 Profile/Canonical/收据下两轮 **`--parallel 8` 重编译通过**；每轮日志核实 3 个 Module TU 均实际编译，随后归档及宿主 ABI Collection 成功 |
| WSL Ubuntu Linux x64 | 正式 `linux` Preset 全量构建；`ctest --preset linux -R AbiCollectionState --output-on-failure` **1/1 PASS**，261.45 秒；非完整 Linux CTest |
| macOS x64 | 固定 PATH，正式 `macos` Preset 全量构建；`ctest --preset macos -R AbiCollectionState --output-on-failure` **1/1 PASS**，4.86 秒；非完整 macOS CTest |
| 质量检查 | 9 个受影响 C++ 文件的 clang-format；7 个实际 Windows 编译数据库 TU 的 clang-tidy；新测试与 Main 修正后无新增用户代码诊断。`DependencyGraph.cpp` 三条既有 trailing-comma 提示及 MSVC STL 两条既有 EnumCastOutOfRange 未扩大处理；差异与本地文档链接检查通过 |

### RED/GREEN 与评审

- 扩展原真实编译器回归前，Canonical 暂态使状态测试 **RED**（101.33 秒，`resolve_request_path` / native 5）；事务等待范围修复后 **GREEN**（126.84 秒）。持有句柄使错误稳定发生在更早解析阶段，和首轮 `output_symlink` 都指向同一待删 Canonical 文件。
- 独立只读评审发现 **1 Important，无 Critical**：正式请求读取、SCC 重读及 Observation 输入校验在事务之前，窄修复未覆盖。增加明确等待策略但尚未实现重试时，三个入口的 9 项断言均失败（3.10 秒）；完整只读重验实现后通过（3.75 秒），最终完整 CTest 又通过（3.72 秒）。该修复通过 TDD 验证，不将其写成又一次独立复审批准。
- 初次强制重编译准备删除对象/归档，却留有旧收据，Guard 正确以 `compile_proof_changed` 拒绝；这次准备失败不作为并发修复失败或通过。清理对应失效收据后冷并行构建通过；最终两轮仅触碰 Android 目标生成头 `inputs.h` 的时间戳，不改内容/源码、不删除完成记录，验证正常 Ninja 重编译流程。
- 窄修复时完整 Windows **40/40** 与 Linux/macOS 早期定向结果仅为阶段记录，未拼入最终矩阵。

### 限制与保留

- Windows 既有文件/目录符号链接负测受当前账户权限限制，测试明确输出 unavailable / SKIP；硬链接/输入别名负测及新增原生 DeletePending 回归执行通过，未扩大账户权限。
- 没有执行 Android/iOS 设备程序；本轮未重建 iOS，未重跑 Linux/macOS 完整 CTest。Schema、Canonical/Fingerprint、最终 EXE/SHARED Loader 信任范围没有变化。
- 无法仅从 native 5 区分 DeletePending 和永久输出 ACL 拒绝；采用有限等待后保留错误。代价是受管输出的永久拒绝可能消耗等待预算，输入错误仍立即失败。
- 原工作区和既有 index 保持，未提交/推送。临时日志和 ledger 保留在本计划 `.superpowers/sdd/` 下；正式回归不依赖该目录。
