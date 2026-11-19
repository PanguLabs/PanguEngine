# 构建系统集成测试

本目录验证 Engine 的构建接线、源码插件与独立 Project 集成、生成代码接线和测试基础设施。
目录名与 CTest 名称按被测职责命名；标签、fast/precommit/full 分层、超时和资源锁保持稳定。

| 目录 / 文件 | 职责 |
| --- | --- |
| `PluginIntegration/` | 源码插件构建接线、宿主工具引导、源码和文件链接变化引发的失效重建；集中登记相关插件与 Project 回归 |
| `ProjectIntegration/` | 独立 Project 通过 FindPanguEngine / pangu_initialize 接入 Engine 的测试脚本、共享辅助函数，以及 MyGame / Engine Plugin 夹具；覆盖包发现、版本锁定、启用、协作、链接、部署和移动最终链接 |
| `TestInfrastructure/` | 测试注册、分类、依赖与基准隔离、构建工具测试准备及 workflow 完整性 |
| `ModuleLinkModes/` | Monolithic / Modular 配置、切换、跨模式回归及独立 SharedProject 接入 |
| `PanguMeta/` | Meta 注册、增量、失败恢复、BuildRoot 归属与 Project 切换 |
| `LogConfig/` | 编译配置与 Assert 选项组合、第三方宏隔离及真实分配夹具验证 |
| `EnvironmentTests.cmake` | 工具链环境变量缺失时的配置预检 |

## 路径迁移

2026-11-14 将 `Tests/CMake/` 迁移为 `Tests/BuildSystem/`，并同步三个子目录：

| 原子目录 | 现子目录 |
| --- | --- |
| `SourcePlugins/` | `PluginIntegration/` |
| `SourcePluginProject/` | `ProjectIntegration/` |
| `Testing/` | `TestInfrastructure/` |

历史设计与验收中的命令保留当时路径，今天执行时使用上表的新路径。当前构建入口、脚本与
规范中的路径已同步。该提交只迁移目录，保留了当时的 CTest 名称；后续按用户要求进一步调整
文件名及相关 CTest 名称，现行映射如下。历史设计和验收保留当时名称。

## 文件与 CTest 名称迁移

| 目录 | 原文件名 | 现文件名 |
| --- | --- | --- |
| PluginIntegration | `BuildToolInvalidationTests.cmake` | `PluginDiscoveryInvalidationTests.cmake` |
| ProjectIntegration | `RunIntegration.cmake` | `ProjectIntegrationTests.cmake` |
| TestInfrastructure | `BuildToolsTests.cmake` | `BuildToolsTestIntegrationTests.cmake` |
| TestInfrastructure | `TestingTestHelpers.cmake` | `TestInfrastructureHelpers.cmake` |
| ProjectIntegration | `StaticCatalogIntegrationTests.cmake` | `HostModuleConsumptionTests.cmake` |
| TestInfrastructure | `ScriptClassificationTests.cmake` | `TestClassificationTests.cmake` |

| 原 CTest 名称 | 现 CTest 名称 |
| --- | --- |
| `SourcePluginBuildTool` | `PluginIntegration.BuildToolConfigure` |
| `SourcePluginBuildToolInvalidation` | `ProjectIntegration.IncrementalBuild`（普通文件失效与恢复） |
| `SourcePluginBuildToolFileLinks` | `PluginIntegration.DiscoveryFileLinks` |
| `SourcePluginBuildToolDirectoryLinks` | `PluginIntegration.DiscoveryDirectoryLinks` |
| `SourcePluginBuildToolSourceChange` | `ProjectIntegration.IncrementalBuild`（工具源码失效；Windows 编译失败与恢复） |
| `SourcePluginEngineRevision` | `ProjectIntegration.EngineRevision` |
| `SourcePluginProject` | `ProjectIntegration.Base` |
| `SourcePluginEmbeddedEngine` | `ProjectIntegration.EmbeddedEngine` |
| `SourcePluginEnable` | `ProjectIntegration.PluginEnable` |
| `SourcePluginStaticCatalog` | `ProjectIntegration.HostModuleConsumption` |
| `SourcePluginSharedDeploy` | `ProjectIntegration.SharedDeploy` |
| `SourcePluginIncremental` | `ProjectIntegration.IncrementalBuild` |
| `SourcePluginTestOwnership` | `ProjectIntegration.PluginTestOwnership` |
| `SourcePluginTestOwnershipProject` | `ProjectIntegration.PluginTestOwnership.Project` |
| `SourcePluginTestOwnershipEngine` | `ProjectIntegration.PluginTestOwnership.Engine` |
| `SourcePluginTestOwnershipProvider` | `ProjectIntegration.PluginTestOwnership.Provider` |
| `SourcePluginCollaboration` | `ProjectIntegration.PluginCollaboration` |
| `TestingRegistration` | `TestInfrastructure.Registration` |
| `TestingDependencyIsolation` | `TestInfrastructure.DependencyIsolation` |
| `TestingBenchmarkIsolation` | `TestInfrastructure.BenchmarkIsolation` |
| `TestingBuildToolsTests` | `TestInfrastructure.BuildToolsTestIntegration` |
| `TestingBuildToolsFailurePropagation` | `TestInfrastructure.BuildToolsFailurePropagation` |
| `TestingScriptClassification` | `TestInfrastructure.TestClassification` |
| `TestingWorkflowCompleteness` | `TestInfrastructure.WorkflowCompleteness` |
| `ProjectIntegration.StaticCatalog`（第一轮命名） | `ProjectIntegration.HostModuleConsumption` |
| `TestInfrastructure.ScriptClassification`（第一轮命名） | `TestInfrastructure.TestClassification` |

