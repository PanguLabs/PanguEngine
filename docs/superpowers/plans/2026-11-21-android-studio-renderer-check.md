# Android Studio Renderer 验证实施计划

**当前打包入口** Android Vulkan 预览统一使用 Android Studio 的 `agdkDebug`，Gradle 编译与 AAR 对应的 AGDK Java 源码。下文的双变体对照为历史采样记录，其中退出按旧脚本的 `host-closed` 标记判定。Activity 正常退出的结果见[主动退出复验](#主动退出复验)。

**目标** 为现有 `PanguNativeActivityCheck` 提供 Android Studio APK 打包入口，在同一条 Vulkan 路径上比较 Forge 随附的 Swappy 与官方 AAR。

**结构** 以 `CppDemo` 的 Kotlin DSL 与 `externalNativeBuild` 为参考。Gradle 从引擎根 CMake 构建现有 `PanguNativeActivityCheck`，由 `Pangu::Renderer` 调用 Forge Graphics。两个 APK 使用独立应用 ID，共用 NativeActivity、测试场景和 GPU 配置资源。

**范围** 验证 Android ARM64 Monolithic，沿用宿主的 API 29。首轮验收采用 Android preset 的原生 Release 配置，恢复原生 Debug 的验证见末节。独立测试构建输出位于 `Build/Android/Tests/NativeActivity/Gradle`。

## 任务一 · Android Studio APK

- [x] 在 `Tests/Renderer/NativeActivity/AndroidStudio/` 接入 Gradle Wrapper、`app` 和 NativeActivity Manifest。
- [x] 增加 `forge` 和 `agdk` 变体。`forge` 使用现有 AGDK 2.0.0 Java 源码和 Forge 随附原生库。`agdk` 使用 AndroidX `games-frame-pacing:2.1.3` 的 Prefab 与 Java 内容。
- [x] 在 `Source/ThirdParty/TheForge/Android.cmake` 增加默认关闭的 Prefab 选择。选中 Prefab 时直接消费 `games-frame-pacing::swappy_static`，匹配的头文件由导入目标提供。
- [x] 构建两个 APK，核对 Manifest、资源、DEX 和最终链接的 Swappy 版本。

本机用 Gradle 9.6.0 和已下载、校验过的官方 AAR 构建 `:app:assembleForgeDebug`、`:app:assembleAgdkDebug`。Google Maven 直连下载 Prefab CLI 超时，实测从临时仓库读取与官方 SHA-256 一致的 CLI，并用同一仓库验证默认 Maven 坐标。两个本地链接命令都包含 `PanguRenderer` 和 Forge；随附构建的原生符号版本为 2.0.0，AAR 构建为 2.1.3 发行包内的 2.1.2。只有随附构建的 APK DEX 包含 `SwappyDisplayManager`。

## 任务二 · 设备对照

- [x] 扩展现有真机采样脚本，通过应用 ID 识别两个 APK，保留告警与呈现时间数据。
- [x] 真机运行两个变体，记录 Vulkan 呈现、Swappy Java 加载和实际帧间隔。
- [x] 记录可复现的构建与采样命令、已验证的变体和实测结果。

首次原生 Debug 包呈现了 421 次，退出时在 Adreno Vulkan 驱动的 `vkDestroyDevice` 崩溃。调试 APK 改用 Android preset 的原生 Release 配置后重新构建、安装，并用 SHA-256 确认手机上的 APK 对应最新产物。采用 [测试工程说明](../../../Tests/Renderer/NativeActivity/AndroidStudio/README.md) 的采样命令，在同一台 120 Hz Android 真机各跑三轮，六轮均呈现并正常退出。

| 变体 | Java 加载警告 | 落在 16.667 ms ±1 ms 内 | 合并 P95 |
| --- | ---: | ---: | ---: |
| `forgeDebug` | 0/3 | 330/375（88.0%） | 24.898 ms |
| `agdkDebug` | 3/3 | 263/378（69.6%） | 33.221 ms |

只使用 AAR 的 `agdkDebug` 三轮均回退到内嵌 DEX 并报告找不到应用原生库。它的第三轮出现明显的节奏波动，单设备短测只记录观察结果。原始 logcat、SurfaceFlinger 时间戳和 `comparison.json` 位于 `Build/Android/Tests/NativeActivity/Gradle`。

## 补充 · `agdkDebug` 的 Java 类加载修复

- [x] 对只使用官方 AAR 的 APK 运行严格采样，确认 `InMemoryDexClassLoader` 找不到 `libPanguNativeActivityCheck.so` 时脚本失败。
- [x] 将 2.1.3 发布记录对应的 Java 源码和许可证放入 `agdk` 源码集，验证 APK 的应用 DEX 和现有原生链接产物。
- [x] 重新安装 `agdkDebug`，严格采样三轮并归档结果。

Gradle 的 `:app:assembleAgdkDebug` 和 `:app:assembleForgeDebug` 通过。新 `agdkDebug` 的应用 DEX 含两个 Swappy Java 类，原生库和资源与修复前的对照 APK 逐字节一致。真机三轮均提交 421 次呈现请求、跳过 0 次并正常退出，内嵌 DEX 回退和 Java 加载警告均为 0/3。373/376 个实际呈现间隔落在目标 16.667 ms 的 ±1 ms 内（99.2%）。这次短测记录位于 `Build/Android/Tests/NativeActivity/Gradle/AgdkWithJava`，修复前复现日志位于 `AgdkWithoutJava`。

## 补充 · 恢复原生 Debug

- [x] 使用同一份原生 Debug 二进制验证 Adreno 750 的内存报告规避规则。对照包在呈现 421 次后发生 SIGSEGV，加入规则的 APK 正常退出 3/3 轮。
- [x] 将规则接入两个打包入口共用的 Android 测试资产，核对 APK 使用的原生编译配置和 GPU 配置。
- [x] 安装 `forgeDebug` 与 `agdkDebug`，各用严格采样完成三轮 Debug 验证。

这台 Adreno 750（`0x5143:0x43051401`、驱动 `0762.36.1`）在启用 Vulkan 设备内存报告时，原生 Debug 对照包在 `vkDestroyDevice` 内复现崩溃。用 Forge 现有的 `devicememoryreportcrashworkaround` 配置后，同一个 Debug 二进制通过三轮对照。`Package.ps1` 与 Gradle 共用 `GPUCfg/gpu.cfg`，并分别验过 APK 资源。Gradle 取消对 CMake 配置的 Release 覆盖，两个最终 APK 通过 ELF Build ID 与原生 Debug 链接输出对应。真机各三轮正常呈现并退出，Swappy Java 加载警告和内嵌 DEX 回退均为 0/6 轮。原始对照和正式 Gradle Debug 验证日志位于 `Build/Android/Tests/NativeActivity/Gradle/DebugDeviceMemoryReport`。

## 收敛 APK 打包入口

移除 `forgeDebug`、旧 `SwappyJava` 源码和 PowerShell 的 Vulkan 预览打包分支。`Package.ps1` 用于 NativeActivity 生命周期 APK，已验证输出 `android:hasCode=false`，并在预览开启时指向 Android Studio。

Gradle 仅保留 `agdkDebug` 预览方案，`:app:assembleAgdkDebug` 通过，APK 中的原生 Debug 库链接官方 AGDK Prefab，应用 DEX 含对应版本的两个官方 Swappy Java 类。安装新 APK 后完成三轮呈现采样，Java 加载警告和内嵌 DEX 回退均为 0/3。日志位于 `Build/Android/Tests/NativeActivity/Gradle/AgdkOnly`。

## 主动退出复验

旧版 `android_main` 写出 `host-closed` 后请求关闭 Activity 并直接返回，NDK Glue 的生命周期回调随后无法完成。新的验收脚本在旧 `agdkDebug` APK 上报告未处理 Activity 销毁。修正后，宿主释放图形资源，再请求关闭并继续处理事件，收到 `APP_CMD_DESTROY` 后才退出原生线程。

在基于 `fdba5cb` 的修正工作区重新构建并安装 `agdkDebug` 后，`ObserveSwappy.ps1` 三轮均验证了主动停止、原生销毁、宿主关闭和 Android 的 Java `performDestroy` 完成。关闭 Vulkan 预览的 Release Monolithic APK 也通过了 `Observe.ps1` 的窗口撤销、重建和系统发起销毁验收。真机日志位于 `Build/Android/Tests/NativeActivity/ActivityExit`。
