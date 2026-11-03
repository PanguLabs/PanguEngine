# Pangu 构建系统

> 状态：Partially Frozen（CMake 4.0+、原生 Ninja 增量、单 Module 单库、Target 命名空间及普通构建零 AbiProfile 已冻结；独立交付格式待设计）
> 更新：2026-11-02
> 决策：[ADR-0001：采用 CMake](../decisions/0001-adopt-cmake.md)、[ADR-0005：一个 Module 一个库](../decisions/0005-one-module-one-library.md)、[ADR-0012：构建树按宿主本地化](../decisions/0012-host-local-build-trees.md)、[ADR-0013：原生构建与 ABI 交付边界](../decisions/0013-native-build-abi-boundary.md)

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
- 普通 configure、编译、归档、最终链接和 Tests 不运行 Pangu 自制 AbiProfile；同树、同配置并随宿主配套交付的插件同样豁免。独立 SDK/预编译库和单独交付给已有宿主的插件才进入二进制接纳边界。
- CMake 是构建关系的权威来源；Plugin 的安装、发现和版本信息仍可能需要独立 Manifest，不因此形成第二份构建配置。

## CMake Target 命名与职责（Frozen）

| 命名 | 归属 | 示例 |
| --- | --- | --- |
| `Pangu::` | **仅限真实 Pangu Module**；每个 Module 唯一对应实际 `STATIC` / `SHARED` Target | `Pangu::Foundation`、`Pangu::Module` |
| `ThirdParty::` | 第三方依赖的 CMake 构建接口；允许 `INTERFACE` / `STATIC` / `SHARED` / `IMPORTED` 和上游 Target `ALIAS` | `ThirdParty::NlohmannJson`、`ThirdParty::Flecs` |
| 无上述命名空间 | Pangu 自有构建策略、宿主程序、构建工具、测试或辅助库（不是 Module） | `PanguBuildOptions`、`PanguToolchainLinkTests` |

```cmake
add_library(PanguFoundation STATIC Private/Error.cpp)
add_library(Pangu::Foundation ALIAS PanguFoundation)
pangu_configure_module(PanguFoundation)

add_library(ThirdPartyNlohmannJson INTERFACE)
add_library(ThirdParty::NlohmannJson ALIAS ThirdPartyNlohmannJson)
```

`PanguBuildOptions` 是普通 `INTERFACE` 配置目标，不创建 `Pangu::BuildOptions` 别名；第三方目标也不使用旧 `Pangu::NlohmannJson` 别名。**CMake 构建身份不等于引擎 Module 运行时身份。** `pangu_configure_module()` 仅用于 Pangu Module，不要为了包装第三方编译配置增加新的 Pangu Module。

第三方可直接被现有 Target 引用、在现有 Module 内适配、作为独立技术编译 Target，或由承担 ECS/Renderer/Physics 等独立引擎功能的 Pangu Module 使用；只有最后一种引擎功能边界需要独立 Module 身份。不引入额外 ThirdParty Manager、External Module 或统一 Wrapper 系统。

## Monolithic / Modular 与 Module 库类型的边界

**已确定的设计约束**：`STATIC` / `SHARED` 是 CMake 原生的 **Library Target 类型**，直接描述每个 Pangu Module 的编译、链接和产物；`Monolithic` / `Modular` **不是 Module 类型，也不是新的引擎架构层**。**只有在需要对整个 Engine / Game / Editor 统一施加部署或链接策略时，才将 Monolithic / Modular 作为顶层 CMake 构建选项**（如 Preset 或顶层 Cache 配置），不要求每个工程必须声明或选择该选项。

- 顶层策略可影响 Module 的默认 `STATIC` / `SHARED` 选择及最终装配方式，但不能取代每个 Module 最终落实为唯一真实 CMake Library Target 的既有规则；允许因平台或功能需要显式混用两类目标。
- `Monolithic` 不等于禁止所有动态 Plugin，`Modular` 也不等于所有 Module 必须为 `SHARED`；两者描述的是整体部署取向，而非逐 Module 类型。
- CMake 内置的 `BUILD_SHARED_LIBS` 仅控制**未明确指定**库类型的 `add_library()`，不把它误当成 Pangu 的全局部署协议，也不无差别覆盖 ThirdParty 的库类型。
- 顶层选项的**实际名称、默认值、Preset 组织、平台映射与是否需要实现**尚未冻结；本次仅确定其适用层级和按需引入原则。

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

