# PanguEngine 分层测试方案

> 状态修订：Partially Superseded（2026-11-06）。StaticModuleHost、混合/同库承载与整体 Shutdown 顺序以[新批准规格](2026-11-06-module-manager-link-modes-design.md)及 [ADR-0015](../../../wiki/decisions/0015-unified-link-modes-module-manager.md) 为准；其它有效测试/身份/所有权契约保留。以下保留当时设计与结果，不改写为当前 Modular 验收。

> 状态：Approved（用户已审阅通过）。
> 日期：2026-11-05
> 审批：2026-11-05，用户确认 spec 已审阅并要求编写实施计划。
> 实现状态：已完成（2026-11-06），任务评审、整分支评审及收尾见[实施计划](../plans/2026-11-05-layered-testing.md)；平台、Google Benchmark 验收及非阻断维护项见[验收记录](../plans/2026-11-05-layered-testing-validation.md)。
> 范围：现有正式测试（包括独立 PanguBuildTool 子项目测试）的组织、框架接入、运行入口、迁移与验收。

## 1. 意图与决策来源

本次优先解决分层测试：开发者应能看清测试验证哪个行为边界、何时执行、失败意味着什么。GoogleTest 提供 C++ 用例和断言，GoogleMock 控制必要的依赖交互，Google Benchmark 提供性能测量；CTest 继续调度正确性测试。

用户已选择：**验证边界与执行频率分开**。测试按功能领域维护，不为每个运行入口复制测试源码。后续 grilling 的 Q1–Q18 已全部确认，关键决策汇总见第 16 节。

已确认使用独立测试可执行程序、累计运行集合、原生 CMake 配套构建与测试入口。Benchmark 必须交付，使用独立开关；三桌面平台必须完成正确性验收，Windows 提供正式性能基线，Linux/macOS 提供基准运行冒烟。时间预算为优化目标。Plugin 测试跟随其所属 Engine 或 Project，不建立第三类独立归属。

本文更新这些已确认决策及其约束；依赖版本、辅助函数参数、目标拆分等仍由实施者在边界内确定，不将实施建议表述成已实现功能。

## 2. 现状与约束

- 根工程通过 `BUILD_TESTING` 与 `PANGU_BUILD_ENGINE_TESTS` 接入 Tests；Engine standalone 默认开启内部测试，嵌入 Project 默认关闭，父工程拥有 CTest 开关。
- Foundation、Module 和 JSON 测试主要使用自写 `main()`、布尔返回值和组合条件。失败通常只能定位到测试程序，多个场景之间还存在短路。
- Module 已有注册、依赖排序、Scope 装配、借用、关闭、失败回滚和析构终止回归，不能以更换框架为由缩减。
- Static Catalog 使用两座真实 STATIC 夹具库、生成的 Catalog、引用根及平台链接裁剪选项；这些条件本身就是验收对象。
- 构建系统回归通过 CMake 脚本验证 Engine/Project 嵌入、Plugin 启用、源码变更、原生增量与部署。它们有独立夹具工程和配置/构建成本。
- Toolchain 同时包含编译型 STATIC 探针、桌面最终链接/运行检查及显式移动端最终链接冒烟。
- `BuildTools/PanguBuildTool/Tests/` 另有 PluginManifest、PluginDiscovery 和 PluginCli 三项正式测试，当前只在工具子工程启用 `BUILD_TESTING` 时登记。普通工具 bootstrap 明确关闭测试，仅构建工具，因此根 `SourcePlugin*` 回归不能替代这三项。
- 桌面平台为 Windows、WSL Linux、macOS；Android/iOS 当前不创建根工程中的设备运行测试程序。编译通过、最终链接通过和设备运行通过必须分别报告。
- 继续使用 C++23、Clang、CMake 4.0+ 和现有平台工具链。Pangu 自有测试保持无异常策略；第三方告警和编译参数由自身 Target 表达。
- 正式依赖固定版本，CMake configure 不联网、不更新 submodule。第三方接口使用 `ThirdParty::`，测试和辅助库不获得 `Pangu::` Module 身份。
- 遵守 [Tests 维护规范](../../../Tests/README.md)、[构建系统](../../../wiki/foundation/build-system.md)、[ThirdParty 接入](../../../wiki/foundation/third-party.md) 和各平台 BuildRoot 规则。

## 3. 目标、非目标与失败标准

### 目标

1. 每个运行用例有明确的验证边界、功能领域、执行层级和资源条件。
2. 提供快速、提交前、完整三个累计的正确性测试入口，允许按领域和用例进一步定向。
3. C++ 场景拆成可独立报告的 GoogleTest 用例；失败展示具体断言、预期与实际值。
4. GoogleMock 接入真实存在的依赖边界，保留需要真实组件才能验证的集成行为。
5. 建立 Google Benchmark 的独立构建/运行方式和可比较的结果记录。
6. 迁移前后核对原触发条件、失败路径、并发行为、平台差异与关键断言。
7. 测试接线可由 Engine、Project 及它们显式启用的 Plugin 按需复用；Plugin 跟随所属工程的测试策略。
8. 构建工具类别及其共用测试接线统一命名为 `BuildTools`；`PanguBuildTool` 等具体工具保留各自名称。“宿主”表示执行平台条件，不作为另一套工具类别命名。

### 非目标

- 本次不实现 Editor、Renderer、ECS 或生产 Loader 的新功能，也不为尚不存在的功能建立空测试工程。
- 不实现 AzTestRunner 等动态测试库加载器，不引入测试专用 Runtime Module 或统一测试管理器。
- 不整体重写 CMake 回归脚本，不增加 Python/PyTest 依赖；测试中需要新增复杂工具逻辑时遵守 C++ 优先原则。
- 不建设 CI 服务、调度平台、覆盖率平台或自动影响分析；本次提供的入口可以被未来 CI 调用。
- 不建设 Android/iOS 部署、签名、模拟器或真机运行器；不把桌面结果外推成五平台运行覆盖。
- 不在初次接入时设定性能百分比硬门槛，不恢复 AbiProfile。

### 不可接受的结果

