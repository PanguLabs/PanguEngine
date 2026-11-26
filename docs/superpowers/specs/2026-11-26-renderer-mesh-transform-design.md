# Renderer 索引网格逐帧变换

设计于 2026-11-26 确认并已实现。Windows、Linux 与 Android 已验证实际画面，Apple 已验证原生最终链接。引擎基线为 `b4d87f1`。本轮扩展[静态索引网格](2026-11-25-renderer-indexed-mesh-design.md)，沿用仓库固定的 The Forge v1.63。[实施计划](../plans/2026-11-26-renderer-mesh-transform.md)

## 目标与范围

Project 在每次绘制时指定一个从网格局部坐标到裁剪空间的 4×4 矩阵。GPU 对已有四边形应用该矩阵，使同一设备上的连续帧可以呈现不同的位置、方向和大小。设备周期内只上传一次静态几何。首轮覆盖单个已有索引网格，场景层级、相机、实例化和通用数学库留待各自的用例确定。

## 方案选择与依据

| 方案 | 取舍 |
| --- | --- |
| 只传二维偏移 | 足以验证左右移动，增加旋转或缩放需要扩充参数。 |
| 二维仿射变换 | 可表达本轮全部可见动作，需要另订二维数据布局和 shader 约定。 |
| 单个 4×4 矩阵（已选） | 每次传入 16 个浮点数，直接沿用 Forge 样例的矩阵乘法；Project 可以自行组合变换。 |

Forge 的 `01_Transformations` 在 CPU 组合矩阵，通过每帧的 uniform buffer 发布给 GPU，绑定对应描述符集后绘制；其顶点 shader 将组合矩阵乘以齐次顶点。样例分别传入视图投影和各实例的世界矩阵。本轮由 Project 提供已经组合好的单个矩阵，继续使用现有六索引绘制。[CPU 与资源更新](../../../Source/ThirdParty/TheForge/Repo/Examples_3/Unit_Tests/src/01_Transformations/01_Transformations.cpp) · [PerFrame 声明](../../../Source/ThirdParty/TheForge/Repo/Examples_3/Unit_Tests/src/01_Transformations/Shaders/FSL/Global.srt.h) · [顶点 shader](../../../Source/ThirdParty/TheForge/Repo/Examples_3/Unit_Tests/src/01_Transformations/Shaders/FSL/basic.vert.fsl)

## Project 契约

Application 和 Renderer 为现有 `PresentIndexedMesh(clearColor)` 提供接收矩阵的重载，原调用继续产生单位变换的结果。矩阵作为绘制参数仅借用到调用返回。公开数据使用引擎侧的 16 个单精度浮点数，以列为序，元素下标为 `4 * 列 + 行`（行列从零起）。平移量 x、y 分别位于下标 12、13。具体类型名和字段名见实施计划。

Project 向新重载传入各元素均为有限值的矩阵，需要单位变换时显式传入单位矩阵。顶点坐标计算为 `clip = M × (x, y, 0, 1)ᵀ`，输出保留齐次 w，由图形管线完成裁剪和透视除法。项目坐标沿用 X 向右、Y 向上，颜色属性随原顶点插值。组合矩阵 `R × S` 对顶点先缩放后旋转。

## Forge 接入及生命周期

- 复用现有 Application → RendererSession → Forge 绘制路径。调用矩阵重载的 Project 在构建时显式选用变换着色器。桌面独立部署和 Android APK 按该声明打包使用 PerFrame 常量缓冲的 FSL 顶点着色器及配对的颜色片元着色器。具体 CMake 选项名留给实施计划。已有清屏、三角形和静态索引 Project 沿用原接线，其中单参数索引绘制入口使用原有 Graph 着色器。
- 变换绘制路径使用设备所属的 uniform buffer 及对应描述符，用 Forge 的资源更新接口写入矩阵。缓冲数与可同时在途的命令帧数一致。现有单命令 fence 路径可复用一份缓冲，写入前确认上一次使用已结束；命令记录绑定与写入匹配的描述符。若上传走异步队列，沿用 Forge 提交所需的同步。交换链图像编号不决定缓冲复用时机。
- 普通 resize 和 Android 窗口重新挂接沿用已有表面与 Pipeline 生命周期。恢复绘制后使用当次调用的矩阵。完整设备重开后重建绑定及缓冲，并从 Project 重新取得静态几何。初始化中途失败或退出时沿用现有资源释放及队列同步顺序，只释放已取得的资源。

## 验收

- 桌面验收使用同一测试工程 `RendererProject`（`Tests/Renderer/Project`）的两个应用目标，各自在独立部署目录运行。现有目标 `IndexedMesh` 只部署原有 Graph 着色器，以单参数入口绘制原四边形并核对取色。变换目标 `MeshTransform` 显式选用变换着色器，用矩阵重载完成下述画面验收。
- `MeshTransform` 用确定的矩阵序列绘制。显式单位矩阵应与旧入口得到相同的红、绿和背景取色。连续提交不同矩阵，用左右平移让已知采样点进入和离开覆盖区域，再用非等比缩放叠加旋转区分乘法顺序和旋转方向。另取一个使左右顶点产生不同正 w 的矩阵，在透视除法前后覆盖结果不同的位置取色。所有预期值均按公开矩阵约定独立推导，沿用既有颜色容差。
- resize 和 Android 窗口恢复前让四边形保持在左侧，恢复后持续提交右移矩阵，待画面可采样时核对右侧四边形及左侧背景。
- Windows D3D12、Linux Wayland Vulkan 在 Modular 和 Monolithic 下验证取色、resize 和显式设备重开。通过定向调用记录或图形捕获核对矩阵资源的写入与绑定、跨帧几何复用及重开后的资源重建。
- Android Vulkan 沿用 NativeActivity 两种链接模式和已有真机观察器，验证窗口撤销、恢复后的变换画面及正常销毁。macOS 和 iOS 沿用现有原生编译、最终链接边界；Metal 字节码和设备呈现按实际环境单独记录。[既有平台验收边界](../plans/2026-11-25-renderer-indexed-mesh.md) · [测试维护规范](../../../Tests/README.md)
