# PanguEngine 源码插件与独立 Project 构建设计

> 状态：Draft（Grill Me Q1–Q41 设计取舍已确认；待正式 ADR、实现与验收）
> 日期：2026-11-02
> 更新：2026-11-04
> 关联：[Plugin / Module 架构](../../../wiki/core/plugin-module.md)、[构建系统](../../../wiki/foundation/build-system.md)、[ADR-0013](../../../wiki/decisions/0013-native-build-abi-boundary.md)
> 实现状态：本篇规定目标方案，示例中的新增接口尚未实现；不得将设计确认视为已通过五平台构建或运行时验收。

## 1. 背景、目标与非目标

PanguEngine 采用**源码 Plugin**：Engine 或独立 Game Project 持有插件源码，使用各目标平台已选择的 Clang/C++23 Toolchain，在 Project 所属的**单一 CMake 4.0+ / Ninja 构建图**中创建 Module Target。配置期发现插件源码与 Manifest；不在运行时扫描未知第三方 DLL 来发现可安装的原生 C++ 插件。Engine 本身始终可独立配置、编译与测试，不要求存在 Game Project。

核心目标：

- Engine、Project 分别持有 `Plugins/`；Plugin ID 稳定且全局唯一，插件目录移动或改名不影响启用配置。
- 发现与启用分离，启用与实际编译、部署、运行时激活进一步分离；**只有实际消费者的 Target 依赖才驱动构建**。
- 保留单 Module 单 `STATIC`/`SHARED` 库、`Pangu::`/`ThirdParty::` Target 约定、STATIC Catalog、SHARED C++23 Query/CodePin 与生命周期规则。
- CMake/Ninja 是唯一构建内核和编译依赖图；构建工具仅为真实消费需求服务，普通增量编译不运行逐 TU ABI 证明。

明确不做：独立交付、供已有宿主接纳的 Pangu 原生 C++ Plugin/SDK 二进制工作流；`AbiProfile`、`config-v2`、Fingerprint、可信文件绑定与该类插件的 OS Load 前 ABI Preflight；通用 Plugin 构建器、第二构建内核、Plugin 级依赖求解器、强制 `Module.toml`、任意路径的运行时二进制插件安装。系统 SDK/工具链运行库和按现有 ThirdParty 规范接入的外部库不因本政策被要求提供源码。

## 2. 主要架构选择

| 主题 | 采用 | 不采用及原因 |
|---|---|---|
| 插件选择 | 自动发现、显式启用、消费者驱动构建 | 发现即全部编译或启用即自动进入 `ALL`，增加编译成本 |
| 插件身份 | `Plugin.toml` 中声明的全局唯一稳定 ID | `Engine/<Folder>`、`Project/<Folder>` 路径身份，目录迁移会破坏引用 |
| 构建依赖 | CMake 原生 Target 与 Ninja 增量图 | Plugin Manifest 再维护依赖 DAG、自动启用依赖或逐 TU 收据 |
| 工具 | 独立且最小的 `PanguBuildTool`，使用 `toml++` | 重新实现 PanguBuild、共享 Bootstrap/复杂发布缓存 |
| 工程所有权 | 独立 Game Project 拥有顶层 CMake，Engine 也可单独作为顶层 | 将 Game 放入 Engine 的 `Projects/` 或以预编译 Pangu SDK 取代引擎源码 |
| 运行时 | Host 管理静态 Catalog 与显式 SHARED 加载 | 扫描目录中的 DLL 即视作获准执行 |
| 交付 | 显式的部署/安装目标与应用侧选择 | 通用 `POST_BUILD` 自动复制所有插件 |

## 3. 仓库、顶层初始化与构建树

下列为**规划中的目录示意**，不是当前仓库已经存在的插件实现：

