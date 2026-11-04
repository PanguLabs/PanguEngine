# BuildTools — 独立宿主构建工具

`PanguBuildTool/` 是源码 Plugin 的独立 Build Tool，不是 Runtime Module，不提供 `Pangu::` Alias。已实现基于 `toml++` 的严格 `Plugin.toml` 校验、固定 Engine/Project 双根发现和稳定 JSON 输出。**所有顶层 Configure 固定配置、请求构建并执行该工具，包括零 Plugin**；首次实际编译，后续由独立 Ninja 图复用未变化的产物。普通无变更 Ninja 不启动工具或扫描插件。

工具由独立的宿主 Clang/C++23 CMake BuildRoot 构建，不继承 Android/iOS 目标 Toolchain 或 `PanguBuildOptions`；普通 Ninja 的编译和链接由 CMake 原生依赖管理。原 AbiProfile、Compiler Launcher、逐 TU 收据和 Pangu 自制构建内核已退役，参见 [ADR-0013](../wiki/decisions/0013-native-build-abi-boundary.md)、[ADR-0014](../wiki/decisions/0014-source-plugins-project-build.md)。

Linux 宿主工具使用现有 `cmake/Platform/Linux/Toolchain.cmake` 选择匹配 LLVM 的 libc++，不依赖系统 libstdc++。嵌套构建会迁移本工具私有缓存中旧的 C++/EXE 链接初始化值；独立手工配置若复用过旧缓存，会明确要求重置对应变量或使用新 BuildRoot。Windows 使用原生宽字符 CLI 入口，文件系统路径保留原生表示，JSON 与诊断使用 UTF-8。

资产打包、Shader 处理、代码生成由实际消费者或显式命令触发，不提前建立通用发布缓存、隐式 `ALL`、`POST_BUILD` 流水线或独立 Pangu Plugin/SDK 二进制 ABI Preflight。

`cmake/PanguPlugins.cmake` 提供 `pangu_discover_plugins(ENGINE_ROOT <checkout> [PROJECT_ROOT <project>] OUTPUT_JSON <variable>)` 和 `pangu_add_enabled_plugins(ENGINE_ROOT <checkout> [PROJECT_ROOT <project>] [ENABLED <id>...])`。根参数指向持有 `Plugins/` 的工程根，函数内部扫描其直接插件子目录；不是把 `Plugins/` 本身作为参数。所有候选都校验，启用目录按 ID 排序并以 `EXCLUDE_FROM_ALL` 纳入消费者的原生目标图。

每个插件的 JSON `configure_inputs` 数组由 C++ 工具收集，包含必需文件的实际目标及完整链接链路的父目录，覆盖工程根参数、插件目录、多级文件链接和 Windows Junction 的删除/改指。CMake 只把这些输入登记为原生 Configure 依赖。目录链接的目标目录不作为扫描依赖，新增 Plugin 仍需显式 Configure；父目录项增删可能使使用链接的插件额外重配。路径元数据缓存只在一次发现调用内有效，不建立永久索引。

独立 Project 使用 `cmake/PanguEngineRevision.cmake` 校验自己的 `Engine.commit`。受检目录必须是实际 Git 工作树根，支持独立 checkout、Submodule 和 linked worktree；正式配置默认核对完整 SHA、tracked 干净状态和递归 Submodule 状态。Project 同时把锁文件登记为 `CMAKE_CONFIGURE_DEPENDS`，修改锁文件后普通 Ninja 会重新校验。本地联调需显式设置 `PANGU_ENGINE_ALLOW_LOCAL_OVERRIDE=ON` 并保留差异诊断，不自动 checkout 或联网。`Tests/CMake/SourcePluginProject/` 是测试夹具，实例化时填充锁定 SHA；源码中的占位 `Engine.commit` 不代表可直接运行的正式游戏项目。

三桌面运行与 Android/iOS 编译、链接、显式部署的实际范围见[验收记录](../docs/superpowers/plans/2026-11-04-source-plugins-project-build-validation.md)。Game/Editor 各自手写 STATIC Catalog 仅用于首版集成验证；SHARED 文件部署不代表生产 Loader、CodePin 或设备运行验收。
