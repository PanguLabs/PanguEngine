# Renderer 首个三角形实施计划

> 本轮实现与验收已归档。Apple 的原生编译和最终链接已通过，Metal 字节码与设备运行待环境具备后验证。

**目标**　独立 Project 通过 `PresentTriangle` 提交每帧顶点，并在实际窗口显示三角形。

**架构**　Application 复用现有窗口与宿主流程。Renderer 会话通过 Forge 公开 API 上传顶点、记录绘制并呈现。绘制目标显式请求构建和部署着色器。

**工具链**　C++23、CMake 4.0+、Ninja、固定 The Forge v1.63、FSL 和各目标的着色器编译器。

**规格**　[Renderer 首个三角形设计](../specs/2026-11-25-renderer-first-triangle-design.md)

## 全局约束

- 使用 Forge gitlink `7443530ef7e888771f307de74b7872becfd64450`。以 Forge 示例为参考，在 PanguEngine 根目录的 `Shaders/FSL/` 维护 `Graph.vert.fsl` 和 `Graph.frag.fsl`，保留复用内容的上游许可和来源。
- `PresentTriangle` 只借用当前调用的 `std::array<TriangleVertex, 3>`。使用二维裁剪坐标、线性 RGBA 和单颜色附件。无效顶点错误可恢复。窗口与设备的关闭沿用会话契约。
- Windows 先行实机验收。随后执行 Linux、Android 图形验证。macOS 和 iOS 的原生编译、最终链接是本轮交付条件。分别记录 Apple 着色器生成和设备运行结果。
- 遵守仓库的 [测试维护规范](../../../Tests/README.md)、正式 preset、共享源码根和宿主隔离 BuildRoot。构建使用离线依赖。修改的 C++ 和 CMake 使用仓库格式与检查配置。独立 Project 最终验收使用干净提交及 Git 锁。

## 评审核对

| 风险 | 验证位置 |
| --- | --- |
| Apple 原生链接意外依赖 Metal 编译器，或验收文件和 Forge 加载路径不一致 | 任务 1 的独立构建接线和任务 3 的链接与 FSL 容器检查。 |
| 部署遗漏着色器或根签名，或文件存在但容器不完整，Forge 加载时断言 | 任务 2 的部署负例与结构化错误判据。 |
| 无效顶点使会话进入关闭，或已经取图 | 任务 2 的同一回调修正并重试用例。 |
| 附件退休后沿用过期资源，或 Vulkan GPU 未读到本帧顶点写入 | 任务 2 的会话调用记录和映射内存约束，以及任务 4 的 Vulkan 更新与窗口重建验收。 |
| 按交换链代次缓存顶点，或用固定颜色替代插值 | 任务 2、4 的同代次位置和颜色更新与独立 resize 判据。 |

## 任务 1 · Forge 示例着色器与构建产物

**涉及文件**　在仓库根目录的 `Shaders/FSL/` 新增 `Graph.vert.fsl`、`Graph.frag.fsl`、`shaders.list`、`README.md` 和所需许可材料。新增 `cmake/PanguTriangleShaders.cmake`。修改 `cmake/PanguApplication.cmake`、`Tests/Toolchain/CMakeLists.txt` 与 `Tests/Toolchain/TheForge/CMakeLists.txt`。

**交付接口**　`pangu_add_application(TARGET <name> KIND GRAPHICS TRIANGLE_SHADERS SOURCES <files...>)` 为应用启用资源构建。测试根显式设置 `PANGU_BUILD_TRIANGLE_SHADER_CHECK=ON` 后提供 `PanguTriangleShaders` 构建目标。Asset 根名为 `CompiledShaders`，保留 FSL 写出的目标子目录布局。