- 为达到快速预算删除慢测试、减少竞态触发条件或放宽行为断言。
- 只把旧的整段布尔函数包进一个 `EXPECT_TRUE`，仍无法判断具体失败原因。
- 用 Mock 替换 StaticModuleHost、RegistrationScope、ScopeAccess 等被集成验证的主体。
- 快速入口遗漏核心关闭/回滚行为，或完整入口静默遗漏现有正式回归。
- 依赖预先存在的 `.probe/`、未版本化输入或其它平台的 BuildRoot。

## 4. 方案比较（Approaches）

| 方案 | 做法 | 收益与代价 | 结论 |
| --- | --- | --- | --- |
| 按频率统一分组 | 只标注快速、完整、专项 | 接入简单，但执行速度无法说明验证边界，难以判断 Mock 和真实集成的覆盖关系 | 未选 |
| 验证边界与执行频率分开 | 独立记录 unit/integration/system，另行编排运行入口 | 需要少量元数据和接线校验，可同时回答“测什么”和“什么时候测” | **用户已选择** |

参考 O3DE 的 CTest 统一调度、套件筛选、平台能力判断、独立测试目标和报告输出。Pangu 首期使用独立可执行程序，直接链接现有生产 Target；根据当前需求，不移植 O3DE 的动态测试库加载和引擎测试环境框架。

## 5. 分类模型

### 5.1 验证边界

每个普通运行用例恰好有一个主边界：

| 边界 | 判定原则 | 当前例子 |
| --- | --- | --- |
| `unit` | 验证单个组件的行为，外部依赖可使用 Fake/Mock，不驱动多个组件的完整生命周期 | Error/Result、ModuleId 解析格式化、ScopeAccess 局部门控和计数 |
| `integration` | 多个真实组件共同完成一个行为，结果依赖它们之间的真实连接 | 注册与回滚、StaticModuleHost 与 RegistrationScope、Scope 生命周期、两座 STATIC 夹具和 Catalog |
| `system` | 从进程或工程外部驱动完整工具、工程配置/构建或部署流程 | BuildEnvironment、Plugin/Project 配置与增量回归 |

边界由用例验证对象决定，不由文件名、耗时、线程数量或使用了 GoogleTest 决定。Scope 析构终止回归的主体仍是 Module 组件协作，因此归入 integration，另加子进程条件。

### 5.2 正交属性

- `area.*`：功能领域，例如 `area.foundation`、`area.module`、`area.build`、`area.thirdparty`、`area.toolchain`。
- `type.contract`、`type.regression`：表达规范契约或缺陷回归，可以叠加在任意边界，不设置相互排斥的目录。
- `requires.*`：实际需要的条件，例如 `requires.threads`、`requires.subprocess`、`requires.nested-build`；以后出现真实 GPU/设备用例再新增对应条件。
- `owner.engine` / `owner.project`：测试只有 Engine 或 Project 两类归属。Plugin 自带测试随其声明来源继承归属；可另以 Plugin ID 属性精确筛选，不创建 `owner.plugin`。具体规则见第 12 节。
- 平台支持通过注册条件和维护说明表达；CTest 中可记录本次目标的 `platform.windows`、`platform.linux` 或 `platform.macos`。这些标签不表示未执行平台已经验收。

编译契约、最终链接检查和性能测量另有验收规则，不强行伪装成普通运行用例。正向 `static_assert` 留在相关测试 TU 或编译探针，随对应构建目标检查；现有 STATIC 探针保持真实编译边界。正式 full 验收包含这些必需构建检查，单独运行 full CTest 集合不能代替。本次不为没有需求的负向编译测试建立通用框架。

## 6. 执行频率与入口

### 6.1 累计集合

每个普通正确性用例选择一个最低执行层级 `fast`、`precommit` 或 `full`，接线自动派生累计标签：

| 最低层级 | 派生的 CTest 标签 |
| --- | --- |
| `fast` | `run.fast`、`run.precommit`、`run.full` |
| `precommit` | `run.precommit`、`run.full` |
| `full` | `run.full` |

因此 fast 是 precommit 的子集，precommit 是 full 的子集。普通正确性用例不得只注册到专项入口而从 full 消失。标签是执行集合的表达，不复制用例。

### 6.2 初始预算与选择

| 入口 | 内容 | 初始目标 |
| --- | --- | --- |
| 快速 | 单元测试，加关键且短的集成回归 | 已完成增量构建后，测试执行目标 30 秒、预算 60 秒 |
| 提交前 | 快速集合，加稳定的 Module 集成、真实 STATIC Catalog、受控子进程等回归 | 测试执行预算 5 分钟 |
| 完整 | 当前桌面配置注册的全部正确性测试，包含嵌套配置/构建回归 | 先测量并记录，不预设时长上限 |
| 专项 | 按领域、条件或实际平台选择；长时间压力和性能另行显式运行 | 由实际场景决定 |

预算是用户已接受的初始优化目标，不是已有实测结果，不是迁移完成的硬门槛，也不是要求超时杀死整套测试的总时限。单用例超时根据有界等待和实际运行成本设置。构建耗时、测试耗时分开记录；嵌套构建本身就是系统测试行为，其时间计入相应测试执行成本。超预算时记录数据并优化准备和调度，不缩减有效覆盖。

短小且确定的 Scope 失败回滚、借用阻止销毁、停止顺序等核心行为应进入 fast。真实多库链接裁剪回归至少进入 precommit。现有并发回归保留原触发强度，初期进入 precommit；实测稳定且满足预算后可以提升到 fast。长时间压力变体不替代原有短回归。

### 6.3 配套构建、测试与 workflow

桌面每个平台保留现有基础 configure/build preset 和 BuildRoot，新增配套 build/test/workflow preset，不创建额外构建树。以下名称为计划入口：

- `<platform>-fast`：筛选 `^run[.]fast$`。
- `<platform>-precommit`：筛选 `^run[.]precommit$`。
- `<platform>-full`：执行本配置全部注册的普通正确性用例。
- 现有 `<platform>` test preset 保持完整正确性语义，与 full 一致。

