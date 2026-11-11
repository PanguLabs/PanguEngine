# Meta 访问基线

默认关闭 `PANGU_BUILD_META_BENCHMARKS`，依赖正式 Engine 测试夹具和既有固定版本 Google Benchmark 边界。三项分别测量本地对象直接读写、同语义生成成员访问、每次重新经过 Host/Module Gate 与 RTTI 查询的描述借用。描述场景包含 Pin 获取/释放；启动与关闭属于基准准备，不计入循环。数据是延迟基线，不代表加载或指令缓存强制清空后的延迟。

```powershell
cmake --preset windows -DPANGU_BUILD_META_BENCHMARKS=ON
cmake --build --preset windows --target PanguMetaAccessBenchmarks
Build/Windows/Bin/PanguMetaAccessBenchmarks --benchmark_min_time=0.1s --benchmark_repetitions=3
cmake --preset windows -DPANGU_BUILD_META_BENCHMARKS=OFF
```

两种 LinkMode 在同一 BuildRoot 串行配置和运行；切换时显式传 `-DPANGU_LINK_MODE=Monolithic` 或 `Modular`。Linux/macOS 使用对应正式 preset 和宿主 BuildRoot。保存编译配置、提交、模式、CPU、命令和原始输出，比较时保持输入与优化级别相同。

2026-11-11 首次基线：Windows、Clang 23.1.5（以实际 binary 核对，旧 CMake cache 曾显示 23.1.0）、Release（`-O3 -DNDEBUG`）、12 逻辑处理器 / 3700 MHz；以上命令的 3 次重复 wall-time 均值如下。对应版本为引入本文件的提交；这是短时微基准，不构成跨机器性能保证，也不能用小于噪声的差异断言生成访问更快。

| 模式 | DirectAccess | GeneratedAccess | DescriptionColdQuery |
| --- | ---: | ---: | ---: |
| Modular | 1.07 ns | 1.04 ns | 45.0 ns |
| Monolithic | 1.05 ns | 1.01 ns | 36.8 ns |
