# Renderer 索引网格逐帧变换实施计划

> 已按当前会话实施方式完成。2026-11-26 完成整分支独立评审、负对照及跨平台验收，实际范围见文末。

**目标**　Project 对同一对静态几何缓冲逐帧提交局部到裁剪空间的矩阵，四边形在桌面和 Android 随矩阵改变姿态。

**架构**　矩阵沿 GraphicsContext、RendererSession 和已有的帧提交路径传递。Forge 后端用 PerFrame 描述符绑定矩阵缓冲，由现有命令 fence 保护缓冲复用。新顶点着色器复用现有顶点布局与颜色片元着色器。

**技术与依据**　C++23、Clang、CMake 4.0+、Forge v1.63、FSL。执行前阅读[已批准规格](../specs/2026-11-26-renderer-mesh-transform-design.md)、[Forge 渲染接入原则](../../../wiki/runtime/rendering.md)、[C++ 规范](../../../wiki/foundation/cpp-guidelines.md)及[测试维护规范](../../../Tests/README.md)。

## 全局约束

- 公开矩阵为列序的 16 个有限单精度数，顶点输出为 `M × (x,y,0,1)ᵀ`。旧单参数入口使用原 Graph 资源。按已批准规格处理输入范围和资源寿命。
- 构建用根 preset 和宿主独立 BuildRoot。Windows D3D12、Linux Wayland Vulkan、Android Vulkan 使用 Modular、Monolithic 两种模式。Apple 的原生链接、Metal 字节码和设备画面分别记录。
- 定向 CTest 使用 `--no-tests=error --output-on-failure`。正式独立 Project 验收使用已提交源码及实际 Git 锁。临时调用记录放入宿主 BuildRoot，临时实施上下文放入 `.superpowers/sdd/renderer-mesh-transform/`。

## 评审核对

| 条件 | 可观察判据 | 任务 |
| --- | --- | --- |
| 部署目录仅含原 Graph 资源 | 旧入口仍能打开、绘制并通过原取色 | 1、3 |
| 连续提交 A、B，随后立即改写调用方数据 | 两次提交各自使用传入的矩阵；旧 GPU 读取完成后才重写缓冲 | 2、3 |
| 转置、旋转符号错误或丢失 w | 非对称颜色与透视覆盖取样分别能检出错误 | 3 |
| resize、设备重开、窗口恢复 | 同设备保留静态几何，新设备重新上传，恢复后显示新的姿态 | 3、4 |
| 新绘制资源准备中途失败 | 沿用首错与清理路径，已取得的资源各释放一次，随后可重新打开 | 2 |

## 任务 1 · 按需构建和部署 FSL

涉及 `cmake/PanguApplication.cmake`、`cmake/PanguTriangleShaders.cmake`、`Shaders/FSL/`、`Tests/Renderer/Project/CMakeLists.txt` 和 `Tests/Renderer/Acceptance/`。新增 Project 开关 `MESH_TRANSFORM_SHADERS`，表示为 GRAPHICS 目标构建变换绘制所需资源。已有 `INDEXED_MESH` 继续负责提供几何；可独立使用以做纯原生链接验证。

- [x] 在正式消费夹具中加入显式开关，注册 `Renderer.MeshTransformShaders`。从独立的旧、新部署目录检查平台文件清单；新目录需含 `MeshTransform.vert`、`Graph.frag` 及平台必需产物，旧目录仅含原有产物。先确认当前接线因未知选项或缺少新产物而失败。
- [x] 在 `Shaders/FSL/` 新增 `MeshTransform.vert.fsl`、矩阵资源声明和变换入口清单。对照 [Forge PerFrame 声明](../../../Source/ThirdParty/TheForge/Repo/Examples_3/Unit_Tests/src/01_Transformations/Shaders/FSL/Global.srt.h) 让 C++ 绑定和 FSL 使用相同的 set、资源索引及矩阵布局。顶点输出用完整四分量乘积，颜色沿用 Graph 语义。使用样例所用的默认根签名，并核对实际 D3D12 产物。
- [x] 复用现有 FSL 编译、依赖追踪和按平台部署逻辑。保留旧清单，显式声明新顶点源码、资源头、清单与编译产物的依赖。共享输出目录时每个物理文件只设一个生产者，复用共有的片元着色器与根签名。分别以两种目标声明顺序并行构建，确认新旧顶点文件俱全；修改新资源头后增量构建会重新生成变换产物。
- [x] 在启用 Renderer 测试及着色器检查的 Windows preset 运行 `ctest --preset windows -R '^Renderer[.]MeshTransformShaders$' --no-tests=error --output-on-failure`。验收实际 FSL 容器和旧部署文件清单后提交 `feat: 支持按需部署网格变换着色器`。

