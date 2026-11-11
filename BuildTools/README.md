# BuildTools — 独立宿主构建工具

`PanguMetaTool/` 是正式独立元数据工具，包含 Model、Frontend、Output、CodeGen 普通静态实现目标；不是 Runtime Module，也不提供 `Pangu::` Alias。`--check-sdk` 验证显式准备的 LLVM/Clang SDK；`generate --request <absolute-json>` 消费 v1 scan/query/catalog 派生请求。成功/处理失败/CLI 用法错误分别返回 0/1/2，诊断写 stderr，不导出磁盘 IR。

SDK 使用 `PanguMetaTool/Sdk/Prepare.cmake` 显式准备并生成 `PanguMetaSdk.cmake`；Windows 使用 MSVC STL，Linux/macOS 使用 libc++，自有工具及依赖保留 RTTI。普通引擎构建不隐式编译 LLVM。生成器按目标数据库读取真实上下文，目标编译器编译生成 C++；移动图不导入 LLVM targets。每 Target/Config 唯一发布批次，内容相同保持 mtime、stamp 最后提交，失败可重试且只清理本批次拥有产物。

Meta 生产缓存为 `<HostBuildRoot>/BuildTools/PanguMetaTool`，测试缓存为 `<HostBuildRoot>/Tests/BuildTools/PanguMetaTool`。根 Full 构建准备全部工具测试目标，再由九个互斥 `BuildTools.PanguMetaTool.Meta*` 组运行内层测试；内外层分别保留 JUnit/日志。原生工具输入失效才重建，共享宿主缓存有短期锁，普通 no-op 不启动工具。三桌面/ARM64 移动、性能、限制及决策归档见[正式验收](../docs/superpowers/plans/2026-11-09-pangumeta-tool-validation.md)。

`PanguBuildTool/` 是源码 Plugin 的独立 Build Tool，不是 Runtime Module，不提供 `Pangu::` Alias。已实现基于 `toml++` 的严格 `Plugin.toml` 校验、固定 Engine/Project 双根发现和稳定 JSON 输出。**所有顶层 Configure 固定配置、请求构建并执行该工具，包括零 Plugin**；首次实际编译，后续由独立 Ninja 图复用未变化的产物。普通无变更 Ninja 不启动工具或扫描插件。

工具由独立的宿主 Clang/C++23 CMake BuildRoot 构建，不继承 Android/iOS 目标 Toolchain 或 `PanguBuildOptions`；普通 Ninja 的编译和链接由 CMake 原生依赖管理。原 AbiProfile、Compiler Launcher、逐 TU 收据和 Pangu 自制构建内核已退役，参见 [ADR-0013](../wiki/decisions/0013-native-build-abi-boundary.md)、[ADR-0014](../wiki/decisions/0014-source-plugins-project-build.md)。

三类工具测试源码集中在 `Tests/BuildTools/PanguBuildTool/`。根工程通过
`cmake/PanguBuildToolsTesting.cmake` 的 `pangu_add_build_tools_tests()` 显式登记工具工程、
测试组、原生构建目标及 CTest 正则；共用机制不内置具体工具。根配置仅登记测试准备目标，
`PanguBuildToolTestsFast/Precommit/Full` 在构建验收阶段配置独立宿主测试工程，并依次准备累计集合。
它们由 `PanguTestsFast/Precommit/Full` 依赖，不属于普通 `ALL`。

测试分类与单 BuildRoot 会话边界见[测试规范](../wiki/foundation/testing.md)。根调度与独立工具 CTest 的结果应分轮记录，不合并计数。

Windows 验收入口如下；Linux/macOS 将 preset 改为 `linux` / `macos`，直接 CTest 的目录分别为
`$HOME/PanguEngineBuild/Linux/Tests/BuildTools/PanguBuildTool` 和
`$HOME/PanguEngineBuild/macOS/Tests/BuildTools/PanguBuildTool`。

```sh
cmake --preset windows
cmake --build --preset windows --target PanguTestsPrecommit
ctest --preset windows -R '^BuildTools[.]PanguBuildTool[.]' --no-tests=error --output-on-failure
ctest --test-dir Build/Windows/Tests/BuildTools/PanguBuildTool --no-tests=error --output-on-failure
```

根入口三个组依次选择 `^PanguPluginManifestTests[.]`、`^PanguPluginDiscoveryTests[.]`、
`^PluginCli$`；独立宿主 CTest 仍按 GoogleTest 用例逐项登记。两个入口可分别使用，正式一轮
完整验收从根入口调度一次，避免重复执行工具用例。内层失败、空匹配、缺程序、关键跳过
均使根调度失败；每次原始 JUnit XML 和日志保存在根 BuildRoot 的
`Tests/Results/BuildTools.PanguBuildTool.<Group>/`，GoogleTest XML 保存在宿主测试
BuildRoot 的 `Tests/Results/<target>/`。

