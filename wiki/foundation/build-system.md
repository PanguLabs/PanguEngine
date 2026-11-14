# Pangu 构建系统

测试构建目标与累计运行集合的契约见[测试分类与运行契约](testing.md)；实际命令和维护要求见 [Tests](../../Tests/README.md)。普通 ALL 与测试专用准备目标是不同入口。

本机终端可使用 `Scripts/build.ps1`（Windows/Android）或 `bash Scripts/build.sh`（Linux/macOS/iOS）。无参数显示中文构建清单，支持普通/纯引擎构建、三档桌面测试、样例、基准和移动最终链接验证；前置条件、参数、五平台示例和产物目录见 [本机构建脚本](../../Scripts/README.md)。两个入口只调用现有 Preset/正式验证，不改变 CMake/Ninja 的构建职责。

> 状态：Partially Frozen（CMake 4.0+、Ninja 原生增量、单 Module 单库、Target 命名空间及源码 Plugin/独立 Project 构建政策已冻结；源码插件与统一 ModuleManager/Loader 已实现，实际平台验收单独记录，完整发布未实现）
> 更新：2026-11-11
> 决策：[ADR-0001：采用 CMake](../decisions/0001-adopt-cmake.md)、[ADR-0005：一个 Module 一个库](../decisions/0005-one-module-one-library.md)、[ADR-0012：构建树按宿主本地化](../decisions/0012-host-local-build-trees.md)、[ADR-0013：原生构建](../decisions/0013-native-build-abi-boundary.md)、[ADR-0014：源码 Plugin 与独立 Project](../decisions/0014-source-plugins-project-build.md)

## 已确定的决策

- **使用 CMake 作为构建系统基础**，不自行维护完整自研构建内核。
- **最低 CMake 版本为 4.0**；新建或修改的 `CMakeLists.txt` 统一使用 `cmake_minimum_required(VERSION 4.0)`。
- C++ 编译器、标准库与平台 SDK/NDK 遵守 [Toolchain](toolchain.md)。Android 使用 NDK Clang，不能套用其它平台 LLVM Clang 23.x 的版本检查。
- **一个 Pangu Module MUST 对应唯一的实际 CMake Library Target**（`STATIC` 或 `SHARED`），编译后只产生一个主要静态库或动态库；Module 可以包含多个 `.cpp` Translation Unit。
- 不允许把一个 Module 拆成多个 Module Library Target，也不允许多个逻辑 Module 合并定义为同一个 Module Library Target。Meta 生成注册代码应加入同一 Module Target。
- **Plugin 是功能分发/启用单位，不要求有自己的库目标**，可以包含多个 Module 或只包含 Assets。
- ThirdParty 可以是独立的 CMake 目标，并被 Pangu Module 依赖；不强制使用 Package Adapter，也不要求每个 ThirdParty 目标都成为 Pangu Module。
- 第三方依赖统一使用 `ThirdParty::` CMake Target 名称，优先原生 Target/ALIAS；源码获取可选固定 Git submodule、Vendor Snapshot、预编译 SDK 或系统依赖，不强制所有 API 经 Module 私有封装。详见 [ThirdParty 接入](third-party.md)。
- Ninja 根据 CMake 规则、编译器头依赖、输出与命令变化判断重建；Pangu 不用逐 TU 收据、编译数据库扫描或第二套依赖图替代 Ninja。
- 普通 configure、编译、归档、最终链接和 Tests 不运行 Pangu 自制 AbiProfile；同树、同配置源码插件同样豁免。Pangu 原生 Plugin/SDK 不再以独立预编译二进制交付给已有宿主，故不实施独立插件装载前 Preflight；ThirdParty/系统预编译库继续单独接受 Toolchain/ABI 适配审核。
- CMake 是构建关系的权威来源；源码 Plugin 必须有严格校验的 `Plugin.toml` 用于身份发现、`CMakeLists.txt` 用于真实编译/链接表达。发现、启用、实际编译、显式部署与运行时激活彼此独立。

## 源码 Plugin / Project 构建政策与最小实现

依据 [ADR-0014](../decisions/0014-source-plugins-project-build.md)：Engine 可独立顶层构建；Game Project 自行选择 Toolchain、CTest、隔离 BuildRoot，通过 Engine 源码 `add_subdirectory()` 进入同一目标图。配置期仅从 Engine/Project 两个固定 `Plugins/` 根扫描直接子目录，所有候选必须同时包含 `Plugin.toml` + `CMakeLists.txt`，严格验证稳定 ID 和可选 SemVer。顶层每次 Configure（**包括无 Plugin**）都须通过宿主 Clang/C++23 独立增量构建并运行 `PanguBuildTool`（`toml++`、JSON）；不能因插件为空跳过。Plugin 显式启用后仍用 `EXCLUDE_FROM_ALL`，只由实际 Game/Editor/Tool Target 链接/依赖触发编译，部署和激活另行控制。

