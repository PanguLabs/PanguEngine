# 原生增量构建与 ABI 交付边界：验收记录

> 状态：Task 1–6 本期构建改造已验收（五平台默认 ALL、三桌面 CTest、移动端链接冒烟）；独立交付 SDK/Plugin 的兼容性实现属于后续任务。
> Plan：[2026-11-02-native-build-abi-boundary.md](2026-11-02-native-build-abi-boundary.md)
> Spec：[设计](../specs/2026-11-02-native-build-abi-boundary-design.md)
> 日期：2026-11-02

## Task 2：改造前默认 `ALL` 基线 A

Windows、Linux、Android 目标使用原版 `9a123d6f70b8`。macOS、iOS 的有效重测期间，另一个会话已于 2026-11-02 17:40:39 +0800 将共享源码 checkout 切至 `533b816`；两提交在 `CMakeLists.txt`、`CMakePresets.json`、`cmake/`、`Source/`、`Tests/`、`BuildTools/` 与 `.gitmodules` 的差异为零（`git diff --quiet` 返回 0），只有 wiki/docs 变化。因此 macOS、iOS 是**构建输入等价、Git checkout 不同**的改造前样本，不得声称精确 9a 提交。此偏差已在整分支自审中记录为明确裁决：构建输入等价可用于本轮性能判断，但并不满足字面意义的“精确 checkout 9a”；若后续要求精确 SHA，应在隔离 checkout 重测。

计时均使用仓库正式 preset、Release/Ninja、原始 `BUILD_TESTING` 设置、`CMAKE_BUILD_PARALLEL_LEVEL=8`；单轮用全新宿主本地 `BuildRoot/native-abi-bench/<run>/<A-轮次>` 目标目录和独立 `PANGU_BUILD_TOOLS_TEST_ROOT`（旧工具宿主缓存）。逐轮单独记录 `configure`、不指定 `--target` 的 `cmake --build <Bdir>` 默认 `ALL` 和两者总和；CTest 不计入。新构建树不等于冷 OS Page Cache。原始 stdout/stderr、JSON、执行脚本在本计划专属 `.superpowers/sdd/` 和各宿主 BuildRoot，Git 只提交此摘要。

| 平台 / 宿主 | Configure 轮 1/2/3（秒） | 默认 ALL 轮 1/2/3（秒） | 总和轮 1/2/3（秒） | Configure / ALL / 总和中位数（秒） |
|---|---|---|---|---|
| Windows / NTFS | 14.863 / 14.980 / 15.613 | 137.087 / 137.034 / 137.648 | 151.950 / 152.014 / 153.261 | 14.980 / **137.087** / 152.014 |
| Linux / WSL ext4 | 14.322 / 11.758 / 11.683 | 177.833 / 177.833 / 175.633 | 192.155 / 189.591 / 187.316 | 11.758 / **177.833** / 189.591 |
| Android ARM64 / Windows NTFS | 14.901 / 13.562 / 13.726 | 115.601 / 116.998 / 115.758 | 130.502 / 130.560 / 129.483 | 13.726 / **115.758** / 130.502 |
| macOS / APFS（构建输入等价） | 62.021 / 60.768 / 62.557 | 257.259 / 260.350 / 253.468 | 319.280 / 321.118 / 316.024 | 62.021 / **257.259** / 319.280 |
| iOS ARM64 / macOS APFS（构建输入等价） | 61.593 / 62.007 / 61.904 | 119.722 / 117.678 / 119.613 | 181.315 / 179.686 / 181.517 | 61.904 / **119.613** / 181.315 |

验证：十五份 JSON 样本均记录零退出码后的时间，Windows/Linux/macOS 末尾是测试或 Static Catalog 链接，移动端末尾是 `PanguModule` 静态归档。Android/iOS 此处只记录默认 `ALL`，不代表目标最终 EXE 链接或设备运行成功。工具版本 Windows CMake 4.4.3、Linux 4.4.2、macOS 4.4.3；MacPorts Clang 23.1.2，Linux 与 Windows 为项目既定 Clang 23，Android 使用项目现有 NDK Clang。

macOS 初次计时的 3 个 JSON 含负耗时：旧 Bash 脚本通过不同 Python 进程采样 `time.perf_counter()`，在该主机上得到不一致起点。**全部判为无效、不参与中位数**；使用单个持久 Python 进程重测三轮，日志保存于 `baseline/A/macos-r2/`。iOS 也使用相同的持久 Python 计时脚本。没有清除无效记录或其他任务缓存。

## Task 4：旧 AbiProfile 清理与 Windows 构建图验收

