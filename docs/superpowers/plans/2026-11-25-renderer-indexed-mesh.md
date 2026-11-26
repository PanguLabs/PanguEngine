# Renderer 可复用索引网格实施计划

> 实现和整分支评审已完成，2026-11-26 完成 Android 真机双模式验收。各平台覆盖范围见文末。

**目标**　桌面和 Android 的同一份 Project 夹具用四个顶点、六个索引绘制四边形，静态几何每个设备周期上传一次。

**架构**　参照 Forge 的 `34_Input`，Project 提供静态数据，Renderer 在设备打开时创建两个缓冲，在每帧绑定并绘制，在设备关闭时释放。现有 Application 负责挂接、呈现和显式重开，Graph 着色器与颜色 Pipeline 继续服务两种绘制。

**工具与基线**　C++23、Clang、CMake 4.0+、Forge v1.63 `7443530ef7e888771f307de74b7872becfd64450`。执行前阅读[已确认规格](../specs/2026-11-25-renderer-indexed-mesh-design.md)、[C++ 规范](../../../wiki/foundation/cpp-guidelines.md)和[测试维护规范](../../../Tests/README.md)。

## 全局约束与评审核对

保留现有清屏、逐帧三角形、HEADLESS 和 Android APK 默认入口。使用正式 preset 和各宿主独立 BuildRoot，独立 Project 的正式验收使用干净提交和 Git 锁。Windows 先验证 D3D12，随后验证 Linux、Android Vulkan 和 Apple 原生链接。以下五项在对应任务内留下证据。

| 条件 | 判据 |
| --- | --- |
| 旧应用打开设备（任务 1、2） | 没有索引几何时不上传静态缓冲，原应用继续通过编译、链接及其绘制测试。 |
| 第二个缓冲创建失败（任务 1） | 已发起的上传安全结束，首个缓冲与加载器按顺序回收，下次打开可成功。 |
| 多帧及 resize（任务 1、2） | 同一设备上的几何上传计数不变，两次取色之间有多次带六个索引的绘制。 |
| 完整重开（任务 2） | 新设备从同一份 Project 数据重新上传，重开后的帧通过取色。 |
| Android 窗口撤销（任务 3） | 在同一 Activity 的第二代窗口采到完整图形，并观察到新的实际呈现时间戳。 |

## 任务 1 · 设备级静态缓冲与索引绘制

**文件**　`Source/Runtime/Renderer/Public/{RendererTypes.h,RendererSession.h}`、`Private/{RendererSession.cpp,FrameBackend.h,Backends/ForgeBackend.cpp}`（后者均相对 Renderer 根目录），以及 `Tests/Renderer/{FakeFrameBackend.h,SessionTests.cpp}`。

**接口**　在 `pg::renderer` 定义本用例的 `IndexedQuad`，成员为 `std::array<TriangleVertex, 4> Vertices` 与 `std::array<std::uint16_t, 6> Indices`。在 `RendererOpenDesc` 末尾增加默认空的 `const IndexedQuad* Quad{}`。数据仅在后端执行本次 Open 时读取。新增 `RendererSession::PresentIndexedMesh(LinearColor clearColor) noexcept -> Result<FrameResult>`。内部为帧提交区分清屏、动态三角形和已上传索引网格，共用一次取图与提交仲裁。

- [x] 在会话测试覆盖 `Open → Attach → 两次索引呈现 → Detach/Attach → 再次呈现 → Close → Open`。断言假后端收到两次带同一数据的 Open、三次索引提交，逐帧提交不携带网格数组。保留已有清屏和动态顶点断言。用 `cmake --build --preset windows --target PanguRendererTests` 确认基线因缺少网格入口失败。
- [x] 在 `ForgeBackend::Open` 完成设备准备后处理可选几何。复用现有 GPU 顶点打包布局，对索引越界在上传前返回 Open 错误。用两次 `addResource` 创建 GPU_ONLY 顶点和索引缓冲，起始状态分别为顶点读取和索引读取；按 `34_Input` 完成本批上传。初始化与关闭在同一设备内各管理一次 ResourceLoader。处理中途失败时也先结束已发起的上传，再按所有权释放。将原来在 `OpenTriangleResources` 中一起创建的加载器、动态映射缓冲和着色器分开按需准备。
- [x] 复用已有附件格式、顶点布局、viewport/scissor、Pipeline 和呈现流程。索引分支绑定静态 VB、以 `INDEX_TYPE_UINT16` 绑定 IB，调用 `cmdDrawIndexed(Command, 6, 0, 0)`。无配置时调用索引入口在取图前返回 `kSubmit/kInvalidState`。在 `RetireSurface` 保留两缓冲，设备关闭时在队列同步后、退出加载器前释放。
- [x] 构建 `PanguRendererTests`，运行 `ctest --preset windows --no-tests=error -R '^PanguRendererTests[.]' --output-on-failure`。用定向失败注入在第二个缓冲创建处使初始化失败，记录已完成的等待、回收和下一次成功打开。优先使用测试夹具或调试器断点，避免增加公开故障开关。提交 `feat: 接入 Forge 静态索引绘制`。

