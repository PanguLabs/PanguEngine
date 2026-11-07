# Module 性能测量

开启 `PANGU_BUILD_BENCHMARKS` 后构建 `PanguModuleBenchmarks`，独立于正确性测试。`ModuleIdentityParse` 和 `ModuleIdentityFormat` 分别测量固定合法 ID 的解析和格式化；`ScopeAccessBorrowRelease` 测量已准备好的 gate 上借用、读取和释放；`CatalogCreate/1`、`/8`、`/32` 使用预先准备的线性依赖，业务输入仍为 1/8/32 项，另包含两个必要基础常驻根，计时覆盖 ModuleManager::Create 和对应 Manager 析构。失败以 `SkipWithError` 标记，结果验收必须检查 JSON 中错误。dry run 仅为冒烟，不是性能基线。

以下命令从指定共享源码目录运行，`run` 是本次结果目录名，应按需要替换。正式采样记录环境、版本、配置、原始 JSON 和统计信息，并检查每个场景的错误字段。五次重复采样不会自动建立跨宿主可比的性能基线；本轮 Linux/macOS 仅完成 dry run。测试筛选与维护见 [Tests 规范](../../README.md)。

Windows PowerShell（BuildRoot 为 `D:/Git/PanguEngine/Build/Windows`）：

```powershell
Set-Location D:/Git/PanguEngine
cmake --preset windows -DPANGU_BUILD_BENCHMARKS=ON
cmake --build --preset windows --target PanguModuleBenchmarks
New-Item -ItemType Directory -Force Build/Windows/BenchmarkResults/run
& ./Build/Windows/Bin/PanguModuleBenchmarks.exe --benchmark_repetitions=5 --benchmark_out=Build/Windows/BenchmarkResults/run/results.json --benchmark_out_format=json
```

WSL/Linux shell（BuildRoot 为 `$HOME/PanguEngineBuild/Linux`）：

```sh
cd /home/moozen/WindowsGit/PanguEngine
cmake --preset linux -DPANGU_BUILD_BENCHMARKS=ON
cmake --build --preset linux --target PanguModuleBenchmarks
mkdir -p "$HOME/PanguEngineBuild/Linux/BenchmarkResults/run"
"$HOME/PanguEngineBuild/Linux/Bin/PanguModuleBenchmarks" --benchmark_repetitions=5 --benchmark_out="$HOME/PanguEngineBuild/Linux/BenchmarkResults/run/results.json" --benchmark_out_format=json
```

macOS shell（BuildRoot 为 `$HOME/PanguEngineBuild/macOS`）：

```sh
cd /Users/moozen/WindowsGit/PanguEngine
cmake --preset macos -DPANGU_BUILD_BENCHMARKS=ON
cmake --build --preset macos --target PanguModuleBenchmarks
mkdir -p "$HOME/PanguEngineBuild/macOS/BenchmarkResults/run"
"$HOME/PanguEngineBuild/macOS/Bin/PanguModuleBenchmarks" --benchmark_repetitions=5 --benchmark_out="$HOME/PanguEngineBuild/macOS/BenchmarkResults/run/results.json" --benchmark_out_format=json
```

仅冒烟时，将运行命令中的 `--benchmark_repetitions=5` 替换为 `--benchmark_dry_run`，使用独立 JSON 文件；不得将其当作正式测量。完成后在同一源码目录执行对应平台的 `cmake --preset <windows|linux|macos> -DBUILD_TESTING=ON -DPANGU_BUILD_ENGINE_TESTS=ON -DPANGU_BUILD_BENCHMARKS=OFF`，再构建所需测试集合，避免遗留 benchmark cache 状态。

当前 v1.10.0 的三桌面验收和 Windows 基线见 [验收记录](../../../docs/superpowers/plans/2026-11-06-google-benchmark-1.10.0-validation.md)；旧 v1.9.5 基线保留为历史。

## 统一模式后的比较边界

两模式都显式构建并 dry run 全部六场景。CatalogCreate 的依赖业务输入和操作目的不变，但 Manager 接纳 Foundation/Module 两个持久基础根；深复制、依赖检查及同步 Shutdown/析构成本仍在原区间，不用 PauseTiming 移走。Modular 的这些已链接 benchmark Query 是显式常驻集合，不测试动态 Load 吞吐。旧 StaticModuleHost 样本只有业务 Catalog，不能直接混合比较。新[验收记录](../../../docs/superpowers/plans/2026-11-06-module-manager-link-modes-validation.md)分别保存模式/源码/JSON，dry run 不建立新性能基线。

模式循环使用 `cmake --preset <platform> -DPANGU_LINK_MODE=<Monolithic|Modular> -DPANGU_BUILD_BENCHMARKS=ON`，构建并以 `--benchmark_dry_run --benchmark_out=<独立结果文件> --benchmark_out_format=json` 运行。结束显式恢复桌面 Modular、BUILD_TESTING=ON、PANGU_BUILD_ENGINE_TESTS=ON、PANGU_BUILD_BENCHMARKS=OFF。