- [x] 用 Windows `cmake --preset windows -DBUILD_TESTING=ON -DPANGU_BUILD_ENGINE_TESTS=ON -DPANGU_BUILD_FORGE_LINK_CHECK=ON -DPANGU_BUILD_TRIANGLE_SHADER_CHECK=ON` 配置，执行 `cmake --build --preset windows --target PanguTriangleShaders`，确认基线缺少目标。
- [x] 以 `Examples_3/Unit_Tests/src/03_MultiThread/Shaders/FSL/` 中两份 Graph 文件为起点，复制到仓库根目录的 `Shaders/FSL/` 并按规格维护。本地 `shaders.list` 只构建 Graph 顶点和片元阶段及所需的默认图形根签名，从固定 Forge 引用 `Common_3/Graphics/FSL/defaults.h`。
- [x] 根据 PanguEngine 源码根定位 `Shaders/FSL/shaders.list`，在请求三角形资源时调用固定 Forge `fsl.py --compile`。选择 `DIRECT3D12`、`VULKAN`、`ANDROID_VULKAN`、`MACOS`、`IOS`，为各目标定位宿主可执行的着色器工具。声明源码、清单与工具依赖及输出，按应用、目标平台与配置隔离生成目录。桌面部署将 `CompiledShaders` 放在可执行文件旁。原生应用和移动链接探针可独立构建。
- [x] 重跑 Windows 构建，核对 `CompiledShaders/DIRECT3D12` 下 `Graph.vert`、`Graph.frag` 和 `default.rootsig` 均由 FSL 生成且非空。核对 Ninja 输入包含 Graph 文件、清单和 FSL 工具，第二次构建复用产物。使用指向无效着色器工具的测试配置确认报出目标平台和缺少的编译器，再核对未选择资源构建的原生目标可链接。
- [x] 提交 `build: 接入 Forge 三角形着色器构建`。

## 任务 2 · 三角形绘制与 Windows 实机验收

**涉及文件**　修改 `Source/Runtime/Renderer/Public/RendererTypes.h`、`RendererSession.h`、`Private/RendererSession.cpp`、`Private/FrameBackend.h`、`Private/Backends/ForgeBackend.cpp`、`Source/Runtime/Application/Public/PanguApplication.h`、`Private/GraphicsApplication.cpp` 及 `Source/ThirdParty/TheForge/` 的加载器接线。复用 `Tests/Renderer/FakeFrameBackend.h`、`SessionTests.cpp`、`GraphicsApplicationTests.cpp`。新增 `Tests/Renderer/Project/FirstTriangle.cpp`，扩展同目录构建和 `Tests/Renderer/Acceptance/` 中现有独立 Project 驱动与 Windows 观察器。

**交付接口**　`pg::renderer::TriangleVertex` 包含 `float X`、`float Y`、`LinearColor Color`。Application 公开同一类型。`GraphicsContext` 和 `RendererSession` 提供 `[[nodiscard]] Result<FrameResult> PresentTriangle(LinearColor clearColor, const std::array<TriangleVertex, 3>& vertices) noexcept`。`RendererError` 增加 `kInvalidVertex` 和 `kShaderUnavailable`，均从 `kSubmit` 阶段报告。