对应 build preset 必须构建该集合的测试程序及所需生产依赖，覆盖 `EXCLUDE_FROM_ALL` 的已启用 Plugin 测试目标，以及本集合需要的独立宿主工具测试目标。full 构建还必须包含 Toolchain 编译探针、最小最终链接程序及所有已登记的正确性测试目标。按最低执行层级派生聚合构建依赖即可，不另建构建系统或维护第二套源码依赖图。独立宿主工具测试的准备、执行和结果汇总见第 7.5 节。

每个同名 workflow 使用 CMake 原生步骤顺序执行 configure → build → test；其 build/test preset 引用同一个起始 configure preset。CTest 本身不会重建根测试程序，直接调用 test preset 只证明所运行产物的结果。正式验收必须先完成对应构建，推荐使用 workflow。

严格区分：**full 测试集合**是 CTest 用例集合，**正式 full 验收**是有效配置、必需构建检查和 full 测试集合共同通过。Engine 正式 full 验收要求 `BUILD_TESTING=ON`、`PANGU_BUILD_ENGINE_TESTS=ON`（Toolchain 随 Engine 测试接入，不再设独立开关）；配置不满足时明确拒绝称为正式 full 验收，不静默验收缩减后的注册集。该约束不禁止父 Project 自己运行仅包含 Project 测试的集合。

新入口通过 test preset 的 `execution.noTestsAction=error` 等原生配置对无测试匹配返回失败，避免标签错误变成空跑通过。定向检查可在选定 preset 上增加精确的测试名或领域标签；多个 `-L` 是交集选择，单个正则中的 `|` 是并集选择，文档必须给出不含歧义的示例。

设计期示例，当前尚不存在这些新增入口：

```sh
cmake --workflow --preset windows-fast
cmake --workflow --preset windows-precommit
cmake --workflow --preset windows-full
# 已完成对应构建后的定向运行。
ctest --preset windows-full -L '^area[.]module$' --output-on-failure
```

提交前验证不是唯一合并条件。改动构建系统、平台适配、链接接线或资源条件时，必须追加对应的 full/专项验证，并明确遗漏范围。本次不自动安装 Git hook 或建立隐藏的影响分析规则。

## 7. 框架接入与测试目标

### 7.1 第三方依赖

新增 `Source/ThirdParty/GoogleTest/` 和 `Source/ThirdParty/GoogleBenchmark/`，使用现有依赖目录约定：`Repo/` 固定 Git submodule，外层维护 CMake 和 README。GoogleMock 来自同一 GoogleTest 仓库，不单独复制源码。

建议提供普通原生 ALIAS：

| 构建接口 | 上游用途 |
| --- | --- |
| `ThirdParty::GoogleTest` / `ThirdParty::GoogleTestMain` | `gtest` / `gtest_main` |
| `ThirdParty::GoogleMock` / `ThirdParty::GoogleMockMain` | `gmock` / `gmock_main` |
| `ThirdParty::GoogleBenchmark` / `ThirdParty::GoogleBenchmarkMain` | `benchmark` / `benchmark_main` |

依赖只在实际需要的消费路径配置。`BUILD_TESTING=OFF` 或 `PANGU_BUILD_ENGINE_TESTS=OFF` 时，不因 Engine 内部正确性测试新增 GoogleTest/GoogleMock 构建依赖；Project 自己启用正确性测试时可以按需使用这些框架。交叉目标仅启用现有 Toolchain 验证时，不配置这些运行测试依赖。

新增 Engine 基准开关 `PANGU_BUILD_BENCHMARKS`，默认 OFF，独立于 `BUILD_TESTING` 与 `PANGU_BUILD_ENGINE_TESTS`。原生桌面目标显式开启该开关时即可构建 Engine 基准及已启用 Engine Plugin 的相应基准；Project 自有基准由其所属 Project 显式配置，不为 Plugin 新增独立开关。关闭时不要求 Google Benchmark 源码已初始化。仅开启基准不因本测试基础设施额外引入 GoogleTest/GoogleMock，关闭正确性测试也不得隐式关闭已显式请求的基准。

构建上游静态测试框架，关闭其自身测试、示例、安装和 benchmark 依赖下载选项。选项在依赖目录内设置，不向父 Project 强制修改全局 `BUILD_TESTING`、`BUILD_SHARED_LIBS` 或平台 CRT 策略。若父工程已提供同名上游目标，仅在可证明符合版本、来源和编译契约时复用；否则明确诊断冲突，不隐式接受任意版本或联网补齐。

实施者选择当时兼容 CMake 4.0、Clang 23 和目标标准库的稳定发布版，记录具体 tag、完整 commit、许可证及兼容性证据。不得直接沿用本地 O3DE 的 GoogleTest v1.15.2 或 Google Benchmark 旧包版本。依赖缺失时给出明确的初始化命令，不在 configure 时运行它。

### 7.2 异常、RTTI 与真实链接配置

Runtime 相关 Pangu 自有测试继续依赖 `PanguBuildOptions`，保持无异常。BuildTools 测试沿用各工具自身的编译策略，不继承 Runtime 的 `PanguBuildOptions`；当前 `PanguBuildTool` 保持无异常并保留 RTTI，所用测试框架与消费方的能力宏须一致。为 GoogleTest/GoogleMock 和 Benchmark 按所选上游版本设置目标级无异常选项，并核对库与消费方对异常能力宏的一致认识；不用异常抛出来实现断言失败或错误注入。

2026-11-05 用户明确覆盖旧决定：Pangu 自有 Runtime、BuildTools、测试和基准均保留 RTTI，不主动禁用；无异常策略保持不变。GoogleTest/GoogleMock 统一实际启用 RTTI 与 `GTEST_HAS_RTTI=1`，直接消费固定原始 `Repo/`，移除无 RTTI 专属补丁及头文件副本。保留 `pangu_require_googletest(RTTI ON)` 接口，OFF 请求明确拒绝。CRT、STL、体系结构和配置与实际平台工具链一致；不复制 O3DE 针对其版本的特定补丁。Windows 核对上游 CRT 默认值，避免同一测试程序混合不同运行库。第三方不继承 Pangu 的整套告警参数；使用 SYSTEM include 边界及必要的最小兼容配置。

