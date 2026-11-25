# Toolchain / ThirdParty 编译与链接测试

本目录保留真实 C++23 编译和链接回归，不经过 AbiProfile，不生成逐 TU 收据，也不扫描 `compile_commands.json` 来判断编译失效。对应 [Tests 维护规范](../README.md) 与 [ADR-0013](../../wiki/decisions/0013-native-build-abi-boundary.md)。

平台检查的编译、最终链接、宿主检查与设备运行边界见[测试分类与运行契约](../../wiki/foundation/testing.md)。

`BUILD_TESTING=ON` 且 `PANGU_BUILD_ENGINE_TESTS=ON` 时始终配置 Toolchain；任一关闭则不接入 Engine 测试。交叉编译时只构建编译型探针，不运行设备程序。

- `ThirdParty/CompiledProbe.cpp`：真实 `ThirdParty::CompiledProbe` STATIC 库，直接使用平台 Clang、STL、C++23，不继承 Pangu Module 专属告警/异常选项。
- `Module/PolicyProbe.cpp`：真实 `PanguToolchainProbe` Module，服从 `PANGU_LINK_MODE`，编译期检查 C++23、无异常和平台 STL。
- `LinkProbe.cpp`：桌面构建 `PanguToolchainLinkTests`，实际链接两座库，CTest 名 `ToolchainLibraryLink`，运行时验证跨库 `std::expected<int, int>`；Modular 下显式导入 Probe 函数。
- `Flecs/CompileProbe.cpp`：`FlecsCompileProbe` 验证 `ThirdParty::Flecs` 的链接模式、公开宏和原生 C++23 消费。桌面生成可执行文件，交叉编译生成 STATIC 归档。

上述 Toolchain 测试的静态库、导入库、动态库及链接验证程序统一输出到所属宿主
`<BuildRoot>/Tests/Toolchain/`，不在 BuildRoot 顶层另建 `Probe` 目录。

桌面正式构建：

```sh
cmake --preset windows
cmake --build --preset windows-full
ctest --preset windows-full -R '^ToolchainLibraryLink$' --output-on-failure
```

完整验证可用 `cmake --workflow --preset windows-full`；普通 ALL 构建不保证独立宿主测试已准备。Linux/macOS 改用相应 preset。Android/iOS 默认 `BUILD_TESTING=OFF`；要检查上述编译型 target，可在 configure 时显式追加 `-DBUILD_TESTING=ON`。这不验证设备执行或独立预编译 SDK 的 ABI 兼容。

macOS 的 `Renderer.TriangleLink` 使用 Engine Git 锁构建独立 `FirstTriangle` Project，检查同一绘制回调和 Renderer 入口的最终链接。该用例通过 `ctest --preset macos --no-tests=error -R '^Renderer[.]TriangleLink$'` 选取，无需 Metal 着色器编译器和窗口。

## 现行移动端真实 Module 最终链接

移动端显式启用 `PANGU_BUILD_FORGE_LINK_CHECK=ON` 后，构建 `PanguForgeLinkCheck` 同时触发独立的 Renderer 链接探针。Android 的 `PanguVulkanRendererLinkCheck` 和 iOS 的 `PanguMetalRendererLinkCheck` 通过 `Pangu::Renderer` 调用公开 Module Query。最终可执行文件用于宿主链接检查，设备启动和图形呈现另行验收。

iOS 探针还通过真实的冷 `RendererSession` 调用 `PresentTriangle`，以便检查最终 Mach-O 中的三角形代码。显式启用 `PANGU_BUILD_TRIANGLE_SHADER_CHECK` 并构建 `PanguTriangleShaders` 时，iOS 着色器部署到探针 `.app` 内的 `CompiledShaders/IOS/`。

Android 的 `PanguAndroidJniThreadCheck` 目标生成可执行文件 `PanguJniThreadCheck`。它使用 NDK 的 JNI 调用表验证线程已附加、需要附加及两种失败路径。在 Android ARM64 设备上运行该检查可核对原始 JNI 状态码和线程分离责任。

