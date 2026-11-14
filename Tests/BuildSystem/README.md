# 构建系统集成测试

本目录验证 Engine 的构建接线、源码插件与独立 Project 集成、生成代码接线和测试基础设施。
既有测试名称、标签、fast/precommit/full 分层、超时和资源锁保持稳定；目录名按被测职责命名。

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
规范中的路径已同步。CTest 的 `SourcePlugin*`、`Testing*` 等既有用例名继续沿用，不能由目录名推导改名。

## 执行与产物

由 [Tests/CMakeLists.txt](../CMakeLists.txt) 接入根 CTest。使用相应宿主的根 Preset，先完成所选测试集合的构建，再执行 CTest；具体规则见 [Tests 维护规范](../README.md)。

根 CTest 注册的工作目录及本目录测试的生成夹具使用各宿主 `<BuildRoot>/Tests/Work/`。
显式移动最终链接入口保留构建脚本定义的 `<BuildRoot>/Tests/ModuleLinkModes/<模式>/` 隔离目录；
两者均不在本目录修改共享源码。
Project / Full 的 Git 锁校验保持原策略；正式验收记录固定源码版本、override 状态和实际执行范围。
已有嵌套夹具的 CMake Cache 若记录原源码目录，迁移后需重新生成该夹具缓存；只处理受影响的
隔离测试构建目录，不复用旧源码路径的 Cache，也不清空整个宿主 BuildRoot。
Android/iOS 的 `ProjectIntegration/CrossCompileTests.cmake` 验证真实 Runtime / 业务 Module 的最终链接，
不表示设备执行、打包或签名。