- [x] 在真实 Application 入口的测试中先提交含 NaN 位置或无限颜色的顶点，再在同一次 `OnFrame` 提交有效顶点。断言第一步返回 `kSubmit/kInvalidVertex`，取图、提交和呈现调用各为零。第二步断言 `kPresentRequested`，后端记录实际收到的三个顶点。会话用例验证暂停时无取图，撤销后返回 `kReattach`，重新挂接后提交新数据。先构建 `PanguRendererTests`，确认缺少新入口时测试失败。
- [x] 在 Application 和 RendererSession 接线新入口，复用 `Present` 的窗口、取图、提交许可、首错和清理流程。顶点校验早于取图，参数错误可以在回调内修正。后端绘制输入在调用内消费。现有清屏提交继续走同一个会话流程。
- [x] 把 Forge 的公开 Shader 和根签名加载入口接入 `ThirdParty::TheForge`。对照 `IResourceLoader` 和实际链接依赖接入必要编译单元及 Apple 编译语言。使用 `RD_SHADER_BINARIES` 和 Forge 的目标子目录。在调用 Forge 加载前检查两个阶段及所需根签名可读，校验 `@FSL` 头、编译变体记录和非空载荷范围。校验失败返回 `kSubmit/kShaderUnavailable`。
- [x] 按需创建 Shader、顶点缓冲区和与当前附件兼容的三角形列表 Pipeline。使用现有命令 Fence 等待上一次 GPU 读取完成后再写顶点。Vulkan 映射缓冲区使用 `RESOURCE_MEMORY_USAGE_CPU_TO_GPU` 和 `BUFFER_CREATION_FLAG_HOST_COHERENT`。清屏后绑定全目标 viewport、scissor、Pipeline 和顶点缓冲区，再调用 `cmdDraw`。退役交换链时等待后释放 Pipeline。关闭设备前释放会话绘制资源。
- [x] 为启用资源验收的 Windows 构建注册 `Renderer.DesktopTriangle`，与 `Renderer.DesktopFrame` 共用独立 Project 的构建、搬移部署和 GPU 窗口资源锁。`FirstTriangle` 从 `PanguApplication.h` 使用公开接口。背景为黑色。初始顶点依次为红 `(-0.875,-0.5)`、绿 `(-0.125,-0.5)`、蓝 `(-0.5,0.5)`。在同一交换链代次内将所有顶点的 X 增加 `1.0F`，颜色依次改为蓝、红、绿。完成第二阶段取样后独立触发 resize，并继续验证第二阶段的绘制结果。
- [x] 按下表以客户区左上角为原点取样。在同一交换链代次步骤中通过 Project 控制信号切换顶点，核对窗口身份和客户区尺寸。会话用例核对连续提交两套顶点期间无重新挂接。颜色为 sRGB，每通道容差 4。记录像素、累计至少 120 次呈现请求及退出 0。

  | 阶段 | 左上 `(25%,40%)` | 左下 `(25%,60%)` | 右上 `(75%,40%)` | 右下 `(75%,60%)` |
  | --- | --- | --- | --- | --- |
  | 初始 | `#6C6CDA` | `#A0A095` | `#000000` | `#000000` |
  | 同代次更新 | `#000000` | `#000000` | `#6CDA6C` | `#A095A0` |
  | resize 后 | `#000000` | `#000000` | `#6CDA6C` | `#A095A0` |

- [x] 在隔离部署副本分别移除片元着色器、移除 D3D12 根签名、将片元着色器置空、保留文件名和 `@FSL` 头但截断其载荷。逐项确认在调用 Forge 加载前返回 `kSubmit/kShaderUnavailable` 并正常清理。核对 Modular 从部署目录加载 Renderer，Monolithic 产物无 Renderer 动态依赖。
- [x] Windows 正式 preset 串行验证 Release Monolithic 和 Modular。每轮启用 `PANGU_BUILD_TRIANGLE_SHADER_CHECK`，构建 `PanguRendererTests`、`PanguRendererHostTests`、`PanguForgeLinkCheck`，执行 `ctest --preset windows --no-tests=error -L '^area[.]renderer$' --output-on-failure`。记录 Windows GPU、像素、窗口尺寸和退出状态。补运行 Windows Debug 的 Renderer 定向测试，覆盖 Forge 断言启用路径。
- [x] 提交 `feat: 通过 Project 顶点绘制首个三角形`。

## 任务 3 · macOS 与 iOS Metal 编译和最终链接

**涉及文件**　修改 `Tests/Toolchain/TheForge/RendererLink.cpp`、`Tests/Renderer/Project/CMakeLists.txt` 和必要的 Apple 构建接线。复用 `Tests/Renderer/Acceptance/` 的 macOS 观察器。

