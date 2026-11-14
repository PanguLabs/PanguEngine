# PanguMetaTool SDK 配套依赖实施计划

> **For agentic workers:** 使用 superpowers:executing-plans 在共享源码功能分支逐项实施，结尾独立评审。

**Goal:** 通过仓库固定源码及 Prepare.cmake 准备 LLVM SDK 的 zlib/zstd 配套依赖，不使用 vcpkg。

**Architecture:** ThirdParty 固定 submodule + 独立宿主 CMake 静态库工程；Prepare 构建并生成查找配置，然后 Check 验证 SDK。Engine 主图只消费工具和 SDK 配置。

**Tech Stack:** CMake 4、Ninja、Clang 23、zlib 1.3.2、zstd 1.5.7、Git submodule。

**Spec:** [设计](../specs/2026-11-14-pangumeta-sdk-dependencies-design.md)

## Global Constraints

- 不使用 vcpkg，不构建/下载 LLVM；CMake 不联网、不更新子模块。
- 固定源码与许可证纳入仓库，宿主产物不进入 Engine/移动目标。
- 使用所选宿主 Clang、Release 和匹配 SDK 的 CRT；BuildRoot 按宿主隔离。
- 不修改子模块源码，不覆盖无关目录；Check 失败不发布 SDK 配置。

## Review Focus

- 重复运行和并发准备不能破坏已有 SDK 或重编不变依赖。
- 子模块提交不匹配、源码缺失和已有未知目录必须明确拒绝。
- Windows 静态库的 CRT/导出目标及 DIA SDK 定位应匹配现成 SDK。
- 依赖配置必须可重用，不保留 vcpkg 或其他宿主绝对路径。
- 依赖准备成功但 SDK 检查失败不能发布成功配置。

### Task 1: 固定依赖与独立宿主工程

**Files:** `.gitmodules`、`Source/ThirdParty/{Zlib,Zstd}/`、`BuildTools/PanguMetaTool/Sdk/Dependencies/`、`Tests/BuildTools/PanguMetaTool/`。

**Interfaces:** 独立 Ninja 工程构建并安装 zlib/zstd 静态库，导出 LLVM SDK 需要的标准查找目标。

- [x] 添加真实依赖准备/压缩往返测试并观察旧入口失败。
- [x] 获取官方固定 submodule，记录提交和许可证；实现原生 CMake 适配。
- [x] 构建/运行独立依赖测试，验证安装包发现及上游源码保持干净。

### Task 2: Prepare 工作流与接入

**Files:** `Sdk/Prepare.cmake`、`Sdk/Check.cmake`、`Sdk/Dependencies/`、正式测试接线与 README/wiki。

**Interfaces:** 现有显式 SDK 路径 + `PG_BUILD_JOBS`，成功发布 `PanguMetaSdk.cmake`。

- [x] 实现身份/锁保护、依赖构建/安装、Windows DIA 定位和 Check 调用。
- [x] 验证 Prepare→Check→工具全套测试，检查无变化构建和失败路径。
- [x] 同步规范、使用命令与三平台验收；独立评审并修复有证据的问题。
- [x] 归档摘要，任务退出后清理本计划临时目录，保留正式测试及源码。

## 执行记录

上一阶段先按用户要求提交为 `528c736`。本计划从该提交开始，不重跑未受影响的完整 Engine/移动矩阵。

### 实施结论

固定 zlib 1.3.2（`da607da739fa6047df13e66a2af6b8bec7c2a498`）与 zstd 1.5.7（`f8745da6ff1ad1e7bab384bd1f9d742439278e99`）官方 submodule。CMake 只消费已检出的源码，不联网或更新子模块；两个上游工作区在构建后保持干净，无补丁。

Prepare 使用独立 `NativeDependencies/Build` 和 `Install`，保留 LLVM 及其他任务产物。身份标记防止误用目录，锁覆盖整个准备过程。Windows 通过 VS 环境/vswhere 定位 DIA，通过仅加载 SDK 元数据的 Inspect 工程选择实际 CRT。当前 SDK 导出 `MultiThreaded`，原生 zs/zstd 对象的 `/DEFAULTLIB:libcmt.lib` 已核对，修正后的工具链接不再出现此前 DLL CRT 导入/rpmalloc 的 LNK4217 提示。

Check 在候选文件上成功后才发布稳定依赖与 SDK 配置；无变化准备不重编库、不改写库/SDK/依赖配置 mtime。Windows/Linux/macOS 正式本机 SDK 配置已切到 NativeDependencies；Windows/Linux 正式宿主工具通过 BuildMetaTool.cmake 更新并通过 `--check-sdk`，不再引用上一阶段的 vcpkg 库目录。旧目录不自动删除。

