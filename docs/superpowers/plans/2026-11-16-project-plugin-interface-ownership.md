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

接口：新增 FixtureEnginePhysicsHeaders、MyGameGameplayHeaders、MyGameEditorExtrasHeaders、MyGameSharedProbeHeaders；实际 Module 目标按下文 Runtime 专用命名边界更新，保留 pg::fixture 身份/API。

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

## c0efdd2 评审修正

- 按 `wiki/core/plugin-module.md` 的 Frozen 规则移除四个头文件辅助目标的 `Pangu::` 别名，消费者直接使用普通目标名；同步夹具说明。
- 验证：复用已有 Windows 构建缓存，先恢复被负例修改的夹具副本，再重新配置、增量构建 MyGame/MyEditor × Modular/Monolithic、SharedProbe 部署和独立 PhysicsConsumer，六项均通过。四个宿主实际运行并校验 Service 隔离与关闭；Modular 原生导入表不包含业务库，SharedProbe 部署哈希一致，Physics 自身 CTest 通过。
- 六份实际 `compile_commands.json` 在改名前后哈希相同；辅助目标别名及其旧引用已清除，`git diff --check` 通过。本轮没有 C++ 源码修改，未重跑完整测试集或其他平台；根配置保持 local override OFF。
- 记录：`Build/Windows/Tests/Results/PluginInterfaceOwnership/ReviewFix/`。
- 按用户补充意见将 MyGame 自有的三个辅助目标改用 MyGame 前缀，Engine 侧 Physics 保留 Pangu 前缀；对应引用与说明一并更新。补充改名后重新配置、增量构建并验证上述六个场景，全部通过，六份编译数据库哈希仍保持不变；记录位于 `Build/Windows/Tests/Results/PluginInterfaceOwnership/ReviewFix/MyGamePrefix/`。

## Runtime 专用 Pangu 命名空间

用户新增约束：仅 `Source/Runtime/` 定义 `Pangu::` 别名。其他目录依赖 Runtime 时仍引用 Runtime 别名；自身目标使用普通名称。

目标映射：`Pangu::Physics` / `PanguPhysics` → `FixtureEnginePhysics`，Physics 辅助目标 → `FixtureEnginePhysicsHeaders`；MyGame 的 Gameplay、EditorExtras、SharedProbe 直接使用 `MyGameGameplay`、`MyGameEditorExtras`、`MyGameSharedProbe`；移除 ToolchainProbe 别名，直接引用 `PanguToolchainProbe`。同步由目标名派生的导出头、库文件、Meta 路径、部署和测试断言，保留 C++ Query、Module 身份与测试名称。

- [x] 更新 AGENTS 和权威命名规范，清除 Runtime 外的保留别名。
- [x] 完成目标与消费接线改名，显式检查 Gameplay/EditorExtras 的 Physics 依赖，保留配置期失败行为。
- [x] 运行 Windows 针对性构建、配置与集成回归及必要静态检查，记录平台边界。
- [x] 完成独立评审、验收归档及本轮临时目录清理。

### 本轮验收

基于 `c0efdd2` 与当前未提交修改，Windows / Release / Modular 父配置、benchmark OFF。先按根 `windows` preset 配置并定向构建 `PanguToolchainLinkTests`，再由 `ctest --preset windows-full --parallel 4 -R <精确正则> --stop-on-failure --output-on-failure` 分两组执行以下八项。测试期间临时开启 local override，结束恢复 OFF；不是完整平台矩阵验收。

| CTest | 结果 | 秒 |
| --- | --- | ---: |
| ToolchainLibraryLink | 通过 | 0.04 |
| ProjectIntegration.PluginEnable | 通过 | 86.10 |
| ProjectIntegration.PluginTestOwnership | 通过 | 309.12 |
| ProjectIntegration.HostModuleConsumption | 通过 | 145.38 |
| ProjectIntegration.IncrementalBuild | 通过 | 148.97 |
| ProjectIntegration.SharedDeploy | 通过 | 33.39 |
| ProjectIntegration.PluginCollaboration | 通过 | 36.96 |
| ProjectIntegration.Base | 通过 | 82.09 |