## 任务 2 · 单网格矩阵重载与 Forge 提交

涉及 `Source/Runtime/Renderer/Public/{RendererTypes.h,RendererSession.h}`、`Private/{FrameBackend.h,RendererSession.cpp,Backends/ForgeBackend.cpp}`（均相对 Renderer）、Application 的 `Public/PanguApplication.h`、`Private/GraphicsApplication.cpp` 及 `Tests/Renderer/{FakeFrameBackend.h,SessionTests.cpp,GraphicsApplicationTests.cpp}`。

- [x] 在会话与 Application 测试增加矩阵转发用例。显式构造单位矩阵 A，拷贝为 B 后置 `B.Elements[12] = 0.5F`。连续提交 A、B，返回后修改原变量；假后端记录的两次按值快照分别为 A、B 的提交值。旧入口提交无矩阵；已暂停的会话返回原有跳帧结果。构建 `PanguRendererTests`，先验证失败原因是新接口缺失。
- [x] 在 `pg::renderer` 定义 `MeshTransform`，字段 `std::array<float, 16> Elements`。在 `RendererSession` 和 `GraphicsContext` 增加 `PresentIndexedMesh(LinearColor clearColor, const MeshTransform& transform) noexcept -> Result<FrameResult>`（Application 引用 renderer 类型）。在 `FrameDraw` 增加仅借用到返回的矩阵指针，让准备阶段也能区分静态索引和带矩阵的提交。保留取图前准备、撤销仲裁和既有失败处理。
- [x] 在 `ForgeBackend.cpp` 为变换路径按需建立 shader、对应 Pipeline、uniform buffer 和描述符。设备上共享根签名初始化及清理。只在实际选中的路径检查其资源文件及路径长度。在上一条命令的 fence 完成后、提交绘制前，用 Forge 更新当帧矩阵，按样例绑定 PerFrame 集合及原静态 VB/IB，执行六索引绘制。与旧入口交替调用时选择各自的 Pipeline。
- [x] 沿用设备和表面两级清理。resize 退休依赖附件的 Pipeline；重开或资源准备失败时，按 GPU 同步、对象依赖次序回收。用定向调试记录在 Forge 能报告失败的准备步骤中断一次，核对部分回收、首错和下一次打开。
- [x] 运行 `cmake --build --preset windows --target PanguRendererTests PanguRendererHostTests` 和 `ctest --preset windows -L '^area[.]renderer$' --no-tests=error --output-on-failure`。核对新转发用例及已有清屏、三角形、静态索引回归后提交 `feat: 逐帧提交索引网格变换`。

## 任务 3 · 桌面实际画面与资源寿命

新增 `Tests/Renderer/Project/MeshTransform.cpp`，接入 `Tests/Renderer/{CMakeLists.txt,Project/CMakeLists.txt}`，扩展 `Acceptance/{BuildProject.cmake,RunDesktop.cmake,ObserveWin32.ps1,ObserveWayland.py}`。目标名为 `MeshTransform`，CTest 名为 `Renderer.DesktopMeshTransform`。复用旧四边形的顶点颜色、索引、黑底及颜色容差。下表坐标为 Y 向上的归一化设备坐标，观察器换算到实际窗口像素。

| 矩阵或阶段 | 独立像素判据 |
| --- | --- |
| 显式单位矩阵 | 沿用旧用例的绿、红、背景三点，与独立部署的 `Renderer.DesktopIndexedMesh` 比较。 |
| x、y 均缩放 0.4，再沿 x 平移 −0.5 或 +0.5 | 两者的 x 覆盖分别是 [−0.8, −0.2] 与 [0.2, 0.8]。局部红点 (0.375, −0.375) 分别落在 (−0.35, −0.15)、(0.65, −0.15)，对侧采样点为黑色。 |
| 先缩放 (0.5, 0.25)，再绕 Z 正转 90° | (0.10, 0.25) 对应局部 (0.50, −0.40)，红色线性分量为 0.60。颠倒乘法顺序会落在背景，反转旋转方向会采到绿色。 |
| 保持 x、y，令 w = 1 + 0.5x | 右边界从 0.75 变为约 0.545。在 (0.65, 0) 应取黑色，误将 w 写为 1 会取到红色。在 (0.4, 0) 对应的局部点 (0.5, 0) 取红色，线性分量为 1/3，用于排除整帧空白。 |