## 任务 2 · 独立 Project、Windows D3D12 与 Linux Wayland

**文件**　`cmake/PanguApplication.cmake`、`Source/Runtime/Application/{Public/PanguApplication.h,Private/GraphicsApplication.cpp}`、`Tests/Renderer/{GraphicsApplicationTests.cpp,CMakeLists.txt,Project/CMakeLists.txt}`。新增 `Tests/Renderer/Project/IndexedMesh.cpp`，扩展 `Tests/Renderer/Acceptance/{BuildProject.cmake,RunDesktop.cmake,ObserveWin32.ps1,ObserveWayland.py}`。

**接口**　为 GRAPHICS 目标增加可选的 `INDEXED_MESH`，独立运行目标与 `TRIANGLE_SHADERS` 组合，原生链接检查可单独使用。该选项仅在本目标的 EngineEntry 上启用对 `pg::app::kIndexedQuad` 的引用；该常量由所选 Project 源文件以外部链接定义，直接供首次 Open 和 `ReopenGraphics()` 使用。公开头声明该常量及 `GraphicsContext::PresentIndexedMesh(LinearColor clearColor) noexcept -> Result<FrameResult>`。现有应用不需提供新符号。

- [x] 在 Application 测试与独立 Project 中先调用新入口，注册 `Renderer.DesktopIndexedMesh`。将现有 `First${fixture}` 目标名拼接对新用例映射为 `IndexedMesh`，生成并读取 `IndexedMeshArtifacts.cmake`。先确认基线因接口或目标缺失失败。完成 Application 转发并沿用现有平台的 OpenDesc 字段、窗口循环与关闭路径；不向 Project 传递 Forge 头或编译宏。
- [x] 夹具黑底，四角依次位于裁剪空间左下、右下、右上、左上，坐标绝对值 0.75。顶点色依次黑、红、黑、绿，Alpha 均为 1，索引为 `{0,1,2,0,2,3}`。两观察器在窗口内按左上原点取样：`(31.25%,31.25%)` 为绿、`(68.75%,68.75%)` 为红，线性主通道均约 0.5，sRGB 约 188；`(5%,5%)` 为黑。使用现有桌面像素容差。依次观察初始、resize 后、显式重开后三阶段。沿用现有文件信号同步，重开成功且呈现新帧后再向观察器确认。
- [x] Windows、随后 Ubuntu 真机分别以 `windows`、`linux` preset 配置 Release、两种 `PANGU_LINK_MODE`、`PANGU_BUILD_ENGINE_TESTS=ON`、`BUILD_TESTING=ON`、`PANGU_BUILD_TRIANGLE_SHADER_CHECK=ON`。构建 Renderer 测试，Windows 同时构建 `PanguRendererHostTests`。按宿主执行 `ctest --preset windows --no-tests=error -L '^area[.]renderer$' --output-on-failure` 或对应 `linux` 命令。已有 Frame、Triangle 和 HEADLESS 用例按其注册条件通过。
- [x] 在 Windows 或 Linux 至少一次记录真实 Forge 调用（调试器跟踪或图形捕获）。同时记录网格两次 `addResource` 的输入和每帧绑定、绘制的参数。从首次打开跟到重开后的绘制，按设备周期核对各一次静态 VB/IB 上传，resize 两侧继续使用同一对缓冲，绘制命令索引数为六。记录像素、实际调用和退出码。提交 `feat: 通过独立 Project 验证索引四边形`；需要干净提交的正式验证在提交后执行。

## 任务 3 · Android 窗口恢复与 Apple 最终链接