本次从生产 CMake、Runtime、Catalog 测试夹具和 Toolchain 默认 `ALL` 中移除 ABI launcher、guard/collect、逐 TU 收据、宿主 bootstrap 及其专用测试。仅保留原生 CMake/Ninja 和有效的 `PanguBuildOptions`/Toolchain 编译策略；四个先前受 hook 管理的库（Foundation、Module、Catalog First/Second）明确保留 `CXX_SCAN_FOR_MODULES OFF`，没有全局禁用扫描。

| 旧测试组 | 去留理由 / 替代验证 |
|---|---|
| `AbiCollectionLauncherContract`、`AbiCollectionBuildContract*`、`AbiCollectionBuildTools*`、`AbiCollectionCMakeGraph`、`AbiCollectionDependencyGraph` | 只测试已删除的 launcher、bootstrap、依赖闭包和收据重建算法；不保留空壳 |
| `AbiCollectionGroup`、`AbiCollectionState`、`AbiCollectionOutputWait`、`AbiCollectionCompileCommands`、`AbiCollectionProcess`、`AbiCollectionRequest` | 对象证据的采集、状态锁、等待、进程包装、命令重算已经退出生产契约；依赖失效由 Ninja 负责 |
| `AbiProfileVectors`、`AbiProfileCli`、`AbiProfileIo`、`AbiWindowsProfileMapping`、所有 `AbiCollector*` 和平台 CLI | 历史 strict-v1 Schema/Canonical/Fingerprint 与五平台 collector 退役；历史 schema/vectors 仍保存在 wiki，未来独立交付另行冻结 |
| `AbiCompileAudit*` 及其 RTTI/异常/缺数据库负例 | 默认 TU 宏审计已退役；编译器/异常/STL/C++23 约束由真实 `PolicyProbe.cpp` 与平台 Toolchain 配置检查 |
| `JsonExtras` 与 nlohmann/json 缺头配置 | 已迁至 `Tests/ThirdParty/NlohmannJson`，新增 `NlohmannSubmoduleRequired`，独立 CTest 两项通过 |
| `BuildEnvironment`、`ToolchainLibraryLink`、Foundation、Module 注册、静态 Catalog、Scope/生命周期相关测试 | 保留现有行为与实际跨库链接；未因 ABI 测试目录删除而丢失 |

Task 4 RED：未移除接线时，`ninja -t commands all` 有 21 条匹配旧 ABI 命令且产生 `Abi/` 目录。GREEN：全新 Windows `V-on-native` 默认 `ALL` 成功，重复构建 `ninja: no work to do.`，CTest **13/13** 通过；全新 `V-off-native` 默认 `ALL` 成功，重复构建零动作，CTest 注册 **0** 项。ON/OFF configure 和生成命令均没有 `PanguAbiProfileTool`、旧 `AuditCompilerMacros`、`inputs.h`、`Abi/` 目录或隐式资产/打包步骤。原始输出保存在本计划自有 workspace，未更改原有 BuildRoot。

## Task 5：真实增量与移动端最终链接

Windows 在忽略的 `Build/Windows/native-abi-verify/incremental-checkout` 创建独立 detached worktree，提交 `15275fd`，并从本机已固定的上游 checkout 本地复制相同 gitlink `55f93686` 的 nlohmann/json；不修改共享源码。相应构建树位于独立 `incremental-build`。

| 验证条件 | 实际 Ninja/Clang 动作 |
|---|---|
| 初次构建 Foundation、Module、ModuleIdentityTests、RegistrationTests | 16 个原生扫描、编译、归档和链接动作；再构建显示 `ninja: no work to do` |
| 触碰私有 `ModuleIdentity.cpp`，只构建 `PanguModule` | **1 个 TU 编译 + 1 个归档**；没有 Foundation 或其他 Module TU 编译 |
| 再请求两个 Module 测试 EXE | **仅 2 个链接动作**，没有二次编译共享库 |
| 触碰公共 `ModuleIdentity.h` | `ninja -t deps` 有 7 条命中；编译其依赖的 **5 个 Module TU** + 1 个归档，Foundation 没有重编；随后 no-op |
| 仅对 `PanguModule` 临时增加 PRIVATE `PG_PLAN_FLAG_PROBE=1` | **5 个 Module TU** 编译命令都含该宏 + 1 个归档；Foundation 无变化，随后 no-op |

临时定义只存在于一次性工作树，未写入 feature branch。第一次检查私有 TU 时误将归档命令列出的其他输入对象误判为重新编译；改为核对 Ninja 实际 `[N/M]` 动作后重新 touch 并通过验收。此误判未修改生产代码。