```text
PanguEngine/                          # 独立 Engine 仓库
  CMakeLists.txt                       # Engine standalone 顶层入口
  CMakePresets.json
  Source/Runtime/
  Source/ThirdParty/
  BuildTools/PanguBuildTool/           # 独立构建的配置期工具
  Plugins/
    Physics/
      Plugin.toml
      CMakeLists.txt

MyGame/                               # 独立 Project 仓库
  CMakeLists.txt                       # Game/Editor 的唯一顶层入口
  CMakePresets.json
  Source/
  Plugins/
    Gameplay/
      Plugin.toml
      CMakeLists.txt
    EditorExtras/
      Plugin.toml
      CMakeLists.txt
```

**顶层工程负责初始化**：首次 `project(... LANGUAGES CXX)` 之前由 Project Preset/Toolchain 选定编译器、标准库、SDK，并调用 Engine 提供的可复用环境预检；顶层统一控制测试开关与构建配置。Engine 在 Project 中通过 `add_subdirectory(<EngineSource> <ProjectBinaryDir>/Engine)` 纳入同一 CMake Target/Ninja 图。CMake 子目录调用 `project(PanguEngine ...)` 本身可以合法存在，但不得重新选择编译器、重新初始化全局构建选项或重复 `include(CTest)`。平台规则和 `PanguBuildOptions` 只在同一构建图中初始化一次；必要的 Engine 平台校验仍要执行。

**没有 Game Project 时，Engine 自身是顶层工程**，继续支持 `cmake --preset windows|linux|macos|android-arm64|ios-arm64` 及现有 Engine CTest 行为。Engine standalone 默认不启用 Plugin；开发者可以显式设定启用 ID。Project 统一配置 `PANGU_ENABLED_PLUGINS`，Game/Editor 首版共享这个构建配置级集合，但各自只构建实际依赖的 Target；无需为 Game 和 Editor 强制复制两棵构建树。

Project 通过待实现的 `PANGU_ENGINE_SOURCE_DIR` 指向本地 Engine checkout；CMake 配置前校验该路径与预期源码结构，不复制 Engine，也不通过 `find_package()` 接入预编译 Pangu SDK。Project 记录预期 Engine Git Commit：**正式构建/CI 必须核对 Commit 且确认 Engine checkout 干净**；本地联调可以显式覆盖锁定校验并显示版本差异，不允许静默忽略。CMake Configure 不联网、不更新 Submodule、不自动下载 Engine。

每个独立顶层工程拥有自己的 BuildRoot、CMake Cache 与 Ninja 图，禁止与 Engine standalone 或其它 Project 共用可写输出：Windows 可用 `MyGame/Build/Windows`，Linux 可用 `$HOME/PanguEngineBuild/Projects/MyGame/Linux`，macOS 可用 `$HOME/PanguEngineBuild/Projects/MyGame/macOS`；Android/iOS 沿用各自宿主本地文件系统与隔离规则。复用既有五平台 Toolchain 策略，不因 Project 接入修改 Engine 现行 Preset。

`BUILD_TESTING` 由顶层决定；Engine 内部 Tests 另受独立开关控制：Engine standalone 默认保持既有测试行为，Game Project 默认不加入 Engine 内部 Tests，但仍可独立启用 Project Tests。Tests 不经过 AbiProfile。

## 4. Plugin.toml、稳定身份与源码发现

### 4.1 必须存在的文件与最小 Schema

每个插件目录必须**同时**包含 `Plugin.toml` 与 `CMakeLists.txt`，包括只有 Assets、没有任何 C++ Module 的插件。前者只定义 Plugin 的稳定身份/基本元数据；后者仍是 CMake Target、源文件、编译和链接依赖的唯一权威。不要求 `Module.toml`，也不把 Manifest 变成另一套构建描述语言。

`Plugin.toml` 首版格式：

```toml
[plugin]
id = "pangu.physics"
name = "Physics"
version = "1.0.0"
```

- `plugin.id`：**必填字符串、全局唯一、稳定且区分大小写的受限小写标识符**；v1 校验语法固定为 `^[a-z][a-z0-9]*(?:-[a-z0-9]+)*(?:[.][a-z][a-z0-9]*(?:-[a-z0-9]+)*)*$`。禁止空段、路径分隔符、`..`、空格或大写字母；ID 不从目录名生成。
- `plugin.name`：可选、用于展示的字符串，允许中文，不参与唯一性判定。
- `plugin.version`：可选字符串；填写时必须符合 SemVer 2.0.0（包括合法的预发布和构建元数据），仅供诊断、显示和未来版本管理参考。首版**不做版本约束求解**。
- 未知 Section/字段、字段类型错误、损坏的 TOML 或非法 ID 均为配置错误；Schema 扩展必须后续显式修订，不悄悄接受拼写错误。