本节记录当前落地的目录组织、五平台最小 CMake Presets 和 **Module Identity 切片**编译证据；不意味着完整 Build System API、运行时注册/Loader、Android/iOS 应用链接、签名/真机或多平台 Runtime 已冻结/完成。

## 构建环境预检与基础回归（2026-10-22）

根 `CMakeLists.txt` 已在 `project()` **之前**引入 `cmake/ValidateBuildEnvironment.cmake`：桌面平台及 iOS 检查 `LLVM_ROOT` 和必要的 Clang / `clang-scan-deps` 可执行文件；Android 在官方 NDK Toolchain 的 `CMAKE_TOOLCHAIN_FILE` 已选择时检查 `ANDROID_HOME`、`ANDROID_NDK_HOME` 和 Toolchain 文件是否存在。这样新建缓存时缺失环境变量会直接得到带 `Pangu ... preflight` 的诊断；现有平台 Settings 仍继续做后续工具链身份与版本校验。

正式 `Tests/CMake/EnvironmentTests.cmake` 在 Windows、WSL Ubuntu 和 macOS 通过 CTest 验证缺失环境变量时必须明确拒绝。历史三平台曾通过 `BuildEnvironment`、`FoundationError`、`ModuleIdentity` 等回归；Android/iOS 的 `PanguFoundation` 和 `PanguModule` 有 STATIC 目标编译/归档历史结果，不提供交叉设备运行结果；旧 Host ABI 采集已退役。新增 `Source/Runtime/Foundation/` 对应独立 `PanguFoundation` STATIC Target，符合一 Module 一库。

## ABI Profile v1 构建期工具（2026-10-27 历史实现，现已删除）

以下为 2026-10-27 的历史状态；相关构建入口与测试已按 [ADR-0013](../decisions/0013-native-build-abi-boundary.md) 从源码删除，不能作为当前操作说明：

- 当时 CMake 通过 `cmake/BuildTools.cmake` 为 Windows/Linux/macOS 原生宿主构建并版本化发布 `PanguAbiProfileTool`；Android/iOS 的交叉编译使用对应 Windows/macOS 构建工具，不在目标平台运行。它不是 Pangu Runtime Module，不改变单 Module 单 Library Target 约束。`cmake/BuildTools.cmake` 已删除。
- `Source/ThirdParty/NlohmannJson/Repo` 是保留的 nlohmann/json v3.12.0 Git submodule；`ThirdParty::NlohmannJson` 是 INTERFACE Target，设置 `JSON_USE_GLOBAL_UDLS=0`。旧 ABI 工具已删除，当前由 `Tests/ThirdParty/NlohmannJson` 验证；不配置上游工程，不在构建时下载。
- 当时 `Tests/AbiProfile` 包含冻结 Schema/JCS/SHA256 向量、真实 CLI、依赖闭包/SCC、Host Bootstrap、Compiler Launcher/收据和五平台 Collector 测试；该目录已删除，历史结论仅保留在 [旧验收记录](../../docs/superpowers/plans/2026-10-23-abi-collection.md) 与 [ABI Profile / Fingerprint v1（历史协议）](abi-profile-fingerprint.md)。
- 当时 Windows/Linux/macOS 与 Android/iOS 的正式 STATIC Module 均接入了 C++ 全 TU 编译收据、Profile 自动采集及依赖校验。该机制不再运行；独立交付二进制的可信产物绑定与 OS Load 前后复核属于 ADR-0013 的后续边界，当前尚未实现。

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
- `PanguBuildOptions` 是无产物、无 `Pangu::` 别名的 **INTERFACE 编译策略 Target**，仅为显式依赖它的 Pangu 自有 Target 设置 C++23、`-fno-exceptions` 和基础告警参数；工具链/标准库选择由 Preset/Toolchain 在 `project()` 之前完成，普通 ThirdParty Target 不需要链接 `PanguBuildOptions`。RTTI、CRT/Iterator 等有效配置遵守平台工具链策略，不恢复逐 TU Collector。
- `pangu_configure_module(<existing-target>)` 仅为现有 `add_library(... STATIC|SHARED ...)` Target **PRIVATE** 应用 Pangu 编译策略，并设置 PIC 和输出目录；必须**先创建**真实 Module Target，不隐式生成 Library、Manifest、Catalog 或 ModuleId。当前 `PanguModule` 通过 `add_library(PanguModule STATIC ...)` 建立，`Pangu::Module` 仅是 Alias。
- 平台 Settings 负责目标特殊规则：Windows 使用 Clang + MSVC STL（Windows ARM 不在 v1）；Linux Toolchain 在 `project()` 前选择并核对匹配 LLVM 的 libc++ 头文件、`__config_site`、运行库及 RPATH；缺失时拒绝，不退回 `libstdc++`，不同安装布局可用 `PANGU_LIBCXX_ROOT` 显式覆盖；macOS Toolchain 统一选择 MacPorts Clang 23 与 libc++；Android 采用 NDK Clang；iOS 采用 MacPorts Clang 23 + 独立 iPhoneOS SDK。
- `cmake/` 不写死本机工具链安装路径。Windows、Linux、macOS、iOS 从 `LLVM_ROOT` 派生编译器和 `clang-scan-deps` 路径，并校验编译器位于该安装目录内；Android 从 `ANDROID_HOME` 读取 SDK 目录、从 `ANDROID_NDK_HOME` 选择官方 NDK Toolchain，并校验实际使用的 NDK 与环境变量一致。
- `Lib/` 存放 STATIC Library（以及平台归档产物）；`Bin/` 存放动态库/可执行程序。当前 Runtime 含 Foundation 错误值、Module Identity/Descriptor、STATIC Catalog 与声明事务切片，不意味着完整 Plugin、ModuleManager 或 Shader/Asset Pipeline 已实现。
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

