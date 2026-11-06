# Module 性能测量

开启 `PANGU_BUILD_BENCHMARKS` 后构建 `PanguModuleBenchmarks`，独立于正确性测试。`ModuleIdentityParse` 和 `ModuleIdentityFormat` 分别测量固定合法 ID 的解析和格式化；`ScopeAccessBorrowRelease` 测量已准备好的 gate 上借用、读取和释放；`CatalogCreate/1`、`/8`、`/32` 使用预先准备的线性依赖，包含 Create 和对应 Host 析构。失败以 `SkipWithError` 标记，结果验收必须检查 JSON 中错误。dry run 仅为冒烟，不是性能基线。

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