Android 通过 `pangu_add_application(KIND GRAPHICS)` 构建 `PanguNativeActivityCheck`，经 `Pangu::Renderer` 使用 Forge 的 Vulkan 取图和呈现。
用 `PANGU_BUILD_NATIVE_ACTIVITY_TRIANGLE=ON` 配置 Android preset 可检查三角形夹具最终链接，字节码检查由 `PANGU_BUILD_TRIANGLE_SHADER_CHECK` 控制。`agdkDebug` 打包同一绘制夹具及 Forge 资源的 ARM64 APK，支持 Monolithic 和 Modular。此 APK 链接官方 Swappy 2.1.3 AAR 的原生库，并编译对应版本的官方 Java 源码。[Android Studio 测试工程](../Renderer/NativeActivity/AndroidStudio/README.md)

安装对应链接模式的 APK 后，生命周期脚本在前台呈现后把 Activity 切到后台并恢复，核对窗口撤销、同一 Activity 的窗口重建、继续呈现和宿主主动关闭。[生命周期观察脚本](../Renderer/NativeActivity/Observe.ps1)

呈现采样脚本默认采集三轮 SurfaceFlinger 实际呈现时间，并检查 Swappy Java 类加载与正常退出。[呈现采样脚本](../Renderer/NativeActivity/ObserveSwappy.ps1)

Linux 首帧使用原生 Wayland 桌面。Ubuntu 构建依赖包括 `libwayland-dev`、`libdecor-0-dev`（0.2.2+）及 Forge 头所需的 `libx11-dev`。GNOME 上使用 `libdecor-0-plugin-1-gtk` 提供标题栏、拖动边框和窗口按钮。GPU 必须能够向目标窗口呈现。选卡时查询实际 Vulkan Surface 的队列族支持，再按 Forge 规则排序。在支持 xdg-shell v6 的 libdecor 和合成器上，收到 `suspended` 后暂停提交，恢复时采用最近协商的内容尺寸。

在 `moozen-ubuntu` 的共享源码根运行以下命令。桌面验收使用 Python 3、Sway（含 `swaymsg`）和 `grim`。通过 SSH 执行时，使用桌面会话所属用户的 `XDG_RUNTIME_DIR` 和 `WAYLAND_DISPLAY`（本机桌面为 `wayland-0`）。

```sh
cmake --preset linux
ctest --preset linux --no-tests=error -R '^Renderer[.]DesktopFrame$' --output-on-failure
```

`Renderer.DesktopFrame` 构建、搬移并运行独立 Project。[Wayland 观察器](../Renderer/Acceptance/ObserveWayland.py) 清除 X11 显示入口，在隔离的嵌套桌面按 PID 定位窗口。它先采样 720×480 窗口的中心像素 `#3F99C8`，调整到 560×360 后触发换色，确认 `#FF0000` 与至少 120 次呈现请求，再由合成器关闭窗口并检查进程成功退出。该轮的像素与退出摘要保存在测试运行目录的 `deployment.log`，逐帧计数在 `Deploy/first-frame.stdout`。[Renderer 接入规格](../../docs/superpowers/specs/2026-11-20-renderer-forge-igraphics-design.md)

