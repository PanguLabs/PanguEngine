# Google Benchmark v1.10.0 定向验收（2026-11-06）

三桌面完成 benchmark-only 配置、实际编译及最终链接、六场景 JSON dry run，`TestingBenchmarkIsolation` 均通过；显式恢复 `BUILD_TESTING=ON`、`PANGU_BUILD_ENGINE_TESTS=ON`、`PANGU_VALIDATE_TOOLCHAIN=ON`、`PANGU_BUILD_BENCHMARKS=OFF` 后 fast 各 59/59。Windows 另完成五次正式采样。此轮不重复 full、移动端或跨宿主性能比较。此前 [v1.9.5 验收](2026-11-05-layered-testing-validation.md) 与结果保留为历史。

三宿主源码 HEAD 均为 `9ea0f2b79077bb1d722ee9a8e68cb997be2def83`，工作树为本升级的 gitlink、两个 revision 和 README 修改；依赖均为 `a6b4a9f00b36c19af9997d68a1cf6e0b5b3d032c`。上游 `git ls-remote https://github.com/google/benchmark.git refs/tags/v1.10.0 'refs/tags/v1.10.0^{}'` 返回 tag 对象 `f2bc56f35f4f2c9921496d19e25f558e69d1eff7`、解引用提交即上述 SHA；Apache-2.0，源码无本地修改。Windows/Linux Clang 23.1.0，macOS Clang 23.1.2；Release、C++23，Windows MSVC STL。Windows 上游报告禁用异常时 `std::regex` 不完全支持，六场景实测未报告错误。

三宿主分别在仓库规范路径，以 `<host>` 为 `windows`、`linux`、`macos`，`<root>` 为各自正式 BuildRoot，执行：

```sh
cmake --preset <host> -DBUILD_TESTING=OFF -DPANGU_BUILD_ENGINE_TESTS=OFF -DPANGU_VALIDATE_TOOLCHAIN=OFF -DPANGU_BUILD_BENCHMARKS=ON
cmake --build --preset <host> --target PanguModuleBenchmarks
<root>/Bin/PanguModuleBenchmarks[.exe] --benchmark_dry_run --benchmark_out=<root>/BenchmarkResults/google-benchmark-1.10.0-20261106/dry.json --benchmark_out_format=json
cmake --preset <host> -DBUILD_TESTING=ON -DPANGU_BUILD_ENGINE_TESTS=ON -DPANGU_VALIDATE_TOOLCHAIN=ON -DPANGU_BUILD_BENCHMARKS=OFF
ctest --test-dir <root> -R '^TestingBenchmarkIsolation$' --output-on-failure
cmake --preset <host> -DBUILD_TESTING=ON -DPANGU_BUILD_ENGINE_TESTS=ON -DPANGU_VALIDATE_TOOLCHAIN=ON -DPANGU_BUILD_BENCHMARKS=OFF
cmake --workflow --preset <host>-fast
```

Windows 正式采样前重新执行 benchmark-only 配置和构建，然后运行：

```powershell
./Build/Windows/Bin/PanguModuleBenchmarks.exe --benchmark_repetitions=5 --benchmark_out=Build/Windows/BenchmarkResults/google-benchmark-1.10.0-20261106/results.json --benchmark_out_format=json
```

完成后再次显式恢复上述正确性配置并运行 `cmake --workflow --preset windows-fast`。主机 Ryzen 5 7500F，6 核 12 线程，采样前 CPU 负载快照 15%；空闲窗口内其他宿主和控制器未运行构建或扫描，快照无法保证整段采样无干扰。每行 CPU 时间，单位 ns，五个原始样本按 repetition index 0–4 排列；中位数和标准差由框架提供：

