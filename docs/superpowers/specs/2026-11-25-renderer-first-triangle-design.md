# Renderer 首个三角形设计

> 状态：首轮实现和验收已归档。公开接口尚未冻结。Apple 的 Metal 字节码和设备运行待具备环境后验证。[实施及验收记录](../plans/2026-11-25-renderer-first-triangle.md#首轮平台验收)

## 目标与方案

独立 Project 在 `OnFrame` 中提交三角形顶点，Renderer 通过固定版本的 The Forge v1.63 上传顶点、绘制并呈现。现有 Application 负责窗口和 EngineHost，Renderer 负责 GPU 资源和命令记录。本轮范围是单个有色三角形和现有清屏路径，后续按实际用例设计 Material、场景和资产接口。[Renderer 首版接入规格](2026-11-20-renderer-forge-igraphics-design.md)

| 方案 | 取舍 |
| --- | --- |
| Project 通过 Application 接口提交顶点（已选择） | 验证实际 Project 提供的数据到达 GPU，公开接口只承担一次固定格式的绘制和呈现。 |
| Renderer 内部使用固定三角形 | 可以验证 Forge 的绘制能力，Project 提交数据、接口错误和真实消费目标的覆盖留到下一轮。 |

## Project 接口

拟在 `PanguApplication.h` 公开 `TriangleVertex` 和 `GraphicsContext::PresentTriangle`，沿用现有 `OnFrame` 和 `FrameResult`。顶点由 Project 按值准备，每帧可提交不同的数据。

```cpp
struct TriangleVertex
{
    float X{};
    float Y{};
    LinearColor Color{};
};

// GraphicsContext 的成员函数。
[[nodiscard]] Result<FrameResult> PresentTriangle(
    LinearColor clearColor, const std::array<TriangleVertex, 3>& vertices) noexcept;
```

三个顶点使用二维裁剪空间，X 向右、Y 向上。顶点着色器固定 Z 为 `0`、W 为 `1`。顶点颜色使用线性 RGBA，在三角形内部插值。使用单颜色附件，viewport 和 scissor 覆盖整个绘制目标，关闭深度测试、混合和背面剔除。顶点借用只持续到调用返回。相关类型由 Pangu 定义，Forge 声明和 GPU 数据布局留在 Renderer 私有实现。公开 API/ABI 随后续用例演进。

顶点的位置和颜色分量须为有限值。遇到 NaN 或无穷大时，在取图前返回可恢复的参数错误。Project 可在 `OnFrame` 中修正数据后再次调用。若 `OnFrame` 返回错误，Application 按现有流程关闭。

`PresentTriangle` 先按 `clearColor` 清屏，在同一帧绘制三角形，再走现有提交与呈现流程。`kPresentRequested` 仍只表示 Forge 的呈现调用已返回。跳帧、窗口代次、`ReopenGraphics()` 及首错处理沿用现有会话语义。原有 `Present(LinearColor)` 继续作为清屏入口。

## Renderer 与构建边界

- 使用现有 RendererSession 的线程和窗口借用契约。按需创建 Shader、Pipeline 和一个可重用顶点缓冲区，按当前附件格式及采样数选择 Pipeline。写入前用现有命令 Fence 确认前一次 GPU 读取已完成。Vulkan 直接写入映射缓冲区时使用 Forge 的 `BUFFER_CREATION_FLAG_HOST_COHERENT`。若采用拷贝上传，绘制依赖拷贝完成和顶点读取所需的资源状态转换，回收时等待所有使用该缓冲区的队列。
- 窗口撤销后暂停提交。交换链退休使用既有队列等待，随后释放依赖附件的 Pipeline。兼容的设备资源可跨新窗口保留。关闭会话时结束 GPU 使用，按依赖顺序释放 Pipeline、顶点缓冲区、Shader 及其初始化所需资源，再销毁设备。初始化部分失败时清理已取得的资源，报告可观察的阶段错误。
- 以 Forge v1.63 的 `03_MultiThread` 示例为参考，将 `Graph.vert.fsl` 和 `Graph.frag.fsl` 复制到 PanguEngine 根目录的 `Shaders/FSL/`，由 PanguEngine 按自身绘制需求维护。保留复用内容的上游许可声明，并记录来源。[顶点着色器](../../../Source/ThirdParty/TheForge/Repo/Examples_3/Unit_Tests/src/03_MultiThread/Shaders/FSL/Graph.vert.fsl) · [片元着色器](../../../Source/ThirdParty/TheForge/Repo/Examples_3/Unit_Tests/src/03_MultiThread/Shaders/FSL/Graph.frag.fsl)
- 使用固定版本的 FSL 工具生成目标平台字节码，使用公开的 Forge 资源加载和绘制接口。Windows 生成 DXIL 和 Forge 的默认图形根签名，Linux 和 Android 生成 SPIR-V。macOS 与 iOS 分别生成 Forge 实际加载的 `Graph.vert.metal` 和 `Graph.frag.metal`，其中 `@FSL` 容器携带各自目标 SDK 的 Metal 库载荷。生成产物进入对应宿主的隔离 BuildRoot，正式构建沿用离线依赖。
- 使用三角形绘制的 Project 目标显式启用着色器的生成和部署。外置字节码使用应用部署和平台资源位置接线。原生链接可独立于着色器生成。开启三角形资源构建时，缺少编译器应报出目标平台及缺少的工具。运行时在调用 Forge 加载前检查着色器和所需根签名是否可读，并校验 FSL 容器头和载荷范围。文件缺失、为空或被截断时返回可观察错误，完成会话清理。

## 验收顺序

| 平台 | 本轮验收 |
| --- | --- |
| Windows D3D12 | 先行。独立消费 Project 在同一交换链代次内改变位置和顶点颜色。观察器多点采样，确认旧位置恢复背景、新位置出现更新后的插值颜色，再单独验证 resize 后仍能正确绘制。累计至少 120 次呈现请求且正常退出。分别验证 Monolithic 和 Modular 隔离部署。 |
| Linux Wayland Vulkan | Windows 通过后，使用同一 Project 源码及现有 Wayland 观察器，在两种链接模式下验证同代次更新、resize 和关闭。 |
| Android Vulkan | Windows 通过后复用 NativeActivity Gradle APK 和正式设备测试入口。构建两种链接模式，在可用设备上验证图形呈现、后台恢复及销毁，分别记录像素或屏幕证据和实际呈现时间戳。 |
| macOS Metal | 同期实现并用 `macos` preset 验证独立 Project 和真实绘制代码的编译、最终链接。有可用 Metal 设备和取色权限时执行窗口验收。 |
| iOS Metal | 同期实现并用 `ios-arm64` preset 编译真实绘制代码、在 Monolithic Renderer ARM64 `.app` 探针中最终链接并核对产物。使用现有 iOS 链接探针作为入口，本轮范围为图形代码与着色器，完整 UIKit 图形宿主另行设计。 |

macOS 与 iOS 的 C++/Objective-C++ 编译和最终链接是交付条件。macOS 独立 Project 与 iOS 探针均须通过真实代码引用把三角形绘制路径纳入最终链接。已有环境检查确认 Mac 的 Command Line Tools 可访问共享源码，当时 `xcrun` 找不到 `metal`。按固定 FSL 的实际调用检查相应目标 SDK 的官方工具，分别验证 `CompiledShaders/MACOS/` 和 `CompiledShaders/IOS/` 下两个阶段的部署路径、`@FSL` 容器及库载荷。有工具时，即使缺 GPU 或签名也执行着色器构建。原生链接、着色器产物和设备运行各自按实际证据归档。[Apple Metal 工具](https://developer.apple.com/metal/tools/) · [Apple 工具链基线](../../../wiki/foundation/apple-toolchain.md)

复用正式 Renderer 与 HEADLESS 测试入口。独立 Project 验证同代次位置和颜色更新、resize 后绘制及正常退出。部署负例覆盖缺失和存在但被截断的着色器文件。会话验证覆盖无效顶点、跳帧或窗口撤销后的绘制恢复及原有清屏调用。[测试维护规范](../../../Tests/README.md)