- [x] 让 iOS 链接探针实际引用 `RendererSession::PresentTriangle`，使最终 ARM64 `.app` 可检查真实绘制路径。注册 macOS 原生链接用例 `Renderer.TriangleLink`，独立编译和链接同一份 `FirstTriangle.cpp`。
- [x] 从 `moozen-macos` 的 `/Users/moozen/WindowsGit/PanguEngine` 执行 `macos` 和 `ios-arm64` 正式 preset。关闭 `PANGU_BUILD_TRIANGLE_SHADER_CHECK`。macOS 构建 Modular `PanguRenderer` 并用 `ctest --preset macos --no-tests=error -R '^Renderer[.]TriangleLink$'` 检查独立 `FirstTriangle`。iOS 使用 Monolithic 构建 `PanguForgeLinkCheck`，检查 `PanguMetalRendererLinkCheck.app` 的 Mach-O 架构、平台和三角形绘制符号。
- [x] 按固定 Forge FSL 的调用核对两个 SDK 对应的官方 `metal` 工具，当前路径缺失时查看其他可用的 Apple 官方工具。可用时分别启用 `PANGU_BUILD_TRIANGLE_SHADER_CHECK` 并构建 `PanguTriangleShaders`。记录实际使用的 SDK，分别核对 `CompiledShaders/MACOS/` 和 `CompiledShaders/IOS/` 下的 `Graph.vert.metal`、`Graph.frag.metal` 及其中的 `@FSL` 容器和目标 SDK 的库载荷。核对 macOS Project 和 iOS 着色器部署目录采用 Forge 预期的名称和相对路径。有可用 Metal 设备和窗口采样权限时，使用任务 2 的同代次更新和 resize 判据验证 macOS Project。分别记录原生链接、着色器构建和实机运行证据。
- [x] 提交 `test: 验证 Apple 平台三角形编译和链接`。

## 任务 4 · Linux Wayland 与 Android Vulkan 验收

**涉及文件**　扩展 `Tests/Renderer/Acceptance/ObserveWayland.py`、`Tests/Renderer/NativeActivity/AndroidStudio/app/build.gradle.kts`、`Tests/Renderer/NativeActivity/PathStatement.txt`、Android 观察器及测试构建接线。更新 `wiki/runtime/rendering.md` 与适用的测试说明。

- [x] 将 `Renderer.DesktopTriangle` 扩展到启用资源验收的 Linux 构建。在 `moozen-ubuntu` 的 `/home/moozen/WindowsGit/PanguEngine` 用 `linux` preset 串行验证 Release Monolithic 和 Modular。使用同一 `FirstTriangle` 和 Wayland 搬移部署，应用任务 2 的三阶段采样判据。核对呈现请求及合成器请求关窗后的正常退出。以 `ctest --preset linux --no-tests=error -L '^area[.]renderer$' --output-on-failure` 核对 Renderer 与 HEADLESS 回归。
- [x] 为 NativeActivity 测试 APK 接入同一绘制夹具与 `CompiledShaders/ANDROID_VULKAN` 生成资源。Gradle 的生成资源接线声明原生着色器构建依赖，按变体与链接模式隔离输出。使用 Forge 的 Android `PathStatement` 挂载 `RD_SHADER_BINARIES`，核对 APK 中含两个阶段，且 Modular 含独立 Renderer `.so`。
- [x] 用 `android-arm64` preset 验证两种链接模式的最终原生库。分别用 `:app:assembleAgdkDebug -PpanguLinkMode=Monolithic` 和 `-PpanguLinkMode=Modular` 构建测试 APK。在设备可用时使用 `Observe.ps1` 和 `ObserveSwappy.ps1` 验证后台、窗口代次、恢复绘制和 Activity 销毁。按代次记录像素或截图和实际呈现时间戳。
- [x] 核对所改 C++ 的 `clang-format` 与实际编译数据库的 `clang-tidy`、所改 CMake 的 `gersemi` 和 Git 差异。按平台和模式记录命令、被测提交或 tree、测试数量、产物及运行证据。根据首轮目标实际执行情况更新 Runtime 文档和计划验收状态。
- [x] 提交 `test: 验收 Vulkan 三角形部署和窗口生命周期`。

## 首轮平台验收

各行记录实际被测的提交和 tree。Renderer CTest 只统计相应 `area.renderer` 或指定测试，交叉构建使用宿主的链接与产物检查。Apple 工具可用性与真机运行分别记录。