接入验收至少包含普通断言、失败诊断、使用真实边界的 GoogleMock 交互及一个 Benchmark 可执行程序的编译/链接/运行，验证实际组合，而非只验证上游库归档成功。

### 7.3 目标粒度和目录

测试源码继续按领域组织，例如 `Tests/Foundation/`、`Tests/Module/`、`Tests/Toolchain/`、`Tests/CMake/`；基准放在相应领域的 `Benchmarks/`。现有构建工具测试集中迁入根 `Tests/BuildTools/PanguBuildTool/`，源码和测试接线只维护一份，保留独立宿主构建能力。不建立与每个频率对应的源码目录。

普通 C++ 测试按“功能领域 + 验证边界 + 必要运行条件”合成可执行程序，例如 `PanguFoundationUnitTests`、`PanguModuleUnitTests`、`PanguModuleIntegrationTests`。同一程序中的用例默认具有相同的元数据；确有不同最低执行层级或进程条件时拆成另一个有明确用途的目标，避免实现按 C++ 宏推断标签的系统。

不要求每个 `.cpp` 一个程序，也不将所有测试合并成唯一程序。Static Catalog 保留独立目标，保持两座真实夹具库、生成 Catalog、链接裁剪和引用根；Toolchain 最小最终链接程序同样保留，不为使用 GoogleTest 扩大它所验证的链接依赖。

优先使用上游 `gtest_main` 或 `gmock_main`；使用 GoogleMock 的目标选用能够初始化其参数的 main。需要特殊子进程模式的夹具保留专用入口。首期不建立通用 Pangu 自定义 main、全局测试环境或强制所有测试依赖 GoogleMock/Benchmark 的聚合库。

### 7.4 轻量 CMake 接线

新增 `cmake/PanguTesting.cmake`，提供少量明确职责的辅助函数：

1. 登记测试的边界、领域、最低执行层级和资源条件，派生累计标签并拒绝缺失/非法值。
2. 为已有 GoogleTest 可执行目标调用 `gtest_discover_tests()`，设置唯一测试名前缀、`DISCOVERY_MODE PRE_TEST`、标签、工作目录和 XML 输出位置。
3. 为脚本、Toolchain 最小程序和特殊子进程父检查登记同一套元数据，沿用 CMake 原生 `add_test()` 与属性；将已有测试目标加入相应运行集合的配套构建依赖。

目标创建、源码列表和生产依赖继续由领域内的普通 CMake target 命令表达。辅助函数不替代 `add_library()` / `add_executable()`，不发展成通用构建 DSL。辅助文件可以被 Engine、Project 及其 Plugin 按需引入，不要求先开启 Engine 内部测试；仅引入函数定义不配置 Google 框架、不扫描测试。资源使用明确的 Engine 根路径或函数自身文件目录定位，不把父工程 `PROJECT_SOURCE_DIR` 当成 Engine 根。测试归属由登记参数或明确的工程上下文提供；Plugin 继承所属方上下文，见第 12 节。

普通 GoogleTest 发现到的每个用例均成为独立 CTest 条目，因此可以定位、定向执行和按进程并发。用户已接受每用例启动进程的成本；首期不实现批处理调度，直接运行整个测试程序仍支持批量执行。发现过程不应构造需要启动引擎、访问可变全局资源或运行测试逻辑的静态对象。GoogleTest 发现失败必须使验收失败；`PRE_TEST` 只是推迟发现时点，不提供设备执行能力。

### 7.5 BuildTools 测试接线与 PanguBuildTool 接入

BuildTools 这一类工具允许共用测试构建、分类、执行和结果汇总接线；具体工具分别维护自己的用例、测试目标、夹具和编译策略。共用命名采用 `BuildTools` / `build_tools` / `BUILD_TOOLS`，例如 `PanguBuildToolsTesting.cmake`、`pangu_add_build_tools_tests()`、`RunBuildToolsTests.cmake` 和 `PANGU_BUILD_TOOLS_TEST_*`；具体工具保留 `PanguBuildTool` 等名称。

调用方显式提供工具名称、工程路径和测试组的目标/选择条件，共用接线不把 `PanguBuildTool` 的路径或测试名称写死。测试源码和专用构建目录分别遵循 `Tests/BuildTools/<ToolName>/`、`<BuildRoot>/Tests/BuildTools/<ToolName>/`。本次以已有 PanguBuildTool 的三类测试完成首个实际接入，不为尚不存在的工具建立空工程。

用户已确认将三项正式测试的源码及其 CMake 测试接线迁入根目录：

```text
Tests/BuildTools/PanguBuildTool/
  CMakeLists.txt
  PluginManifestTests.cpp
  PluginDiscoveryTests.cpp
  PluginCliTests.cmake
```

`BuildTools/PanguBuildTool` 继续是独立宿主 CMake 工程。在其 `BUILD_TESTING=ON` 时，通过明确的源码路径和本构建树内的 binary directory 引入上述测试目录；不得进入根 `Tests/CMakeLists.txt`，也不反向依赖 Runtime 工程。Manifest/Discovery 的 C++ 行为迁入 GoogleTest，CLI 脚本保留进程、退出码、stdout/stderr、JSON 和原生路径判据；测试工作目录和依赖定位均显式传递。

工具测试归属 `owner.engine`、领域 `area.build`，按第 11 节登记边界和最低执行层级。独立工具工程可自行配置、构建和运行这些测试，复用同一份辅助接线和固定第三方源码；框架适配可在各独立构建图中配置，不跨构建树共享目标、缓存或产物，也不继承 Android/iOS 目标 Toolchain。

根工程启用 Engine 正确性验证时，配套 build/workflow 在当前宿主 BuildRoot 内准备专用宿主测试构建，路径为 `${CMAKE_BINARY_DIR}/Tests/BuildTools/PanguBuildTool`，显式使用宿主工具链并开启该子工程测试，构建本集合所需的工具和测试目标。该目录与普通 bootstrap 的 `${CMAKE_BINARY_DIR}/BuildTools/PanguBuildTool` 隔离，后者继续保持 `BUILD_TESTING=OFF`，不为普通 configure 或交叉编译 bootstrap 增加测试依赖。构建动作属于验收准备阶段，不放进每个普通测试用例中重复执行。