已实现 `BuildTools/PanguBuildTool`、`cmake/PanguPlugins.cmake` 的发现/启用接线及 `cmake/PanguEngineRevision.cmake` 的 Project Git 锁定校验。`ENGINE_ROOT` / `PROJECT_ROOT` 参数指向持有 `Plugins/` 的工程根。Engine standalone 默认启用内部测试；嵌入 Project 时内部测试默认关闭，父级拥有 `BUILD_TESTING`，需要时另开 `PANGU_BUILD_ENGINE_TESTS`。Project 及 Game/Editor STATIC Catalog、Assets 和显式 SHARED 部署以正式测试夹具验收；ModuleManager 原生 Loader 的当前实现与双模式实际验收见[新验收](../../docs/superpowers/plans/2026-11-06-module-manager-link-modes-validation.md)；PanguMetaTool 首版现已单独实现，App 发布未因此实现。平台命令、结果和性能证据见[本次验收](../../docs/superpowers/plans/2026-11-04-source-plugins-project-build-validation.md)。

## CMake Target 命名与职责（Frozen）

| 命名 | 归属 | 示例 |
| --- | --- | --- |
| `Pangu::` | **仅限真实 Pangu Module**；每个 Module 唯一对应实际 `STATIC` / `SHARED` Target | `Pangu::Foundation`、`Pangu::Module` |
| `ThirdParty::` | 第三方依赖的 CMake 构建接口；允许 `INTERFACE` / `STATIC` / `SHARED` / `IMPORTED` 和上游 Target `ALIAS` | `ThirdParty::NlohmannJson`、`ThirdParty::Flecs` |
| 无上述命名空间 | Pangu 自有构建策略、宿主程序、构建工具、测试或辅助库（不是 Module） | `PanguBuildOptions`、`PanguToolchainLinkTests` |

```cmake
pangu_module_library_type(moduleType)
add_library(PanguFoundation ${moduleType} Private/Error.cpp)
add_library(Pangu::Foundation ALIAS PanguFoundation)
pangu_configure_module(PanguFoundation)

add_library(ThirdPartyNlohmannJson INTERFACE)
add_library(ThirdParty::NlohmannJson ALIAS ThirdPartyNlohmannJson)
```

`PanguBuildOptions` 是普通 `INTERFACE` 配置目标，不创建 `Pangu::BuildOptions` 别名；第三方目标也不使用旧 `Pangu::NlohmannJson` 别名。**CMake 构建身份不等于引擎 Module 运行时身份。** `pangu_configure_module()` 仅用于 Pangu Module，不要为了包装第三方编译配置增加新的 Pangu Module。

第三方可直接被现有 Target 引用、在现有 Module 内适配、作为独立技术编译 Target，或由承担 ECS/Renderer/Physics 等独立引擎功能的 Pangu Module 使用；只有最后一种引擎功能边界需要独立 Module 身份。不引入额外 ThirdParty Manager、External Module 或统一 Wrapper 系统。

## Monolithic / Modular 与 Module 库类型的边界

依据 [ADR-0015](../decisions/0015-unified-link-modes-module-manager.md)，`PANGU_LINK_MODE=Monolithic|Modular` 在同一目标图统一全部 Pangu Module 为 STATIC/SHARED。桌面默认 Modular，Android/iOS 默认 Monolithic；Editor Modular 与 Game Monolithic 分别配置，不在同图为同 Module 建双目标。Engine 子目录服从父 Project 缓存或普通变量，非法枚举和同图中途改值配置期拒绝。

同一个 BuildRoot 可在上一验证会话结束后串行重新配置切换模式；模式改变由原生 CMake/Ninja 图重编/重链接，禁止混用旧发现/部署产物。无变化构建不新增扫描/生成/重链接。`BUILD_SHARED_LIBS` 不作为全局 Pangu 策略，也不影响 ThirdParty 的自有类型。Monolithic 的宿主 Catalog 保留归档根引用；Modular 每 Module 独立库、固定 Query，无内嵌逻辑 STATIC Module 或 LinkedStaticModules。

## 当前正式构建目录

普通开发、维护和诊断工具源码位于根目录 `Tools/`。旧 `BuildTools/AbiProfile/` 与 `cmake/BuildTools.cmake` 已按 ADR-0013 从源码移除。未来若出现真实代码生成等构建工具，只按实际消费者显式接入，不预建通用 Bootstrap，也不引入新的构建内核。

正式构建输出**按宿主文件系统隔离**：Windows/Android 使用 Windows NTFS；WSL Linux 使用 ext4；macOS/iOS 使用 macOS APFS。源码仍为 Windows 上唯一工作区，Linux 与 macOS 直接从 SMB 挂载读取同一份源码；不再要求正式构建输出都位于源码仓库内。不再借用 `.probe/` 存放正式构建结果：