| 平台／配置 | 被测提交及 tree | 验收结果 |
| --- | --- | --- |
| Windows D3D12 Release，Monolithic 和 Modular，Debug Modular | `11f32c0091b98492555bf27f90b03d3473ba884d` · `8d0d53bda80a462aa890074d1adecfd170eea006` | 三组各 20/20。窗口依次为 698×424、698×424、518×304。实采四点分别为 `6C6CDA A0A095 000000 000000`、`000000 000000 6CDA6C A095A0`、`000000 000000 6CDA6C A095A0`。按 Release Monolithic／Modular、Debug Modular 的顺序记录 123／121／121 次呈现、0 跳帧和退出 0。 |
| Windows Debug Modular 的评审修正 | `33ff42f7118a7f0403e6892e535b5ccd7c3d3054` · `789c6dbecca2afd0dd20f7a57b4e8ae2efcc6b8e` | 17/17 Renderer 单测、1/1 独立三角形用例。同一窗口三阶段四点和两个尺寸通过，123 次呈现、0 跳帧、退出 0。四个资源负例均为 `kSubmit/kShaderUnavailable`、0 次呈现和退出 1。 |
| Windows Release Modular 的当前源码定向复验 | `35531cd88ff4f026ac728ff9ee31d660452f741d` · `e6d75e043d5598bcffc7b72cc7a56721ab3091a8` | Renderer 单测和独立三角形 18/18。真实 Git 锁、同 HWND 的三阶段四点与 resize、四个资源负例均通过。122 次呈现、0 跳帧、0 失败、退出 0。 |
| macOS Metal Release Modular | `459fd34ccec17055172e9999bcf4fd6273f2fe7c` · `c044b117efdcb033ba20d4434ac6319b37769272` | 原生 Renderer 和独立 FirstTriangle 真实最终链接，`Renderer.TriangleLink` 1/1，通过符号、dylib 依赖以及 macOS minOS 14.0／SDK 15.2 检查。其余 Renderer 测试 18 项通过、1 项跳过。 |
| iOS Metal Release Monolithic | `ba1493f48d374d6c5e8360cb47cf53c8ff4f05c2` · `43290d2d44da2ac0d138c447565aa124ce10efdc` | ARM64 `PanguMetalRendererLinkCheck.app` 最终链接，iOS minOS 18.0／SDK 18.2。实际入口调用 `PresentTriangle`，最终绘制代码含 `cmdBindVertexBuffer` 和 `cmdDraw`。交叉构建登记 0 项 CTest。 |
| Linux Wayland Release，Monolithic 和 Modular | Modular 及 Monolithic 三角形复验 `35531cd88ff4f026ac728ff9ee31d660452f741d` · `e6d75e043d5598bcffc7b72cc7a56721ab3091a8`。Monolithic 首轮构建 `bcfbf036aa0a5dd7fba2a39ebae30c17c96a2301` · `ab801e37fc5ae30cb4649be425aa73bc6669a639`。 | Modular 22/22。Monolithic 首轮 20/21，唯一因 Git 锁检测到脏工作树而停止的三角形用例定向复验 1/1。两种模式均真机验证三阶段 RGB、同窗口、两次交换链创建与合成器关窗。 |
| Android ARM64 Native Release、APK Debug，Monolithic 和 Modular | 原生和 APK `bcfbf036aa0a5dd7fba2a39ebae30c17c96a2301` · `ab801e37fc5ae30cb4649be425aa73bc6669a639`。生命周期脚本 `35531cd88ff4f026ac728ff9ee31d660452f741d` · `e6d75e043d5598bcffc7b72cc7a56721ab3091a8`。 | 两组原生最终链接、APK 签名和资源均通过。Modular 包含独立 `libPanguRenderer.so`。两种模式各通过一次双窗口代次观察及三次 Swappy 采样，均完成 420 次呈现请求、0 跳帧和 Activity 销毁。 |

### 命令与原始产物