Engine 和 Project 使用**一个 Plugin ID 命名空间**。例如 `pangu.physics` 可以从 Engine 移至 Project 而不改启用配置；若两个目录声明相同 ID，配置立即失败，Project 不自动覆盖 Engine。Plugin ID 与运行时 `ModuleId`、真实 CMake Target 名各自独立，不能通过改写 Plugin ID 自动修改 ModuleId。

### 4.2 两个发现根与错误边界

- Engine standalone：仅发现仓库根 `PanguEngine/Plugins/`。
- Game Project 顶层：发现 `${PANGU_ENGINE_SOURCE_DIR}/Plugins/` 和 `${CMAKE_SOURCE_DIR}/Plugins/`；同一实际目录不得重复发现。不支持其它任意插件搜索根或在配置期联网获取插件。
- 每个根只检查**直接子目录**，不递归扫描代码文件。目录中只出现 `Plugin.toml` 或 `CMakeLists.txt` 任意一个即视为不完整插件，必须报错；二者均不存在的普通目录可以忽略。
- 对目录与文件做规范化路径校验：拒绝逃逸发现根、重复映射、符号链接暗中指向其它插件仓库和异常文件类型。与文件系统大小写有关的碰撞也应给出清晰诊断。
- **所有候选插件**都要解析并验证 Manifest，包括未启用者；任意 Manifest 错误或重复 ID 均使本次 Configure 失败。未启用插件的 `CMakeLists.txt` 则绝不执行。

Configure 时扫描候选并建立 ID → 源码目录映射，按稳定 ID 排序。对已发现目录中的 `Plugin.toml` 与 `CMakeLists.txt` 都登记 CMake 的 `CMAKE_CONFIGURE_DEPENDS`：内容变化或删除均触发重新 Configure，未启用插件也不能因删除其文件而静默逃过验证；`PanguBuildTool` 的 CMakeLists、源文件和直接输入同样必须纳入触发集合，确保变更工具实现时能自动重配和重编。**新建插件目录**在下次显式 `cmake --preset ...` 后被发现。禁止 `GLOB_RECURSE`、`GLOB CONFIGURE_DEPENDS` 及每次无改动 Ninja 构建的目录递归扫描。一般 `.cpp`/头文件增量依旧完全由 CMake/Ninja 的正常规则处理。

## 5. PanguBuildTool：配置期 Manifest 解析

`PanguBuildTool` 是 **Build Tool（不是 Host Tool 这个架构名称）**，首版只负责发现、解析和验证 Plugin Manifest、输出稳定的身份映射。使用固定版本的开源 `toml++` 解析 TOML（按 ThirdParty 版本与许可规范接入），以 `TOML_EXCEPTIONS=0` 的无异常模式读取显式解析结果并处理错误；工具本身遵守 C++23、Clang 与无异常规范。`toml++` 只作为构建工具的私有依赖，不传播到 Runtime Module 公共接口；不引入新的 Module 描述文件或 ABI 采集。PanguMetaTool、Shader Compiler、Asset Cooker 等后续仍作为各自独立、按需触发的 Build Tool，不并入一个万能工具。

其启动边界：

