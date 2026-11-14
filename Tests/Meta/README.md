# Meta 正式契约测试

`Tests/Meta` 验证正式生成 Module 的强类型字段访问、描述、Host Gate、Module Pin、ScopeBorrow 与真实物理卸载。Foundation 的轻量类型/Traits 测试、独立工具的上下文/属性/IR/生成/发布测试及 CMake 原生图回归分别位于 `Tests/Foundation`、`Tests/BuildTools/PanguMetaTool`、`Tests/CMake/PanguMeta`。所有测试长期保留。

桌面从根 preset 构建真实目标后运行：

```sh
cmake --build --preset windows --target PanguMetaIntegrationTests PanguMetaLifetimeFixture
ctest --preset windows -R '^(PanguMetaIntegrationTests[.](MetaIntegration|MetaUnload)[.]|MetaLifetime$)' --no-tests=error --output-on-failure
```

Linux/macOS 使用对应正式 preset 与宿主 BuildRoot；两模式须在同一 BuildRoot 串行切换。`NoServiceDescription` 不要求 Service；生成成员访问与直接 C++ 访问核对同一对象。成功描述路径走 Host 公共入口；仅未 Active 错误顺序用例通过生产 `detail::ModuleMetadataAccess` 验证，未伪造 Host 状态。描述借用只保护代码与元数据，实例仍需 ScopeBorrow。

移动正式入口为 `Tests/CMake/PanguMeta/CrossCompileTests.cmake`，必传参数为 `PANGU_ENGINE_SOURCE_DIR`、`PANGU_TEST_ROOT`、`PANGU_TARGET_PRESET`、`PANGU_LINK_MODE`、`PANGU_META_HOST_TOOL`、`PANGU_ENGINE_ALLOW_LOCAL_OVERRIDE`。仅接受 `android-arm64` / `ios-arm64` 与 `Monolithic` / `Modular`。它先使用根 preset 构建，再将实际工具链/SDK配置传入 Mobile 夹具；生成器复用已核验宿主程序。

Mobile 以真实目标 static_assert 核对 sizeof、AArch64、Android API / iOS deployment 参与求值的 Key 和 ReadOnly，检查目标对象及最终链接产物的架构、iOS 平台命令、动态依赖和最终 no-op。它不运行交叉 GTest，不声明设备运行、签名、安装或打包通过。结果与限制见[正式验收](../../docs/superpowers/plans/2026-11-09-pangumeta-tool-validation.md)。

访问性能与描述冷查询基线见 [Benchmarks](Benchmarks/README.md)，默认关闭。根调度与独立工具内层测试证据分开记录，不累加重复计数。
