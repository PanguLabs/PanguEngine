# Renderer 可复用索引网格设计

> 静态索引绘制已实现，Android 真机已通过 Modular 和 Monolithic 两种模式的窗口恢复验收。[实施与验收记录](../plans/2026-11-25-renderer-indexed-mesh.md)。引擎基线为 `d33bb2d`，The Forge v1.63 固定于 `7443530ef7e888771f307de74b7872becfd64450`。

## 交付目标

独立 Project 提供四个带颜色的二维顶点和六个共享索引，使用现有 Graph 着色器将两个三角形画成四边形。每次打开 Renderer 设备只需上传一次几何，后续帧重复绑定同一对缓冲并绘制。桌面在现有 `RendererProject` 增加 `IndexedMesh` 目标，Android 在现有 `PanguNativeActivityCheck` 中选择同一份 `IndexedMesh.cpp` 夹具。[桌面 Project](../../../Tests/Renderer/Project/CMakeLists.txt) · [Android Studio 工程](../../../Tests/Renderer/NativeActivity/AndroidStudio/README.md) · [现有颜色与着色器约定](../../../wiki/runtime/rendering.md)

## Forge 示例的选择

| 上游示例 | 源码中的做法 | 对本轮的意义 |
| --- | --- | --- |
| [34_Input](../../../Source/ThirdParty/TheForge/Repo/Examples_3/Unit_Tests/src/34_Input/34_Input.cpp) | `Init()` 里用 `BufferLoadDesc` 上传四个顶点和六个索引，离开函数前调用 `waitForAllResourceLoads()`。每帧绑定两个缓冲并调用 `cmdDrawIndexed(cmd, 6, 0, 0)`，在 `Exit()` 释放缓冲。 | 作为主参考。采用它的静态几何与设备生命周期，顶点属性使用 Pangu 已有的位置和颜色。 |
| [19_CodeHotReload](../../../Source/ThirdParty/TheForge/Repo/Examples_3/Unit_Tests/src/19_CodeHotReload/19_CodeHotReload_Main.cpp) | 精灵四边形同样在 `Init/Exit` 创建和释放静态缓冲，以实例化绘制复用几何。 | 佐证静态缓冲可跨帧和窗口 resize 保留。 |
| [12_ZipFileSystem](../../../Source/ThirdParty/TheForge/Repo/Examples_3/Unit_Tests/src/12_ZipFileSystem/12_ZipFileSystem.cpp) 和 [28_Skinning](../../../Source/ThirdParty/TheForge/Repo/Examples_3/Unit_Tests/src/28_Skinning/28_Skinning.cpp) | 从文件加载 `Geometry`，绘制时直接使用其中的索引类型和计数。 | 文件资产已有 Forge 入口。本轮夹具的几何来自内存数组，直接使用缓冲上传。 |
| [01_Transformations](../../../Source/ThirdParty/TheForge/Repo/Examples_3/Unit_Tests/src/01_Transformations/01_Transformations.cpp) | 球体支持顶点布局切换，在带有 `SHADER` 或 `RENDERTARGET` 标志的重载中重新生成。 | 四边形布局固定，采用 `34_Input` 的缓冲寿命。 |

## 接入与生命周期

Project 保存四个顶点和 `{0, 1, 2, 0, 2, 3}` 六个 16 位索引的静态数组。Renderer 负责 Forge 设备对象、上传和命令记录。几何来源只需提供给本夹具的设备初始化路径，完整重开时仍可从 Project 取得同一份数组。沿用现有 Application 的窗口与帧循环。具体的函数拼写和目标接线留给实施计划，本轮公开接口仅需承载这一条绘制路径。

| 时机 | 行为 |
| --- | --- |
| 设备初始化 | 分别用 `addResource` 创建 GPU 顶点缓冲和索引缓冲，按主参考完成本批上传后开始绘制。创建中途失败时按已有 Renderer 错误路径释放已取得的资源。 |
| 每帧 | 沿用已有的取图、清屏、颜色 Pipeline 和呈现路径，以 `cmdBindVertexBuffer`、`cmdBindIndexBuffer` 绑定静态缓冲，索引类型为 `INDEX_TYPE_UINT16`，调用 `cmdDrawIndexed` 绘制六个索引。 |
| 窗口 resize 或 Android 窗口撤销、重新挂接 | 继续使用现有表面退休与挂接流程。只要设备仍存活，几何缓冲继续用于后续帧。Forge Android 的窗口撤销进入 `Unload(ALL)`；`34_Input` 将这两个缓冲保留到 `Exit()`。[Android 宿主流程](../../../Source/ThirdParty/TheForge/Repo/Common_3/OS/Android/AndroidBase.cpp) |
| 完整重开或退出 | 按既有队列同步结束 GPU 对旧缓冲的使用，调用 `removeResource` 并关闭设备。显式 `ReopenGraphics()` 成功打开新设备后，复用上述初始化路径从夹具数组创建新缓冲，再恢复绘制。[Forge 完整重置顺序](../../../Source/ThirdParty/TheForge/Repo/Common_3/OS/Windows/WindowsBase.cpp) |

16 位索引是此四边形的数据格式。跨会话句柄、网格注册表、引擎保存恢复副本和通用 Project 资源回调都超出本次接入范围。

## 验收

- Windows D3D12 先行，再用 Linux Wayland Vulkan 运行同一独立 Project。分别在两片三角形的内部和背景取色，验证普通 resize 后的画面。用显式重开验证设备初始化路径可再次生成并绘制四边形。
- 用针对性的调用记录或图形捕获核实上传和绘制次数。一个设备周期内，多帧及 resize 共用同一对缓冲；完整重开后在新设备上重新上传。画面采样只证明绘制结果。
- Android 使用现有 NativeActivity APK，核对窗口撤销、恢复后的四边形、实际呈现和正常销毁。沿用已有两种链接模式与正式平台测试入口。macOS、iOS 沿用已有 Metal 编译和最终链接检查，设备画面按现场条件记录。[Renderer 工具链验收](../../../Tests/Toolchain/README.md) · [测试维护规范](../../../Tests/README.md)