根运行入口显式选择并执行同一运行层级对应的宿主 CTest 用例，不因它们位于另一构建图而遗漏。GoogleTest 行为用例在宿主 CTest 中仍逐用例登记；从根入口调度时保留内层用例名称、选中集合、日志和 XML，每项场景每轮只执行一次。独立工具工程的命令和用例定向方式写入维护说明。根 full 验收必须实际执行这三类工具测试，将构建、发现、空匹配、运行失败以及关键场景跳过传播到整轮验收状态；仅构建 PanguBuildTool、运行发现 CLI 或通过 SourcePlugin* 回归均不能代替。

## 8. GoogleMock 的使用边界

GoogleMock 用于观察真实接口的交互次数、参数、错误注入和必要时序。优先复用已有虚接口、显式回调和可替换的边界，例如 ScopeBuildContext 已有的 ScopeBuildSink 适配边界。

迁移中选一个现有边界用例使用 GoogleMock，验证错误锁存后不会继续发起不应发生的依赖交互等真实契约。实施者根据已有用例确定精确位置；不为完成框架接入编造生产 API。

需要提供状态模型、对象所有权或真实销毁行为时，保留简单 Fake 和真实组件。Mock 不是测试分层的判据：使用 GoogleMock 不会自动把完整生命周期协作变成单元测试。

不为可测试性向所有生产类添加虚函数，不以宏替换生产符号，不强制全项目模板化。自由函数/静态回调不能自动被 GoogleMock 拦截；只在已有 seam 上适配，新增生产接口须有独立的设计理由。

## 9. 特殊回归、隔离与结果

### 9.1 进程终止和并发

现有 ScopeDestructor 的语义包括特定退出码、ready 标记存在、被借用对象未析构。保留 CTest/CMake 父检查与专用子程序，完整保留这些判据。可以重组源码，但不能简单替换成“子进程死亡即通过”的 death test。

并发测试使用确定的同步点、有界等待和可诊断的失败收集。GoogleTest 致命断言只退出当前函数，放在线程内不代表安全结束整项测试；避免在工作线程依赖致命断言控制流程，先安全 join，再由主测试线程检查结果。失败路径也要能够回收线程和临时状态。

### 9.2 文件、环境和资源

每个 CTest 用例/父检查在当前宿主 BuildRoot 的 `Tests/Work/` 下拥有独立目录；多轮运行的可变夹具按需使用运行实例标识。日志、可变输入和嵌套构建产物都在该工作目录内。只读源码夹具可以按现有规则复制到本宿主测试工作目录；这不允许把整个 Engine 源码复制到另一宿主临时目录构建。

首期约定同一 BuildRoot 同时只有一个验证会话，包含 configure、build、发现和 test；不承诺多个 CTest/workflow 会话共用同一 BuildRoot 的安全性，也不增加跨会话调度平台。单会话内部可以并行运行互相隔离的用例。嵌套构建必须结合外层 CTest 并发配置处理器预算，避免每项固定启动大量编译进程造成并发倍增。

Test executable 的启动目录不依赖源码目录；为依赖相对路径的场景显式传参。每项测试设置所需环境，恢复进程内全局回调和静态夹具状态，不依赖前一个用例。GoogleTest 每用例启动进程不免除其在直接运行整个测试程序时的隔离责任。

单会话中的真实共享资源通过 CTest `RESOURCE_LOCK` / `RESOURCE_GROUPS` 等原生能力按需约束；只有确需整体互斥时才用 `RUN_SERIAL`，这些属性不提供不同 CTest 进程之间的互斥。现有源码变更回归操作的是测试目录内的工具源码副本，核查未发现直接改共享 Engine 源码；迁移仍须逐项确认实际资源和失败恢复路径，不能仅因为目录不同就启用并行。

### 9.3 报告与不稳定测试

- CTest 保存调度结果和失败日志；GoogleTest 使用 `XML_OUTPUT_DIR` 输出用例 XML，避免多个进程写同一 XML 文件。
- 每轮验收记录源码状态、宿主/目标、配置、命令、选中集合、耗时、首轮失败、重跑、隔离与未验证范围。不同运行轮次使用独立结果目录或验收前归档，防止旧 XML 混入当前结论。
- 快速/提交前/完整集合没有自动重跑掩盖失败的规则。暂时隔离需有原因、平台、修复事项和恢复条件，验收显示覆盖缺口。
- `DISABLED_`、CTest `DISABLED` 或跳过都不能被写成已验证通过；完整验收必须说明这些条目。环境或权限导致未执行的分支应尽可能拆成可单独报告的场景，不能只打印跳过日志后把整个场景算作通过。关键迁移场景未执行时，相应平台验收保持未完成。现有维护规范中的保留和替代规则继续有效。

## 10. Google Benchmark

### 10.1 首期范围

独立基准可执行程序直接链接真实生产目标与 Google Benchmark，不进入正确性 CTest 注册集合。因此即使显式开启基准构建，现有桌面 test preset 和不带筛选的原始 CTest 仍只执行正确性回归。

首期必须交付三类代表性基准：ModuleId 解析/格式化、ScopeAccess 稳态借用/释放、StaticModuleHost 对不同规模 Catalog 的创建/依赖处理。每类的参数、计时区间和对象销毁成本需在基准说明中明确；不能混用“含初始化成本”和“稳态成本”结果。使用上游防优化设施，结果要被实际消费。默认关闭构建开关不表示交付可选，不能只接上依赖和空目标就完成本次任务。

Google Benchmark 源码按需初始化后，预期操作为：

```sh
cmake --preset windows -DPANGU_BUILD_BENCHMARKS=ON
cmake --build --preset windows --target PanguModuleBenchmarks
```

还须验收 `BUILD_TESTING=OFF` 且 `PANGU_BUILD_ENGINE_TESTS=OFF` 时单独开启基准的配置，确认没有多余的正确性测试目标或 GoogleTest/GoogleMock 依赖。