配置 Linux preset 时传入 `-DPANGU_BUILD_TRIANGLE_SHADER_CHECK=ON` 注册 `Renderer.DesktopTriangle`。它复用搬移部署和合成器关窗，在同一 Sway 窗口的四个采样点验证初始三角形、同交换链代次换位和 resize 后的绘制。`deployment.log` 记录三阶段尺寸、view ID、RGB 和呈现请求汇总，`Deploy/first-triangle.stdout` 保留原始交换链与逐帧记录。[首个三角形验收](../../docs/superpowers/plans/2026-11-25-renderer-first-triangle.md#首轮平台验收)

2026-11-23 在 `moozen-ubuntu` 验证了 `f2d551d` 加本轮未提交修复的 Release / Modular 工作区。独立 Project 使用本地源码锁覆盖。上述预览 CTest 在嵌套 Sway 中采到两种指定颜色和尺寸，并由合成器关闭成功。原生 Ubuntu v6 桌面运行了 120 次呈现请求，零跳帧。另用隔离的 Mutter 46 虚拟显示器最小化窗口，稳定后一秒内计数保持 98，恢复后增长至 160，随后收到关闭事件并正常退出。预览关闭的独立 Project 在无显示服务时通过 Open 拒绝检查。恢复预览和本地覆盖为 OFF 后，16 项 Renderer 单测及 Vulkan 导出符号检查通过，其中无可用呈现队列与查询失败由单测覆盖。

同日在 `789f662` 加窗口装饰修复的 Release / Modular 工作区复测。Ubuntu 的 libdecor 0.2.2 与 GTK 插件在原生 GNOME 会话协商 xdg-shell v6，完成 120 次呈现请求，零跳帧。独立 Mutter 46 会话中，鼠标拖动标题栏使窗口移动 65×38 像素，拖动右侧边框使窗口增宽 90 像素，最小化时呈现计数停止，恢复后继续增长，点击标题栏关闭按钮后进程成功退出。嵌套 Sway 的原有清屏像素、调整尺寸及关闭验收和 17 项 Renderer 定向 CTest 通过。关闭预览重新构建独立 Project 后，仍可在无桌面服务时返回呈现不可用。

2026-11-24 的干净提交双模式复验见 [Linux Wayland 双模式验收](../../docs/superpowers/plans/2026-11-20-renderer-forge-igraphics.md#linux-wayland-双模式验收)。

旧 `LinkSmoke/` 空程序 SDK 冒烟已删除，其最终链接能力由下面的真实 Runtime/业务宿主检查承接。
正式根分别 `cmake --preset android-arm64|ios-arm64 -DPANGU_LINK_MODE=<Monolithic|Modular> -DBUILD_TESTING=ON -DPANGU_BUILD_ENGINE_TESTS=ON` 并用对应 build preset 编译，再显式执行：

```sh
cmake -DPANGU_ENGINE_SOURCE_DIR=<共享源码根> -DPANGU_TEST_ROOT=<本平台BuildRoot>/Tests/ModuleLinkModes/<模式> -DPANGU_TARGET_PRESET=<android-arm64|ios-arm64> -DPANGU_LINK_MODE=<模式> -P Tests/BuildSystem/ProjectIntegration/CrossCompileTests.cmake
```

Android Windows 宿主根为 `D:/Git/PanguEngine`，iOS 在 `ssh -o BatchMode=yes moozen-macos` 的 `/Users/moozen/WindowsGit/PanguEngine` 执行，源码不能复制到临时目录。夹具只在平台 BuildRoot 生成可变 Project；Engine 仍指共享源码。Monolithic 真归档进入 Android 原生宿主 .so/iOS Mach-O executable；Modular 真实 Foundation/Module/业务库、固定 Query、Runtime 依赖及最小宿主最终链接均需宿主工具断言。Android 确认 ELF/AArch64，iOS 确认 ARM64/platform 2。精确命令、结果、原始路径见[验收记录](../../docs/superpowers/plans/2026-11-06-module-manager-link-modes-validation.md)。完成恢复移动 Monolithic、benchmark OFF。只做编译/链接/宿主检查，不是设备运行、打包、签名或设备级卸载证据。

正式 `mobile-link` 同时准备 ECS 业务夹具。Android 构建 SHARED `MyGameEcsHost` 和导入 `PanguEcsHostCheck` 的可执行 `MyGameEcsHostCaller`，检查 ELF AArch64 和 Android ELF note。iOS 构建同源 `MyGameEcsHost` 可执行文件，检查 arm64 与 iPhoneOS Load Command。Modular 核对实际 ECS/flecs 动态依赖和各业务库唯一的 Module Query，Monolithic 核对最终镜像中的业务 Query 与静态 flecs。交叉目标不构建 GoogleTest，宿主 Meta 工具与目标链接隔离。验收后恢复移动 Monolithic、`BUILD_TESTING=OFF`、`PANGU_BUILD_BENCHMARKS=OFF`。[ECS 测试入口](../ECS/README.md) · [ECS 最终验收](../../docs/superpowers/plans/2026-11-18-ecs-world.md#ecs-交付验收)
