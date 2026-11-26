# Android Studio Vulkan 真机测试

索引四边形与桌面共用 `IndexedMesh.cpp`。在下列 Gradle 命令附加 `-PpanguIndexedMesh=true` 选择该夹具，APK 和 CMake 缓存进入对应链接模式的 `IndexedMesh/` 子目录。安装该 APK 后，给 `Observe.ps1` 加 `-IndexedMesh`，验证同一 Activity 两代窗口中的绿色、红色三角区域和黑色背景。

打开本目录，在 Android Studio 中选择 `agdkDebug`。它构建引擎的 `PanguNativeActivityCheck`，使用与桌面相同的 `FirstTriangle` 夹具，从 `NativeActivity` 经 `Pangu::Renderer` 调用 The Forge Graphics。调试 APK 使用原生 Debug、ARM64 和 API 29，链接模式默认为 Monolithic。Gradle 与 CMake 中间产物放在 `Build/Android/Tests/NativeActivity/Gradle/<链接模式>`。

测试应用使用 [Forge GPU 配置](../GPUCfg/gpu.cfg)。对设备 ID 为 `0x43051401` 的 Adreno 750，使用 Forge 自带的设备内存报告规避规则，避免真机在 `vkDestroyDevice` 中崩溃。

原生侧链接官方 AndroidX 2.1.3 AAR 的 Prefab `swappy_static`。Gradle 将 `agdk` 源码集里的 `SwappyDisplayManager` 和 `ChoreographerCallback` 编译进 APK，让应用类加载器直接加载这两个类。 [对应版本的官方源码](https://android.googlesource.com/platform/frameworks/opt/gamesdk/+/f81f888fe11e9540dd580edf5993232172ed3cbe/games-frame-pacing/extras/src/main/java/com/google/androidgamesdk/)

命令行使用 `JAVA_HOME` 指定的 JDK，本机使用 Android Studio 随附的 JBR 25。通过 Android Studio 构建时使用 IDE 的 Gradle JDK 设置。
配套工具链为 Android SDK 37、NDK `30.0.16248370` 和 SDK CMake `4.1.2`。`ANDROID_HOME`、`ANDROID_NDK_HOME` 和 `LLVM_ROOT` 按 [Android 工具链](../../../../wiki/foundation/android-toolchain.md) 设置。Windows 上在本目录执行：

```powershell
.\gradlew.bat :app:assembleAgdkDebug -PpanguLinkMode=Monolithic
.\gradlew.bat :app:assembleAgdkDebug -PpanguLinkMode=Modular
```

默认从 Google Maven 解析 `games-frame-pacing:2.1.3`。使用已下载的同一 AAR 时可传 `-PpanguSwappyAar=D:/Downloads/games-frame-pacing-2.1.3.aar`。

Gradle 原生目标生成 Forge FSL 资源，再将 `CompiledShaders/ANDROID_VULKAN/Graph.vert` 和 `Graph.frag` 合并到 APK。生成目录是 `Gradle/<链接模式>/NativeShaders/<变体>/`。APK 位于对应链接模式的 `app/outputs/apk/agdk/debug/`。

每次构建后安装对应 APK 并验证，再切换链接模式。可用 `:app:installAgdkDebug` 和同一组 Gradle 参数安装。保持真机解锁，从仓库根目录运行生命周期和呈现采样脚本，使用分别命名的输出保留两种模式的数据。

```powershell
.\Tests\Renderer\NativeActivity\Observe.ps1 -LogPath Build/Android/Tests/NativeActivity/Gradle/lifecycle.log
.\Tests\Renderer\NativeActivity\ObserveSwappy.ps1 -LogDirectory Build/Android/Tests/NativeActivity/Gradle/AgdkWithJava
```

生命周期脚本在首轮呈现后切到后台，恢复同一个 Activity 后从 SurfaceFlinger 核对第二代窗口的实际呈现。两代窗口分别保留 `*.surface-*` 的原始时间戳、`*.window-*.png` 截图和对应 `*.pixels` 四点 RGB。第一代显示左侧图形，恢复后的第二代显示右侧换位图形。采样脚本记录实际呈现间隔，检查 Swappy Java 类加载，并在每轮主动退出后核对原生 `APP_CMD_DESTROY` 和 Android 的 Java `performDestroy` 完成事件。

`-ClearFrame` 适用于设备上已安装的 `FirstFrame` 清屏 APK。当前 Gradle 配置构建的是 `FirstTriangle`，运行两个观察器时均使用默认参数。
