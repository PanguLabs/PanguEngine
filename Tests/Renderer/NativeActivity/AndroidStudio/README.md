# Android Studio Vulkan 真机测试

打开本目录，在 Android Studio 中选择 `agdkDebug`。它构建引擎的 `PanguNativeActivityCheck`，从 `NativeActivity` 经 `Pangu::Renderer` 调用 The Forge Graphics。调试 APK 使用原生 Debug、ARM64 和 API 29，链接模式默认为 Monolithic。Gradle 与 CMake 中间产物放在 `Build/Android/Tests/NativeActivity/Gradle`。

测试应用使用 [Forge GPU 配置](../GPUCfg/gpu.cfg)。对设备 ID 为 `0x43051401` 的 Adreno 750，使用 Forge 自带的设备内存报告规避规则，避免真机在 `vkDestroyDevice` 中崩溃。

原生侧链接官方 AndroidX 2.1.3 AAR 的 Prefab `swappy_static`。Gradle 将 `agdk` 源码集里的 `SwappyDisplayManager` 和 `ChoreographerCallback` 编译进 APK，让应用类加载器直接加载这两个类。 [对应版本的官方源码](https://android.googlesource.com/platform/frameworks/opt/gamesdk/+/f81f888fe11e9540dd580edf5993232172ed3cbe/games-frame-pacing/extras/src/main/java/com/google/androidgamesdk/)

命令行使用 `JAVA_HOME` 指定的 JDK，本机使用 Android Studio 随附的 JBR 25。通过 Android Studio 构建时使用 IDE 的 Gradle JDK 设置。
配套工具链为 Android SDK 37、NDK `30.0.16248370` 和 SDK CMake `4.1.2`。`ANDROID_HOME`、`ANDROID_NDK_HOME` 和 `LLVM_ROOT` 按 [Android 工具链](../../../../wiki/foundation/android-toolchain.md) 设置。Windows 上在本目录执行：

```powershell
.\gradlew.bat :app:assembleAgdkDebug -PpanguLinkMode=Monolithic
.\gradlew.bat :app:assembleAgdkDebug -PpanguLinkMode=Modular
```

默认从 Google Maven 解析 `games-frame-pacing:2.1.3`。使用已下载的同一 AAR 时可传 `-PpanguSwappyAar=D:/Downloads/games-frame-pacing-2.1.3.aar`。

每次构建后安装对应 APK 并验证，再切换链接模式。可用 `:app:installAgdkDebug` 和同一组 Gradle 参数安装。保持真机解锁，从仓库根目录运行生命周期和呈现采样脚本。

```powershell
.\Tests\Renderer\NativeActivity\Observe.ps1 -LogPath Build/Android/Tests/NativeActivity/Gradle/lifecycle.log
.\Tests\Renderer\NativeActivity\ObserveSwappy.ps1 -LogDirectory Build/Android/Tests/NativeActivity/Gradle/AgdkWithJava
```

生命周期脚本在首轮呈现后切到后台，恢复同一个 Activity 后从 SurfaceFlinger 核对第二代窗口的实际呈现。采样脚本记录实际呈现间隔，检查 Swappy Java 类加载，并在每轮主动退出后核对原生 `APP_CMD_DESTROY` 和 Android 的 Java `performDestroy` 完成事件。