1. 源码位于独立的 `BuildTools/PanguBuildTool/` 最小 CMake 工程；由顶层 Configure 显式调用独立配置/构建，仅构建这个小工具。**工具以当前构建宿主的 Clang/C++23 编译**，Android/iOS 交叉构建分别运行 Windows/macOS 宿主程序，绝不继承 Android/iOS 目标编译参数。缺失宿主工具链时给出错误，不以目标平台二进制代替。特别是 Windows 宿主的 Android 构建：当前目标平台验证走 NDK Toolchain，不等于宿主 Clang 已可用；即使没有任何 Plugin，也必须验证可运行的 Windows Clang/C++23 以构建该工具。宿主工具链缺失时应清楚报错，不得回退到 Android 目标编译器或跳过工具。
2. 每个顶层 BuildRoot 自己维护此工具的独立 CMake/Ninja 缓存与可执行产物；不跨 Engine/Project 或宿主共享可写缓存，不引入发布锁、全局版本目录或自制哈希/时间戳失效规则。
3. CMake/Ninja 根据工具源码、头、构建配置与 `toml++` 依赖判定是否重编。**每次顶层 Configure 都必须配置并请求构建 `PanguBuildTool`，即使没有任何 Plugin、启用 ID 集合为空**；首次配置实际编译工具，后续由独立 CMake/Ninja 图判定无变化时不重复编译/链接。工具随后扫描固定发现根；没有 Plugin 时返回合法的空 JSON 结果，不跳过解析流程。工具构建失败则整体 Configure 失败，绝不继续使用陈旧程序。顶层 CMake 应明确跟踪工具 CMakeLists、源码和必要的配置输入，避免工具变更后仅在开发者手动 Configure 时才重新构建；新增源码拓扑时显式重新 Configure。
4. 工具一次扫描适用的两个（或 standalone 一个）发现根，仅向 stdout 输出结构明确、顺序稳定的 **JSON**，至少包含 Plugin ID、规范化目录与 Manifest 路径，以及可选元数据；诊断输出到 stderr，错误用非零退出码。CMake 使用内置 `string(JSON)` 读取结果，校验退出码和 JSON 结构；**不得生成并 `include()` 执行来自 Manifest 的 CMake 脚本**。
5. 没有永久 Plugin 索引，也不逐文件保存 ABI/编译收据。PanguBuildTool 的变化不能单独使无关 Engine Module 重新编译；无改动 `ninja` 不能额外执行工具或编译任务。

本设计将 **配置期 `PanguBuildTool` 构建与执行**作为 Engine standalone 和 Project 构建的统一固定步骤，不以 Plugin 数量为开关；仍只建立针对该工具的最小启动链，不恢复退役 AbiProfile 的通用 Bootstrap、Compiler Launcher 或多平台共享缓存。首次编译工具带来的 Configure 成本必须单独测量，但不能为省去该成本而跳过构建。

## 6. 显式启用与 CMake 按需构建

### 6.1 启用集合、构建依赖与接线

`PANGU_ENABLED_PLUGINS` 按**稳定 Plugin ID**选择候选项，不再使用 `Engine/Physics` 或 `Project/Gameplay` 这样的目录身份。例如：

```cmake
set(PANGU_ENABLED_PLUGINS
    pangu.physics
    mygame.gameplay
)
```

启用项必须存在且不得重复；未启用项的 Manifest 仍须合法，但其 `CMakeLists.txt` **不执行**、不创建 Target/生成代码/资产命令。初版只允许 Engine 与 Project 两个固定源码根，不自动启用缺失的依赖插件，也不维护 Plugin 级拓扑排序或版本求解。插件作者继续通过 `add_library()`、`target_link_libraries()` 等原生 CMake 命令声明真实 Target 及依赖。

按 Plugin ID 稳定排序，对启用目录使用 `add_subdirectory(... EXCLUDE_FROM_ALL)`，每个插件使用唯一 BinaryDir。**启用只表示其 CMake Target 可被消费，不代表进入默认 `ALL`**：Game/Editor/Tool/显式部署 Target 的链接与构建依赖才决定哪些插件实际编译。Plugin 不能通过无关的全局 `ALL`、通用 `POST_BUILD` 或配置时副作用绕过此约束。