独立 LinkSmoke 的 RED 是不存在 `Tests/Toolchain/LinkSmoke` 时配置失败；GREEN 使用实际工具链链接 `PanguPlatformLinkSmoke`，不加入默认 `ALL`、不调用 AbiProfile、不链接引擎生产库，也不执行移动设备程序：

- Android：Windows NDK r30 Clang 21，`aarch64-none-linux-android29`，`clang++` 确实完成最终链接；`llvm-readobj --file-headers` 显示 ELF64/AArch64，`EM_AARCH64`。
- iOS：MacPorts Clang 23.1.2，iPhoneOS18.2 SDK，arm64 / iOS 18.0；`clang++` 实际链接 `PanguPlatformLinkSmoke.app/PanguPlatformLinkSmoke`，`otool -hv` 为 Mach-O ARM64 EXECUTE，`LC_BUILD_VERSION platform 2`，minos 18.0、SDK 18.2。第一次检查误用裸二进制路径而失败，改为 Bundle 内实际路径后通过。

## Task 6：最终 B 候选默认 ALL 与五平台验收

最终冻结的 B 候选提交为 `82598e65358d2369765295acbd1a1f7bcfd3ab19`，已包含 Foundation 的 `cmake_minimum_required(VERSION 4.0)`（原修复提交 `0c9e527`）和历史文档维护。三轮独立新目录、正式 Preset、Release/Ninja、并行度 8、保留测试设置；以 `cmake --build <Bdir>` 不指定 Target 的墙钟耗时为唯一 10% 性能门槛。CTest 单独执行、不计时；A 和 B 同平台单独比较，不跨平台平均抵消。

| 平台 | A configure 中位数 | B configure 中位数 | A ALL 中位数 | B ALL 中位数 | B/A | ALL 降幅 | A 总和中位数 | B 总和中位数 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Windows | 14.980 | 1.105 | 137.087 | **4.680** | 0.0341 | 96.6% | 152.014 | 5.785 |
| WSL Linux | 11.758 | 0.396 | 177.833 | **3.169** | 0.0178 | 98.2% | 189.591 | 3.522 |
| Android ARM64 | 13.726 | 0.606 | 115.758 | **2.234** | 0.0193 | 98.1% | 130.502 | 2.843 |
| macOS | 62.021 | 1.610 | 257.259 | **22.622** | 0.0879 | 91.2% | 319.280 | 24.149 |
| iOS ARM64 | 61.904 | 1.123 | 119.613 | **5.617** | 0.0470 | 95.3% | 181.315 | 6.722 |

上述 5 项全部小于 `1.10`，因此符合本计划的默认 `ALL` 性能门槛。下表为最终 B 的三轮原始时间（秒，顺序 `configure / ALL / 总和`；每次成功返回，未因失败替换样本）：

| 平台 | B-1 | B-2 | B-3 |
|---|---|---|---|
| Windows | 1.105 / 4.680 / 5.785 | 1.067 / 4.555 / 5.622 | 1.227 / 4.961 / 6.189 |
| Linux | 0.422 / 4.010 / 4.432 | 0.396 / 3.066 / 3.462 | 0.354 / 3.169 / 3.522 |
| Android | 0.507 / 2.420 / 2.927 | 0.606 / 2.234 / 2.840 | 0.717 / 2.127 / 2.843 |
| macOS | 1.779 / 23.265 / 25.044 | 1.527 / 22.622 / 24.149 | 1.610 / 22.025 / 23.636 |
| iOS | 1.211 / 6.114 / 7.325 | 1.105 / 5.617 / 6.722 | 1.123 / 5.385 / 6.508 |

三桌面最终 B-3 完整 CTest：**Windows 13/13、Linux 13/13、macOS 13/13，合计 39/39**。再次运行相同目标均输出 `ninja: no work to do`。五平台最终默认 `ALL` 的 Ninja 命令没有旧 AbiProfile、launcher、逐 TU 收据、强制 `inputs.h` 或隐式资产处理；`Abi/` 派生目录不存在。Android 与 iOS 正式 preset 保留 `BUILD_TESTING=OFF`，对应 B-3 构建产出实际 `PanguModule` 静态库；Task 5 已另行验证两平台独立最小 EXE 最终链接，**没有设备执行或引擎生产库最终链接的声明**。

