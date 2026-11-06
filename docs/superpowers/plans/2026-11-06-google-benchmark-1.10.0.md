# Google Benchmark 1.10.0 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将 Google Benchmark 固定依赖升级到 v1.10.0，验证三桌面兼容性并建立新的 Windows 性能基线。

**Architecture:** 保持现有 ThirdParty 适配、Benchmark 独立开关、三类六场景测量及正式 BuildRoot，只更新固定依赖和必要适配。v1.9.5 结果作为历史保留，新增独立版本的验证与采样记录。

**Tech Stack:** C++23、Clang、CMake 4.0+、Google Benchmark v1.10.0。

**Spec:** [已批准的分层测试规格](../specs/2026-11-05-layered-testing-design.md)，特别是第 7、10、12、13 节；本轮用户明确同意升级到 v1.10.0、补验三桌面和重新采集 Windows 基线，覆盖旧计划中 v1.9.5 的选版。本计划记录该已授权的具体修订，不重新打开已完成的九项迁移。

**Status:** Completed（2026-11-06）；实施提交 `bd06529`，任务规格/质量及本升级整体评审通过，新增问题 C0/I0/M0。已归档必要结论并清理本计划临时目录；分支 `docs/layered-testing` 保留，未合并或推送。[验收记录](2026-11-06-google-benchmark-1.10.0-validation.md)。

## Global Constraints

- 固定 v1.10.0 的完整提交 `a6b4a9f00b36c19af9997d68a1cf6e0b5b3d032c`，实施前核对官方 tag；不使用浮动 main，不修改上游源码。
- CMake configure 不联网、不更新 submodule；获取固定依赖仅由显式 Git 操作完成，不修改全局 Git/SSH/代理配置。
- 继续 C++23、Clang、无异常、保留 RTTI 和现有 ThirdParty 目标接口；不改变三类基准的输入或计时边界来获得更好结果。
- Benchmark-only 不引入 GoogleTest/GoogleMock 或正确性 CTest；完成后各宿主显式恢复测试 ON/Engine Tests ON/Toolchain ON/Benchmark OFF。
- Windows 使用 `D:/Git/PanguEngine`、`Build/Windows`；Linux 从 `/home/moozen/WindowsGit/PanguEngine` 运行，BuildRoot 为 `$HOME/PanguEngineBuild/Linux`；macOS 从 `/Users/moozen/WindowsGit/PanguEngine` 运行，BuildRoot 为 `$HOME/PanguEngineBuild/macOS`，通过 `ssh -o BatchMode=yes moozen-macos` 调度，禁止复制源码构建。
- 同一 BuildRoot 同时只有一个验证会话；正式 Windows 性能采样时控制器及其它宿主不启动额外构建/扫描，记录宿主负载。
- Windows 六场景各至少五次正式原始样本，逐项验证名称、迭代、单位、错误状态、重复索引，记录环境、中位数和离散程度；Linux/macOS 完成编译、最终链接和 dry run，不比较跨宿主性能。
- 旧 v1.9.5 验收与原始结果完整保留，不将旧结果改写成 v1.10.0。当前版本说明与历史结果必须明确区分。
- 临时执行上下文仅放本计划 `.superpowers/sdd/2026-11-06-google-benchmark-1.10.0/`；原始结果放各宿主 BuildRoot，必要结论归档后清理本计划目录，不操作旧计划目录或其它任务。
- 提交与文档中文优先，无工具或模型署名。不增加无关功能，不修复上一轮已归档的四项 Minor，除非本升级实际依赖该修复。

## Review Focus

1. gitlink、版本 guard、目标 revision 属性和 README 一致；实际上游版本由 tag/HEAD/构建输出共同核实。
2. 新版本选项与无异常/RTTI、MSVC STL/libc++ 兼容；三桌面实际编译链接，保留原始诊断。
3. 同树开关往返不残留旧测试或依赖；三宿主运行既有 TestingBenchmarkIsolation，恢复后 fast 验证。
4. 六场景 JSON 的逐项有效性与五次 Windows 原始样本；不能以 dry run 或旧版本 JSON 代替。
5. 历史文档与当前规范不冲突；升级说明记录现行 pin，旧计划/旧基线加后续升级链接而不伪造历史。

