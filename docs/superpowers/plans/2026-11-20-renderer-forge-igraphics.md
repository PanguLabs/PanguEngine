# Renderer 原版 Forge IGraphics 迁移实施计划

> **实施方式**　按任务验证。入口迁移可以独立落地。后端替换在同一轮收束源码、功能补丁和对应的故障注入测试。

**目标**　以原版 Forge 图形接口完成 Pangu 首帧接入，Application 源码迁入 `Source/Runtime/Application/`。

**架构**　Application 入口负责宿主与原生窗口，Host Scope 的 Renderer 封装 Forge 的公开图形接口。

**工具链**　CMake 4.0+、Ninja、项目 Clang preset、The Forge v1.63。

**规格**　[Renderer 首版接入 The Forge IGraphics](../specs/2026-11-20-renderer-forge-igraphics-design.md)。基线为现有 `feat/forge-first-frame` 的 `18541d2`。

## 全局约束

- 固定 Forge gitlink `7443530ef7e888771f307de74b7872becfd64450`，正式配置离线，第三方补丁只做编译、链接兼容。
- 使用仓库指定的共享源码根与隔离的宿主 BuildRoot。Application 不引入新的 Module 或 `Pangu::` 别名。
- Project 回调沿用 Engine 入口，Forge 构建参数和 Renderer 私有目录留在对应编译目标。
- 记录实际命令、被测提交或工作区状态及可观察结果。平台发布门槛按规格记录。

## 评审核对

| 风险 | 对应验证 |
| --- | --- |
| 入口目录迁移后仍用旧绝对路径 | 任务 1 从独立 Project 重建图形及无图形目标。 |
| 私有后端或功能补丁混入统一后的 Renderer | 任务 2 检查正式源清单、补丁和实际链接对象。 |
| Forge 返回 `void` 时把调用计为原生接纳 | 任务 2、3 核对公开枚举和独立 Project 日志。 |
| 旧后端测试的通过结果被引用为新路径的证据 | 任务 3 对新路径复测颜色、窗口和退出。 |
| 通过 Vulkan 冒烟运行就宣布旧链回收安全 | 任务 3 分别记录冒烟结果与正式生命周期能力。 |

### 任务 1 · Application 迁入 Runtime

- [x] 将 `Source/Application/` 迁至 `Source/Runtime/Application/`。同步 `cmake/PanguApplication.cmake` 的应用根与 Win32 Agility 入口路径，以及 `Tests/Renderer/CMakeLists.txt` 的共享入口源和 include 路径。
- [x] 核对八个文件的内容及公开头拼写。运行 `cmake --preset windows -DPANGU_LINK_MODE=Modular -DPANGU_ENGINE_ALLOW_LOCAL_OVERRIDE=ON` 与 `cmake --build --preset windows --target PanguRendererTests`。Windows Renderer 测试已从迁移后的源编译。
- [x] 运行 `ctest --preset windows --no-tests=error --output-on-failure -R '^Renderer[.](Headless|DesktopFrame)$'`。两个独立 Project 用例均通过。此结果验证入口迁移，使用基线 `18541d2` 上的未提交工作区及开发 override，测试后恢复原 override 设置。

### 任务 2 · 统一公开 Forge 后端

- [x] 在现有 `Source/Runtime/Renderer/Private/Backends/` 内收束共同的设备、队列、交换链、取图、清屏和呈现路径，使用 Forge `IGraphics` 公共 API。保留 `MakeForgeBackend()` 的 Module 接线。原生句柄转接使用 `NativeSurface`。
- [x] 将 `RendererTypes.h` 中的呈现结果改为 `kPresentRequested`，同步 `RendererSession`、`GraphicsContext` 和消费回调。共用生命周期测试覆盖进程独占、窗口代次、暂停及关闭。保留公开 API 可观察的正常失败分支。
- [x] 从正式编译图移除自制 D3D12/Vulkan 渲染路径、Metal 功能桥及只用于这些路径的注入测试。撤掉 `0003-d3d12-open-rollback.patch`、`VulkanWsi.patch`、`MetalCompletion.patch`，更新 Forge CMake 与 README。剩余配置适配限于目标工具链与依赖裁剪。
- [x] Windows 正式 Release Modular 的 Renderer 测试 23/23，Forge 链接探针通过。正式编译图只将共享 Forge 接线编入 Renderer。单次返回的呈现调用只计一次，独立 Project 用公开 Application 接口编译。