| 目标平台 | 构建宿主 / 文件系统 | 正式 BinaryDir（相对于宿主） |
| --- | --- | --- |
| Windows | Windows / NTFS | `${sourceDir}/Build/Windows` |
| Linux | WSL Ubuntu / ext4 | `$HOME/PanguEngineBuild/Linux` |
| macOS | macOS / APFS | `$HOME/PanguEngineBuild/macOS` |
| Android | Windows / NTFS | `${sourceDir}/Build/Android` |
| iOS | macOS / APFS | `$HOME/PanguEngineBuild/iOS` |

Linux 通过 WSL Ubuntu 的 SMB3/CIFS 挂载 `/home/moozen/WindowsGit/PanguEngine` **只共享源码**，从该目录执行 `cmake --preset linux`、`cmake --build --preset linux`、`ctest --preset linux`，所有正式 Linux 构建文件写入 ext4 的 `/home/moozen/PanguEngineBuild/Linux`。不得从 `/mnt/d/Git/PanguEngine` 混用另一份源码绝对路径或复用旧 CMake Cache。

macOS 使用 `ssh -o BatchMode=yes moozen-macos`，通过 `/Users/moozen/WindowsGit/PanguEngine` 直接读取同一份 Windows 源码，不复制源码；本机 APFS 构建目录分别是 `/Users/moozen/PanguEngineBuild/macOS` 和 `/Users/moozen/PanguEngineBuild/iOS`。Windows/Android 仍使用仓库 `Build/Windows` 与 `Build/Android`。Preset 通过 `$env{HOME}` 解析 Linux/Apple 宿主目录，实际编译器、libc++ / SDK/NDK 由平台 Toolchain 控制。

**迁移规则**：在新 BinaryDir 执行首次 configure，重新生成全套 CMake Cache/Ninja/compile_commands.json 与构建收据；禁止复制、移动或复用任何旧 Build 树的缓存、锁、对象和 ABI 完成记录。先前的 `$HOME/PanguBuild/PanguEngine/Linux`、`$HOME/PanguBuild/PanguEngine/macOS`、`$HOME/PanguBuild/PanguEngine/iOS` 以及更早的仓库内 `Build/Linux`、`Build/macOS`、`Build/iOS` 仅作为历史构建结果保留，未经单独确认不删除、不建立反向挂载或链接。产物需要共享时采用显式 Artifact Export，不跨宿主共用可变 BuildRoot。

`Build/` 已被 `.gitignore` 忽略。临时设计探针及其局部构建才使用 `.probe/`；此前 `.probe/module-public-api-build`、`.probe/module-public-api-linux` 和对应临时源码包已在正式构建验证后清理；**不得把其他已有 `.probe` 当成本次构建删除**。

本节定义当前目录组织与五平台 Preset 入口；目录本身不构成 Runtime 或设备验收证明。现行 ModuleManager/Loader、两模式真实应用链接与平台结果见[正式验收](../../docs/superpowers/plans/2026-11-06-module-manager-link-modes-validation.md)，Public C++ ABI、完整发布、签名和真机运行仍未由此冻结或完成。

## 构建环境预检与基础回归（2026-10-22）

根 `CMakeLists.txt` 已在 `project()` **之前**引入 `cmake/ValidateBuildEnvironment.cmake`：桌面平台及 iOS 检查 `LLVM_ROOT` 和必要的 Clang / `clang-scan-deps` 可执行文件；Android 在官方 NDK Toolchain 的 `CMAKE_TOOLCHAIN_FILE` 已选择时检查 `ANDROID_HOME`、`ANDROID_NDK_HOME` 和 Toolchain 文件是否存在。这样新建缓存时缺失环境变量会直接得到带 `Pangu ... preflight` 的诊断；现有平台 Settings 仍继续做后续工具链身份与版本校验。

正式 `Tests/CMake/EnvironmentTests.cmake` 在 Windows、WSL Ubuntu 和 macOS 通过 CTest 验证缺失环境变量时必须明确拒绝。历史三平台曾通过 `BuildEnvironment`、`FoundationError`、`ModuleIdentity` 等回归；Android/iOS 的 `PanguFoundation` 和 `PanguModule` 有 STATIC 目标编译/归档历史结果，不提供交叉设备运行结果；旧 Host ABI 采集已退役。新增 `Source/Runtime/Foundation/` 对应独立 `PanguFoundation` STATIC Target，符合一 Module 一库。

## ABI Profile v1 构建期工具（2026-10-27 历史实现，现已删除）

以下为 2026-10-27 的历史状态；相关构建入口与测试已按 [ADR-0013](../decisions/0013-native-build-abi-boundary.md) 从源码删除，不能作为当前操作说明：