基准通过构建目录中的实际可执行程序显式启动，参数指定 JSON 输出和重复次数。README 分平台给出正确的可执行路径/扩展名和创建结果目录的步骤；产物基名保持一致，不包含平台名。关闭开关后恢复普通测试构建，不残留对 Benchmark 依赖的要求。

### 10.2 结果使用

结果写入宿主 BuildRoot 的独立 `BenchmarkResults/<run>/`，保留 JSON 原始测量与环境说明。至少记录主仓库/依赖版本、编译器、标准库、优化配置、CPU/体系结构、宿主、参数和重复次数；框架不自动输出的字段通过附加说明或自定义 context 记录。

首期在 Windows 对三类基准完成正式重复测量并建立基线，正式测量至少重复 5 次，保留中位数和离散程度，说明机器负载。WSL Linux、macOS 本次必须完成这三类基准的编译、最终链接和运行冒烟，正式性能基线以后分别积累。不同 CPU、配置、标准库或执行环境的绝对值不直接判定提交退化。

验收时区分“程序能运行”“测量有效”“性能变化满足某门槛”。一次迭代或 dry run 只能证明运行冒烟，不能形成性能基线。统计显著性、持续对比和性能门禁留待有稳定基线与实际需求后设计。

## 11. 现有测试迁移归属

以下为初始归属，实施者按原用例逐项拆分并保留覆盖清单，不能仅按文件整体改名：

| 当前测试/源码 | 边界或验收方式 | 最低执行层级 | 迁移要求 |
| --- | --- | --- | --- |
| FoundationError / ErrorTests | unit；编译期约束仍随构建检查 | fast | 值、错误传播和 Status 分为独立断言场景 |
| ModuleIdentity | unit；保留布局/constexpr 断言 | fast | 解析、非法值、格式化及往返可分别报告 |
| ScopeAccess | unit，部分为并发回归 | fast；原有竞态场景 precommit | 保留 gate/count/关闭后的行为和原竞态触发条件 |
| ScopeBuild | unit，保留真实 owning model 与必要 Fake | fast | 所有权、类型错误、错误锁存和 move-only factory 分开报告；按需引入 Mock |
| StaticModuleHost / Registration | integration | fast | 保留真实注册、元数据生命周期、依赖错误和回滚；实测后细化分组 |
| ScopeLifecycle 普通场景 | integration | fast | 核心关闭、回滚、跨 Scope 隔离和销毁顺序不降频 |
| ScopeDestructor | integration + requires.subprocess | precommit | 保留退出码 91、ready 标记和未析构判据 |
| StaticModuleCatalog | integration + 链接契约 | precommit | 保留独立可执行目标、两座夹具库、链接裁剪与生成 Catalog |
| JsonExtras | unit + 第三方配置契约 | fast | 断言分解，不扩展为上游库的完整测试套件 |
| Toolchain 编译型探针 | 构建验收 | 随所选构建执行 | 保持真实 STATIC 库与 Pangu/ThirdParty 策略差异 |
| ToolchainLibraryLink | integration + 最终链接契约 | precommit | 保持最小测试程序，登记统一标签即可 |
| BuildEnvironment | system | precommit | 保留真实错误环境的子进程验证 |
| BuildTool PluginManifest | unit + Manifest 契约 | fast | 迁入 `Tests/BuildTools/PanguBuildTool/`，拆分有效/非法 TOML、ID、SemVer、错误路径及缺失输入场景，沿用独立宿主测试构建 |
| BuildTool PluginDiscovery | integration + 文件系统/双根发现契约 | precommit | 同目录迁移，保留真实目录、排序、未启用 Plugin 校验、重复 ID、边界逃逸及符号链接场景，跳过显式报告 |
| BuildTool PluginCli | system + requires.subprocess | precommit | 同目录迁移，保留真实 CLI、JSON/诊断/退出码、Unicode 原生路径及空根/非法输入行为 |
| SourcePlugin* 与独立 Project 回归 | system + requires.nested-build | full | 保留配置、增量失效、修复重试、Catalog 和部署的原验证条件 |
| 移动端最终链接冒烟 | 显式构建/链接专项 | 显式调用 | 不注册为本机运行测试，不计为设备运行覆盖 |
| CrossCompileTests 独立 Project 回归 | 移动端配置/最终链接/部署专项 | 显式调用 | 保留并验收 Android/iOS 的 Game、Editor 最终链接、SHARED 部署和宿主工具运行；不计为设备运行 |

GoogleTest 的 suite/test 名称采用清晰的行为名称并保持稳定；CTest 前缀区分目标与领域。用户允许调整旧名称，不保留会重复执行的兼容测试入口。原测试名到新用例或领域筛选的映射写入迁移记录。更新依赖旧测试名和总数量的脚本及文档，尤其是 SourcePluginProject 的 EngineOptIn 验证：显式构建需要发现的 Engine 测试目标，再检查 Engine 自有用例及其归属；不能把尚未构建的发现占位条目当成有效覆盖，也不能因 GoogleTest 拆成多个用例继续断言旧的固定总数。

## 12. 平台与嵌入 Project

- Windows：使用根 windows preset 与 `Build/Windows`，完整验证新接线与全部原正确性覆盖。
- WSL Linux：从 `/home/moozen/WindowsGit/PanguEngine` 执行根 linux preset，产物写 `$HOME/PanguEngineBuild/Linux`。
- macOS：通过 `ssh -o BatchMode=yes moozen-macos`，从 `/Users/moozen/WindowsGit/PanguEngine` 执行根 macos preset，产物写 `$HOME/PanguEngineBuild/macOS`。
- Android/iOS：保持现有默认关闭运行测试的策略；显式开启 Toolchain 验证时只构建支持的编译目标，最终链接冒烟独立验收。缺少设备运行器时不尝试发现或启动交叉编译的 GoogleTest 程序。
- 嵌入 Project：父工程继续决定是否启用 CTest；Engine 内部测试默认关闭。Project 可独立复用接线，无需开启 Engine 测试。显式开启 Engine 测试时，测试工作目录、资源定位和目标名称均不得误用父工程的源码根，也不得改变父工程已有 test preset 的筛选语义。