- [x] 在观察器加入上述阶段和预期值，复用现有观察器与应用间的进度信号，等每阶段稳定取色后再前进。先用单位矩阵贯穿各阶段，验证平移、旋转和 w 判据分别能检出错误。
- [x] 用任务 2 的重载提交确定的矩阵序列。resize 前保持左移，resize 后保持右移；显式重开后用已知姿态再次核对。分别从各自干净的部署目录运行 `RendererProject` 的 `IndexedMesh` 和 `MeshTransform`，确认仅部署原有着色器的 `IndexedMesh` 仍能正确绘制。
- [x] 在 Windows、Ubuntu 共享源码上分别使用 `windows`、`linux` preset，Release 配置开启 `PANGU_BUILD_ENGINE_TESTS`、`BUILD_TESTING`、`PANGU_BUILD_TRIANGLE_SHADER_CHECK`。对 Modular、Monolithic 各运行 `ctest --preset windows -R '^Renderer[.]Desktop(IndexedMesh|MeshTransform)$' --no-tests=error --output-on-failure` 或对应 linux 命令。正式验收按独立 Project 的提交锁执行。
- [x] 在至少一个桌面后端用调试器或图形捕获记录 A、B 对应的矩阵更新与绑定、绘制及完整重开的资源创建。核对 resize 两侧共用同一对 VB/IB，各设备周期各上传一次几何。提交 `test: 验证桌面网格变换与部署兼容`。

## 任务 4 · Android 恢复、Apple 链接与归档

涉及 `Tests/Toolchain/{CMakeLists.txt,TheForge/CMakeLists.txt,TheForge/RendererLink.cpp}`、`Tests/Renderer/{CMakeLists.txt,Acceptance/CheckTriangleLink.cmake,NativeActivity/Observe.ps1,NativeActivity/AndroidStudio/app/build.gradle.kts}` 与对应测试说明。

- [x] Android 新增 `-PpanguMeshTransform=true` 选择上述 Project 与着色器，按用例、链接模式隔离 APK 及资源目录。扩展 `Observe.ps1 -MeshTransform`，使用现有的 Activity 和 SurfaceFlinger 观察流程。夹具与观察器通过测试信号切换左右姿态，在确认第一代窗口左移后切后台，撤销期间设置恢复姿态，同一 Activity 恢复后持续画右移并观察实际呈现、两侧像素及正常销毁。先验证旧资源包不能满足新用例的资产检查，再核对新 APK 的实际容器与 ABI。
- [x] Android 使用 `android-arm64` preset 构建 `PanguForgeLinkCheck`，在现有 Gradle 工程运行 `.\gradlew.bat :app:assembleAgdkDebug -PpanguMeshTransform=true -PpanguLinkMode=Modular`，再切换 Monolithic。两份签名 APK 分别在真机运行观察器，沿用带 ICC 配置的截图解码与设备容差，按模式保存采样结果。
- [x] macOS 为纯原生夹具注册 `Renderer.MeshTransformLink`。iOS 在 `RendererLink.cpp` 引用新重载。符号检查匹配含 `MeshTransform` 参数的解码签名，避免同名旧重载误过。先用仅调用旧入口的独立夹具核对检查能失败，再在 Mac 共享源码上运行 `ctest --preset macos -R '^Renderer[.](MeshTransform|IndexedMesh)Link$' --no-tests=error --output-on-failure`（两种模式）及 `cmake --build --preset ios-arm64 --target PanguForgeLinkCheck`。有实际 Metal 工具链或设备时分别记录字节码与呈现结果。
- [x] 执行针对性 clang-format、基于目标编译数据库的 clang-tidy、CMake 格式检查及 `git diff --check`。同步 `wiki/runtime/rendering.md` 与测试说明，提交 `test: 验证移动宿主网格变换与 Apple 链接`。
- [x] 在本计划末尾回填实际提交、平台、链接模式、像素与资源记录摘要，列明环境限制。通过评审和验收、归档必要结论并确认无运行依赖后，清理本计划的临时 SDD 目录。

## 验收记录

实现提交为 `16e1057`（FSL 与部署）、`cce007d`（矩阵重载及 GPU 提交）、`d81d6cd`（桌面夹具）和 `a1621b9`（移动与 Apple）。整分支独立评审后的验收修正为 `48a97d2`。后续 `b74c152` 整理静态检查，`a5655f3` 让 Android 变换夹具在观察器确认恢复画面后退出。

