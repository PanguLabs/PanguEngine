# Project 集成夹具插件接口归属实施计划

> 执行方式：当前会话直接实施用户已选择的方案，使用 writing-plans 与 executing-plans；不另建工作树，根据用户后续指令提交，不发布。

**目标：** 插件公开声明归属提供方，消除对 MyGame/Source 的反向依赖。

**架构：** 各插件提供 Public 目录及仅头文件目标；实际 Module 和宿主分别消费所需接口。身份声明与 Query 导出声明分离，保持 Modular 宿主动态加载业务库。

**技术：** C++23、Clang、CMake 4.0+、Ninja、GoogleTest。

**规格：** [接口归属设计](../specs/2026-11-16-project-plugin-interface-ownership-design.md)。

## 全局约束与评审重点

- 保留全部身份值、Query、Service 类型、测试名称及 Engine/Project 归属。
- Physics 及其自身测试不得依赖 MyGame/Source。
- 无消费者的插件不进入 ALL；仅头文件依赖不引入业务库链接。
- Monolithic Catalog 与 Modular 候选、部署、原生导入行为保持。
- 各测试只修改隔离工作目录中的夹具副本。

## 任务一：接口归属、消费者接线与回归

文件：ProjectIntegration 下四个插件的 Public、CMakeLists、实现与测试；MyGame/Source 与根 CMake；PluginTestOwnershipTests.cmake；PluginIntegration/Fixtures/PhysicsConsumer/CMakeLists.txt。

接口：新增 Pangu::PhysicsHeaders、Pangu::GameplayHeaders、Pangu::EditorExtrasHeaders、Pangu::SharedProbeHeaders；保留现有实际 Module 目标及 pg::fixture 身份/API。

- [x] 新增独立 Physics 消费者，并接入现有 Provider 场景；确认旧实现因依赖 MyGame 头文件而编译失败。
- [x] 迁移四个插件的公开声明，拆分身份，调整 CMake、Meta Query 和消费者依赖。
- [x] 执行独立 Physics、双模式宿主消费及相关插件回归；检查原生链接边界和不相关目标隔离。
- [x] 对修改的 C++ 执行 clang-format、基于实际编译数据库的 clang-tidy；更新夹具说明。
- [x] 完成独立代码评审，记录验收摘要和未验证平台，清理本计划临时目录。

## 验收摘要

源码基线：`33526eeed05a0843c1634545d96f542254bb1315` 加本次实现。用户在验收后要求提交，本计划与实现一并提交；关键提交见本文件的 Git 历史。

Windows / Release / Modular 父配置，benchmark OFF。通过正式根 preset 配置并定向运行以下 CTest；嵌套夹具按各自脚本覆盖 Monolithic / Modular。验证期间设置 `PANGU_ENGINE_ALLOW_LOCAL_OVERRIDE=ON`，完成后恢复 OFF；这是工作区定向验证，不是固定提交的完整平台矩阵验收。

| CTest | 最终结果 | 秒 |
| --- | --- | ---: |
| ProjectIntegration.HostModuleConsumption | 通过 | 141.45 |
| ProjectIntegration.PluginTestOwnership | 通过 | 286.06 |
| ProjectIntegration.IncrementalBuild | 通过 | 144.64 |
| ProjectIntegration.PluginEnable | 通过 | 80.99 |
| ProjectIntegration.SharedDeploy | 通过 | 33.12 |
| ProjectIntegration.PluginCollaboration | 通过 | 30.43 |
| ProjectIntegration.Base | 通过 | 84.43 |
| ProjectIntegration.EmbeddedEngine | 通过 | 366.03 |

命令均使用 `ctest --preset windows-full --parallel 4 -R <精确名称正则> --stop-on-failure --output-on-failure`，分别选择 HostModuleConsumption、其余七项，以及末轮 Base/EmbeddedEngine。

红绿与评审结论：

- 独立 PhysicsConsumer 在旧实现下真实编译失败：缺少 `FixtureModules.h`、`PhysicsModule.h`；迁移后编译通过。独立运行进一步暴露 DLL 部署依赖，已由插件自身测试显式部署解决。
- 独立评审发现依赖子串匹配误判 Headers 目标，以及共享运行目录导致无关宿主构建 Physics；分别改为链接成员精确判断和独立测试运行目录。新增 EngineProviderOnly 在修复前实际构建 Physics.cpp/DLL 而失败，修复后通过。
- 首次八项回归在 Ownership 通过后主动停止以集中修复评审问题；不计作完整通过。
- 后续八项中 Base 因旧固定工作目录残留已迁走头文件而失败；改为每次独立运行目录后，重跑共享入口的 Base/EmbeddedEngine 均通过。其余六项复用本轮已通过结果。
- 24 个夹具 `.h` / `.cpp` 通过仓库 clang-format 检查；9 个已构建翻译单元使用实际目标编译数据库运行 clang-tidy，无诊断，覆盖四个插件实现、两个宿主和三个插件/协作用例。含 CMake 占位符的候选模板通过实际双模式生成、构建与运行验证。
- 插件目录中 `CMAKE_SOURCE_DIR`、`FixtureModules.h`、`MyGame/Source` 反向引用为零；`git diff --check` 通过。

测试日志留在 `Build/Windows/Tests/Results/PluginInterfaceOwnership/`；本计划 `.superpowers/sdd` 临时目录已清理。未运行其余根 CTest、Linux/macOS 或 Android/iOS 本轮验收，无遗留实现问题。