任务 2 已完成，提交 `2666f27`、`cf5ccfa`、`f451b0d`。Vulkan 在设备初始化和窗口借用前报告 `kOpen/kPresentationUnavailable`，等待上游公开接口补齐发布所需的生命周期能力。

### 任务 3 · 新路径消费及平台验证

- [x] 复用任务 2 已同步的 `Tests/Renderer/Project/FirstFrame.cpp` 与观察器。检查至少 120 次呈现调用、目标色 `#3F99C8` 和 `#FF0000`、正常退出。同一 Project 的 HEADLESS 与 GRAPHICS 分别部署，核对运行时依赖与编译参数隔离。
- [x] Windows 正式 preset 串行验证 Monolithic 和 Modular，使用实际 GPU 核对 resize 和关闭。Linux 和 macOS 正式 preset 验证独立构建及宿主可执行的启动路径。Android arm64 与 iOS arm64 完成真实 Renderer 最终链接。
- [x] 记录平台限制、命令与被测提交。定向 clang-format、clang-tidy、CMake 格式和 Git 检查已执行。

### 任务 3 验收记录

桌面及 iOS 运行在干净提交 `11a9b277ac8b4b18b962367cf01be936de6e5329`。桌面独立 Project 的 Engine Git 锁均为此提交，`PANGU_ENGINE_ALLOW_LOCAL_OVERRIDE=OFF`。Android 在干净提交 `fb48518226f760fc923ca665b9e8c4eb1c9f078c` 验证，后一个提交的构建改动仅在 Android Forge 接线生效。

| 平台 | 验收结果 |
| --- | --- |
| Windows Release Monolithic | 23 项 Renderer 测试和 2 项独立 Project，25/25。AMD Radeon RX 580 2048SP 的客户端中心先采到 `#3F99C8`（698×424），resize 后采到 `#FF0000`（518×304）。122 次呈现调用、0 跳过、0 失败、退出 0。加载部署的 D3D12 Agility 和 AMD AGS，进程无 Renderer DLL。 |
| Windows Release Modular | 同样 25/25。同一 GPU、尺寸和像素结果。121 次呈现调用、0 跳过、0 失败、退出 0。Renderer、D3D12 Agility 和 AMD AGS 均从部署目录加载。 |
| WSL Ubuntu Release Modular | 22/22，包含 ELF 符号检查和独立 Xlib Project。宿主唯一 Vulkan 设备是 CPU 驱动 llvmpipe。实际应用在初始化 Forge 和借用窗口前报告 `kOpen/kPresentationUnavailable`（265）、退出 1。无帧回调，定向验收记录为安全拒绝。 |
| macOS VM Release Modular | 20 项 Renderer 测试和 1 项 Mach-O 符号检查通过。独立 AppKit Project 启动后无物理 Metal 设备，报告 `kOpen/kNoDevice`（261）并退出 1。无帧回调，CTest 将该用例标记为跳过。有可用设备时，该观察器按呈现调用计数和正常退出记录调用冒烟。 |
| Android arm64 Release Monolithic | NDK r30（Clang 21）生成真实 `PanguVulkanRendererLinkCheck`。最终 ELF 为 AArch64，Android note 为 API 29，动态依赖含 `libc++_shared.so`。确认 `QueryRendererModule`、`MakeForgeBackend` 和 Vulkan `queuePresent`。ADB 无设备。 |
| iOS arm64 Release Monolithic | MacPorts Clang 23 与 iPhoneOS 18.2 SDK 生成 `PanguMetalRendererLinkCheck.app`。最终 Mach-O 为 ARM64，平台 iOS、最低 18.0，确认 `QueryRendererModule`、`MakeForgeBackend` 和 Metal `queuePresent`。宿主无有效签名身份。 |