- 当时 CMake 通过 `cmake/BuildTools.cmake` 为 Windows/Linux/macOS 原生宿主构建并版本化发布 `PanguAbiProfileTool`；Android/iOS 的交叉编译使用对应 Windows/macOS 构建工具，不在目标平台运行。它不是 Pangu Runtime Module，不改变单 Module 单 Library Target 约束。`cmake/BuildTools.cmake` 已删除。
- `Source/ThirdParty/NlohmannJson/Repo` 是保留的 nlohmann/json v3.12.0 Git submodule；`ThirdParty::NlohmannJson` 是 INTERFACE Target，设置 `JSON_USE_GLOBAL_UDLS=0`。旧 ABI 工具已删除，当前由 `Tests/ThirdParty/NlohmannJson` 验证；不配置上游工程，不在构建时下载。
- 当时 `Tests/AbiProfile` 包含冻结 Schema/JCS/SHA256 向量、真实 CLI、依赖闭包/SCC、Host Bootstrap、Compiler Launcher/收据和五平台 Collector 测试；该目录已删除，历史结论仅保留在 [旧验收记录](../../docs/superpowers/plans/2026-10-23-abi-collection.md) 与 [ABI Profile / Fingerprint v1（历史协议）](abi-profile-fingerprint.md)。
- 当时 Windows/Linux/macOS 与 Android/iOS 的正式 STATIC Module 均接入了 C++ 全 TU 编译收据、Profile 自动采集及依赖校验。该机制不再运行；当时 ADR-0013 曾计划独立 Pangu Plugin 二进制的可信绑定与 Preflight，但这一后续路线现已由 ADR-0014 取消。

## 已落地的轻量化 CMake 配置（实现层，非新架构冻结）

参考本地 O3DE 的分平台 CMake Presets、平台分发与编译规则复用思想，**仅借鉴组织方式**；不引入其 `ly_add_target` / `GEM_MODULE` / 全局 Monolithic 配置、资源/安装系统或第二构建内核。

```text
CMakeLists.txt
CMakePresets.json
cmake/
  PanguPlatform.cmake
  Platform/
    Common/BuildOptions.cmake
    Common/Module.cmake
    Common/CMakePresets.json
    Windows/Settings.cmake + CMakePresets.json
    Linux/Toolchain.cmake + Settings.cmake + CMakePresets.json
    macOS/Toolchain.cmake + Settings.cmake + CMakePresets.json
    Android/Settings.cmake + CMakePresets.json
    iOS/Settings.cmake     + Toolchain.cmake + CMakePresets.json
```

- `PanguPlatform.cmake` 在 `project(... LANGUAGES CXX)` 后按 **目标平台** 选择配置，并拒绝错误的 Clang 版本/不支持的平台；Android 只允许官方 NDK Toolchain 的 Clang，不套用桌面 LLVM 23 规则。
- `PanguBuildOptions` 是无产物、无 `Pangu::` 别名的 **INTERFACE 编译策略 Target**，仅为显式依赖它的 Pangu 自有 Target 设置 C++23、`-fno-exceptions` 和基础告警参数；工具链/标准库选择由 Preset/Toolchain 在 `project()` 之前完成，普通 ThirdParty Target 不需要链接 `PanguBuildOptions`。Pangu 自有 Runtime、BuildTools、测试和基准保留 RTTI；独立 PanguMetaTool 的 Clang 适配编译单元匹配 Clang 库 RTTI，Linux 宿主工具可使用 libstdc++，例外见 [ADR-0018](../decisions/0018-installed-pangumeta-sdk.md)；框架与消费方保持 RTTI 一致，CRT/Iterator 等有效配置遵守平台工具链策略，不恢复逐 TU Collector。
- `pangu_configure_module(<existing-target>)` 仅为现有 `add_library(... STATIC|SHARED ...)` Target **PRIVATE** 应用 Pangu 编译策略，并设置 PIC 和输出目录；必须**先创建**真实 Module Target，不隐式生成 Library、Manifest、Catalog 或 ModuleId。当前 `PanguModule` 通过 `pangu_module_library_type` 得到统一模式类型后创建，`Pangu::Module` 仅是 Alias。
- 平台 Settings 负责目标特殊规则：Windows 使用 Clang + MSVC STL（Windows ARM 不在 v1）；Linux Toolchain 在 `project()` 前选择并核对匹配 LLVM 的 libc++ 头文件、`__config_site`、运行库及 RPATH；缺失时拒绝，不退回 `libstdc++`，不同安装布局可用 `PANGU_LIBCXX_ROOT` 显式覆盖；macOS Toolchain 统一选择 MacPorts Clang 23 与 libc++；Android 采用 NDK Clang；iOS 采用 MacPorts Clang 23 + 独立 iPhoneOS SDK。
- `cmake/` 不写死本机工具链安装路径。Windows、Linux、macOS、iOS 从 `LLVM_ROOT` 派生编译器和 `clang-scan-deps` 路径，并校验编译器位于该安装目录内；Android 从 `ANDROID_HOME` 读取 SDK 目录、从 `ANDROID_NDK_HOME` 选择官方 NDK Toolchain，并校验实际使用的 NDK 与环境变量一致。
- `Lib/` 存放 STATIC Library（以及平台归档产物）；`Bin/` 存放动态库/可执行程序。当前 Runtime 含 Foundation 错误值/纯协议、ModuleManager、原生 Loader、Pin、增量 Scope 与声明事务；另已交付 EngineHost 最小生命周期及 Concrete 资源查询；完整 ServiceRegistry 或 Shader/Asset Pipeline 未实现。
- CMake Preset 默认 Ninja + Release，配置并导出 `compile_commands.json` 供 IDE/clang-tidy 使用；根目录通过 `BUILD_TESTING` 控制 `Tests/` 总入口。`BUILD_TESTING=OFF` 完整排除测试目标；交叉编译开启测试时保留编译型 Toolchain 验证，不创建需要设备运行的测试程序。Android ARM64/API29 与 iOS arm64/iOS18.0 是当前验收参数，**不是永久支持下限**。

