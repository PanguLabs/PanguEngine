# BuildTools — 构建工具

此目录保存正式构建流程依赖的工具源码。它与根目录 [`Tools/`](../Tools/README.md) 中的普通开发、维护和诊断工具区分；BuildTool 不是 Pangu Runtime Module，也不是 EngineHost。

当前包含 [`AbiProfile/`](AbiProfile/README.md)：采集实际目标编译配置、验证 ABI 与依赖闭包并生成 Profile/Fingerprint。

## 构建工具准备（Bootstrap）

`cmake/BuildTools.cmake` 的 `pangu_prepare_build_tools(outExecutable)` 准备工具并返回可执行程序路径：

1. 通过 `AbiProfile/Bootstrap/CMakeLists.txt` 配置独立的宿主本机 CMake 工程，使用 C++23、Clang 23 与 Release。
2. 复用同一份 Foundation 源码，在独立构建树中编译工具所需的依赖；不接入正式目标的 ABI 钩子。
3. 按工具源码、Probe、CMake 配置与编译器输入计算缓存键；命中时复用已发布版本。
4. 持锁构建并经暂存目录发布工具，正式目标引用 `published/<输入哈希>/PanguAbiProfileTool[.exe]`。

构建工具运行在构建宿主上：Windows 工具可服务于 Windows/Android 构建，macOS 工具可服务于 macOS/iOS 构建。宿主平台与目标平台的技术含义不变。

| 构建宿主 | 工具构建与缓存根目录 |
| --- | --- |
| Windows | `${sourceDir}/Build/BuildTools/Windows/` |
| WSL Linux | `$HOME/PanguEngineBuild/BuildTools/Linux/` |
| macOS | `$HOME/PanguEngineBuild/BuildTools/macOS/` |

各根目录下 `build/` 是可变的独立 CMake 构建树，`staging/` 用于暂存，`published/` 保存不可变工具版本。Linux/macOS 使用宿主本地 ext4/APFS，源码仍直接读取既有 SMB 挂载。

从历史 `Build/Host` 或 `$HOME/PanguEngineBuild/Host` 布局升级时，重新执行正式 CMake Preset，在新根目录生成工具；不迁移旧 CMake Cache，也不自动删除历史构建树。本次分类不改变工具和引擎目标的 STATIC/SHARED、Debug/Release 或 ABI 协议。