Windows 两模式的 HEADLESS 副本从独立部署目录去掉 PATH 后启动，均输出 `context-closed` 并退出 0。检查应用及随附库的 COFF 导入，无 Renderer 或 Forge。GRAPHICS 和 HEADLESS 的四个实际编译数据库均未向 Project 回调传递 Renderer 私有包含目录或 Forge 编译选项。GRAPHICS 的 Office 档位负例报告 261、退出 1。三桌面的 Forge 链接探针启动退出 0。

Android 新探针先在原 CMake 平台检查处失败，开放内部 Renderer 构建后实际链接又发现 Forge 的可选 Swappy 依赖 `AndroidAttachToCurrentThread`。用项目私有头仅在 `Vulkan.c` 关闭该可选能力。复测确认最终可执行文件没有 Swappy 或该线程入口符号，编译数据库只有一个编译单元使用这项配置。

Windows、Linux、macOS 正式根依次为 `D:/Git/PanguEngine/Build/Windows`、`~/PanguEngineBuild/Linux`、`~/PanguEngineBuild/macOS`。Linux 从 WSL 的 `/home/moozen/WindowsGit/PanguEngine` 运行，使用 `/opt/llvm-23.1.0` 及 `~/PanguEngineBuild/ThirdPartyDeps/Forge-v1.63/sysroot` 的 pkg-config 搜索路径。macOS/iOS 以 `ssh -o BatchMode=yes moozen-macos` 从 `/Users/moozen/WindowsGit/PanguEngine` 运行，使用 `/opt/local/libexec/llvm-23`。Linux/Mac 都向 Git 传入 `GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0=core.autocrlf GIT_CONFIG_VALUE_0=true`，与共享工作树的 Windows 行尾规则一致。各宿主核对了测试前后的提交及干净工作树。

Windows 的 `$mode` 依次取 `Monolithic`、`Modular`，模式切换在上一轮 CTest 完成后执行。

```powershell
cmake --preset windows "-DPANGU_LINK_MODE=$mode" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DPANGU_BUILD_ENGINE_TESTS=ON -DPANGU_BUILD_FORGE_LINK_CHECK=ON -DPANGU_ENGINE_ALLOW_LOCAL_OVERRIDE=OFF -DPANGU_TEST_BUILD_JOBS=4
cmake --build --preset windows --parallel 4 --target PanguRendererTests PanguForgeLinkCheck
ctest --preset windows --no-tests=error -V -R '^PanguRendererTests[.]|^Renderer[.](Headless|DesktopFrame)$' --output-on-failure
```

以下命令中的桌面组合依次为 `linux / 4 / VulkanAdapterSymbols` 和 `macos / 3 / MetalAdapterSymbols`。

```sh
cmake --preset "$preset" -DPANGU_LINK_MODE=Modular -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DPANGU_BUILD_ENGINE_TESTS=ON -DPANGU_BUILD_FORGE_LINK_CHECK=ON -DPANGU_ENGINE_ALLOW_LOCAL_OVERRIDE=OFF "-DPANGU_TEST_BUILD_JOBS=$jobs"
cmake --build --preset "$preset" --parallel "$jobs" --target PanguRendererTests PanguForgeLinkCheck
ctest --preset "$preset" --no-tests=error -V -R "^PanguRendererTests[.]|^Renderer[.](DesktopFrame|$adapter)$" --output-on-failure
```

移动端分别使用 `android-arm64` 和 `ios-arm64`，产物位于各自正式 BuildRoot 的 `Tests/Toolchain/`。使用 `llvm-readobj --file-headers --notes --needed-libs` 核对 Android ELF，使用 `llvm-objdump --macho --private-headers` 核对 iOS `.app` 内可执行文件，两边均用 `llvm-nm --demangle` 检查真实实现符号。

```sh
cmake --preset "$preset" -DPANGU_LINK_MODE=Monolithic -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DPANGU_BUILD_ENGINE_TESTS=ON -DPANGU_BUILD_FORGE_LINK_CHECK=ON -DPANGU_ENGINE_ALLOW_LOCAL_OVERRIDE=OFF
cmake --build --preset "$preset" --parallel 3 --target PanguForgeLinkCheck
```