Windows 使用 `windows` preset 配置 `PANGU_BUILD_TRIANGLE_SHADER_CHECK=ON`、`PANGU_ENGINE_ALLOW_LOCAL_OVERRIDE=OFF`，构建 `PanguRendererTests PanguRendererHostTests PanguForgeLinkCheck`，执行 `ctest --preset windows --no-tests=error -L '^area[.]renderer$' -V --output-on-failure`。评审修正使用 Debug Modular，执行 `^PanguRendererTests[.]` 和 `^Renderer[.]DesktopTriangle$` 两组筛选。GPU 为 AMD Radeon RX 580 2048SP。原始记录在 `Build/Windows/Tests/Results/RendererTriangle-11f32c0` 和 `RendererTriangle-33ff42f`，搬移产物在 `Build/Windows/Tests/Work/Renderer.DesktopTriangle/<运行标识>/Deploy`。当前源码使用 Release Modular 正式 preset 另构建 `PanguTriangleShaders`，并运行 `ctest --preset windows --no-tests=error -R '^Renderer[.]DesktopTriangle$|^PanguRendererTests[.]' -V --output-on-failure`，日志在 `Build/Windows/Tests/Results/RendererTriangle-35531cd`。D3D12 四个负例分别移除片元着色器、移除根签名、置空片元文件及截断 FSL 载荷。

macOS 和 iOS 分别用 `macos`、`ios-arm64` preset 配置 `PANGU_BUILD_FORGE_LINK_CHECK=ON`、`PANGU_BUILD_TRIANGLE_SHADER_CHECK=OFF`。macOS 构建 `PanguRenderer PanguRendererTests PanguMacWindowSampler PanguForgeLinkCheck`，分别用 `ctest --preset macos --no-tests=error -R '^Renderer[.]TriangleLink$'` 和 `-L '^area[.]renderer$' -E '^Renderer[.]TriangleLink$'` 验证。iOS 构建 `PanguForgeLinkCheck`，用 `llvm-objdump` 和 `llvm-nm` 检查 `~/PanguEngineBuild/iOS/Tests/Toolchain/PanguMetalRendererLinkCheck.app`。共享源码在 `/Users/moozen/WindowsGit/PanguEngine`，日志保存在对应宿主 BuildRoot 的 `Logs/RendererFirstTriangle/`。

两个 Apple SDK 的 `xcrun --sdk <macosx|iphoneos> --find metal` 和启用着色器选项的 CMake 配置均确认缺少编译器。源码核对的部署路径为独立 macOS 应用旁的 `CompiledShaders/MACOS/` 和 iOS 链接探针 bundle 内的 `CompiledShaders/IOS/`。`Graph.vert.metal`、`Graph.frag.metal` 的实际生成、`@FSL` 与目标 SDK 库载荷仍待核验。Mac 没有可用 Metal 设备和屏幕捕获权限，macOS 窗口运行及 iOS 设备运行亦待验。

Linux 从 Ubuntu 真机的 `/home/moozen/WindowsGit/PanguEngine` 共享源码根使用宿主 ext4 BuildRoot。GPU 为 GTX 1050 Ti，NVIDIA 580.178.04。设 `LLVM_ROOT=/opt/llvm-23.1.0`、`XDG_RUNTIME_DIR=/run/user/$(id -u)`、`WAYLAND_DISPLAY=wayland-0`，并以 `GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0=core.autocrlf GIT_CONFIG_VALUE_0=true` 读取共享工作树。两种模式串行执行以下命令。

```sh
cmake --preset linux -DCMAKE_BUILD_TYPE=Release "-DPANGU_LINK_MODE=$mode" \
    -DPANGU_BUILD_TRIANGLE_SHADER_CHECK=ON -DPANGU_ENGINE_ALLOW_LOCAL_OVERRIDE=OFF \
    -DBUILD_TESTING=ON -DPANGU_BUILD_ENGINE_TESTS=ON -DPANGU_TEST_BUILD_JOBS=4
cmake --build --preset linux --target PanguRendererTests --parallel 4
ctest --preset linux --no-tests=error -L '^area[.]renderer$' --output-on-failure -V
```