### 验收（2026-11-14）

| 平台 | 现成 SDK | 最终独立 PanguMetaTool CTest | 覆盖 |
| --- | --- | --- | --- |
| Windows | Clang/LLVM 23.1.5 | 73/73，通过 | 真实静态 SDK/native zlib/zstd 链接、CRT、属性/生成消费及准备回归 |
| WSL Ubuntu | Clang/LLVM 23.1.0 | 74/74，通过 | 原生依赖安装/导出、现成 clang-cpp、生成目标 libc++ 与准备回归 |
| macOS | MacPorts 23.1.2 | 74/74，通过 | 共享源码/APFS 原生依赖、Clang/LLVM 动态库及同范围回归 |

工具测试缓存均为宿主 BuildRoot 下 `Tests/BuildTools/PanguMetaInstalledSdk`。命令：先以 `cmake -S BuildTools/PanguMetaTool -B <cache> -G Ninja -DBUILD_TESTING=ON -DPANGU_META_SDK_CONFIG=<本机配置>` 配置，再 `cmake --build <cache> --parallel 4`，最后 `ctest --test-dir <cache> --output-on-failure --no-tests=error --parallel 4`。Prepare 使用 README 中的显式 SDK 参数和 `PG_BUILD_JOBS`。

新增五项正式 MetaSdk.NativeDependencies 测试：RoundTrip、RejectsOccupiedDependencyRoot、RejectsInvalidSdk、RejectsMissingSources、RejectsWrongRevision。RoundTrip 包含真实压缩/解压、已安装导出接入、旧 zstd 缓存迁移、无变化 Ninja 复用和 mtime；InvalidSdk 先建立有效配置，Windows 再切换到另一份实际 DIA 夹具并故意破坏资源头，验证主配置及被引用依赖配置均保持原状。未降低旧 Prepare 目录保护和旧 LLVM 源码入口拒绝覆盖。

静态检查：新增 C 夹具使用实际独立依赖工程的编译数据库通过仓库 clang-tidy；显式补齐 zconf.h 后无该项 include-cleaner 诊断。补头文件只影响测试夹具，在上述完整套件后又在三桌面各重编并运行压缩往返，分别 1/1 通过，不将其与根测试调度数量累加。clang-format 和 `git diff --ignore-submodules --check` 通过。本轮未修改 C++ TU，未重复全库 clang-tidy。

### RED/GREEN 与独立评审

- 初始 RoundTrip 在旧 Prepare 入口失败；实现原生依赖准备后通过。
- 上游 ZLIB::ZLIBSTATIC 的别名不能完整进入 SDK 的 try_compile 闭包；改用标准 FindZLIB 构造真实导入目标后通过，不修改上游。
- 评审 P2：NO_DEFAULT_PATH 未覆盖有效旧 zstd_DIR 缓存。已种植会主动失败的旧包路径，观察 RED，再强制缓存指向原生安装，定向与完整套件 GREEN；正式 Windows Check 缓存也已确认迁移。
- 收尾补验同类 SDK 元数据缓存：Windows RoundTrip 的重复准备阶段种植有效但错误的 LLVM_DIR，观察 RED，再让 Inspect 与主工程一样固定所选 SDK 路径，定向 GREEN。该补验只影响 Windows 元数据选择；其他已通过套件不重复运行。
- 评审 P2：检查前覆盖旧 SDK 引用的依赖配置。已用另一 DIA 路径加无效资源复现 RED；改为候选验证后发布，主配置及引用配置均保持的回归和完整套件 GREEN。
- Ruling：Windows SDK 明确导出静态 CRT，因此读取 SDK 元数据，替代早期猜测 DLL CRT 的实现；不会通过抑制链接警告掩盖配置差异。
- Ruling：用户已批准方案并要求继续执行，沿用共享源码功能分支，不再重复询问设计、工作区或依赖获取方式。

### 收尾与边界

实现随本计划以 `feat: 原生准备 PanguMetaTool SDK 配套依赖` 提交，基线提交为 `528c736`；不推送或合并。正式测试、规格和验收摘要长期保留，仅删除本计划 `.superpowers/sdd/2026-11-14-pangumeta-sdk-dependencies/` 临时工作区。

未重跑完整 Engine full、链接模式或 Android/iOS 矩阵；本轮改动限定宿主依赖准备和工具接纳。仍需预先安装 Clang 开发包、CMake/Ninja/Git，以及 SDK 声明的系统工具（如 Windows DIA SDK）；不提供 SDK 下载器或完整发行安装器。