按前缀筛选可使用 `ctest --preset windows-full -R '^(PluginIntegration|ProjectIntegration|TestInfrastructure)[.]'`；
精确筛选时用 `[.]` 表达名称中的点，例如 `-R '^TestInfrastructure[.]TestClassification$'`。
先按下文准备对应测试构建。名称派生的测试工作目录随新名变化，仍位于同一宿主 BuildRoot 的 Tests/Work 内。

## 执行与产物

`ProjectIntegration/FixtureEnginePlugins/Physics` 与 `MyGame/Plugins` 下各插件拥有各自的 `Public/`
接口、身份和 Query 声明；插件实现及自身测试不读取 `MyGame/Source`。`FixtureEnginePhysicsHeaders`、
`MyGameGameplayHeaders`、`MyGameEditorExtrasHeaders`、`MyGameSharedProbeHeaders` 只传递公开头文件及 Runtime 使用要求，
Modular 宿主读取身份和 Service 类型不会因此直接链接业务库。Query 导出声明由实际 Module 目标提供。
`Pangu::` 别名仅由 `Source/Runtime/` 定义；测试和 Project 自身使用普通目标名。模拟 Engine 的 Physics
Module 使用 `FixtureEnginePhysics`，MyGame 的 Module 使用 `MyGameGameplay`、`MyGameEditorExtras`、
`MyGameSharedProbe`，对应头文件辅助目标按同一来源命名。
`MyGame/Source` 只保留宿主、运行断言和候选装配。

Physics 夹具的 Plugin ID 为 `fixture.physics`，与实际库目标 `FixtureEnginePhysics` 分别表示插件身份和构建身份。
Engine/Project 来源由发现根决定，测试归属仍为 `owner.engine`；启用列表、访问记录和插件标签使用同一 ID。
Physics 自身测试目标为 `FixtureEnginePhysicsPluginTests`，对应 CTest 名称前缀与构建、发现及归属筛选一致。

`ProjectIntegration.PluginTestOwnership` 的 Provider 场景还使用
`PluginIntegration/Fixtures/PhysicsConsumer` 构造不含 MyGame 的独立顶层工程，构建并运行 Physics
自身测试，保护 Engine 插件的接口边界；插件测试显式部署自己的运行依赖。
插件自身测试使用独立运行目录，避免共享 DLL 部署生产者使普通宿主隐式构建无关业务库；
Provider 场景同时检查开启 Engine 测试时单独构建 MyGameProbe 不会构建 Physics。
Base、EmbeddedEngine 及手动 All 每次使用新的夹具运行目录，防止覆盖复制遗留已删除或迁移的头文件。

`ProjectIntegration.FindPackage` 遵循 ProjectIntegration 的 full 分层。临时 CMake 工程以 `LANGUAGES NONE` 验证发现、重复查找无副作用、QUIET/REQUIRED 失败语义及相对 BuildRoot 拒绝，另用 CXX-only 场景验证初始化时缺少 C 的诊断。MyGame、ModuleLinkModes、LogConfig 和 PanguMeta 夹具覆盖真实初始化、链接与宿主运行。完整 Engine 消费者在顶层 `project()` 启用 C 与 CXX，使用 `find_package(PanguEngine REQUIRED MODULE)` 后调用 `pangu_initialize()`。单模块隔离夹具保留直接消费模块的边界。

Shared 模块的独立配置入口为 `Tests/Module/Fixtures/SharedProject/`，Engine 内部目标仍定义在 `Tests/Module/Fixtures/Shared/`。`ModuleSharedProject` 验证独立配置、原生模块测试构建和运行；入口与内部目标分目录，避免同一源码目录被顶层和 Engine 子图重复加入而形成目录属性遍历环。

由 [Tests/CMakeLists.txt](../CMakeLists.txt) 接入根 CTest。使用相应宿主的根 Preset，先完成所选测试集合的构建，再执行 CTest；具体规则见 [Tests 维护规范](../README.md)。

根 CTest 注册的工作目录及本目录测试的生成夹具使用各宿主 `<BuildRoot>/Tests/Work/`。
显式移动最终链接入口保留构建脚本定义的 `<BuildRoot>/Tests/ModuleLinkModes/<模式>/` 隔离目录；
两者均不在本目录修改共享源码。
Project / Full 的 Git 锁校验保持原策略；正式验收记录固定源码版本、override 状态和实际执行范围。

Project、ModuleLinkModes、LogConfig 和 TestInfrastructure 的共享命令辅助函数还会在本轮夹具根写入 `command-times.tsv`，按 `configure`、`build`、`discovery`、`test`、`run` 记录命令壁钟耗时（微秒）、退出结果及原命令，不改变捕获输出与失败判断。配置耗时可能包含宿主工具准备，构建耗时可能包含嵌套工具构建，不能将这些阶段当作纯 CMake 或纯编译 CPU 时间。比较时保持相同源码、平台、配置及夹具准备条件。

已有嵌套夹具的 CMake Cache 若记录原源码目录，迁移后需重新生成该夹具缓存；只处理受影响的
隔离测试构建目录，不复用旧源码路径的 Cache，也不清空整个宿主 BuildRoot。
Android/iOS 的 `ProjectIntegration/CrossCompileTests.cmake` 验证真实 Runtime / 业务 Module 的最终链接，
不表示设备执行、打包或签名。
