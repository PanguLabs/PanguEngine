# ECS 测试与基准

## 桌面运行

- `PanguEcsWorldTests.*` 覆盖原生类型身份、显式组件绑定、推进、World 隔离和借用释放。
- `FlecsMissingComponentRegistration` 以隔离进程执行登记成功和遗漏登记对照，检查上游诊断及失败退出。Release 和 Debug 均可运行。
- `ProjectIntegration.EcsWorld` 从独立 Project 构建并运行 `MyGameEcsHost`，覆盖 Gameplay/Consumer 的组件和枚举绑定、推进、多宿主、单关与回滚。Modular 检查实际 flecs 导入、加载路径与 Gameplay 原生卸载顺序，Monolithic 检查最终链接的 Query 与 flecs。
- 父 Scope、宿主整体关闭与外部 Pin 的回归在 `Tests/EngineHost` 和 `Tests/Module`。

在 Windows 使用以下命令，Ubuntu 真机和 macOS 分别换为 `linux` 和 `macos`。正式 Project 回归使用干净 Git 提交且关闭本地锁覆盖。分别按 `Monolithic` 和 `Modular` 配置。

```powershell
cmake --preset windows -DCMAKE_BUILD_TYPE=Release -DPANGU_LINK_MODE=Modular -DBUILD_TESTING=ON -DPANGU_BUILD_ENGINE_TESTS=ON -DPANGU_ENGINE_ALLOW_LOCAL_OVERRIDE=OFF
cmake --build --preset windows-full
ctest --preset windows-full -R '^(PanguEcsWorldTests\.|FlecsMissingComponentRegistration$|ProjectIntegration\.(EcsWorld|FindPackage|HostModuleConsumption)$)' --no-tests=error --output-on-failure
ctest --preset windows-full -L '^area\.(engine-host|module)$' --no-tests=error --output-on-failure
```

## 移动最终链接

在 Windows 运行 `./Scripts/build.ps1 mobile-link -Platform android -LinkMode Monolithic` 和 `-LinkMode Modular`。在 macOS 的正式挂载路径运行 `bash Scripts/build.sh mobile-link --platform ios --link-mode Monolithic` 和 `--link-mode Modular`。Android 实际链接导出入口及 caller，iOS 链接同源可执行程序。检查由宿主执行，移动设备运行、打包和签名单独验收。[工具链测试](../Toolchain/README.md)

## Release 断言对照基准

Windows 示例使用根 Monolithic Release 配置。两份程序使用同一源码并各静态链接一份 flecs。On 使用生产目标，Off 仅去掉 `FLECS_KEEP_ASSERT`。每个样本建立 10,000 个已登记组件实体，预热 128 帧，计时 8,192 帧。每帧进行一次 `progress(0.016)`、一个更新组件的 System 和一次遍历实体的 typed query。

```powershell
cmake --preset windows -DCMAKE_BUILD_TYPE=Release -DPANGU_LINK_MODE=Monolithic -DPANGU_BUILD_BENCHMARKS=ON
cmake --build --preset windows --target PanguFlecsAssertBenchOn PanguFlecsAssertBenchOff
./Build/Windows/Bin/PanguFlecsAssertBenchOn.exe --benchmark_repetitions=5 --benchmark_report_aggregates_only=true
./Build/Windows/Bin/PanguFlecsAssertBenchOff.exe --benchmark_repetitions=5 --benchmark_report_aggregates_only=true
cmake --preset windows -DCMAKE_BUILD_TYPE=Release -DPANGU_LINK_MODE=Modular -DPANGU_BUILD_BENCHMARKS=OFF
```

断言策略与实测数据分别见 [ECS 规格](../../docs/superpowers/specs/2026-11-17-ecs-world-design.md#7-执行线程和错误边界)及[最终验收](../../docs/superpowers/plans/2026-11-18-ecs-world.md#ecs-交付验收)。
