# 构建系统测试目录迁移实施计划

> 执行方式：superpowers:executing-plans，在当前会话实施，完成后统一只读评审。

**目标：** 按用户确认的名称迁移 Tests/BuildSystem 及三个子目录，保持测试行为与覆盖。

**架构：** 仅调整文件路径和引用，不改变 CMake/Ninja、CTest 或夹具职责。保存旧清单并在迁移后比较，复用已有回归。

**技术：** PowerShell、CMake 4.0+、CTest、现有 Clang 编译数据库。

**规格：** [已确认设计](../specs/2026-11-14-build-system-tests-layout-design.md)。

## 全局约束

- Tests/CMake → Tests/BuildSystem；SourcePlugins → PluginIntegration；SourcePluginProject → ProjectIntegration；Testing → TestInfrastructure。
- 保留所有测试名称、标签、运行集合、超时、处理器预算、资源锁、断言及夹具内容。
- 继续使用同一共享源码和既有宿主 BuildRoot；不复制 Engine 到其他目录构建。
- 历史验收命令保留历史含义，当前可点击源码链接指向新位置。
- 未经用户本轮明确要求不提交、推送或合并。

## 评审关注点

- 当前绝对/相对源码路径、脚本入口和 C++ include 全部迁移。
- Git 忽略例外不隐藏迁移后的 TestInfrastructure 文件。
- 嵌套 Project 夹具与 Meta 共用辅助脚本的关系保持稳定。
- 测试注册的命令、属性和 fast/precommit/full 选择不丢失。
- 历史记录与当前操作说明明确区分，迁移不依赖未版本化探针。

## Task 1：清单、迁移及引用

**文件：** Tests/CMake 全部文件、Tests/CMakeLists.txt、相关 cmake/ 与 Scripts/、Tests/Log/RuntimeNoAllocationFixture.cpp、.gitignore。

- [x] 保存 Git 文件清单、内容和 CTest 注册基线。
- [x] 原位移动四个目录并更新全部活动路径引用，保留编码和换行。
- [x] 比较全部迁移文件的归一化内容及 CTest 注册属性；规定的路径替换及下文记录的隔离契约修正以外无变化。

## Task 2：说明、验证与收尾

**文件：** 新增 Tests/BuildSystem/README.md；更新当前 Tests/wiki/脚本说明和历史源码链接；归档本计划结果。

- [x] 写明目录职责和旧新路径对应关系。
- [x] 重新配置 Windows Preset，定向构建受影响夹具并执行必要的格式/静态检查。
- [x] 运行已有 TestInfrastructure、构建环境、脚本边界及必要的集成回归；逐项记录未执行或被 Git 锁限制的范围。
- [x] 按 superpowers:requesting-code-review 做只读评审，修正有证据的问题。
- [x] 归档结论并在无运行任务依赖后删除本计划 .superpowers/sdd 临时工作区。

## 验收摘要

完成日期：2026-11-14；验证基于 `c89d1a1` 加本次变更。用户随后要求提交，最终提交以 Git 历史为准，未推送或合并。

## 验收结果

112 个原有文件全部迁移；独立评审复核为 106 个文件字节完全一致、6 个文件仅 8 处路径替换。
含中文路径的 `PanguMeta/Fixtures/Consumer/Public/目录/描述.h` 已用 HEAD blob 校验，空夹具保留。
新建目录 README，活动源码、脚本、当前文档及忽略例外同步；历史命令保留原路径语义，当前源码链接无失效项。

Windows 迁移前后 `ctest --preset windows --show-only=json-v1` 均为 315 项，逐项比较名称、标签、命令、超时、工作目录、处理器预算和资源锁。原112文件只允许路径映射。

额外修正：严格的既有 `TestingScriptClassification` 检出 `LocalBuildScripts` 的显式输出目录原为 `Tests/LocalBuildScripts`，不符合 `Tests/Work` 隔离契约。将其纠正为 `Tests/Work/LocalBuildScripts`；保留检查，注册比较明确记录这一处命令参数例外，名称、标签及其他执行属性不变。

| 验证范围 | 结果 |
| --- | --- |
| Windows 正式配置及 `PanguLogRuntimeNoAllocationFixture` 定向重建 | 通过，新的跨目录 include 能编译链接 |
| `BuildEnvironment`、`LocalBuildScripts`、`ToolchainLibraryLink`、`LogRuntimeNoAllocation` | 首轮定向 4/4 通过；隔离修正后的 LocalBuildScripts 另在下轮重验 |
| `TestingRegistration`、`TestingBuildToolsTests`、`TestingBuildToolsFailurePropagation`、`TestingScriptClassification`、`TestingWorkflowCompleteness` | 修正隔离并重置旧来源夹具缓存后 5/5 通过；首轮 2/5，失败及处理见下文 |
| `LocalBuildScripts`、`SourcePluginBuildTool`、`SourcePluginEngineRevision` | 3/3 通过，验证新输出根、插件发现/引导/增量与隔离小型 Git 夹具中的版本锁 |
| WSL Ubuntu 脚本语法及 CLI 回归 | 在规定共享源码路径执行，通过；产物位于 Linux 本地 BuildRoot 的 Tests/Work/LocalBuildScripts |
| macOS Bash 3.2 脚本语法及 CLI 回归 | 在规定共享源码路径执行，通过；产物位于 macOS 本地 BuildRoot 的 Tests/Work/LocalBuildScripts |
| 格式及静态检查 | clang-format 通过；实际编译数据库 clang-tidy 退出 0，有下述既有告警 |
| 独立只读评审 | 无 Critical/Important；一条 Minor 的工作目录文案已按实际移动入口修正 |

首轮基础设施失败：`TestingRegistration`、`TestingBuildToolsFailurePropagation` 的已有 CMake Cache 记录原 Fixture 绝对源码路径；只删除这两个测试隔离工作根内已核对旧来源的 Cache 与 CMakeFiles，保留其他构建状态，重新生成后通过。第三项失败是上述 LocalBuildScripts 隔离问题；没有关闭或放宽分类检查。

clang-tidy 报告 `RuntimeNoAllocationFixture.cpp` 原有全局 delete 重载参数名与 MSVC 声明不同的 12 条 `readability-inconsistent-declaration-parameter-name` 警告。该文件仅第1行 include 路径变化，分配探针实现未改；本次不扩大范围改写既有探针。

未执行完整 Project Git 锁矩阵、完整引擎 Full、Module 双模式/五平台编译与设备运行。本次验证未提交源码的目录迁移，不宣称完成这些矩阵；正式 Project/Full 验收仍应使用固定已提交源码和既定 override 策略。

注册清单与执行日志归档至 `Build/Windows/Tests/BuildSystemLayoutMigration/`，正式测试不依赖这些历史产物。本计划临时脚本、ledger 和评审上下文在确认无运行依赖后清理。