- STATIC Module 由消费者的 `target_link_libraries()` 驱动，显式 Catalog 保留可达根；一个 Module 仍只对应一个实际 `STATIC`/`SHARED` Library Target。
- 通过路径动态加载的 SHARED Module 没有常规链接依赖时，消费者或部署目标必须用明确的 Target 构建依赖保证 DLL/.so/.dylib 已生成；**仅把 ID 写进启用列表不产生此依赖**。
- 不规定插件之间的 CMakeLists 执行顺序必须满足依赖拓扑。允许使用 `Pangu::Physics` 等目标名称表达稍后定义的依赖，在 CMake Generate 阶段检查；不要求配置期 `if(TARGET ...)` 依赖提前存在。若依赖未启用导致目标缺失，应明确失败，不偷偷启动其它 Plugin。
- CMake Target 名和 ModuleId 仍必须各自唯一；插件目录/ID 不隐式修改目标的真实名称。普通 `ThirdParty::` 目标仍由第三方源码/上游 CMake 描述，不变成 Pangu Module。

`EXCLUDE_FROM_ALL` 也会影响目录中原生的安装规则，因此不能依赖子插件的 `install()` 自动完成部署；具体可交付产物由显式部署/安装目标管理。

### 6.2 Project 入口示例（设计提案，非现有 API）

```cmake
cmake_minimum_required(VERSION 4.0)

set(PANGU_ENGINE_SOURCE_DIR "" CACHE PATH "PanguEngine source checkout")
if(NOT EXISTS "${PANGU_ENGINE_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "PANGU_ENGINE_SOURCE_DIR must reference a PanguEngine source checkout")
endif()

# 顶层在 project() 前调用共用预检；编译器/SDK 由 Project Preset 选定。
include("${PANGU_ENGINE_SOURCE_DIR}/cmake/ValidateBuildEnvironment.cmake")
project(MyGame LANGUAGES CXX)

set(PANGU_ENABLED_PLUGINS pangu.physics mygame.gameplay)
add_subdirectory("${PANGU_ENGINE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/Engine")

# 新函数暂未实现；Engine 被纳入后可供 Project 使用。
pangu_add_enabled_plugins(
    ENGINE_ROOT "${PANGU_ENGINE_SOURCE_DIR}/Plugins"
    PROJECT_ROOT "${CMAKE_SOURCE_DIR}/Plugins"
    ENABLED ${PANGU_ENABLED_PLUGINS}
)
add_subdirectory(Source)
```

上述示例不完整表达 Git Commit 锁定、Toolchain Preset、测试选项和消费者链接/部署目标，它们仍须按本设计添加。Engine 作为子工程时只配置 Engine 核心 Targets，不自行重复发现/接入插件；Engine standalone 自己调用同一个薄发现接线函数，仅传 Engine 根。实际函数名 `pangu_add_enabled_plugins`、选项名 `PANGU_ENGINE_SOURCE_DIR`、内部输出布局都属于尚未实现的 API 提案；CMake 只负责 Toolchain/Target 接线与调用 `PanguBuildTool`，不再实现一套依赖引擎。

### 6.3 编译、部署与激活的四个边界

| 阶段 | 判定来源 | 不自动导致 |
|---|---|---|
| **Discovered** | 固定根 + `Plugin.toml` 有效 | Enabled |
| **Enabled** | 构建配置的 `PANGU_ENABLED_PLUGINS` | Built / `ALL` |
| **Built** | 真实消费者 CMake Target 依赖 | Deployed |
| **Deployed** | 显式应用部署/安装目标 | Runtime Activated |

SHARED 部署通过 CMake Target 关系及 `$<TARGET_FILE:...>` 等原生生成器表达式取得实际产物路径，不硬编码平台扩展名；不能把通用 `POST_BUILD` 复制挂在每个 Module 上。Game 与 Editor 可部署不同的运行集合；没有被某个应用需要的 Editor Plugin 不应成为 Game 的额外编译或拷贝成本。

## 7. STATIC/SHARED Module、Assets 与运行时边界

**Plugin 是能力集合，Module 是引擎逻辑代码单元**：一个 Plugin 可含多个 Module，也可只有 Assets。纯 Assets Plugin 的 `CMakeLists.txt` 不需要创建 Module Library；资产处理由真实消费 Target 或显式命令触发，不因插件被发现/启用就进入 `ALL`。