最终 A/B 三轮 JSON 与 stdout/stderr 已保留于各宿主专属 BuildRoot：Windows/Android 的 `native-abi-bench/20261102-native-abi/logs-A-archive` 与 `native-abi-bench/20261102-native-abi-final/logs-B-archive`；WSL Linux 同名宿主本地目录；macOS 的 `20261102-native-abi-macos-r2/logs-A`、`20261102-native-abi-macos-final/logs-B`；iOS 的 `20261102-native-abi-ios-r1/logs-A`、`20261102-native-abi-ios-final/logs-B`。较早的 `-0c9e527` macOS/iOS 目录属于另一轮样本，不用于本表复算。旧 B 候选 `ac712d6` 的数据仅作过渡记录。A 侧 macOS/iOS 的实际 checkout 偏差依上文保留。最低声明为 CMake 4.0，实际五平台使用 CMake 4.4.x，未独立测试 CMake 4.0.x 二进制。

## 当前边界

本期未实现独立 SDK/原生插件的新版 ABI 配置描述、可信装载前预检、Loader、发布器或独立 C++ 二进制兼容性证明；未执行 Android/iOS 设备测试。整分支先由实施者自审，随后于 2026-11-02 完成独立两轴评审（Standards / Spec），发现与处理见下节。审查结论不等于独立交付实现的验收，性能提升也不证明上述未实现能力。

## 独立评审（2026-11-02）

范围：`main...HEAD`（固定点 `9a123d6`，候选 `f356f55`）。两个独立 reviewer 分别按仓库规范与 Spec 核对，结论如下。

| 发现 | 处理 |
|---|---|
| `wiki/foundation/build-system.md` 历史章节仍以现在时描述已删除的 AbiProfile 工具与测试 | 改为历史叙述，明确删除状态并指向 ADR-0013 |
| `docs/superpowers/specs/2026-10-23-abi-collection.md` 设计状态仍为 Approved | 改为 Superseded，保留历史实现状态 |
| `wiki/README.md` 等 6 份文件内容已改但“更新”日期未变 | 统一改为 2026-11-02 |
| 新增 bullet 与原有 bullet 重复“CMake 是构建关系的权威来源” | 改写新增条目，去掉重复子句 |
| 三处 `CXX_SCAN_FOR_MODULES OFF` 无原因说明 | 各补一行注释，说明只对受控目标关闭 |
| `abi-profile-fingerprint.md` 第 6 节标题仍写 Frozen | 改为“独立交付预检原则 Frozen；strict-v1 构建流程已退役” |
| `e9f85fe` 在 `BUILD_TESTING=ON` 时不可配置：文件已 rename，但 `Tests/AbiProfile/CMakeLists.txt` 仍引用旧路径，新 `Tests/ThirdParty/NlohmannJson/CMakeLists.txt` 尚未加入 | 属实；不改写历史。分支尖端 `f356f55` 已验收，bisect 需跳过该中间提交 |
| macOS/iOS 的 A 基线使用 `533b816` 而非字面 `9a123d6` | 已在 Task 2 记录为构建输入等价偏差，保留；不声称精确 SHA |
| LinkSmoke 内联 iOS Clang 版本门 | 不采纳为缺陷：该工程独立于引擎根，检查符合 `wiki/foundation/apple-toolchain.md`；补充引用注释 |

评审期间重新执行的验证（与既有记录独立）：

| 项目 | 命令要点 | 结果 |
|---|---|---|
| clang-format | `clang-format --dry-run --Werror Tests/ThirdParty/NlohmannJson/JsonExtrasTests.cpp Tests/Toolchain/LinkSmoke/Main.cpp` | 通过 |
| clang-tidy | 新建 Windows 验证树导出编译数据库，`clang-tidy -p <tree> tests/.../JsonExtrasTests.cpp` | 0 条用户代码诊断 |
| 缺头负例 | `cmake -DPANGU_SOURCE_ROOT=... -DPANGU_TEST_ROOT=... -P MissingHeaders.cmake` | 两个缺失头均被拒绝，退出 0 |
| 缺头负例反向验证 | 在隔离副本移除 wrapper 缺头检查后重跑同一脚本 | 如期失败（退出 1），证明负例有区分力 |
| Android LinkSmoke | NDK r30 Clang，`--target=aarch64-none-linux-android29` 最终链接 | 退出 0；`llvm-readobj` 为 ELF64/AArch64 |
| iOS LinkSmoke | MacPorts Clang 23.1.2 + iPhoneOS18.2 SDK 最终链接 | 退出 0；`otool` 为 Mach-O ARM64 EXECUTE、platform 2、minos 18.0 |

这些验证只覆盖同树构建与最小链接边界；不改变“独立 SDK/插件尚未实现”的结论。