也可直接配置独立工具工程，显式设置宿主 Clang、原生 Toolchain、`BUILD_TESTING=ON`
及专用 binary directory；仅此开关开启时才访问根测试源码和固定 GoogleTest 依赖。
普通 bootstrap 继续使用 `<BuildRoot>/BuildTools/PanguBuildTool` 与 `BUILD_TESTING=OFF`，
不会访问根 `Tests/`。测试专用 `<BuildRoot>/Tests/BuildTools/PanguBuildTool` 与其缓存、产物完全分离。
测试程序保持工具自身无异常策略并保留 RTTI。CTest 为每例设置独立工作目录；直接批量运行
`PanguPluginManifestTests` / `PanguPluginDiscoveryTests` 时，用 `cmake -E chdir <显式工作目录> <程序路径>`
提供 BuildRoot 内已创建的工作目录。目录重解析逃逸在 Windows 使用真实 Junction，
Linux/macOS 使用真实 directory symlink；先核对 canonical 外部目标，再验证拒绝。
关键链接夹具创建失败会使测试失败。中文/Emoji/空格覆盖的是源码副本、可变文件系统与 CLI
路径，不代表 GoogleTest 支持任意 Unicode BuildRoot。

Linux 宿主工具使用现有 `cmake/Platform/Linux/Toolchain.cmake` 选择匹配 LLVM 的 libc++，不依赖系统 libstdc++。嵌套构建会迁移本工具私有缓存中旧的 C++/EXE 链接初始化值；独立手工配置若复用过旧缓存，会明确要求重置对应变量或使用新 BuildRoot。Windows 使用原生宽字符 CLI 入口，文件系统路径保留原生表示，JSON 与诊断使用 UTF-8。

资产打包、Shader 处理、代码生成由实际消费者或显式命令触发，不提前建立通用发布缓存、隐式 `ALL`、`POST_BUILD` 流水线或独立 Pangu Plugin/SDK 二进制 ABI Preflight。

`cmake/PanguPlugins.cmake` 提供 `pangu_discover_plugins(ENGINE_ROOT <checkout> [PROJECT_ROOT <project>] OUTPUT_JSON <variable>)` 和 `pangu_add_enabled_plugins(ENGINE_ROOT <checkout> [PROJECT_ROOT <project>] [ENABLED <id>...])`。根参数指向持有 `Plugins/` 的工程根，函数内部扫描其直接插件子目录；不是把 `Plugins/` 本身作为参数。所有候选都校验，启用目录按 ID 排序并以 `EXCLUDE_FROM_ALL` 纳入消费者的原生目标图。

每个插件的 JSON `configure_inputs` 数组由 C++ 工具收集，包含必需文件的实际目标及完整链接链路的父目录，覆盖工程根参数、插件目录、多级文件链接和 Windows Junction 的删除/改指。CMake 只把这些输入登记为原生 Configure 依赖。目录链接的目标目录不作为扫描依赖，新增 Plugin 仍需显式 Configure；父目录项增删可能使使用链接的插件额外重配。路径元数据缓存只在一次发现调用内有效，不建立永久索引。

独立 Project 使用 `cmake/PanguEngineRevision.cmake` 校验自己的 `Engine.commit`。受检目录必须是实际 Git 工作树根，支持独立 checkout、Submodule 和 linked worktree；正式配置默认核对完整 SHA、tracked 干净状态和递归 Submodule 状态。Project 同时把锁文件登记为 `CMAKE_CONFIGURE_DEPENDS`，修改锁文件后普通 Ninja 会重新校验。本地联调需显式设置 `PANGU_ENGINE_ALLOW_LOCAL_OVERRIDE=ON` 并保留差异诊断，不自动 checkout 或联网。`Tests/CMake/SourcePluginProject/` 是测试夹具，实例化时填充锁定 SHA；源码中的占位 `Engine.commit` 不代表可直接运行的正式游戏项目。

源码 Plugin 首轮三桌面与 Android/iOS 编译、链接、显式部署的历史范围见[验收记录](../docs/superpowers/plans/2026-11-04-source-plugins-project-build-validation.md)。Game/Editor 的 STATIC Catalog 与有效 Module Query 现由 PanguMetaTool 生成，原手写生产者已撤销；故意返回空 Query 的恶意 ABI 夹具保留手写包装。SHARED 文件部署本身不代表设备运行验收。