| 场景 | 原始样本 | 中位数 | 标准差 |
| --- | --- | ---: | ---: |
| ModuleIdentityParse | 25.670, 25.112, 24.554, 23.438, 25.670 | 25.112 | 0.934 |
| ModuleIdentityFormat | 3.128, 3.209, 3.209, 3.128, 3.209 | 3.209 | 0.044 |
| ScopeAccessBorrowRelease | 21.484, 21.973, 21.973, 21.973, 21.484 | 21.973 | 0.267 |
| CatalogCreate/1 | 481.306, 481.306, 481.306, 470.843, 491.769 | 481.306 | 7.399 |
| CatalogCreate/8 | 3076.177, 3076.177, 3222.661, 3076.177, 3881.842 | 3076.177 | 349.727 |
| CatalogCreate/32 | 25495.200, 26088.111, 26088.111, 25495.200, 24902.288 | 25495.200 | 496.065 |

每项名称、正迭代数、ns、错误字段及重复索引均核对；`CatalogCreate/8` CPU 变异系数约 10.71%，用于后续参考时应考虑离散程度。完整精度、环境和日志位于 `Build/Windows/BenchmarkResults/google-benchmark-1.10.0-20261106/`；Linux、macOS 原始 dry JSON 分别在各宿主 `$HOME/PanguEngineBuild/{Linux,macOS}/BenchmarkResults/google-benchmark-1.10.0-20261106/dry.json`，Windows 目录同时保存副本和逐项检查结果。旧结果未覆盖。

## 评审与收尾

实施提交 `bd06529d662ae9d8c8d1243cad5fd022b1ec7d69`。任务规格/质量评审通过，随后对 `9d18280..bd06529` 的本升级全部变更完成最终评审，结论 Ready to merge: Yes，本次新增 Critical 0 / Important 0 / Minor 0。任务评审提及的无异常 regex 提示经最终评审确认为已披露的既有输入限制，未因本升级改变；不推断任意过滤表达式安全。15% 采样前负载快照与 CatalogCreate/8 的离散程度限制细小性能差异的判断，不设置性能百分比门禁，也不为获得更好数值反复重采。

最终评审独立核对官方 tag、HEAD、gitlink、两个 revision、三宿主真实重编译/最终链接、隔离与恢复日志、六场景五次样本及统计重算；三宿主当前正确性缓存均为 TRUE/ON/ON/OFF。原件与 Windows 副本的字节哈希差异来自 `Set-Content` 将 LF 转为 CRLF；无 BOM，换行归一后内容逐字节一致。字节原件、哈希与核验细节连同任务报告、任务评审、最终评审及账本归档在 `Build/Windows/BenchmarkResults/google-benchmark-1.10.0-20261106/review/`。原件、旧副本及 v1.9.5 历史结果未改写；没有升级前全目录哈希，不额外声明旧目录逐字节历史取证。

确认三宿主无残余构建/测试进程、全部子代理结束后，仅删除本计划 `.superpowers/sdd/2026-11-06-google-benchmark-1.10.0/`。正式测试、其它计划、各宿主 BuildRoot 和原始结果保留。此后的收尾提交仅更新计划元数据与本节归档，不改变已验证实现；未合并或推送。

## 执行裁决
- Ruling: 沿用现有专用分支、共享源码和固定BuildRoot，不再复制或创建工作树 — 本次是用户同意的同分支依赖修订且原有隔离裁决继续适用 — 判断错误需按本升级提交回退，串行实施避免冲突。
- Ruling: 基于已批准规格与用户刚同意的具体升级/验证方案直接执行，文档化为一个任务 — 版本/范围/验收已明确且未改变公共API，无需重复设计或方案确认 — 若有隐藏兼容变化，先依据实际失败做最小修复并记录，不扩大范围。
- Ruling: 保留已披露的无异常regex输入边界、负载与离散程度限制，不宣称任意正则安全或性能百分比结论；不重审旧九项/移动端，不声称没有升级前哈希的旧结果逐字节取证 — 最终评审确认当前支持用法、样本和版本真实有效，这些额外范围无新支持承诺或证据 — 判断错误的代价是未来扩大过滤输入、判定细小性能差异或审计历史字节时须补专项验证/遥测，不能外推本次验收。
