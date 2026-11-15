# 构建系统集成测试

本目录验证 Engine 的构建接线、源码插件与独立 Project 集成、生成代码接线和测试基础设施。
目录名与 CTest 名称按被测职责命名；标签、fast/precommit/full 分层、超时和资源锁保持稳定。

| 目录 / 文件 | 职责 |
| --- | --- |
| `PluginIntegration/` | 源码插件构建接线、宿主工具引导、源码和文件链接变化引发的失效重建；集中登记相关插件与 Project 回归 |
| `ProjectIntegration/` | 独立 Project 接入 Engine 的测试脚本、共享辅助函数，以及 MyGame / Engine Plugin 夹具；覆盖版本锁定、启用、协作、链接、部署和移动最终链接 |
| `TestInfrastructure/` | 测试注册、分类、依赖与基准隔离、构建工具测试准备及 workflow 完整性 |
| `ModuleLinkModes/` | Monolithic / Modular 配置、切换及跨模式回归 |
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
| `SourcePluginBuildToolInvalidation` | `PluginIntegration.DiscoveryInvalidation` |
| `SourcePluginBuildToolFileLinks` | `PluginIntegration.DiscoveryFileLinks` |
| `SourcePluginBuildToolDirectoryLinks` | `PluginIntegration.DiscoveryDirectoryLinks` |
| `SourcePluginBuildToolSourceChange` | `PluginIntegration.BuildToolSourceChange` |
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
