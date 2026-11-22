# Android NativeActivity 宿主实施计划

> 按已确认的 NativeActivity 宿主范围实施并逐项验收。

**目标**是让 Pangu 图形应用通过 Android NativeActivity 入口启动 EngineHost，并在同一个 Activity 内处理窗口撤销和重建。

**架构**采用 NDK native app glue 和 `ALooper_pollOnce()`。Android 宿主复用图形应用的 EngineHost 启动与关闭流程，验证 Renderer 在借用窗口前报告 `kPresentationUnavailable`，然后独立推进窗口事件。

设计边界见 [Renderer 首版接入 The Forge IGraphics](../specs/2026-11-20-renderer-forge-igraphics-design.md)。正式构建使用根目录 `android-arm64` preset 与 NDK r30。Android 窗口绘制仍以原版 Forge 的有界取图和可验证呈现回收为发布条件。

## 应用入口和双模式链接

- [x] 在 Android 工具链检查中用 `pangu_add_application` 创建 `PanguNativeActivityCheck`，以缺少图形入口的配置错误建立回归基线。
- [x] 在 `cmake/PanguApplication.cmake` 接入 Android SHARED 应用目标、NDK glue 源和平台库。保留隔离的应用部署目录。
- [x] 在 `Source/Runtime/Application/Private/Platform/Android.cpp` 实现 `android_main`、窗口事件和 `ALooper_pollOnce`。复用 `GraphicsApplication.cpp` 的宿主装配。将无窗口与应用退出分开处理。
- [x] 使用 `cmake --build --preset android-arm64 --target PanguForgeLinkCheck` 验证 Monolithic 和 Modular，检查原生入口导出、链接依赖与隔离部署。

## APK 与真机

- [x] 提供测试 Manifest 和打包脚本。用 SDK 工具对 ARM64 APK 对齐、签名并检查打包库。
- [x] 在真机启动、切后台、恢复到同一个 Activity 并退出。日志验证启动保护、旧窗口撤销、不同代次窗口重建和 EngineHost 关闭。
- [x] 对共享的桌面 Application 入口执行定向回归，记录仍受阻的 Vulkan 呈现。

## 验收记录

- 新验证目标首次配置可复现缺少 Android 图形入口。接入后，NDK r30、API 29 的 ARM64 双模式均通过最终链接、`ANativeActivity_onCreate` 导出检查和 APK 签名检查。Modular APK 按实际 ELF 依赖打包 Pangu 运行库。
- Windows Renderer 定向回归 25 项通过，Linux 22 项通过。最终 Android 静态检查保留 Foundation 已有的三个 `ModuleIdentity.h` 诊断，Android Application 改动无新增诊断。
- 在 Adreno 750 真机上，通过系统安装器安装 Monolithic APK，设备包的 SHA-256 与本机产物一致。观察脚本通过系统 HOME Intent 切到后台，恢复原 Activity 后清除其任务，按同一个 NDK 工作线程核对 `presentation-unavailable → window-ready 1 → window-retired 1 → window-ready 2 → window-retired 2 → activity-destroyed → host-closed`。原始日志在 `Build/Android/Tests/NativeActivity/Monolithic/device.log`。设备不允许 ADB 注入按键，验收使用 Android 任务切换命令。
- 持续图形呈现继续受 Forge Vulkan 有界取图和交换链回收能力约束。
