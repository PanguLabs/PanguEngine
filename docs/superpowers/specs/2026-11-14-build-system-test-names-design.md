# 构建系统测试文件与 CTest 命名

状态：按用户本轮要求已实施；承接 `2035844` 的目录迁移，替代其继续保留旧 CTest 名称的范围约定。
实际验证范围见[实施计划](../plans/2026-11-14-build-system-test-names.md)。

## 范围与取舍

用户已要求实施四个文件重命名，并将相关 CTest 名称改成适合当前职责的名称。
采用职责前缀加场景名的点分名称，与现有 `BuildTools.*` 一致；仅替换旧前缀仍会保留
`BuildToolInvalidation`、`BuildToolsTests` 等含义不清的场景名，因此一并澄清后缀。
夹具目标、内部函数、标签、运行集合、超时、处理器预算、资源锁和断言保持原契约。

## 文件映射

| 目录 | 旧文件 | 新文件 |
| --- | --- | --- |
| PluginIntegration | BuildToolInvalidationTests.cmake | PluginDiscoveryInvalidationTests.cmake |
| ProjectIntegration | RunIntegration.cmake | ProjectIntegrationTests.cmake |
| TestInfrastructure | BuildToolsTests.cmake | BuildToolsTestIntegrationTests.cmake |
| TestInfrastructure | TestingTestHelpers.cmake | TestInfrastructureHelpers.cmake |

## CTest 映射

| 旧名 | 新名 |
| --- | --- |
| SourcePluginBuildTool | PluginIntegration.BuildToolConfigure |
| SourcePluginBuildToolInvalidation | PluginIntegration.DiscoveryInvalidation |
| SourcePluginBuildToolFileLinks | PluginIntegration.DiscoveryFileLinks |
| SourcePluginBuildToolDirectoryLinks | PluginIntegration.DiscoveryDirectoryLinks |
| SourcePluginBuildToolSourceChange | PluginIntegration.BuildToolSourceChange |
| SourcePluginEngineRevision | ProjectIntegration.EngineRevision |
| SourcePluginProject | ProjectIntegration.Base |
| SourcePluginEmbeddedEngine | ProjectIntegration.EmbeddedEngine |
| SourcePluginEnable | ProjectIntegration.PluginEnable |
| SourcePluginStaticCatalog | ProjectIntegration.StaticCatalog |
| SourcePluginSharedDeploy | ProjectIntegration.SharedDeploy |
| SourcePluginIncremental | ProjectIntegration.IncrementalBuild |
| SourcePluginTestOwnership | ProjectIntegration.PluginTestOwnership |
| SourcePluginTestOwnershipProject | ProjectIntegration.PluginTestOwnership.Project |
| SourcePluginTestOwnershipEngine | ProjectIntegration.PluginTestOwnership.Engine |
| SourcePluginTestOwnershipProvider | ProjectIntegration.PluginTestOwnership.Provider |
| SourcePluginCollaboration | ProjectIntegration.PluginCollaboration |
| TestingRegistration | TestInfrastructure.Registration |
| TestingDependencyIsolation | TestInfrastructure.DependencyIsolation |
| TestingBenchmarkIsolation | TestInfrastructure.BenchmarkIsolation |
| TestingBuildToolsTests | TestInfrastructure.BuildToolsTestIntegration |
| TestingBuildToolsFailurePropagation | TestInfrastructure.BuildToolsFailurePropagation |
| TestingScriptClassification | TestInfrastructure.ScriptClassification |
| TestingWorkflowCompleteness | TestInfrastructure.WorkflowCompleteness |

## 引用与验证

当前注册、精确筛选、前缀分类断言及操作规范同步新名；历史设计、计划和验收结果保留当时名称，
由 Tests/BuildSystem/README.md 给出迁移映射。Tests/MIGRATION.md 保留历史正文并增加现行名称入口。
工作目录中由测试名派生的部分随名称变化，仍在宿主 BuildRoot 的 Tests/Work 内隔离。

比较迁移前后完整 CTest JSON：仅允许表中的名称、四个文件路径和名称派生工作目录变化。
Windows 正式 preset 重新配置并运行受影响的分类、注册、工具测试接线与失败传播等既有回归。
检查 Darwin 专属分支的登记与 1500 秒覆盖，不宣称未运行的平台已验收。
