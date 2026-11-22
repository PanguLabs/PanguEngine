# The Forge v1.63

`Repo/` 固定到 [ConfettiFX/The-Forge](https://github.com/ConfettiFX/The-Forge) 的 v1.63 提交
`7443530ef7e888771f307de74b7872becfd64450`，主许可证为 [Apache-2.0](Repo/LICENSE)。
首次使用时运行 `git submodule update --init Source/ThirdParty/TheForge/Repo`。
CMake 使用本地 Git 应用补丁，配置阶段不访问网络。

`ThirdParty::TheForge` 是普通 STATIC/PIC 目标，图形 Module 可将其作为私有依赖。
`CMakeLists.txt` 列出公共文件系统、压缩解码、日志、计时和配置单元，
各平台脚本列出实际后端和平台实现。编译型依赖使用目标 preset 的 Clang，
公共头以同一份图形配置解析，消费方使用自己的 C++ 标准。

Windows 源清单核对上游 `Examples_3/Unit_Tests/PC_VS2019/Libraries/{OS,Renderer}`；
Apple 核对 `Examples_3/Unit_Tests/macOS_Xcode/The-Forge/The-Forge.xcodeproj` 的
macOS 和 iOS Sources。Linux 与 Android 核对 `Common_3/OS/{Linux,Android}`、
`Graphics/{GraphicsConfig.h,Vulkan/VulkanConfig.h}` 和 Android 平台工程。
每个目标进程只链接一个图形后端，应用自行提供进程入口。

| 平台 | 后端与 SDK/系统依赖 | GPU 数据文件，路径相对于 `Repo/Common_3` |
| --- | --- | --- |
| Windows x64 | Direct3D 12，Windows SDK 的 D3D12/DXGI、GUID、系统窗口和输入库。随 Forge 固定的 Agility SDK、DXC、AGS、NVAPI、WinPixEventRuntime 见 `Graphics/ThirdParty/OpenSource` | `OS/Windows/pc_gpu.data` |
| Linux x86-64 | Vulkan、X11 开发包。Forge 接口使用 X11 头，Application 链接 X11 并处理窗口。系统 `libvulkan.so.1` 由上游 volk 在运行时加载，另需 Vulkan 驱动 | `OS/Windows/pc_gpu.data`（共用桌面 GPU 数据） |
| Android ARM64 | Vulkan、NDK、`libc++_shared.so` 以及系统 Android/Log 等基础库。Forge 公共头包含 NDK NativeActivity Glue 头。Vulkan API 由系统 loader 与驱动提供 | `OS/Android/android_gpu.data` |
| macOS / iOS | Metal，目标 macOS/iPhoneOS SDK；Metal、MetalPerformanceShaders、GameController、CoreHaptics、QuartzCore、Foundation、CoreFoundation、CoreGraphics、CoreVideo，以及 AppKit/IOKit 或 UIKit。用 preset Clang 编译 OBJC/OBJCXX，Apple 语言启用在图形消费者与依赖的共同目录作用域 | `OS/Darwin/apple_gpu.data` |

Linux x86-64 的 Forge 目标使用私有 `-mavx2` 编译选项，运行链接该归档的图形程序需要 AVX2。

Android 启用 Forge Vulkan 的 Swappy。独立 CMake 构建链接固定子模块随附的 ARM64 `libswappy_static.a`，Android Studio Vulkan 预览通过 Prefab 链接官方 AndroidX AAR。`android` 和 `log` 由 Forge 的 Android 接线声明，`dl` 和 `m` 由 NDK 工具链提供。
`AndroidSwappy.c` 为 Forge 提供 Activity 和 JNI 线程接入符号，调用方负责 Activity 与线程的生命周期。
Linux 和 Android 的 Forge 源清单仅保留 Vulkan 及所需的基础实现。原生窗口接入属于 Pangu 的 Application 层。
Windows D3D12 的设备丢失路径引用 Forge `requestReset`，Apple 的文件系统通过计时函数引用 Forge 平台基础代码。
这两条依赖由同一静态归档中的上游平台源文件满足，应用入口由 Pangu 提供。
首帧目标使用 `Resources/ResourceLoader` 下的 `TinyImageFormat` 头文件表示交换链格式，资源加载器实现已从编译清单裁掉。

## PanguRenderer 使用的 Forge 文件

本节 Forge 头文件和源码路径相对于 `Repo/Common_3/`。按 CMake 列出的 Forge 编译单元统计，含 17 个公共源文件，
Windows 编译 33 个，Linux 和 Android 各编译 25 个，macOS 编译 31 个，iOS 编译 30 个。

### PanguRenderer 直接包含的头文件

```text
Graphics/GraphicsConfig.h
Graphics/Interfaces/IGraphics.h
OS/Interfaces/IOperatingSystem.h
Resources/ResourceLoader/ThirdParty/OpenSource/tinyimageformat/tinyimageformat_base.h
Utilities/Interfaces/IFileSystem.h
Utilities/Interfaces/ILog.h
Utilities/Interfaces/IMemory.h
```

`GraphicsConfig.h` 还会引入 `Application/Config.h` 和对应后端的配置头。
[ForgeBackend.cpp](../../Runtime/Renderer/Private/Backends/ForgeBackend.cpp)

### 公共源文件

```text
Graphics/GraphicsConfig.cpp
Utilities/FileSystem/FileSystem.c
Utilities/Log/Log.c
Utilities/Math/StbDs.c
Utilities/MemoryTracking/MemoryTracking.c
Utilities/Timer.c
Utilities/ThirdParty/OpenSource/bstrlib/bstrlib.c
Utilities/ThirdParty/OpenSource/lz4/lz4.c
Utilities/ThirdParty/OpenSource/zstd/common/entropy_common.c
Utilities/ThirdParty/OpenSource/zstd/common/error_private.c
Utilities/ThirdParty/OpenSource/zstd/common/fse_decompress.c
Utilities/ThirdParty/OpenSource/zstd/common/xxhash.c
Utilities/ThirdParty/OpenSource/zstd/common/zstd_common.c
Utilities/ThirdParty/OpenSource/zstd/decompress/huf_decompress.c
Utilities/ThirdParty/OpenSource/zstd/decompress/zstd_ddict.c
Utilities/ThirdParty/OpenSource/zstd/decompress/zstd_decompress.c
Utilities/ThirdParty/OpenSource/zstd/decompress/zstd_decompress_block.c
```

[CMakeLists.txt](CMakeLists.txt)

### Windows 额外编译的源文件

```text
Graphics/Direct3D12/Direct3D12.c
Graphics/Direct3D12/Direct3D12Hooks.c
Graphics/Direct3D12/Direct3D12Raytracing.c
Graphics/Direct3D12/Direct3D12_Cxx.cpp
OS/CPUConfig.cpp
OS/ThirdParty/OpenSource/cpu_features/src/impl_x86_windows.c
OS/ThirdParty/OpenSource/hidapi/windows/hid.c
OS/WindowSystem/WindowSystem.cpp
OS/Windows/WindowsBase.cpp
OS/Windows/WindowsFileSystem.cpp
OS/Windows/WindowsInput.cpp
OS/Windows/WindowsLog.c
OS/Windows/WindowsThread.c
OS/Windows/WindowsTime.c
OS/Windows/WindowsWindow.cpp
Utilities/Math/Algorithms.c
```

`Direct3D12.c` 编译自构建目录中应用 GUID 兼容补丁后的副本。[Windows.cmake](Windows.cmake)

### Linux 和 Android 额外编译的源文件

```text
# Linux 和 Android 共用
Graphics/Vulkan/Vulkan.c
Graphics/Vulkan/Vulkan_Cxx.cpp
Graphics/Vulkan/VulkanRaytracing.c
Utilities/FileSystem/UnixFileSystem.c

# Linux
OS/Linux/LinuxFileSystem.c
OS/Linux/LinuxLog.c
OS/Linux/LinuxThread.c
OS/Linux/LinuxTime.c

# Android
OS/Android/AndroidFileSystem.cpp
OS/Android/AndroidLog.c
OS/Android/AndroidThread.c
OS/Android/AndroidTime.c
```

Android 额外编译 Pangu 维护的 [AndroidSwappy.c](AndroidSwappy.c)。

[Linux.cmake](Linux.cmake) · [Android.cmake](Android.cmake)

### macOS 和 iOS 额外编译的源文件

```text
# macOS 和 iOS 共用
Graphics/Metal/MetalRenderer.mm
Graphics/Metal/MetalRaytracing.mm
OS/CPUConfig.cpp
OS/Darwin/CocoaFileSystem.mm
OS/Darwin/DarwinInput.mm
OS/Darwin/DarwinLog.c
OS/Darwin/DarwinThread.c
OS/ThirdParty/OpenSource/cpu_features/src/impl_aarch64_iOS.c
OS/WindowSystem/WindowSystem.cpp
Utilities/FileSystem/UnixFileSystem.c

# macOS
OS/Darwin/macOSAppDelegate.m
OS/Darwin/macOSBase.mm
OS/Darwin/macOSWindow.mm
OS/ThirdParty/OpenSource/cpu_features/src/impl_x86_macos.c

# iOS
OS/Darwin/iOSAppDelegate.m
OS/Darwin/iOSBase.mm
OS/Darwin/iOSWindow.mm
```

[Apple.cmake](Apple.cmake)

### 链接与部署文件

Windows 链接 Forge 随附的 `amd_ags_x64.lib`、`nvapi64.lib`、`dxcompiler.lib` 和
`WinPixEventRuntime.lib`。[Windows.cmake](Windows.cmake)

图形应用将各平台数据文件作为 `gpu.data` 与应用的 `gpu.cfg` 放在
`RD_GPU_CONFIG` 对应的位置。固定树中的初始规则样本为
`Repo/Examples_3/Unit_Tests/src/01_Transformations/GPUCfg/gpu.cfg`。
Windows 图形部署随所用 API 提供 `amd_ags_x64.dll`、`WinPixEventRuntime.dll`、
`dxcompiler.dll` 和 `dxil.dll`。使用 Agility 时，`D3D12Core.dll` 来自固定树
`Graphics/ThirdParty/OpenSource/Direct3d12Agility/bin/x64`，版本 715，
由应用按 Agility SDK 约定设置导出的 SDK 版本及路径。启用验证层时还需同目录的
`d3d12SDKLayers.dll`。NVAPI 运行时由安装的 NVIDIA 驱动提供。
Pangu 将 Agility SDK 的两个 DLL 部署到应用可执行文件旁的 `D3D12/` 目录。

## 第三方许可

第三方许可文件保留在固定树。压缩与基础代码见 `Common_3/Utilities/ThirdParty/OpenSource`
内的 lz4、zstd、bstrlib 和 Nothings 许可证及源码声明；CPU 检测、HID 和 AGDK 的条款见
`Common_3/OS/ThirdParty/OpenSource` 相应目录。图形 SDK 和组件条款见
`Common_3/Graphics/ThirdParty/OpenSource` 中 Direct3d12Agility、DirectXShaderCompiler、
ags、nvapi、winpixeventruntime、VulkanSDK 与各内存分配器随附的许可证或头文件声明。
使用预编译库和部署其运行文件时按各自上游条款处理。

## 补丁

以下补丁以 v1.63 为基线。配置调用 ThirdParty 的 `pangu_apply_third_party_patch()`，
先核对上游文本规范化后的 SHA-256，再在构建目录应用补丁并核对结果 SHA-256。 [公共补丁接线](../ApplyPatch.cmake)

1. `Patch/0001-application-config.patch` 通过 Forge 的外部配置入口将项目配置用于
   所有编译单元。保留渲染、平台和输入支持，关闭当前接入无需的可选功能，并允许
   preset 的 Windows Clang 使用其选定的 MSVC 兼容版本。
2. `Patch/0002-d3d12-use-sdk-guids.patch` 仅用于 Windows。移除 Forge 自行提供的三个
   D3D12 Work Graphs 后备 IID，由 Windows SDK 的 `dxguid.lib` 提供定义。配置时链接探针
   核对工具链实际选中的库是否提供这三个符号，已验证 Windows SDK `10.0.26100.0`。

两项补丁由 PanguEngine 维护，仓库尚无对应的上游 issue 或 PR 记录。
Renderer 通过 Forge 的公开 `IGraphics` 接口完成交换链、
清屏、提交及呈现，Vulkan 和 Metal 后端直接构建固定提交中的源文件。

## 显式链接检查

普通配置默认关闭 `PANGU_BUILD_FORGE_LINK_CHECK`。使用下面的命令配置并构建实际的
`PanguForgeLinkCheck`，可将 `windows` 换成 `linux`、`macos`、
`android-arm64` 或 `ios-arm64`。

```sh
cmake --preset windows -DBUILD_TESTING=ON -DPANGU_BUILD_ENGINE_TESTS=ON -DPANGU_BUILD_FORGE_LINK_CHECK=ON
cmake --build --preset windows --target PanguForgeLinkCheck
```

链接检查用 C++23（Apple 为 Objective-C++23）读取后端头并保留实际
`initRenderer`、`exitRenderer` 引用。编译断言校验目标平台 API 与 `char8_t`，
测试自身提供 `main`，构建验收不初始化 GPU 或部署运行时文件。