先在启动 CMake 的环境中设置下列变量；目录值由本机配置提供，不写入共享 Preset：

| 环境变量 | 使用平台 | 含义 |
| --- | --- | --- |
| `LLVM_ROOT` | Windows、Linux、macOS、iOS | LLVM 23 安装前缀，其下包含 `bin/clang++`（Windows 为 `.exe`）及 `bin/clang-scan-deps`；Apple 平台选择 MacPorts LLVM |
| `ANDROID_HOME` | Android 的构建宿主 | Android SDK 安装根目录 |
| `ANDROID_NDK_HOME` | Android 的构建宿主 | NDK 安装根目录，其下包含 `build/cmake/android.toolchain.cmake` |
| `PANGU_IOS_SDK_PATH` | iOS 的 macOS 宿主 | 独立安装的 iPhoneOS Device SDK；未设置时先通过 `xcrun --sdk iphoneos --show-sdk-path` 查找，失败或目录无效时回退到 `$HOME/Library/Developer/SDKs/iPhoneOS18.2.sdk`；仍不存在则要求显式设置 |

macOS 可在 `~/.zshenv` 导出 `LLVM_ROOT`；WSL Ubuntu 可由 `/etc/profile.d/llvm.sh` 配置，并在 `~/.bashrc` 中引用。非登录且非交互的 Bash 不会自动读取这些登录配置，自动化调用方应传入环境变量或使用登录 Shell。

从相应平台的 PanguEngine 仓库根目录运行：

```sh
cmake --preset windows        # Windows PowerShell
cmake --build --preset windows
ctest --preset windows

cmake --preset linux          # WSL Ubuntu
cmake --build --preset linux
ctest --preset linux

cmake --preset macos          # ssh moozen-macos；直接使用 WindowsGit SMB 源码
cmake --build --preset macos
ctest --preset macos
```

Android 设置 `ANDROID_HOME`、`ANDROID_NDK_HOME` 后，从 Windows 或已安装 SDK/NDK 的宿主运行 `cmake --preset android-arm64` / `cmake --build --preset android-arm64`。iOS 设置 `LLVM_ROOT` 并确认 SDK 后，在 macOS 上运行 `cmake --preset ios-arm64` / `cmake --build --preset ios-arm64`。两种移动平台日常构建仅编译/归档真实 Runtime target，不运行宿主 ABI 采集；应用链接、设备运行和独立交付兼容性验证另行验收。

Android/iOS Preset 默认 `BUILD_TESTING=OFF`，因此默认构建仅包含正式目标。需要同时编译 Toolchain 回归库时，可显式追加 `-DBUILD_TESTING=ON`；随后仍使用对应的构建 Preset。不会执行宏审计或创建设备测试程序。

本次将 Linux/macOS 标准库参数从 `PanguBuildOptions` 迁移至 Toolchain，**升级已有构建目录时必须使用** `cmake --fresh --preset <name>` 使 `CMAKE_CXX_FLAGS_INIT` 与链接参数重新初始化；之后可使用普通 `cmake --preset <name>`。切换 `LLVM_ROOT` 或 SDK/NDK 时也应重新配置。不同平台及 Host 的二进制缓存必须独立；更换构建根时重新配置，禁止复用旧目录的 CMake Cache。

当前统一链接选项为 `PANGU_LINK_MODE=Monolithic|Modular`：同图全部 Pangu Module 分别使用原生 STATIC 或 SHARED Target，桌面默认 Modular，Android/iOS 默认 Monolithic。Engine 子目录服从父 Project 的缓存或普通变量选择；非法值及同图中途改值在配置期拒绝。ThirdParty 不受该选项影响。同一构建树可重新配置切换模式。