**尚未冻结/实现**：`pangu_configure_module` 的长期包装 API、完整工具链锁定（Windows SDK/MSVC STL、Linux libc++ ABI 配置、各平台 SDK 的精确 Profile）、PanguMeta、完整插件封装、Monolithic/Modular 顶层策略、安装/分发和多配置产物政策。这次完成的是可工作的最小构建接线，不将实验便利配置提升为跨平台 ABI 安全证明。

## “一个 Module 一个库”的范围

| 事项 | 冻结约束 |
| --- | --- |
| Module 构建 | 一个实际 CMake `STATIC` 或 `SHARED` Library Target |
| CMake `ALIAS` | 只是 Target 别名，不是第二个构建目标 |
| 源文件 | 多个 Translation Unit 允许；不强制 Unity Build |
| 生成代码 | 作为该 Module 目标的源文件，不创建第二个 Module 库 |
| 主要产物 | 一个 `.a` / 静态 `.lib`，或一个 `.dll` / `.so` / `.dylib` |
| 辅助产物 | Windows 的 import `.lib`、PDB 等不算第二个 Module |
| STATIC 运行期 | 最终链接到 EXE 或动态库；归档本身不能被单独卸载 |
| SHARED 运行期 | Module 加载器直接管理库句柄和 CodePin，不另建 BinaryImage 架构层 |
| Host/Tool/ThirdParty | 可有自己的普通 CMake Target，但不因此违反 Pangu Module 规则 |

对于 STATIC Module 被链接进可卸载动态库的情形，代码驻留保护 **MUST 覆盖实际承载代码的动态库**；单库构建关系不等同于独立运行时可卸载性。

## 与旧方案的关系

1. 完整自研 `PanguBuild` 构建核心已被 CMake 替代；不恢复 `PanguBuild` 作为第二构建内核或上层调度器。新增构建职责优先通过 CMake Target 与按需工具表达。
2. `Module.toml` 不再是每个 Module 必需文件；`CMakeLists.txt` 是编译和链接配置的权威来源。分发清单 `Plugin.toml` 是否沿用及其字段仍未冻结。
3. 旧方案允许 Module 和多个 CMake Target / 物理镜像之间灵活映射；**ADR-0005 已收紧为一个 Module 对应一个库 Target 和一个主要产物**。
4. 不建立独立 BinaryImageManager / BinaryImageDescriptor；物理库文件和 CodePin 是现有 Module 管理机制的内部实现细节。

## Compile-Time / Build-Time First 的构建职责