- **STATIC**：每个 Game/Editor Host 维护自身 `pg::module::StaticModuleCatalog`，精确引用其静态装配集合。首版最小 fixture 可以显式手写 Catalog；后续由 PanguMetaTool 生成，并把生成代码附着到既有 Module/Host 消费 Target，不在 CMake 插件发现阶段生成全局 Catalog，也不依赖静态初始化顺序。
- **SHARED**：EngineHost/ModuleManager 仅加载其运行时配置明确指定的 Module 路径，不按目录扫描所有动态库并自动赋予执行权限。部署可以生成宿主所需的路径清单，但 **`Plugin.toml` 首版不是运行时加载/激活配置**，不要求在运行时解析源码 Manifest。
- **生命周期**：STATIC 与 SHARED 延续现有 `ModuleDefinition`/Registration 行为；SHARED 使用版本化导出符号定位原生 C++23 Query，继续执行入口、ModuleId、依赖、注册回滚、CodePin 与安全卸载检查。STATIC 链入可卸载 DLL 时仍按真实代码承载体实施驻留保护；不引入独立 BinaryImage 层。

运行时插件启停清单、安装包格式、PanguMetaTool 生成协议、完整 Hot Reload 与移动平台装载策略尚未在本 Spec 中冻结。构建期 Manifest 不得成为第二份源码编译清单。

## 8. ABI 边界与现有规范的迁移

本设计**提出正式停止支持**供已有宿主单独接纳的独立 C++ Plugin/SDK 二进制工作流，因此 ADR-0013 对该特定交付边界所保留的未来 `config-v2`、产物 Digest/可信绑定、TOCTOU 与独立插件 Preflight 计划不再属于目标产品路线。第三方预编译 SDK/系统库仍按既有 ThirdParty/Toolchain 规则独立审查，不能把“Pangu 插件源码交付”推广为禁止一切平台二进制依赖。

与同一受控构建图、同一配置共同编译和配套交付的 Pangu STATIC/SHARED Module **不运行 AbiProfile 或独立二进制 Preflight**，也不以稳定 C ABI 取代统一 C++23 Module Query；此项豁免不取消正常运行时接口有效性与安全卸载检查。

这改变了现行 Accepted ADR-0013 的独立交付适用范围。**本 Spec 仍为 Draft，不能直接宣称既有 ADR 已被修改**。正式冻结时新增替代交付范围的 ADR，并同步调整 ADR-0010、ADR-0013 的状态/引用，及 `wiki/core/{plugin-module,module-api,module-registration-lifetime}.md`、`wiki/foundation/{abi,build-system,third-party}.md`、必要的 EngineHost 与 `BuildTools/README.md`。ADR-0009、旧 Spec/Plan 均作为历史保留，不伪改当时结论或删除证据。

## 9. 实施阶段与验收

### 9.1 实施顺序

1. 冻结新交付边界 ADR 与规范；核对本设计同已冻结的单 Module 单库、`Pangu::`/`ThirdParty::` 和 Toolchain/CTest 规则不冲突。
2. 接入固定版本 `toml++` 与最小 `PanguBuildTool`：先验收宿主工具独立构建、五平台宿主/目标编译器分离、JSON 协议、全候选 Manifest 严格校验及故障诊断。
3. 提炼可复用的顶层环境预检与 Engine 子工程初始化；保持 Engine standalone 五平台 Preset/CTest 行为不变。添加薄 CMake 发现/启用接线与 `EXCLUDE_FROM_ALL`，不新增 Plugin DAG/通用构建器。
4. 建立隔离的最小 Game Project fixture：验证顶层单图、Engine Git Commit 锁定、两个发现根、ID 映射、跨 Plugin 链接、Game/Editor 独立消费、STATIC Catalog 与 SHARED 显式依赖/部署。
5. 在五平台做适用的配置、编译和回归，记录构建性能对照及尚未完成的设备运行/动态库加载范围。实现和文档中的新接口必须与验收证据一致。

### 9.2 功能与失败路径验收