`pangu_configure_module(target EXPORT_MACRO macro)` 验证目标类型并生成 PUBLIC 构建包含目录中的 `<Target>Export.h`；Foundation/Module 分别使用 `PG_FOUNDATION_API`、`PG_MODULE_API`，显式标注所需符号，不使用自动导出全部符号。`pangu_export_module_query(TARGET target ID id_symbol ID_HEADER identity_header QUERY function HEADER query_header)` 在两种模式均登记 Module 身份与 Query binding，供 Meta 描述和 Host Catalog 消费；仅 SHARED Target 发射并编译固定 `PanguModuleQueryV1` 包装。ID 引用 C++ 公共 ModuleId 常量，ID_HEADER 声明该常量，HEADER 声明 Query；两者可为同一头文件。ModuleManager 已消费真实 Loader，当前阶段证据与最终平台矩阵分别记录于[正式验收](../../docs/superpowers/plans/2026-11-06-module-manager-link-modes-validation.md)。

**尚未冻结/实现**：`pangu_configure_module` 的长期包装 API、完整工具链锁定（Windows SDK/MSVC STL、Linux libc++ ABI 配置、各平台 SDK 的精确 Profile）、PanguMetaTool 长期 API、完整安装/分发和多配置产物政策。源码 Plugin/独立 Project 已提供最小构建接线，不将测试夹具提升为完整发布链路或跨平台 ABI 安全证明。

## “一个 Module 一个库”的范围

| 事项 | 冻结约束 |
| --- | --- |
| Module 构建 | 一个实际 CMake `STATIC` 或 `SHARED` Library Target |
| CMake `ALIAS` | 只是 Target 别名，不是第二个构建目标 |
| 源文件 | 多个 Translation Unit 允许；不强制 Unity Build |
| 生成代码 | 作为该 Module 目标的源文件，不创建第二个 Module 库 |
| 主要产物 | 一个 `.a` / 静态 `.lib`，或一个 `.dll` / `.so` / `.dylib` |
| 辅助产物 | Windows 的 import `.lib`、PDB 等不算第二个 Module |
| STATIC 运行期 | Monolithic 最终链接到 Host；归档不能单独卸载，不嵌入另一个逻辑 SHARED Module |
| SHARED 运行期 | Module 加载器直接管理库句柄和 CodePin，不另建 BinaryImage 架构层 |
| Host/Tool/ThirdParty | 可有自己的普通 CMake Target，但不因此违反 Pangu Module 规则 |

普通 ThirdParty STATIC 代码可进入动态库；逻辑 Pangu Module 按统一模式构建，不能恢复旧混合承载。代码、对象及释放器均需受实际驻留保护。

## 与旧方案的关系

1. 完整自研 `PanguBuild` 构建核心已被 CMake 替代；不恢复 `PanguBuild` 作为第二构建内核或上层调度器。新增构建职责优先通过 CMake Target 与按需工具表达。
2. `Module.toml` 不再是每个 Module 必需文件；`CMakeLists.txt` 是编译和链接配置的权威来源。源码 Plugin 必须提供 `Plugin.toml`，它的稳定 ID/可选元数据和两根发现规范已由 ADR-0014 冻结并完成最小实现。
3. 旧方案允许 Module 和多个 CMake Target / 物理镜像之间灵活映射；**ADR-0005 已收紧为一个 Module 对应一个库 Target 和一个主要产物**。
4. 不建立独立 BinaryImageManager / BinaryImageDescriptor；物理库文件和 CodePin 是现有 Module 管理机制的内部实现细节。

## Compile-Time / Build-Time First 的构建职责

按 [C++ 规范与最佳实践](cpp-guidelines.md)，类型与静态分发应尽量在编译期解决。Module 依赖、编译器/标准库配置和按需生成代码属于 CMake 与目标工具链职责；Shader/Asset 处理与打包只由实际消费者或显式命令触发。独立 Pangu 原生 Plugin/SDK 二进制交付不再是产品路线，普通 ThirdParty/系统库仍遵守自身 ABI/Toolchain 约束。

- CMake 仍是构建依赖的权威来源；可由 PanguMetaTool 等工具按需要生成代码，生成源文件归入对应 Module 唯一库 Target。
- PanguMetaTool 的 MetaIR 是进程内拥有数据的强类型模型；类型语义与 GenerationRequest 中的编译上下文、输入输出及 Host 装配分开。原生图按已登记消费者选择生成头、实现或类型描述，不要求所有元数据转换为 C++ 静态表；首版不增加 Meta 文件或 Exporter 构建阶段。
- 编译器与目标平台配置仍由真实 Toolchain/Target 表达并验证；历史 [ABI Profile v1](abi-profile-fingerprint.md) 不再参与同树普通构建。独立 Pangu 原生插件二进制接纳及下一版 ABI Preflight 不再规划；Loader 运行时有效性与 CodePin 不受影响。
- 预生成并不取代 Runtime 对外部输入、动态模块状态、设备能力以及资源版本的必要验证。
- Meta 的工具路线及生成代码存放/编译归属已有明确设计，首版生产生成与原生增量接线已实现；Shader/Asset Pipeline 也不因通用最佳实践而自动视为已实现。