**文件**　`Tests/Toolchain/TheForge/{CMakeLists.txt,RendererLink.cpp}`、`Tests/Renderer/NativeActivity/{AndroidStudio/app/build.gradle.kts,Observe.ps1,AndroidStudio/README.md}`、`Tests/Renderer/{CMakeLists.txt,Acceptance/CheckTriangleLink.cmake}`。复用任务 2 的 Project、Graph 资源部署和像素判据。

- [x] Android 为现有 APK 增加显式选项 `-PpanguIndexedMesh=true`，转发为 `PANGU_BUILD_NATIVE_ACTIVITY_INDEXED_MESH=ON`，选取同一份 `IndexedMesh.cpp` 并启用 `INDEXED_MESH`。保持既有 APK 默认三角形和原生清屏选择，区分网格选项的 Gradle/CMake 输出以免串用旧 APK。观察器增加 `-IndexedMesh`，沿用已有 Activity/SurfaceFlinger 采样和设备容差。
- [x] 用 `android-arm64` preset 构建两种链接模式的 `PanguForgeLinkCheck`，在现有 Android Studio 工程用 `:app:assembleAgdkDebug -PpanguIndexedMesh=true` 分别打包。复验默认三角形构建和原生入口切换。
- [x] 在已解锁且 ADB 在线的测试机上依次安装两种已签名 APK，运行 `Observe.ps1 -IndexedMesh -LogPath <该模式的结果文件>`，记录同一 Activity 两代窗口的四边形像素、SurfaceFlinger 实际呈现和销毁。
- [x] macOS 增加 `Renderer.IndexedMeshLink`，参数化复用现有独立 Project 链接检查，断言最终产物实际引用 `GraphicsContext::PresentIndexedMesh` 和 `RendererSession::PresentIndexedMesh`。iOS 在现有 Metal 探针中引用新入口与初始化数据。Mac 共享源码上用 `macos` preset 运行 `ctest --preset macos --no-tests=error -R '^Renderer[.](IndexedMesh|Triangle)Link$' --output-on-failure`，用 `ios-arm64` preset 构建 `PanguForgeLinkCheck` 并核对 ARM64 最终链接。原生验证可关闭着色器构建，有 Metal 运行环境时另记画面。
- [x] 对改动执行针对性 clang-format、使用目标编译数据库运行 clang-tidy、检查 CMake 格式与 Git 差异。同步 `wiki/runtime/rendering.md` 和测试说明，记录实际验收的平台、模式、提交及尚未验证项。提交 `test: 验证移动宿主和 Apple 索引绘制链接`。验收结果已归档。
- [x] Android 实机验收完成后，补全记录，确认无运行任务依赖并清理本计划的临时工作目录。

## 验收记录

实现提交为 `c356c88`（Renderer）、`57e6010`（桌面 Project）、`1662276`（移动宿主和 Apple 链接）及 `0a01321`（整分支评审修正）。后者的 tree 为 `b1cea6f2b3f6c2e68e10be3726be3c80d0959527`。

**评审修正。** 固定 Forge 以独立 transfer queue 处理带 `pData` 的初始上传。Linux 测试机 GTX 1050 Ti 有独立传输队列族。四边形现先用 `addResource` 分配两块缓冲，再用 `beginUpdateResource/endUpdateResource` 写入。离散显存走 Forge 图形上传队列，以 fence 等待完成，首次图形提交等待上传信号量。UMA 走 CPU 可写内存，Vulkan 要求 host-coherent。两块缓冲均可用后才开始上传。此修正保留固定版本的 Forge 源码。错误回收覆盖 Forge 调用能够返回的失败，原生驱动在 Forge 内部触发断言的情况受其实现约束。

**调用记录。** 在 Ubuntu 真机对独立部署的 Modular `IndexedMesh` 用 GDB 记录 Forge 的缓冲分配、`endUpdateResource`、`cmdBindVertexBuffer`、`cmdBindIndexBuffer`、`cmdDrawIndexed`、交换链创建及资源释放。两个设备周期各上传一次 96 字节顶点和 12 字节索引。120 次绘制均绑定对应设备的两块缓冲，索引数均为六。resize 前后缓冲地址一致，显式重开后重新上传，累计创建三个交换链、释放四块几何缓冲。在第二块目标缓冲的 Forge `addBuffer` 边界模拟未取得资源，确认没有开始上传，已分配的第一块缓冲释放一次，设备关闭一次，返回 `kOpen/kNativeFailure` 并退出 1。随后正常运行成功。调用记录位于 Ubuntu 的 `~/PanguEngineBuild/Linux/Tests/Results/RendererIndexedMesh/{fault,recovery}.log`。