三桌面平台的新测试接线编译、链接和运行全部通过是本次迁移完成的门槛，允许分阶段提交；缺少任一平台的必要验收时保留未完成状态。移动端除原 Toolchain 与 LinkSmoke 外，还须显式验收现有 CrossCompileTests 的独立 Project 配置、Game/Editor 最终链接、SHARED 部署及宿主工具运行。实际未完成的项目如实记录，不预先写成通过。

### Plugin 归属、启用和消费

| 测试定义位置 | 归属 | 正确性测试启用条件 |
| --- | --- | --- |
| Engine 核心与 Engine 根下 Plugin 的自带测试 | `owner.engine` | `BUILD_TESTING AND PANGU_BUILD_ENGINE_TESTS`；Plugin 还须在本配置中显式启用 |
| Project 本体与 Project 根下 Plugin 的自带测试 | `owner.project` | 顶层 Project 的测试策略，至少满足 `BUILD_TESTING`；Plugin 还须显式启用 |
| Project 自己编写的、与 Engine Plugin 协作的集成测试 | `owner.project` | 顶层 Project 的测试策略及所依赖 Plugin 已启用 |

归属随测试定义的所属工程，执行由当前顶层 Engine/Project 统一调度。Project 启用 Engine 自带 Plugin 不会把它自带的测试改成 Project 归属：关闭 Engine 测试时不登记这些自带测试，但 Project 自己的集成测试仍可验证该 Plugin。Plugin 没有独立的 owner 类别、测试开关、CTest 初始化或 workflow。

当前 Plugin 发现/JSON 只保存来源目录等字段，尚无显式测试归属上下文。实施时根据两根发现来源在接线中明确传递 Engine/Project 上下文，在进入 Plugin 子目录前初始化相应测试选项；不因当前调用栈来自 Project 就把 Engine Plugin 错归到 Project，不新增 Plugin Manifest 字段。允许采用普通 CMake 作用域/目录属性表达上下文，具体参数留给实施者。

只为当前配置显式启用且所属方允许测试的 Plugin 登记测试。测试目标是生产 Module 的真实消费者，配套测试构建通过明确的 target 依赖拉入所需 Module，即使 Plugin 目录使用 `EXCLUDE_FROM_ALL` 也必须能够构建该集合；不能为此改变普通 Game/Editor 构建的按需消费语义。未启用的 Plugin 不产生测试目标；full 记录启用的 Plugin 集合，不宣称覆盖了所有可发现 Plugin。

接线验收使用现有最小 Engine/Project Plugin 夹具验证来源继承、开关和显式消费者行为，不预建通用 Plugin 测试管理器。

## 13. 迁移顺序与验收标准

这是设计中的依赖顺序，具体任务和提交粒度由审批后的实施计划确定：

1. 建立现有测试映射与基线，记录当前源码状态、测试名、原场景、构建/运行条件和已有结果；不混用不同版本的通过统计。
2. 固定接入 GoogleTest/GoogleMock，建立元数据接线和单用例发现；先迁移 Foundation/Identity 验证组织方式。
3. 迁移 Module/JSON，并将构建工具三项正式测试集中迁入根 Tests、接入独立宿主测试构建；保留特殊进程和真实链接夹具，同步受测试数量/名称影响的嵌入回归。
4. 为全部脚本/最小程序补齐分类，落地累计集合和配套 build/test/workflow 入口；验证 Project/Plugin 复用与归属，测量快速和提交前集合，归档未匹配、跳过和隔离情况。
5. 必须交付 Google Benchmark 和三类真实基准；完成 Windows 正式重复测量基线及 Linux/macOS 运行冒烟，验证独立开关。
6. 完成目标平台验收，同步长期规范与维护说明，归档必要结论和遗留问题。

完成条件：

- 每个原有有效场景均有新用例映射或原样保留的入口；替代依据包括关键断言和特殊失败条件。
- 所有普通正确性 CTest 条目均有边界/领域/频率归属，所有 fast 都属于 precommit，所有 precommit 都属于 full；空筛选和发现失败均不会通过。
- 可分别执行单个 C++ 行为用例、单个领域、快速集合和完整集合，故意触发的断言失败能够定位并产生非零结果；验证用临时故障不提交。
- Static Catalog 和 Toolchain 的真实编译/链接条件不被框架接入改变；特殊析构用例保留完整判据。
- Engine standalone / Project 嵌入的开关组合正确；Engine/Project Plugin 自带测试归属和启用条件正确，Project 协作测试归属不混淆。关闭正确性测试仍可单独构建基准；两者都关闭时无对应的多余依赖/注册，不发生配置联网。
- 在单 BuildRoot 单验证会话约定下，并行运行没有工作目录、XML 或全局资源冲突，嵌套构建有整体并发预算；直接运行整个 GoogleTest 程序也不依赖用例顺序。
- workflow 完成有效配置、所选测试目标/生产依赖的构建及测试运行；正式 Engine full 验收包含已开启的 Toolchain 探针和最终链接检查。不能用旧产物的 CTest 结果替代当前源码的构建验收。
- PluginManifest、PluginDiscovery、PluginCli 三类原构建工具场景均纳入迁移映射，并在三桌面平台由根 full 入口执行、汇总结果；独立宿主工具仍可单独验证。普通 bootstrap 保持测试关闭，测试专用构建与其缓存/产物隔离，不因源码集中到根 Tests 而改变宿主/交叉目标边界。
- Windows、WSL Linux、macOS 必须全部完成新正确性接线的构建和运行验收；Android/iOS 必须保留并执行现有编译/最终链接和独立 Project 回归。首轮失败、跳过及遗漏分别记录，关键场景缺失或必要平台验收未完成时不得宣布迁移完成。
- 快速预算以主开发 Windows 宿主稳定运行的实测为初始依据，其他宿主分别记录；预算是优化目标，未达标时调查准备/进程成本并调整编排，不减少有效覆盖。
- GoogleMock 有真实交互契约覆盖；Benchmark 独立开关有效，三类基准完成三桌面运行冒烟和 Windows 正式基线，计时范围及测量结果可追溯，不能只以 dry run 代替 Windows 测量验收。