- Engine standalone 在不提供 Game Project、不启用任何 Plugin 时可配置/构建/CTest；**首次 Configure 必须编译并执行 `PanguBuildTool`，得到空 Plugin 列表；重复 Configure 不得重新编译未变化工具**。Project 顶层接入 Engine 后仍仅有一份 Toolchain/BuildOptions 和目标图，不复用其它 BuildRoot 的 Cache。
- 校验 Plugin ID 全局唯一与目录无关，移动目录不改变启用含义；重复 ID、错误 SemVer、未知字段、损坏的 TOML、缺任一必需文件、符号链接越界及配置中不存在/重复的启用 ID **均明确失败**，即使问题插件未启用。
- 未启用 Plugin 的 `CMakeLists.txt` 不执行；已启用但无消费者的 Plugin Target 不进入默认 `ALL`；缺失必需 Target 在 Configure/Generate 阶段报错而不递归启用。
- Game 的静态链接只带入实际消费的 STATIC Module；SHARED 由显式目标依赖产生，再由独立部署目标收集。Game 构建不隐式包含仅 Editor 使用的插件、资产或部署步骤。
- 修改现有 Manifest 自动触发必要的 CMake Configure；新增 Plugin 目录在显式 Configure 后出现；普通源文件改动和无改动构建不因插件扫描/工具启动被额外失效。
- Engine 正式 Windows/WSL Linux/macOS CTest 回归；Engine 与 Project 的 Windows、Linux、macOS、Android、iOS 配置及适用的源码编译/链接回归。Android/iOS 的签名、真机动态装载/卸载不以交叉编译成功冒充已通过，未执行项明确列为后续验收。

### 9.3 构建性能验收（强制）

改造前后在**同一宿主、相同 Toolchain/配置、可比 Target 与测试开关**下记录冷启动与稳定态指标；至少包括首次 Configure（包含首次工具编译）、重复 Configure、无变更 `ninja`、单插件私有 `.cpp` 修改、`Plugin.toml` 修改、启用列表变化、`PanguBuildTool` 源码变化及大量未启用插件的配置场景。记录各阶段耗时、编译/链接任务数量和工具调用数量，不得只给首次编译的单一数字。

通过条件：

1. **无变更 Ninja 为零编译、零链接、零额外 PanguBuildTool/插件扫描任务**（`ninja: no work to do`）。
2. 修改未启用插件私有 `.cpp` 不触发 Engine/Foundation 或其它 Plugin 重编；修改单个被消费插件的私有 `.cpp` 不无故重编 Foundation/其它不相关 Module。
3. 修改已有 Manifest 只重配受影响的构建图，不无条件重编所有 Module；启用列表变化只增减必要的构建依赖和产物；修改 `PanguBuildTool` 不使无关 C++ Target 重编。
4. 重复 Configure 不重新编译未变化的 `PanguBuildTool`，冷启动工具编译耗时必须单独展示；大量未启用插件只增加 Manifest 校验成本，不生成其构建 Target。
5. 以基线测量数据评估 Configure 与增量构建的回退；对明显不必要的扫描、工具启动或 C++ 重编必须修正后才能验收，不能以“功能通过”掩盖性能退化。具体机器、样本规模与耗时结果写入 Plan 验收记录。

## 10. 确认范围与尚未实现

本方案已通过 Grill Me 的 Q1–Q41 决策收敛：源码插件交付、独立 Project 顶层、`Plugin.toml` + `toml++`、全局 Plugin ID、最小 `PanguBuildTool`、固定双发现根、显式启用与消费者驱动编译、独立部署/激活、Project 锁定 Engine Commit、跨平台和性能验收均为已确认方向。

但这些仍属于**待实现目标**：当前仓库没有 `pangu_add_enabled_plugins()` 或 `BuildTools/PanguBuildTool/` 实现，也没有完整的运行时 Plugin 部署/发现流水线。首版不实现 Plugin SemVer 依赖求解、额外源码插件搜索根、跨 BuildRoot 可写工具缓存、独立二进制插件接纳或设备级全平台动态加载保证。后续若确需改变这些边界，必须先修订本 Spec 与对应 ADR，再进入生产实现。