Modular 全量 Renderer 标签和 Monolithic 首轮均含 Headless，输出 `context-closed`。Monolithic 首轮三角形的源码锁因一次短暂的 Android PowerShell 编辑报告 `dirty=ON`，程序尚未启动。两个提交间只有 Android 生命周期观察器发生变化。切回 Monolithic 并冻结源码后，`ctest --preset linux --no-tests=error -R '^Renderer[.]DesktopTriangle$' --output-on-failure -V` 定向通过。

两种模式的同一 Sway view `5` 先后采到 720×480 的 `6C6DDA A0A094 000000 000000`、720×480 的 `000000 000000 6DDA6C A094A0`、560×360 的 `000000 000000 6DDA6C A194A0`。Monolithic 121 次，Modular 123 次呈现请求，关窗均退出 0。两套搬移目录分别为 `Tests/Work/Renderer.DesktopTriangle/e0cbec39c8e8/Deploy` 和 `Tests/Work/Renderer.DesktopTriangle/5a94693d7044/Deploy`。Monolithic ELF 无 Pangu 动态依赖。Modular 使用 `LD_DEBUG=libs` 验证从搬移目录加载 `libPanguRenderer.so`。两套 Vulkan FSL 容器内容一致，顶点和片元分别为 2780、2292 字节，各包含一个有效 SPIR-V 载荷。日志保存在 `~/PanguEngineBuild/Linux/Tests/Results/RendererTriangle-2026-11-25`，含各模式 CTest、JUnit、定向复验、ELF 和加载记录。

Android Windows 主机用 `cmake --preset android-arm64 "-DPANGU_LINK_MODE=$mode" -DBUILD_TESTING=ON -DPANGU_BUILD_ENGINE_TESTS=ON -DPANGU_BUILD_FORGE_LINK_CHECK=ON -DPANGU_BUILD_NATIVE_ACTIVITY_TRIANGLE=ON -DPANGU_BUILD_TRIANGLE_SHADER_CHECK=OFF` 配置，然后 `cmake --build --preset android-arm64 --target PanguForgeLinkCheck --parallel 4`。宿主通过 ELF 检查最终 ARM64 `libPanguNativeActivityCheck.so` 中的 NativeActivity 入口与实际三角形引用。交叉构建无可运行的 Renderer CTest。

Android Studio 工程用本地 Gradle 9.6.0、JBR 25 和已下载的 Swappy 2.1.3 AAR，分别执行 `:app:assembleAgdkDebug -PpanguLinkMode=Monolithic` 与 `-PpanguLinkMode=Modular`，附 `-PpanguSwappyAar=D:/Downloads/games-frame-pacing-2.1.3.aar --offline`。APK 放在 `Build/Android/Tests/NativeActivity/Gradle/<模式>/app/outputs/apk/agdk/debug/`。包内容含 `CompiledShaders/ANDROID_VULKAN/Graph.vert` 1856 字节、`Graph.frag` 1596 字节，两个有效 SPIR-V 容器与各自 native 变体的部署物逐字节相同。包内 ARM64 库的 ELF Build ID 与 Gradle 原生 Debug 产物匹配。Monolithic APK SHA-256 为 `a360a9ac37fad5f8cce6b84716582641b01b98d8818329d358e1a49afdf6da8d`，Modular 为 `4018319acfe5a2c6b66c274b194ccd597cda05f6c3770ffe795a000e9fa99820`。

在 23127PN0CC（Adreno 750）上分别 `adb install -r`，执行 `Observe.ps1` 与 `ObserveSwappy.ps1 -Runs 3`。同一 Activity 的两代窗口截图均为 1200×2670。四点 RGB 从第一代 `6C6CD3 9FA096 000000 000000` 变为第二代 `000000 000000 88D779 9E959E`。Android 合成截图的上部主色、下部颜色和场外背景使用各自容差。采样确认切后台撤销第一代、恢复建立第二代、实际呈现时间戳推进，以及第二代撤销后正常销毁。