Windows 最终回到 Release Modular。移动端恢复 Release Monolithic、`BUILD_TESTING=OFF` 和 `PANGU_BUILD_FORGE_LINK_CHECK=OFF`。`clang-format --dry-run --Werror`、`gersemi --check` 和 `git diff --check` 通过。按实际编译数据库对 Windows 四个 Project 回调和 Android Renderer 链接探针执行 clang-tidy，均退出 0。Android 显示三条已有 `ModuleIdentity.h` 诊断。

## 状态

入口迁移、共享 IGraphics 后端和可用宿主上的新路径验收已完成。Windows `kPresentRequested` 计数表示 Forge 的呈现调用返回，窗口颜色由客户端像素单独核对。

Vulkan v1.63 的公开接口仍缺有界取图及可证明的旧链呈现退休。Linux 和 Android 的原生窗口撤销、安全重挂接、关闭与物理呈现继续作为发布门槛。macOS 的像素、resize 与关闭需有可用 Metal 设备。Android NativeActivity 与 iOS UIKit 图形入口、设备运行和签名安装待验收。

Windows Debug 的 `PanguRendererTests.Host.RendererOwnedByExactHost` 曾报 `0xc0000005`，在最终评审基线 `83fee69` 的同一正式构建根重现。Forge 工具文件系统模式未挂载 `RD_LOG`，内存跟踪却在退出时尝试打开泄漏报告。改用 Forge 工具的无文件初始化方式后，Host 定向用例通过，Debug Modular 的 23 项 Renderer 测试全部通过。Release 两种链接模式的 GPU 证据仍归上表所列提交。当前 Windows 宿主只有一块 GPU，混合 GPU 的 Debug 路径待设备验证。

最终修正提交 `b1dfdf6f56a18abaa5c9bc2588d13d185a9e04b4` 已通过整分支修正复评。Windows Release Modular 定向测试 23/23。该提交在 Engine override OFF 下单独复测 `Renderer.DesktopFrame`，Office 档启动被拒绝，真实 GPU 报告 resize 前后客户端颜色 `#3F99C8` 和 `#FF0000`、122 次呈现调用、0 次跳过、0 次失败、退出 0。

## 后续分支清理

- Linux 的 `ObserveXlib` 只进入构建，现行 `Renderer.DesktopFrame` 直接执行应用并核对 Vulkan 预览关闭时的启动拒绝。删除观察器以及生成、构建接线，保留该测试与 Vulkan 符号检查。
- 移除各平台正式构建均已绕开的 `PanguRendererSession` 独立库。桌面会话测试通过真实 `Pangu::Renderer` 编译和链接，移动端仍做真实 Renderer 最终链接。
- 呈现、交换链清理和后端关闭按 Forge 的公开返回能力建模，删除旧后端的呈现失败、异步退休和清理阶段设备丢失模拟。保留取图与提交失败、首错、进程独占、窗口代次和跨线程撤销覆盖。两个旧应用退出用例收束为一个，验证可观察的提交错误在窗口资源清理后返回。