- 修复前 Runtime 外有 5 处 `Pangu::` 别名定义，修复后为零；剩余 4 处均位于 `Source/Runtime/`。测试和 Project 对 Runtime 别名的引用保留。
- 导出头文件名、Meta Query/Catalog 目标及生成目录、显式部署文件名和原生导入断言同步新目标。缺失 Physics 的配置负例与双模式消费验证通过；没有为缺失普通目标退化为裸链接库名。
- 四个改名 Module 的公开 Query 头通过 clang-format；对应已构建的 Physics、Gameplay、EditorExtras、SharedProbe 四个翻译单元使用实际编译数据库运行 clang-tidy，无诊断。
- 独立只读评审未发现有依据的缺陷。C++ 命名空间、Module/Plugin 身份、Query 协议、导出宏及测试名称保持原契约。
- 未运行其余根 CTest 或本轮 Linux/macOS/Android/iOS 验收。日志位于 `Build/Windows/Tests/Results/RuntimeTargetNamespace/`，本轮临时目录已清理。

## Physics Plugin ID 同步

用户要求将 Physics 夹具的 Plugin ID 统一为 `fixture.physics`。同步 manifest、访问记录、启用列表、独立消费、协作与归属检查、移动入口、BuildTool 解析/发现测试及当前规范示例。ID 排序随之改为 Physics 在 MyGame 插件之前，Engine 来源和 Runtime ModuleId 不变。

- [x] 完成 ID 与排序断言同步，清除当前测试和规范中的旧 ID 引用。
- [x] 验证 BuildTool 解析/发现、双模式启用与测试归属；复用本轮此前通过的未改动 Runtime/部署实现检查。
- [x] 记录实际验证范围，清理本轮临时目录。

本轮使用 Windows 根 preset 配置并构建 `PanguBuildToolTestsPrecommit`，再定向执行以下根 CTest，均通过：

| CTest | 秒 |
| --- | ---: |
| BuildTools.PanguBuildTool.PluginManifest | 1.17 |
| BuildTools.PanguBuildTool.PluginDiscovery | 0.60 |
| ProjectIntegration.PluginEnable | 80.14 |
| ProjectIntegration.PluginTestOwnership | 297.06 |

解析与发现用例验证新 ID 和排序；启用场景覆盖双模式、重复/非法 ID、依赖和目标隔离；归属场景验证 engine 来源、插件标签、协作用例与独立 Physics 消费。两个修改过的 BuildTool 测试翻译单元使用真实编译数据库运行 clang-tidy，无诊断，clang-format 与 `git diff --check` 通过。

当前 Tests/cmake/Scripts/wiki 中旧 Physics Plugin ID 引用为零，历史验收记录保留当时内容。未重跑其余根 CTest 或其他平台；此前 Runtime/部署实现未改动的验证结果仍按其各自记录适用。local override 已恢复 OFF，日志位于 `Build/Windows/Tests/Results/PhysicsPluginId/`，本轮临时目录已清理。

## Physics 测试目标命名

按用户要求使用 `FixtureEnginePhysicsPluginTests` 作为 Physics 自身测试目标；实际可执行程序、CTest 名称前缀、构建目标、筛选正则及归属检查同步此名。当前源码中的旧测试目标引用已清除。

Windows 定向验证完成：复用最近一次 Ownership 验收的 EngineAndProject 与独立 PhysicsConsumer 构建缓存，重新配置并构建 `FixtureEnginePhysicsPluginTests`。EngineAndProject 中 Physics、Gameplay、Collaboration 三个既有子测试，以及独立 PhysicsConsumer 的 Physics 用例均通过。

两处 CTest 清单均唯一登记 `FixtureEnginePhysicsPluginTests.PhysicsPlugin.DefinitionMatchesModule`，不含旧测试名前缀；owner.engine、plugin.fixture.physics、boundary.unit 及 fast/precommit/full 标签完整。`ninja -t commands PanguTestsFast` 确认聚合集合依赖新目标，不再依赖旧目标。无 C++ 源码修改，未重跑整个 Ownership 脚本、其他根测试集或其他平台，根 local override 保持 OFF。

`git diff --check` 与当前源码旧目标引用检查通过；日志位于 `Build/Windows/Tests/Results/PhysicsTestTarget/`，本轮临时目录已清理。

## 后续修正提交归档

用户要求将 Runtime 专用 `Pangu::` 边界、夹具和 MyGame 普通目标命名、`fixture.physics` Plugin ID、`FixtureEnginePhysicsPluginTests` 测试目标及其全部接线、规范和验收记录统一提交。关键提交见本文件 Git 历史；上述分阶段验证范围和未验证平台保持各自记录，不拼接为完整测试矩阵通过结论。