**静态检查。** 变更的 C++ 通过仓库 clang-format，CMake 通过 gersemi。Windows、Linux、Android 和 macOS 使用实际编译数据库执行定向 clang-tidy，改动行无诊断。非 Windows 分支仍报告既有根签名局部变量的 const 建议，Linux 另有既有 Pipeline 初始化列表的逗号建议。

| 平台 | 实测结果 |
| --- | --- |
| Windows D3D12 | 评审前提交 `1662276` 的 Renderer 标签两种链接模式各 24/24。修正后 Release Modular 和 Monolithic 的 Renderer 单测、原三角形与索引绘制定向复验各 21/21。两模式初始取色均为 `00BB00 BB0000 000000`，resize 及设备重开后均为 `00BB00 BC0000 000000`。日志在 `Build/Windows/Tests/Results/RendererIndexedMesh/`。 |
| Linux Wayland Vulkan | 修正后 Modular 的索引 1/1，其余 Renderer 标签 23/23；Monolithic 的索引 1/1，其余 Renderer 标签 22/22。两模式同一窗口三阶段取色均为 `00BB00 BC0000 000000`，各 123 次呈现请求，合成器关窗退出 0。原始日志在 `~/PanguEngineBuild/Linux/Tests/Results/RendererIndexedMesh/`。 |
| macOS Metal | 修正后两模式的 Renderer 单测及独立 Project 索引链接各 18/18。共享盘按 `core.autocrlf=true` 读取 Windows 行尾，正式独立 Project 使用实际 Git 锁。Mac 未提供可用 Metal 设备及 `metal` 编译器，未验证窗口画面或新生成的 Metal 字节码。 |
| iOS Metal | 在共享源码上执行 `ios-arm64` preset，最终链接 Monolithic ARM64 Mach-O 应用；`llvm-nm` 核对 `RendererSession::PresentIndexedMesh` 符号。设备呈现未验。 |
| Android Vulkan | 两种模式原生 ARM64 最终链接、Gradle Debug APK、签名和资源检查通过。两包均含 1856 字节顶点和 1596 字节片元 FSL 容器；Modular 包含独立 `libPanguRenderer.so`。最终两包均在 23127PN0CC 上通过首次呈现、同一 Activity 切后台撤销窗口、恢复绘制和正常销毁验收。 |

最终 Android APK 保存在 `Build/Android/Tests/NativeActivity/Results/RendererIndexedMesh/`。本次实机验收使用 `Modular-review.apk`（SHA-256 `b011111c628b4c99f17eaa2f007e6f4b2b0bab70f1bf95eb5a9012b9d81c5c96`）和 `Monolithic-review.apk`（`9349c363414d4bf526711f14f9bb50f53ec9361e79d956a4c4b65cecd9122243`）。

2026-11-26 使用 `Observe.ps1 -IndexedMesh` 验证两份归档 APK。首次失败定位到截图解码：同一张带 ICC 配置的 PNG 在绿色区域直接读取为 `54B835`，应用内嵌配置后为 `13BB09`。索引观察器改为按 ICC 解码后与 sRGB 期望值比较，沿用原有像素容差。每个模式都在第 120 次呈现请求后切后台，再进入同一 Activity，第二代窗口继续实际呈现，第 420 次呈现请求后记录窗口撤销、Activity 销毁和宿主关闭。

| 模式（进程 / 回调线程） | 第一代图层、实际呈现采样数、绿色 / 红色 / 背景 | 第二代图层、实际呈现采样数、绿色 / 红色 / 背景 |
| --- | --- | --- |
| Modular（13628 / 17663） | `#455607`，17，`13BB09 BC1511 000000` | `#455634`，14，`13BC0C BC1511 000000` |
| Monolithic（16666 / 18084） | `#455697`，13，`13BB09 BC1511 000000` | `#455730`，7，`0FBD0C BD1511 000000` |

四张截图均为 1200×2670。两次恢复后的首个采样时间戳均晚于各自第一代末次采样。APK 同目录下的 `Modular-review.log`、`Monolithic-review.log` 及对应的 `.surface-*`、`.window-*.png`、`.pixels` 文件保存生命周期、SurfaceFlinger 时间戳、画面和取色结果。