清理后的 Windows Renderer 定向验证 17/17 通过，包含独立 Project 首帧与无图形启动。WSL Linux 15/15 通过，包含独立 Project 的 Vulkan 启动拒绝。macOS 14 项通过，独立 Project 按无 Metal 设备条件跳过物理呈现。Android NativeActivity 生命周期 APK 验证窗口撤销、重建和宿主关闭，`agdkDebug` APK 完成三轮真机呈现采样，Swappy Java 加载告警及内嵌 DEX 回退均为 0/3。Activity 正常退出见[主动退出复验](2026-11-21-android-studio-renderer-check.md#主动退出复验)。iOS 通过 Renderer 最终链接。清理阶段的 Android 真机日志位于 `Build/Android/Tests/NativeActivity/RendererCleanup`。

## Linux Wayland 双模式验收

2026-11-24 在 `moozen-ubuntu` 从共享源码根验收 x86-64 Release。被测干净提交为 `e6eef0dac28481f1ec14aa90534478a73e8dce09`，tree 为 `74255ba3cd7240c7d1e9657fa620134e51f3d293`。Clang 23.1.0、CMake 4.4.2、libwayland 1.22.0、libdecor 0.2.2。宿主启用原生 Wayland，Vulkan 使用 GTX 1050 Ti 和 NVIDIA 驱动 580.178.04。独立 Project 锁定同一提交，源码锁覆盖为 OFF。

| 链接模式 | Renderer CTest | 独立部署结果 |
| --- | --- | --- |
| Monolithic | 18/18，通过且无跳过 | FirstFrame 不含 Pangu 动态库依赖。记录 123 次呈现请求，合成器关闭后退出 0。Headless 输出 `context-closed`。 |
| Modular | 19/19，通过且无跳过，包含 Vulkan 导出符号检查 | FirstFrame 从搬移后的部署目录加载 `libPanguRenderer.so`。记录 121 次呈现请求，合成器关闭后退出 0。Headless 输出 `context-closed`。 |

两个模式均由版本化的桌面观察器在嵌套 Sway 中采到 720×480 窗口的 `#3F99C8`，调整到 560×360 后采到 `#FF0000`。测试运行期间隔离了独立 Project 的构建目录。另在独立的 Mutter 46 会话补测 xdg-shell v6 挂起与恢复。Monolithic 最小化时呈现计数稳定在 77，恢复后增长至 138。Modular 对应计数为 23 和 84。两者均收到合成器关闭事件并正常退出。

按 [Linux 构建说明](../../Tests/Toolchain/README.md) 准备 Wayland 环境，从 `/home/moozen/WindowsGit/PanguEngine` 串行执行：

```sh
export GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0=core.autocrlf GIT_CONFIG_VALUE_0=true
for mode in Monolithic Modular; do
    cmake --preset linux -DCMAKE_BUILD_TYPE=Release "-DPANGU_LINK_MODE=$mode" \
        -DPANGU_ENABLE_VULKAN_PREVIEW=ON -DPANGU_ENGINE_ALLOW_LOCAL_OVERRIDE=OFF \
        -DBUILD_TESTING=ON -DPANGU_BUILD_ENGINE_TESTS=ON -DPANGU_TEST_BUILD_JOBS=4
    cmake --build --preset linux --target PanguRendererTests --parallel 4
    ctest --preset linux --no-tests=error -L '^area[.]renderer$' -V --output-on-failure
done
```

日志保存在 Ubuntu 的 `~/PanguEngineBuild/Linux/Tests/Results/Wayland-2026-11-24-QPXC69`，按模式记录 CTest 日志和 JUnit、ELF 依赖及 Mutter 协议日志。独立首帧部署分别位于 `Tests/Work/Renderer.DesktopFrame/ccad07460cfe` 和 `Tests/Work/Renderer.DesktopFrame/8fc729f55763`（相对于 Linux BuildRoot）。结束后恢复 Release / Modular、预览 OFF、源码锁覆盖 OFF 和测试构建预算 2。

此次补齐 Linux 的双模式开发预览与隔离部署验收。有界取图、取图期间的原生窗口撤销，以及旧交换链的呈现退休仍属于正式发布门槛。[Renderer 接入规格](../specs/2026-11-20-renderer-forge-igraphics-design.md#验收口径)

## 2026-11-24 各平台补充验证

被测干净提交为 `9472991d6cb416b3e7591dd71ac19f30f038a755`，tree 为 `5db60bad31177be4e144b3aa9b93828d6c4ce814`。本轮 macOS 和 iOS 的验收范围为编译及最终链接，Windows、Android 和 Linux 按下表验证运行。

| 平台 | 验收结果 |
| --- | --- |
| Windows x64 | Release Monolithic 和 Modular 各 17/17，通过且无跳过。两模式的首帧 Project 均从独立部署运行，客户端中心从 698×424 的 `#3F99C8` 切换到 518×304 的 `#FF0000`，各 123 次呈现请求、零跳帧、零失败、退出 0。独立无图形 Project 均输出 `context-closed`。Engine 源码锁覆盖为 OFF。 |
| Android ARM64 | Release 的 Monolithic 和 Modular 生命周期测试 APK 均在 23127PN0CC（Android 16）安装运行。关闭 Vulkan 预览时验证 Activity 后台切换、窗口代次 1 和 2 的撤销与重建、Activity 销毁及宿主关闭。Modular APK 包含独立的 `libPanguRenderer.so`。 |
| Android Vulkan 开发预览 | `agdkDebug` 的 Debug Monolithic APK 在同一真机通过三轮。每轮 421 次呈现请求、零跳帧、原生宿主关闭及 Java `performDestroy` 完成，Swappy Java 加载警告和内嵌 DEX 回退均为零。SurfaceFlinger 每轮采到 127 个实际呈现时间戳，374/378 个采样间隔落在 16.667 ms 的 ±1 ms 内。 |
| Linux x86-64 | 从 `e6eef0d` 到本轮提交只修改了两份 Markdown 文档，复用上节的 Ubuntu 真机双模式验收，Monolithic 18/18、Modular 19/19。当前提交在 Release Modular、预览 OFF 的正式根补跑 17 项 Renderer 定向回归，17/17，含 Vulkan 导出符号检查。 |
| macOS x86-64 | MacPorts Clang 23.1.2，Release Modular。真实 `libPanguRenderer.dylib` 与 `PanguForgeLinkCheck` 均最终链接为 x86-64 Mach-O。 |
| iOS ARM64 | Release Monolithic，iPhoneOS SDK 18.2。真实 `PanguMetalRendererLinkCheck.app` 可执行文件最终链接为 ARM64 Mach-O，`LC_BUILD_VERSION` 标记平台 iOS、最低系统版本 18.0。 |

Windows 分别用 `windows` preset 配置两种链接模式，构建 `PanguRendererTests`、`PanguRendererHostTests`、`PanguForgeLinkCheck`，再执行 `ctest --preset windows --no-tests=error -L '^area[.]renderer$' -V --output-on-failure`。macOS 用 `macos` preset 构建 `PanguRenderer` 和 `PanguForgeLinkCheck`，iOS 用 `ios-arm64` preset 构建 `PanguForgeLinkCheck`，两者均启用 `PANGU_BUILD_FORGE_LINK_CHECK`。

Android 生命周期用 `android-arm64` preset 分别构建两种模式的 `PanguForgeLinkCheck`，依次运行 `Package.ps1`、`adb install -r` 和 `Observe.ps1`。预览用 `:app:assembleAgdkDebug`，采用校验官方 SHA-256 的 Gradle 9.6.0 分发包及本机的 Swappy 2.1.3 AAR，装包后执行 `ObserveSwappy.ps1 -Runs 3`。预览 APK 的 SHA-256 为 `39621554e440496db30beda50f34f671e6b04a031e64eb4ff9e306eb5c0a4db9`。

原始记录分别位于 Windows 的 `Build/Windows/Tests/Results/Renderer-2026-11-24-190836`、Android 的 `Build/Android/Tests/NativeActivity/Results/Renderer-2026-11-24-191100`、Ubuntu 的 `~/PanguEngineBuild/Linux/Tests/Results/RendererDefault-PX1kBQ` 及 Mac 的 `~/PanguEngineBuild/macOS/Tests/Results/RendererBuild-NRnyvs`。

### 迁移结项

迁移阶段已通过整分支评审和本轮约定的五平台验收。最终复评确认 `b1dfdf6` 解决了三项评审发现。各阶段的关键提交、通过范围和原始验收日志位置记录于本计划。

Linux 和 Android 的 Vulkan 正式发布仍待验证有界取图、呈现期间的原生窗口撤销，以及旧交换链的呈现退休。后续发布验收按 [Renderer 接入规格](../specs/2026-11-20-renderer-forge-igraphics-design.md#验收口径) 跟踪。
