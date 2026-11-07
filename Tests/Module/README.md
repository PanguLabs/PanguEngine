# Module 正式测试

对应契约为 [Module API v1](../../wiki/core/module-api.md)，维护遵循 [Tests 规范](../README.md)。这些测试覆盖尚未冻结的 C++ API 实现，不代表 Plugin Public ABI 或完整生命周期已验收。

独立 `PanguModuleBenchmarks` 覆盖身份解析与格式化、Scope 借用及 Catalog 创建；其测量边界和启用方式见[测试分类与运行契约](../../wiki/foundation/testing.md)，不作为正确性 CTest。

具体配置、三桌面采样与恢复命令见 [Benchmark 说明](Benchmarks/README.md)。

| CTest | 验证对象与失败判据 |
| --- | --- |
| `PanguModuleUnitTests.ModuleIdentity.*` | 固定身份文本/字节往返、非法身份拒绝与精确三段版本 |
| `PanguModuleIntegrationTests.ModuleManager.*` | 描述数据深复制、入口/身份/限制校验、Required/Optional 显式绑定、依赖图环与依赖优先顺序 |
| `PanguModuleIntegrationTests.Registration.*` | 一次注册、声明暂存与原子发布、失败无半发布、粘性错误、报告进度、具体 Scope 隔离和重入拒绝 |
| `PanguStaticCatalogTests.StaticCatalog.RetainsArchiveRootsAndRunsRealFactories` | 两个真实受管 STATIC 夹具、最终 EXE 的生成根引用、链接裁剪后的入口可达与实际注册 |
| `PanguModuleUnitTests.ScopeAccess.*`、`PanguModuleConcurrencyTests.ScopeAccessConcurrency.*` | 原子闸门获取/关闭、借用释放、仅移动工厂与稳定控制块；并发 acquisition/close 的 200 轮回归 |
| `PanguModuleUnitTests.ScopeBuild.*`、`PanguScopeBuildMockTests.ScopeBuildMock.*` | 实例先追踪后 Start、匹配销毁、类型核验与粘性错误；现有 Sink seam 的拒绝顺序 |
| `PanguModuleIntegrationTests.ScopeLifecycle.*` | 多 Scope 局部失败、激活/卸载/Shutdown、Busy 排空、直接/传递 Module 与显式资源依赖顺序、错误/进度、句柄代际及重入；失败注册事务的工厂析构共用报告时仍保留外层原错 |
| `ScopeDestructor` | 子进程确认错误析构 Busy Host 始终终止，未提前 Destroy 被借用对象 |
| `PanguNativeModuleLibraryTests.NativeModuleLibrary.ExplicitHoldsReleaseInBothDependencyLoadOrders` | Provider/Consumer 两个显式装载顺序；自身 Release 成功使 wrapper 不可查询、重复释放安全，最终双方持有释放后真实卸载；不要求 Consumer 先隐式装载时跨 OS 立即物理移除 |

桌面宿主使用根 Preset 的正式 BuildRoot。例如 Windows：

```sh
cmake --preset windows
cmake --build --preset windows
ctest --preset windows -L '^area\.module$' --no-tests=error --output-on-failure
```

Linux/macOS 替换为各自的 `linux`/`macos` Preset，源码仍读取约定共享挂载。夹具与生成 Catalog 放在宿主 BuildRoot；普通 Module 测试不生成 ABI 收据，也不修改共享测试源码。

普通单元、Mock 和真实集成从 `run.fast` 起执行；并发、Catalog 与析构检查从 `run.precommit` 起执行，均累计进入 `run.full`。
真实集成与 Catalog 使用 `boundary.integration;area.module;owner.engine`，逐个 GTest 用例独立登记，默认超时 60 秒。
析构父检查另带 `requires.subprocess`，其专用 `PanguScopeDestructorFixture <marker>` 子程序仍超时 15 秒。
普通集成程序直接批量运行时可使用 GoogleTest 的筛选和 shuffle 参数；它不再解析旧 `--destroy-busy` 分支。
`Fixtures/ScopeLifecycleFixture.h` 只共享生命周期测试和终止夹具所需的局部状态、对象与回调，不暴露生产接口。