分类接线和开关属于本次新增行为，需要针对性配置/CTest 清单校验；测试结果本身不再套一套镜像断言测试。自有 C++ 改动按仓库规范运行定向 clang-format、clang-tidy、构建和 CTest。

若迁移暴露真实生产缺陷，允许必要的最小修复，以独立提交保留原触发场景、归因和修复前后证据；区分迁移引入的问题、已有生产缺陷和错误测试预期，不通过弱化断言统一处理。冻结契约变化或明显扩大功能范围时须重新讨论设计。

## 14. 文档权威与留给实施者的事项

审批并实施后，将长期测试架构整理到 `wiki/foundation/testing.md`，在 wiki 索引登记；`Tests/README.md` 继续负责测试维护、运行命令与验收记录，并链接架构规范。必要时更新 build-system/third-party 文档，避免多个文件重复维护同一分类规则。本规格保留设计选择、范围和验收边界，不提前修改当前已实现状态。

实施者可以自行确定：确切上游稳定版本、辅助函数的参数拼写、具体目标拆分、GoogleTest suite/test 名称、单用例超时、基准参数和少量重复代码的抽取。选择必须遵守本文的可观察行为和仓库规范，并在实施计划中落实。

需要重新讨论的范围变化包括：引入动态测试库 Runner、改变生产 API 以供 Mock 使用、强制性能门禁、建设设备运行环境、删减原正式回归或以新构建系统替代现有 CMake/Ninja 流程。

## 15. 参考依据

- [O3DE 官方测试入门](https://www.docs.o3de.org/docs/user-guide/testing/getting-started/)：CTest 调度、Main/Smoke 套件、C++ 单元测试与系统测试、性能测量的职责。
- 本地 O3DE `D:/Git/o3de/Code/Framework/AzTest/CMakeLists.txt`、`AzTest/AzTest.h`、`3rdParty/Findgoogletest.cmake` 和 `cmake/LYTestWrappers.cmake`：独立测试目标、GTest/GMock、基准入口、资源/套件及输出接线。仅引用本次读取的工作区实现，不把它视为上游当前版本；其 FetchContent 获取方式不符合 Pangu 配置期离线约束，故不照搬。
- [CMake 4.0 GoogleTest 模块](https://cmake.org/cmake/help/v4.0/module/GoogleTest.html)：逐用例发现、PRE_TEST、测试属性、XML_OUTPUT_DIR 和交叉执行限制。
- [CMake 4.0 Presets](https://cmake.org/cmake/help/v4.0/manual/cmake-presets.7.html)：原生 workflow 的 configure/build/test 顺序、共享 configure preset、build targets 和无测试匹配处理。
- [GoogleMock Cookbook](https://google.github.io/googletest/gmock_cook_book.html)：依赖注入、非虚方法和自由函数替换的实际边界。
- [Google Benchmark User Guide](https://github.com/google/benchmark/blob/main/docs/user_guide.md)：JSON 输出、重复测量、防优化设施与 dry run 的限制。具体可用选项以最终固定版本为准。

外部官方资料于 2026-11-05 通过 Firecrawl 核对；上游指南描述框架能力，本文中的层次、预算和适配选择属于 Pangu 设计建议。

## 16. Grilling 决策记录

以下问题经用户确认，Q14 按用户提出的 Plugin 所属工程规则修订，Q17/Q18 明确其边界。确认设计决策不表示实现或平台验收已经完成。

| 问题 | 已确认结论 |
| --- | --- |
| Q1 全部迁移的含义 | 所有现有有效场景纳入新分类和验收；保留适合编译、工程和进程检查的形式 |
| Q2 运行方式 | 首期使用独立测试可执行程序，不实现动态加载测试库的 TestRunner |
| Q3 集合语义 | fast 是 precommit 的子集，precommit 是 full 的子集；full 包含本配置全部普通正确性用例 |
| Q4 Benchmark 交付 | 三类代表性基准及有效测量必须交付，日常构建开关默认关闭 |
| Q5 完成门槛 | 三桌面平台正确性构建与运行全部验收后才算迁移完成，可分阶段提交 |
| Q6 构建与运行 | 提供配套 build/test preset 和原生 workflow；正式 full 包含必要构建及 Toolchain 检查 |
| Q7 跳过 | 显式报告验收缺口，关键场景未执行时相应平台保持未完成 |
| Q8 并发范围 | 同一 BuildRoot 只有一个验证会话，会话内可并行并控制嵌套构建总并发 |
| Q9 时间预算 | fast 60 秒、precommit 5 分钟是测试执行优化目标，构建另记，不作为迁移硬门槛 |
| Q10 运行粒度 | 首期每个 GoogleTest 用例独立注册并启动进程，同时保留程序直接批量运行能力 |
| Q11 基准开关 | 独立于正确性测试；仅启用基准不因本测试接线引入 GoogleTest/GoogleMock |
| Q12 移动端保留范围 | 包含现有 Toolchain、LinkSmoke 及独立 Project 交叉编译、最终链接、部署入口 |
| Q13 性能基线 | Windows 完成正式重复测量；Linux/macOS 本次完成基准编译、链接、运行冒烟 |
| Q14 接线复用与归属 | Engine/Project/Plugin 可按需复用函数；Plugin 跟随所属 Engine 或 Project，不建立独立归属 |
| Q15 名称兼容 | 允许调整旧名称，同步引用并提供映射，不建立重复执行的兼容入口 |
| Q16 生产缺陷 | 允许独立提交必要的最小修复及回归证据，重大范围/冻结契约变化重新讨论 |
| Q17 Engine Plugin 被 Project 消费 | Engine Plugin 自带测试仍随 Engine 开关；Project 自己的协作测试随 Project |
| Q18 Plugin 启用与测试消费 | 只登记本配置显式启用且所属方允许的 Plugin 测试；测试目标显式消费所需生产 Module |

正式 spec 评审后，用户另行确认：构建工具三项正式测试集中迁入 `Tests/BuildTools/PanguBuildTool/`，保留独立宿主构建及单独运行能力，根验收入口负责调度并汇总，普通工具 bootstrap 继续关闭测试。该决定补齐原评审发现的迁移及 full 覆盖遗漏，具体执行契约见第 7.5 节。