按 [C++ 规范与最佳实践](cpp-guidelines.md)，类型与静态分发应尽量在编译期解决。Module 依赖、编译器/标准库配置和按需生成代码属于 CMake 与目标工具链职责；Shader/Asset 处理与打包只由实际消费者或显式命令触发。独立交付二进制的兼容性描述属于交付边界，不挂入普通 Build-Time 增量链路。

- CMake 仍是构建依赖的权威来源；可由 PanguMeta 等工具按需要生成代码，生成源文件归入对应 Module 唯一库 Target。
- 编译器与目标平台配置仍由真实 Toolchain/Target 表达并验证；历史 [ABI Profile v1](abi-profile-fingerprint.md) 不再参与同树普通构建。独立交付 C++ 二进制的下一版配置描述、可信发布和 Loader 绑定另行设计。
- 预生成并不取代 Runtime 对外部输入、动态模块状态、设备能力以及资源版本的必要验证。
- 元数据生成、Shader/Asset Pipeline 和完整增量构建协议均为后续设计，不因通用最佳实践而自动视为已实现。

## 必须解决的设计问题（尚未冻结）

| 主题 | 待决问题 |
| --- | --- |
| CMake 组织 | 根工程、五平台 Presets 和最小 Module 设置已经落地；长期封装 API、Generator 组合、多配置与完整目标 Toolchain 管理尚未冻结 |
| 库类型与部署策略 | `STATIC` / `SHARED` 是逐 Module CMake Target 类型；`Monolithic` / `Modular` 仅在需要统一部署策略时作为顶层 CMake 选项。具体默认库类型、Preset、平台策略与选项命名待决 |
| 产物布局 | 库、Manifest、符号文件、动态库导出、安装和分发目录 |
| Meta 生成 | Clang 属性提取、代码生成、增量依赖、多配置构建 |
| ABI | 同树普通构建与独立交付边界已由 ADR-0013 冻结；历史 strict-v1 已退役。下一版独立交付配置描述、可信产物绑定、预检与二次复核待真实消费者出现后设计 |
| ThirdParty | Source/Binary 版本、目标导出与平台链接 |
| 交叉编译 | Android ARM64/iOS ARM64 的历史 STATIC 编译与归档结果已记录；新方案需验证原生编译与独立链接冒烟，设备执行和外部 SHARED 加载仍待验收 |

## 验证要求

后续至少验证 Module 与 Target 的一对一关系、产物数量、编译数据库、增量构建一致性、Meta 生成依赖、ABI 兼容配置、CodePin 以及平台矩阵。已有 PoC 不等于正式 Runtime / 多平台验收。

## 现行 Toolchain 与 ThirdParty 回归

`BUILD_TESTING=ON` 时，`Tests/Toolchain/` 的 `ThirdParty::CompiledProbe` 和 `Pangu::ToolchainProbe` 使用所选平台 Clang/C++23 编译为真实 STATIC 库。普通第三方库不继承 `PanguBuildOptions`；桌面 `ToolchainLibraryLink` 通过链接两个库并运行 CTest 验证跨库 C++23 返回值。编译器/头依赖与命令变更导致的重编由 CMake + Ninja 判断，不执行自制 ABI 审计。

`BUILD_TESTING=OFF` 时不构建上述测试目标。Android/iOS 正式 Preset 默认关闭 Tests；单独的移动平台最终链接冒烟须由显式目标执行，不纳入默认 `ALL`。详情见 [Toolchain 测试](../../Tests/Toolchain/README.md)。

## 历史 ABI Collection（2026-10-23 至 2026-10-27，已退役）

旧分支曾为受管 STATIC Module 实现逐 TU 宏审计、Compiler Launcher、Guard/Collect、完整收据及闭包检查，历史 CTest 记录为 Windows 36/36、Linux 34/34、macOS 34/34；这些是 **2026-10-27 的旧实现成绩，不是当前测试数量或构建性能**。旧工具及仅验证其机制的 Tests 均已删除，不再将 `complete.json`、`Abi/compile-evidence.json` 作为活动产物。

历史设计与验收仍可在 [2026-10-23 ABI Collection 实施计划](../../docs/superpowers/plans/2026-10-23-abi-collection.md) 和 [v1 历史协议](abi-profile-fingerprint.md) 中查阅；源码如需追溯使用历史 Git 提交，当前目录已不再有该工具。现行边界以 [ADR-0013](../decisions/0013-native-build-abi-boundary.md) 为准。