### PanguMetaTool 生成产物（首版已实现，长期 API 未冻结）

权威目录树、生成头依赖方向和验收要求见 [Reflection / Meta 第 6 节](../core/reflection-meta.md#6-生成代码存放编译与原生构建依赖)，决策见 [ADR-0017](../decisions/0017-standalone-pangu-meta.md)。**生成代码按 Engine / Game Project 源码归属分根存放**：Engine 自有 Module/Plugin 输出到 `<EngineBuildRoot>/Generated/PanguMeta/<Config>/<CMakeTarget>/`，Project 自有 Module/Plugin 及 Game/Editor Host Catalog 输出到 `<ProjectBuildRoot>/Generated/PanguMeta/<Config>/<CMakeTarget>/`。即使 Project 通过 `add_subdirectory()` 引用 Engine 源码，也不把 Engine 生成结果写入 Project 构建树；`CMakeTarget` 是实际编译实现的库或 Host 目标名，不是逻辑 Module 名或 Alias。

头文件/实现使用 `.gen.h` / `.gen.cpp`，代码目录仅保留 Public / Private，不另设 Source 层。公开生成头进入 Public，内部头与所有生成实现进入 Private；只有确需目标非内联实现或类型描述时才生成相应实现，加入所属 Module 的唯一库，Host Catalog 加入对应 Host，不另建 Meta 库。纯模板/强类型访问可仅生成头，构建期检查数据可止于 MetaIR。原始类型头默认不包含自身生成头，只在消费者需要编译期接口时按需包含伴随 `.gen.h`；不要求业务 `.cpp` 手写包含 `.gen.cpp`。

CMake 按源码所有者选择生成根，登记唯一生产者及消费者顺序。PanguMetaTool 按内容差异发布，保留内容不变文件的 mtime；Meta.d 跟踪传递输入，Meta.outputs.json 只列本批次拥有文件，Meta.stamp 最后提交。失败期间清单可为旧新并集，只有成功 stamp 表示完成。

Engine 对象、库、导出头与 Meta 产物已实际分根。同一 EngineBuildRoot 会话串行；A→B→直接 A 通过原生重配恢复共享状态，包括失败恢复和跨配置请求。每 Target/Config 唯一批次，scan 可联合 Query 槽位；不建立第二套调度图或 Project 变体目录。Export 仍由 CMake 生成，Query/Catalog 已由 Meta 唯一生产者接管。

Meta 输入头登记同时接入目标源码登记，每头有唯一 Module 所有者；按头选择消费需求、按 Module Target 汇总，当前图拒绝重复归属。未登记但含 pga 的项目头报错，已登记的其他 Module 作为依赖处理；每 Module 首版只有一套明确 Meta 上下文。ModuleId 仍在 C++ 公共常量中唯一定义，Definition 与生成入口共同引用，CMake 只登记符号和声明头。具体登记 API、外部输出依赖与缓存迁移已按[首版规格](../../docs/superpowers/specs/2026-11-09-pangumeta-tool-design.md)的实施计划落实，不另建人工 Manifest。

## 必须解决的设计问题（尚未冻结）

| 主题 | 待决问题 |
| --- | --- |
| CMake 组织 | 根工程、五平台 Presets 和最小 Module 设置已经落地；长期封装 API、Generator 组合、多配置与完整目标 Toolchain 管理尚未冻结 |
| 库类型与部署策略 | `PANGU_LINK_MODE` 已统一 Module 的 STATIC/SHARED 类型和平台默认值；完整安装与部署政策仍待实现 |
| 产物布局 | 库、Manifest、符号文件、动态库导出、安装和分发目录 |
| Meta 生成 | 首版生成、输入映射、增量依赖已实现；仅 Ninja 单配置，多配置暂不支持，见 Reflection / Meta 第 6 节 |
| ABI | 同树构建普通零 AbiProfile 由 ADR-0013 保留；Pangu Plugin/SDK 独立二进制 Preflight 已被 ADR-0014 取消。运行时 SHARED Query、ModuleId、CodePin 与第三方 ABI 审核仍有各自责任 |
| ThirdParty | Source/Binary 版本、目标导出与平台链接 |
| 交叉编译 | Android/iOS 两模式正式根构建、Runtime/业务库和独立 Project 最终链接按新验收记录；设备执行、打包、签名不在本轮范围 |

## 验证要求

后续至少验证 Module 与 Target 的一对一关系、产物数量、编译数据库、增量构建一致性、Meta 生成依赖、ABI 兼容配置、CodePin 以及平台矩阵。已有 PoC 不等于正式 Runtime / 多平台验收。

## 现行 Toolchain 与 ThirdParty 回归

`BUILD_TESTING=ON` 时，`Tests/Toolchain/` 的两个 Probe 使用所选平台 Clang/C++23 编译为真实库：`ThirdParty::CompiledProbe` 始终为 STATIC，`Pangu::ToolchainProbe` 服从统一链接模式。普通第三方库不继承 `PanguBuildOptions`；桌面 `ToolchainLibraryLink` 通过链接两个库并运行 CTest 验证跨库 C++23 返回值。编译器/头依赖与命令变更导致的重编由 CMake + Ninja 判断，不执行自制 ABI 审计。

`BUILD_TESTING=OFF` 时不构建上述测试目标。Android/iOS 正式 Preset 默认关闭 Tests；单独的移动平台最终链接冒烟须由显式目标执行，不纳入默认 `ALL`。详情见 [Toolchain 测试](../../Tests/Toolchain/README.md)。

## 历史 ABI Collection（2026-10-23 至 2026-10-27，已退役）

旧分支曾为受管 STATIC Module 实现逐 TU 宏审计、Compiler Launcher、Guard/Collect、完整收据及闭包检查，历史 CTest 记录为 Windows 36/36、Linux 34/34、macOS 34/34；这些是 **2026-10-27 的旧实现成绩，不是当前测试数量或构建性能**。旧工具及仅验证其机制的 Tests 均已删除，不再将 `complete.json`、`Abi/compile-evidence.json` 作为活动产物。

历史设计与验收仍可在 [2026-10-23 ABI Collection 实施计划](../../docs/superpowers/plans/2026-10-23-abi-collection.md) 和 [v1 历史协议](abi-profile-fingerprint.md) 中查阅；源码如需追溯使用历史 Git 提交，当前目录已不再有该工具。现行边界以 [ADR-0013](../decisions/0013-native-build-abi-boundary.md) 为准。

## 基础诊断构建

基础诊断增加真实 PanguLog / Pangu::Log Module，跟随统一 Monolithic/Modular 模式。依赖方向为 EngineHost → Module → Log → Foundation；fmt/spdlog 使用普通 STATIC 第三方目标，不导入宿主 STL/LLVM 到移动目标。Assert 经 PANGU_ENABLE_ASSERTS 显式控制，默认 ON，不以 NDEBUG 替代。固定源码的三桌面 Full 与两移动双模式最终链接、恢复配置和实测版本见[基础诊断验收](../../docs/superpowers/plans/2026-11-12-foundation-diagnostics-validation.md)。

## EngineHost 与样例验收入口

`PanguEngineHost`（`Pangu::EngineHost`）为跟随统一链接模式的常驻 Module；BuiltinResidentQueries 已随 Log 接入扩展为 Foundation/Log/Module 三项。根 `PANGU_BUILD_SAMPLES` 默认 OFF，开启后构建 MinimalHost 与独立服务 Module，并将 MinimalHostSmoke 接入 precommit/full。Project 的 Game/Editor/MobileHost 消费同一宿主协议；Modular 不通过隐式业务导入替代显式候选，部署集合仍与消费集合分开。

正式桌面矩阵逐宿主按 Monolithic → Modular 配置 Samples ON，构建并执行对应 full；Modular ModuleRegression 保留旧 Mono 与 Host Mono 覆盖。完成后恢复桌面 Modular、Samples/Benchmark OFF、测试 ON 并验证 fast；移动根恢复 Monolithic、Samples/Benchmark/测试 OFF，构建 EngineHost，另用隔离 Project 的 CrossCompileTests 重验 MobileHost 最终链接。BuildRoot/共享源码规则不变。精确 SHA、完整命令、结果与局限见[EngineHost 验收](../../docs/superpowers/plans/2026-11-07-engine-host-lifecycle-validation.md)。

正式入口为 cmake/PanguMeta.cmake（headers/consume/module/host_catalog）；启用任一任务均要求可用宿主工具，包括仅 Query/Catalog。工具从 LLVM_ROOT 自动发现安装版开发包，zlib/zstd 随独立工具的普通目标构建，不需要 SDK 配置文件或准备阶段；链接后自检失败移除该次工具并阻断生成。宿主工具缓存与测试缓存分离，移动图只调用宿主程序，不导入 LLVM targets。现行构建取舍见 [ADR-0019](../decisions/0019-meta-tool-normal-build.md)，历史结果与限制见[Meta 验收](../../docs/superpowers/plans/2026-11-09-pangumeta-tool-validation.md)。