## Linux/macOS 补充全量验收（2026-11-16）

按用户要求，在固定提交 `07d48464c80e2a8762df0a3f2c07bba4549f715b`、tree `d474ce6ef52899862a0bd6480a60b779a3091903` 上补跑 Linux/macOS 根 Full。两端直接读取同一份共享源码，测试期间不修改 tracked 文件或 HEAD；测试前后均核对完整工作区（含子模块）干净、提交与 tree 一致。上文各阶段的历史验收范围保留，本节单独记录当前提交的补充结果。

配置均为 Release/Modular、`BUILD_TESTING=ON`、`PANGU_BUILD_ENGINE_TESTS=ON`、两项 benchmark 开关 OFF、local override OFF。保持嵌套构建预算 `PANGU_TEST_BUILD_JOBS=2`，full build preset 沿用其 jobs=1，CTest 显式使用 `--parallel 2`。

| 宿主 | 工具链 | 正式 BuildRoot | 根 Full 结果 | CTest 实际耗时 |
| --- | --- | --- | --- | --- |
| WSL Ubuntu | CMake 4.4.2 / Clang 23.1.0 | `/home/moozen/PanguEngineBuild/Linux`（ext4） | 301/301 通过 | 2157.80 秒（35 分 57.80 秒） |
| macOS | CMake 4.4.3 / MacPorts Clang 23.1.2 | `/Users/moozen/PanguEngineBuild/macOS`（APFS） | 304/304 通过 | 7606.81 秒（2 小时 6 分 46.81 秒） |

Linux 从 `/home/moozen/WindowsGit/PanguEngine`（CIFS）执行，`LLVM_ROOT=/opt/llvm-23.1.0`；macOS 通过 `ssh -o BatchMode=yes moozen-macos`，从 `/Users/moozen/WindowsGit/PanguEngine`（SMB）直接构建，`LLVM_ROOT=/opt/local/libexec/llvm-23`。

两端依次执行以下正式入口，`platform` 分别为 `linux`、`macos`，`result_root` 为下列对应结果目录：

```sh
cmake --preset "$platform" -DCMAKE_BUILD_TYPE=Release -DPANGU_LINK_MODE=Modular \
    -DBUILD_TESTING=ON -DPANGU_BUILD_ENGINE_TESTS=ON \
    -DPANGU_BUILD_BENCHMARKS=OFF -DPANGU_BUILD_META_BENCHMARKS=OFF \
    -DPANGU_ENGINE_ALLOW_LOCAL_OVERRIDE=OFF -DPANGU_TEST_BUILD_JOBS=2
cmake --build --preset "${platform}-full"
ctest --preset "${platform}-full" --show-only=json-v1 > "$result_root/selection.json"
ctest --preset "${platform}-full" --parallel 2 --output-on-failure \
    --output-junit "$result_root/full.xml"
```

两端 configure、full 依赖构建、CTest 与外层流程的原生退出码均为 0。分别解析两份 JUnit，核对测试名称与 Full 选择清单逐项一致，失败、错误、跳过、禁用均为零；两端均为本轮首轮完整通过，没有失败后的重跑。macOS 比 Linux 多出的三项恰为插件归属拆分的 `.Project`、`.Engine`、`.Provider`，其余名称完全一致。根调度数量不与工具组或嵌套场景的内层测试数量累加。

覆盖包括插件接口归属和独立消费、`fixture.physics` 身份及测试目标改名、包发现、部署、增量和 Project 切换、链接模式矩阵与往返、ModuleRegression、Log 配置矩阵及全部 Runtime 测试。macOS 的 EmbeddedEngine 为 985.75 秒，Engine 归属场景为 506.54 秒，均在既有超时预算内通过。验收范围为两桌面 Release/Modular 根 Full，嵌套 Monolithic/Modular 场景依既有清单执行。

原始分阶段命令、日志、退出码、选择清单、JUnit、环境、源码前后快照及最终缓存位于各自主机：

- Linux：`/home/moozen/PanguEngineBuild/Linux/Tests/Results/Full-07d4846-20261116T093500/`。
- macOS：`/Users/moozen/PanguEngineBuild/macOS/Tests/Results/Full-07d4846-20261116T093500/`。

结束后两端缓存保持上述 Release/Modular、预算 2、测试 ON、benchmark OFF 和 local override OFF；无残留 macOS 构建/测试进程。本轮没有修改源码或正式测试，验收记录在测试结束并完成源码状态核对后追加。