| Android 代次 | SurfaceFlinger 图层后缀 | 实际呈现数 | 首末时间戳（ns） |
| --- | --- | --- | --- |
| Monolithic 1 | `#453176` | 21 | `1302365183760005` 至 `1302365532521202` |
| Monolithic 2 | `#453207` | 111 | `1302369181407816` 至 `1302371018279794` |
| Modular 1 | `#453088` | 52 | `1302322448793511` 至 `1302323304734709` |
| Modular 2 | `#453122` | 92 | `1302325989519863` 至 `1302327510884238` |

Swappy 每轮分别收集 127／127／127 和 126／127／127 个实际呈现时间戳。六轮均核对 Java 初始化、无 DEX 回退、NativeActivity 原生及 Java 销毁事件。原始 APK 检查、`*-early-lifecycle.log`、`*.surface-1`／`*.surface-2`、逐代 PNG 和像素、`*-final-swappy` 时间戳位于 `Build/Android/Tests/NativeActivity/Results/RendererTriangle-2026-11-25/`。

### 关键提交与静态检查

- `ffe54f4` 接入 FSL 构建，`11f32c0` 完成 Project 部署修正，`33ff42f` 修正资源准备和共享部署规则。
- `459fd34`、`ba1493f` 完成 Apple 的原生编译与链接验收接线。
- `beff858` 接通 Vulkan 部署和窗口生命周期，`bcfbf03`、`4bbf98c`、`35531cd` 完成 Android 换位及观察器判据。
- `4fd4753` 只调整 CMake 的 `add_custom_target` 换行，实际验证产物使用表中所列的提交。改动 C++ 通过 `clang-format --dry-run --Werror`，修改的 CMake 通过 `gersemi --check`。Linux 独立 Project 和 Android 双模式用实际编译数据库执行 `clang-tidy`，无项目文件诊断。

### 分支评审定向复验

被测源码提交为 `e7965fb4dd8ead0c9f705945d935fb16964ddddc`，tree 为 `10831ba4c5587409c443e5ebcac88ab6a8c524fb`。

- Windows D3D12 Release Modular 的 Renderer 单测、Host 检查和独立 `Renderer.DesktopTriangle` 共 19/19。真实 Git 锁下通过同一窗口三阶段取色、resize 及四个资源负例。应用请求呈现 123 次、0 跳帧，正常退出。在独立部署副本中用合法顶点 FSL 容器替换片元容器，修复前记录原生 PSO 失败 `0x80070057` 后访问冲突，修复后同一失败返回 `kSubmit/kNativeFailure`、0 次呈现，关闭 Forge 日志并退出 1。日志位于 `Build/Windows/Tests/Results/RendererTriangle-FinalFixes/`。
- Linux Release Modular 定向构建 Vulkan Renderer，Renderer 单测和 `Renderer.VulkanAdapterSymbols` 共 19/19。日志位于 `~/PanguEngineBuild/Linux/Tests/Results/RendererTriangle-FinalFixes/`。
- Android ARM64 Release Modular 使用同一 `Build/Android`。着色器检查开启时随 `PanguNativeActivityCheck` 部署顶点和片元 FSL 容器。关闭检查并保留缓存的 `PANGU_NATIVE_ACTIVITY_SHADER_OUTPUT_ROOT:PATH` 后，生成成功，`PanguForgeLinkCheck` 实际重链 NativeActivity，ELF 保留对 `libPanguRenderer.so` 的依赖及三角形调用。已恢复原缓存设置。日志位于 `Build/Android/Tests/NativeActivity/Results/RendererTriangle-FinalFixes-Retry/`。
- `clang-format`、`gersemi` 和 Git 差异检查通过。Windows、Linux 和 Android 均以实际编译数据库运行 `clang-tidy`，变更行无诊断。新增 PSO 故障实测仅覆盖 Windows Release，Vulkan 路径以固定 Forge 源码及原生构建核对。Forge 可在原生调用失败处触发 Debug 断言，该场景超出上表的 Debug 容器缺失、置空及截断验收。