| 平台 | 验收结果 |
| --- | --- |
| Windows D3D12 | Release Modular、Monolithic 各通过 23/23 项定向测试（Renderer 单测、部署清单、原四边形及变换画面）。Monolithic 的 Renderer 标签曾通过 27/27。最终共享夹具在两种模式下分别重跑变换桌面用例，均通过。 |
| Linux Wayland Vulkan | Ubuntu 真机 Release，Monolithic 正式定向 CTest 通过 23/23。Modular 首轮通过 22/23，旧入口的独立配置在共享源码发生提交时被 Git 锁拒绝，随后在干净提交上补跑旧入口与着色器部署，通过 2/2。两种模式均实际采到重开后的原位四边形。 |
| Android Vulkan | `android-arm64` 两种模式原生链接完成。两份签名 ARM64 APK 均含 2300 字节变换顶点和 1596 字节颜色片元 FSL 容器。23127PN0CC 真机在同一 Activity 中先左移，切后台撤销窗口，再恢复右移并正常销毁。 |
| macOS Metal | 两种链接模式的旧入口、新重载独立 Project 均通过最终链接。额外用真实回调对象做负对照：只调用旧重载的对象被矩阵检查拒绝，实际矩阵调用通过。 |
| iOS Metal | 共享源码上构建 `ios-arm64` 的 Monolithic ARM64 应用，`llvm-nm` 确认最终程序含矩阵重载。Apple 宿主缺少 `metal` 编译器，本轮未验证实际 Metal 字节码和设备画面。 |

**画面与兼容性。** 旧部署仅含 Graph 资源。新部署复用了原片元着色器。测试检查完整平台清单、容器头及两种共享目录声明顺序。在 Windows 更新 SRT 输入时间后，实际增量构建重新生成并部署 `MeshTransform.vert`。将隔离副本的新顶点着色器替换为恒等绘制后，旋转、透视和左右位移三组取色分别失败。重开负对照也检出了重开前残留的右移画面；最终用例在重开后要求原位四边形。

**资源寿命。** Ubuntu 上针对 Modular 真实 Forge 调用做 GDB 跟踪，独立 Wayland 合成器用于调试采集。两个设备周期共上传四块几何缓冲，普通 resize 复用原 VB/IB；120 次矩阵更新对应 120 次描述符绑定及六索引绘制。共创建三个交换链，最终释放六块缓冲并关闭两个设备周期。单独在创建变换描述符集时模拟未取得结果，得到 `kSubmit/kNativeFailure`，三块已分配缓冲各释放一次、设备关闭一次。下一次正常运行成功。原始记录位于 Ubuntu 的 `~/PanguEngineBuild/Linux/Tests/Results/RendererMeshTransform/{fault,recovery}.log`。

**Android 观察时序。** 固定在第 420 帧退出的旧夹具曾在观察器完成截图前关闭，实际采到了系统界面。变换夹具现在等待第二代窗口的像素确认。额外延迟八秒取色时，真机保持右移姿态，第 740 次呈现请求后正常退出。最终取样均为 1200×2670，两种模式第一代窗口的左、右点为 `BC1511 000000`；第二代分别为 Modular `000000 C11511`、Monolithic `000000 BC1511`。两次恢复均记录了新图层及晚于上一代的 SurfaceFlinger 呈现时间戳。

签名 APK 和观察日志归档在 `Build/Android/Tests/NativeActivity/Results/RendererMeshTransform/`。Modular 的 `Modular-final.apk` SHA-256 为 `ac13a4dc828137813e525ad208cd6d6f5a6b0d68ace0c5da5156fd8ef91deed6`，Monolithic 的 `Monolithic-final.apk` 为 `1a35ec2f4f145d82e27a88c90ecdc74bce900ab5e5988a22ad7d8dcba6099bb9`。

**静态检查。** C++ 使用仓库 clang-format，CMake 使用 gersemi，Git 空白检查通过。按 Windows、Android、Linux 和 macOS 的目标编译数据库运行定向 clang-tidy。Linux 仍提示既有 Pipeline 初始化列表的条件尾逗号，macOS 仍提示既有类型转换可用 auto；Forge 的 Metal SRT 宏在 Apple 编译时产生上游返回值 const 限定警告。

**Linux 显示环境。** 默认 GNOME 桌面会话中的嵌套窗口偶发截图超时，既有索引二进制也可复现。GDB 观察到 NVIDIA 的 Wayland present 正等待显示事件。正式复验在同一 Ubuntu 主机启动隔离的 Mutter 虚拟输出，原有 Sway 观察器继续按 PID 查窗口、按尺寸取色并请求关闭，GPU 及测试容差沿用原配置。验证边界为实际合成画面。复现命令见 [Linux 桌面验收说明](../../../Tests/Toolchain/README.md#索引网格变换的-linux-桌面验收)。

**Windows 全量回归。** 从 `a5655f3` 启动 `windows-full`，首轮通过 338/343。分类测试检出新着色器 CTest 未预留嵌套构建预算，已在 `19fda5f` 增加 `PROCESSORS`。其余四项分别遇到冷启动发现超时、析构用例退出异常或共享源码 Git 锁拒绝。重新配置后串行执行五个失败项和着色器测试，6/6 通过，343 项均取得通过结果。最后的桌面夹具声明整理 `890e2fd` 已单独复验实际窗口和消费目标 clang-tidy。Windows 日志在 `Build/Windows/Tests/Results/RendererMeshTransform/`。