`Fixtures/First.cpp` 和 `Second.cpp` 各对应一个真实 Module STATIC Target，由 CMake + Ninja 原生编译/归档。最终 EXE 开启平台链接裁剪，只经生成 Catalog 直接引用 Query；不用全局业务构造或 whole-archive。Catalog 输入故意为依赖方在前，注册顺序必须为 Provider 在前。

当前已新增拥有型工厂与实际 Scope 资源，使用资源生命周期适配而非统一业务 Service 基类。两个真实 STATIC 夹具分别提供 Host 范围非平凡 vector 对象和 Context 消费者，装配期执行跨归档 `Dependency<T>` 核验。标准拥有型容器 OOM 使用无异常终止政策；管理调用需外部串行化，已导出访问令牌支持并发关闭。声明注册域须比 Host 活得更久，回调不得移动/销毁 Host 或保留借用 Context。Shutdown 撤销本 Host 声明；撤销同样使原 Declarations 视图失效。

Host 退出前显式推进 Shutdown；若仍有工作或 Borrow，保持 Host 存活并重试。析构只允许安全完成，否则使用始终生效的终止路径。子进程测试安装确定的 terminate handler，以 `.ready` 标记存在、退出码精确为 91、无 Destroy 标记联合验证，不能将任意崩溃都视为通过。父脚本显式传入 BuildRoot 内的标记路径。

Android/iOS 正式 Preset 默认不构建这些可运行测试，交叉编译验证 Runtime 库的目标编译/归档，不运行宿主 ABI 采集。STATIC 夹具的链接与注册不证明外部预编译 C++ 库的兼容性；完整业务 Service/TaskScheduler/GPU 排空与设备运行不在本轮范围；桌面真实 Pin/动态卸载见下方正式覆盖。

## ModuleManager 双模式覆盖（2026-11-06）

现行测试按模式明确分层：旧同二进制 Query 的元数据、注册、Scope、真实归档与终止回归保留在 Monolithic；Modular 的 `ModuleRegression` 从 precommit/full 自动配置独立 Monolithic 子图，构建并执行 `area.module` 的 precommit 集合。`PANGU_TEST_MODULE_MATRIX_CHILD` 只禁止再次登记 `ModuleRegression`，不关闭其它有效测试。`Fixtures/ManagerFixture.h` 是测试装配函数，依次创建常驻根和 Load 业务批次；生产接口只保留 `ModuleManager`。

新增 `ModuleManager` 场景验证域隔离、代际和常驻占用预算；`ResidentModule` 验证显式启动、前置拒绝、执行失败请求整体关闭、Busy 时原错与句柄保留，以及最终统一失效；`BatchLoad` 验证空批次与模式来源限制，Modular 还执行真实 DLL 的依赖顺序、失败无部分句柄及快照跨卸载。

Modular 同时执行 NativeModule、ModuleCodePin、ModuleUnload/Concurrency、SharedScopeLifecycle 和 ModuleLifetime：覆盖真实导入与额外持有、失败批次/元数据环清理、原生关闭失败/重入、并发 Pin、已有 Scope 装配、跨 DLL RTTI/STL/私有类型及物理卸载后残留凭据。Task 4–7 阶段结果与最终平台矩阵分开，见[正式验收](../../docs/superpowers/plans/2026-11-06-module-manager-link-modes-validation.md)。所有迁移及废弃契约见 [测试迁移清单](../MIGRATION.md)。可用 `ctest --preset windows-precommit -L '^area\.module$' --no-tests=error` 运行该阶段的全部 Module 覆盖（先构建同名 build preset）。