## Task 1: 升级固定依赖并完成兼容验证和新基线

**Files:**
- Modify: `Source/ThirdParty/GoogleBenchmark/Repo`（gitlink）、`Source/ThirdParty/GoogleBenchmark/CMakeLists.txt`、`Source/ThirdParty/GoogleBenchmark/README.md`、`cmake/PanguTestDependencies.cmake`。
- Modify as needed: `Tests/Module/Benchmarks/README.md`、`wiki/foundation/third-party.md`、旧计划及验收记录（仅明确历史版本及后续链接）。
- Create: `docs/superpowers/plans/2026-11-06-google-benchmark-1.10.0-validation.md`。
- Test: 既有 `TestingBenchmarkIsolation`、`PanguModuleBenchmarks`、三宿主恢复后的 fast。

**Interfaces:** 消费 `pangu_require_googlebenchmark()`、`PanguModuleBenchmarks` 及既有正式 preset；不改变 API。输出固定 v1.10.0 的源码/产物和独立的验证、Windows 性能记录。

- [x] **Step 1: 核对基线与上游。** 确认干净工作区、各宿主无占用，检查官方 v1.10.0 的 tag/完整 SHA、许可证、CMake 选项和已消费 API。对象已存在可复用；缺失时显式获取固定 tag，避免不必要的完整 clone。
- [x] **Step 2: 更新 pin 与对应说明。** 更新 gitlink、两处 revision guard/属性及当前 README。核对新版本配置选项，必要兼容适配保持最小。机械 pin 更新复用既有回归，不新增镜像测试；若自然观察到旧 guard 拒绝新 HEAD，保留证据，不人为破坏测试。
- [x] **Step 3: 三桌面定向验收。** 三宿主在正式 BuildRoot 依次进行 benchmark-only 配置、构建、六场景 JSON dry run；运行已有 `TestingBenchmarkIsolation` 覆盖开关矩阵/同树恢复，再显式恢复三项测试开关 ON、Benchmark OFF 并运行相应 fast workflow。记录首轮、失败及重跑，不依赖旧缓存中的旧版产物。无需重复无关 full/移动端验收。
- [x] **Step 4: Windows 正式采样。** 使用正式 BuildRoot 重新构建 v1.10.0，在空闲环境运行 `--benchmark_repetitions=5 --benchmark_out_format=json`，结果放 `Build/Windows/BenchmarkResults/google-benchmark-1.10.0-20261106/`；记录完整依赖 SHA、主仓库版本/源码改动、编译器/STL/配置、CPU/负载、每项原始样本/中位数/离散程度。采样结束再恢复正确性配置及 fast，勿覆盖 v1.9.5 的目录。
- [x] **Step 5: 文档、检查和提交。** 新验收记录给出精确命令、各宿主源码与依赖 SHA、结果、环境和限制；旧验收保持历史，只追加后续链接。核对 `git diff --check`、当前版本引用及干净上游；仅修改 CMake/依赖与文档时不重跑自有 C++ 静态检查，若必须改自有 C++ 则按仓库规范执行。中文提交，自审后报告。

## 收尾

- [x] Task 1 独立规格与质量评审。
- [x] 本升级范围最终评审及必要修复/限定复评。
- [x] 归档必要裁决、状态、关键提交与验证摘要；确认无运行任务依赖后清理本计划临时目录。

官方依据：[v1.10.0 发布记录](https://github.com/google/benchmark/releases/tag/v1.10.0)，已核对完整提交与发布说明。计划自检：五项 Review Focus 均由 Task 1 对应步骤覆盖；无新公共接口或无关测试维护。
