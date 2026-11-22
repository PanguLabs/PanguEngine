# Renderer 首版：五平台首帧

> **已被替代（Superseded）。** 此文保留原首帧设计与探针记录。自有后端、功能补丁和原生呈现验收的安排由 [Renderer 首版接入 The Forge IGraphics](2026-11-20-renderer-forge-igraphics-design.md) 替代。

## 交付目标与 Project 接入

一份独立消费 Project 使用同一套启动、清屏和停止代码，在 Windows、Linux、macOS、Android、iOS 上连续呈现指定颜色。**Project 只接入 Engine**，沿用 `find_package(PanguEngine REQUIRED MODULE)` 和 `pangu_initialize()`。Project 负责应用身份、打包配置和业务逻辑。Engine 提供可复用的 Pangu Application 平台入口，并按可执行目标选择和部署运行依赖。切换 Monolithic 与 Modular 只改变构建配置，Project 的 CMake 接线、应用入口和渲染调用保持一致。[构建系统](../../../wiki/foundation/build-system.md)

同一 Project 可以同时交付图形应用和明确声明为无图形的工具。工具可以使用 EngineHost、日志或 ECS 等非图形能力；其启动和业务执行都独立于 Renderer 运行库。首版无需为这种工具实现 `NullRenderer`。

底层固定 The Forge **v1.63**，gitlink 锁到 `7443530ef7e888771f307de74b7872becfd64450`。清屏通过颜色附件的 Load Action 完成，首帧无需着色器、材质、场景或 ECS World。[上游固定版本](https://github.com/ConfettiFX/The-Forge/tree/7443530ef7e888771f307de74b7872becfd64450)

## 入口、平台与所有权

Pangu Application 拥有 EngineHost，管理窗口或 View，接收事件并驱动每一帧。Renderer 借用原生显示目标，拥有设备、呈现队列、交换链、命令和同步对象。EngineHost 管理 Module 和 Scope 的生命周期。探针曾复用上游 `IApp` 快速验证后端；正式入口采用 Pangu Application，以便由 Engine 统一提供窗口事件和退出流程。[EngineHost 契约](../../../wiki/core/engine-host.md)

| 平台 | 首版后端 | Application 借出的显示目标 | 正式运行验收模式 |
| --- | --- | --- | --- |
| Windows | D3D12 | Win32 客户区的 `HWND` | Monolithic、Modular |
| Linux | Vulkan | X11/Xlib 的 `Display*` 与 `Window` | Monolithic、Modular |
| macOS | Metal | 带 `CAMetalLayer` 的 `NSView` | Monolithic、Modular |
| Android arm64 | Vulkan | 当前有效的 `ANativeWindow*` | Monolithic、Modular |
| iOS arm64 | Metal | 带 `CAMetalLayer` 的 `UIView` | 仅 Monolithic |

平台适配层构造原生句柄，尺寸以可绘制像素计。Linux 首版窗口接口为 Xlib，涵盖原生 X11 和提供 XWayland 的环境。Apple 的 View、Layer 由 Application 保活，并与 Renderer 协调设备及 drawable 像素尺寸。iOS 的 Monolithic 要求适用于 Pangu 自有模块，应用按需链接 Metal、UIKit 等系统 Framework。

## 图形会话与清屏

1. **打开会话。** 对于图形目标，Engine 的 Application 接线在 `EngineHost.Start` 时通过 `AdditionalResidentQueries` 接纳 Renderer Module。Host Scope 提供具名 Concrete 入口，Application 在 Ready 后取得 `ScopeBorrow`，在借用期内完成图形操作与清理。Project 通过 Engine 请求图形能力。设备在显式打开时创建，此前先初始化 The Forge 所需的内存、文件系统、诊断和 GPU 配置。
2. **挂接窗口。** v1.63 的 GPU 选择和配置具有进程静态状态。每进程同时只允许一个图形会话，每个会话同时只挂接一个 Surface。并发开启第二个会话、或在 A 仍挂接时请求挂接 B，均返回占用错误。A 脱离后可挂接 B，会话关闭后可重新打开。其他无图形 EngineHost 可在同进程运行。
3. **呈现一帧。** Project 通过 Engine 发起一次清屏呈现调用并传入线性 RGBA。Renderer 获取图像、等待可复用的帧资源、转换资源状态、清屏、提交并呈现。调用结果要让 Application 能决定继续、暂时跳帧、重新挂接或结束失效会话。帧对象、命令缓冲和同步对象留在 Renderer 内部。探针以每帧排空队列便于观测，正式实现可用有限数量的帧资源和 Fence。
4. **调用线程。** 每个会话指定一条调用线程，挂接、呈现和脱离均在该线程串行执行。Application 转交来自其他线程的平台通知。取图等待须可取消或在有限时间内返回会话线程；Vulkan 适配层需处理 v1.63 默认的无限取图等待。每次重试前先处理撤销通知。一旦撤销当前挂接，停止为它取图和提交新帧，按后端规则退休已取得的图像和在途工作。平台适配层在原生窗口的有效期内完成脱离；Android 的窗口销毁回调返回前，Renderer 必须结束该窗口的借用。[Android 窗口销毁回调](https://developer.android.com/ndk/reference/struct/a-native-activity-callbacks#onnativewindowdestroyed)

## 暂停、恢复与关闭

| 事件 | 行为 |
| --- | --- |
| 桌面窗口仅失去键盘焦点 | 只要窗口仍可绘制就继续呈现。 |
| 桌面窗口最小化、可绘制尺寸为零，或移动应用进入后台 | 暂停提交并保留健康设备。恢复时检查当前句柄与像素尺寸，按需重建交换链。 |
| 单帧 drawable 暂不可用 | 跳过本帧，随后重试。Metal v1.63 的 `acquireNextImage` 在 `nextDrawable` 为空时仍可能返回合法索引，适配层须检查 drawable。 |
| 尺寸变化、交换链过期或 Surface 销毁 | 退休旧交换链，结束对应窗口借用，以新尺寸和有效句柄重新挂接。通知关联到产生它的那次挂接，迟到的旧通知不得拆掉新 Surface，即使平台复用了原生句柄值。 |
| 设备丢失 | Renderer 停止接纳新帧，报告设备丢失，按后端失效设备的销毁协议清理，避免等待已无法完成的 Fence。Application 决定正常退出或清理后在同一 EngineHost 中显式重开。最小应用选择正常退出。 |

各后端须向会话线程报告获取、提交、呈现、Fence 等待和关闭排空的结果。设备选择须处理候选集为空。初始化或挂接失败时，返回带阶段的错误并清理已经建立的资源。

- **Vulkan。** 在原生结果仍可用的位置区分暂时无图像、交换链过期、Surface 丢失与设备丢失。提交失败后停止呈现该帧，避免等待已重置且没有成功提交与之关联的 Fence。提交成功而呈现失败时，仍须跟踪该次提交。等待 Fence 或排空队列时首次报告设备丢失，也进入同一失效设备清理路径。
- **D3D12 与 Metal。** D3D12 适配层将 Present 或等待期间检测到的设备移除报告给 Application，由它决定是否重开。Metal 的异步完成回调须保留命令执行错误，交回会话线程处理；即使最后一帧后直接关闭，也要在释放会话状态前收齐相关回调及其结果。

**资源回收有两个完成条件。** 命令缓冲和逐帧资源须等使用它们的队列提交完成。旧交换链和呈现等待信号量还须等呈现侧结束使用。Vulkan 的渲染提交 Fence 只证明第一项；适配层须按设备支持的 WSI 机制取得呈现资源可回收的依据，覆盖独立呈现队列和旧交换链停止取图的情形。[Khronos 交换链信号量生命周期](https://docs.vulkan.org/guide/latest/swapchain_semaphore_reuse.html)

正常关闭时，Application 停止新提交，处理已记录或在途的命令。满足两项回收条件后释放交换链并结束窗口借用，再释放队列、设备和 `ScopeBorrow`，最后完成 EngineHost 关闭。设备丢失时按失效设备路径收尾。重复停止或重复收到 Surface 通知均须安全。[GPU 资源回收](../../../wiki/core/memory-management.md)

清屏色按实际渲染视图和采样颜色空间核对。例如线性 sRGB `(0.05, 0.32, 0.58, 1)` 编码成 8 位 sRGB 为 `#3F99C8`。回读还须考虑 RGBA 或 BGRA 通道顺序。

## 构建与部署

Engine 在 `Source/Runtime/` 下定义真实 Renderer Module，内部可使用 `Pangu::Renderer`。Engine 负责模块接纳、依赖链接和部署。上游源码使用普通 ThirdParty Target，构建配置、原生头文件和兼容性补丁留在适配边界。[ThirdParty 接入规范](../../../wiki/foundation/third-party.md)

Windows 正式接入使用根 preset 选定的 Clang C/C++ 驱动（C++ 为 `clang++.exe`），通过独立 Project 验证。探针使用过 `clang-cl.exe`。v1.63 默认 Windows 配置要求 `_MSC_VER == 1929`，上游入口宏的 `u8` 字面量也与 C++23 类型不匹配。兼容选项限于需要它们的第三方目标，Pangu 自有代码使用项目 C++23 配置。部署时核对 D3D12 Agility 导出、版本和相对路径。

各图形目标部署平台所需的 GPU 配置。Android 采用项目选定的 NDK，APK 包含 Activity、资源及目标 ABI 的原生依赖；Modular APK 还须包含独立 Renderer 共享库及其运行依赖。使用自有入口可避开上游 AndroidBase 中 NDK r30 已移除的 `ALooper_pollAll`。两个 Apple 目标分别使用对应 SDK，iOS 交付可签名安装的 Monolithic 应用。

## 正式验收

| 场景 | 判定依据 |
| --- | --- |
| 九个运行组合 | 用同一份独立 Project 源码构建各组合，从实际部署目录或安装包运行，使源码树和构建树不可访问，包括通过 RPATH 等途径回退查找。记录平台、模式、GPU/驱动、后端、自有运行库及 GPU 配置的实际来源和关闭结果；Modular 核对实际加载的 Renderer 库，系统框架和驱动可由系统提供。Linux 用物理 GPU 验收，llvmpipe 可用于软件回归。 |
| 连续呈现与颜色 | 每个组合在窗口可绘制期间至少取得 120 次后端成功接受的呈现请求。跳帧、失败分别计数。正常呈现用例出现提交、呈现或异步执行错误即为失败。此计数只衡量后端接纳。先呈现并采样颜色 A，再改为颜色 B 并重新采样；客户区采样或对应帧的交换链回读须匹配各自的期望值，记录颜色空间和通道顺序。 |
| 每个平台的窗口生命周期 | 各选一种受支持模式。桌面失焦后验证新帧，调整尺寸后在新交换链上换色并采样，再关闭窗口。桌面最小化、零尺寸或移动端进入后台后，从暂停生效至恢复期间，新提交计数保持不变；记录设备实例以验证健康设备跨暂停保留。恢复后切换颜色，采样新帧，再显式停止；iOS 通过应用生命周期验证回收。 |
| 会话与窗口撤销 | 验证第二个会话或第二个 Surface 返回占用，释放后可再次打开或挂接。覆盖零候选设备和挂接失败。在无可用图像、取图尚未完成时触发窗口销毁，确认会话线程能处理撤销并及时结束借用；挂接 B 后重放 A 的旧销毁通知，B 仍可呈现。 |
| 提交失败与回收 | 覆盖获取成功后提交失败、提交成功后呈现失败，以及关闭排空时首次报告设备丢失。验证失败帧的在途状态、关闭可完成和同宿主显式重开。让渲染 Fence 先完成、呈现侧延迟完成，分别立即触发 resize 和 stop，检查呈现同步对象与旧交换链的实际回收时点；重复停止安全。 |
| 无图形工具 | 在支持 Modular 的平台，从同一消费 Project 部署明确声明为无图形的工具。仅提供其运行依赖，确保源码树、构建树和其他 Renderer 库均不可访问。工具必须启动 EngineHost，通过引擎完成一项可观察的非图形操作（例如创建并关闭一个 Context），然后正常关闭 EngineHost。 |

以下探针记录用于说明各平台已验证的接入路径。正式验收以本节的消费工程和运行组合为准。

## 本轮探针结果

验证基线为 PanguEngine `68aa3e0`、The Forge v1.63。下面的程序在探针工作区构建；Renderer 生产 Module、正式消费工程与 CTest 接线仍待实现。上游库使用按平台裁剪的编译配置，窗口样例与 Pangu 代码使用 C++23。

| 平台 | 实测范围 |
| --- | --- |
| Windows x64 | Clang 23.1.5、D3D12、Radeon RX 580 2048SP。上游 WindowsMain 配合真实 EngineHost 呈现 120 帧以上；仅采样该窗口，客户区中心由黑色变为 RGB(63,153,200)。独立的 Application 自建 HWND、处理 Win32 消息，将交换链从 704×441 重建为 524×321；第 80 帧从新后备缓冲区中心回读 BGRA(200,153,63,255)。第 120 帧后依次释放 Renderer、关闭 EngineHost，退出码 0。 |
| Linux x86_64 | WSLg X11、Mesa llvmpipe 软件 Vulkan。原版排除 CPU 设备后未正确处理零候选。仅在探针构建允许该 CPU 设备，并在配置中给 Mesa 测试设备分配 low preset 后，上游 LinuxMain 创建 2560×1600 交换链，连续清屏并调用 Present 120 次，X11 独立采样窗口中心得到 `#3F99C8`。随后释放 Renderer，退出码 0。运行时另有 libunwind FDE 警告。仍需在物理 GPU 和正式 Application 入口验证。 |
| macOS x86_64 | MacPorts Clang 23.1.2、SDK 15.2、最低版本 14.0。23 个上游编译单元与共用清屏程序链接为应用。当前虚拟 Mac 的 Metal 默认设备为空；SSH 启动时 AppKit 连接中断，15 秒内没有呈现记录。 |
| Android arm64 | 小米 23127PN0CC，Android 16 / API 36，Adreno 750。NDK r30 / Clang 21 构建、最低 API 29 的探针通过文件管理器安装。2670×1200 交换链实际清屏颜色正确。v3 完成三轮正常退出和一轮前后台恢复后退出。 |
| iOS arm64 | MacPorts Clang 23.1.2、iPhoneOS SDK 18.2、最低版本 18.0，Metal 后端与共用清屏程序链接为未签名设备应用，原生依赖含 UIKit 与 Metal。Mac 上无可用签名身份、Simulator SDK 和设备部署链路，尚无运行证据。 |

### Android 真机复验

2026-11-20，使用同一份 28,269,360 字节的 v2 签名 Debug APK，手机端校验 SHA-256 为 `2543c4d3657833e3bae7cd2cf6cdb2e3a53476126ea5c4dcc65b6d48655a90c7`。应用使用上游 Activity，探针副本中两处旧轮询调用换为 `ALooper_pollOnce`。

- **首帧与颜色通过。** Vulkan 选中 Adreno 750，交换链尺寸 2670×1200，驱动将图像数从请求的 3 调整为 4。应用位于前台时，连续四次获取的屏幕中心像素均为 `#5897C4`。另一次原始屏幕采样报告 RGBA8888、颜色空间编号 2（Display P3），该值与线性 sRGB 清屏色转换到 Display P3 后的 8 位值一致。
- **前后台切换。** 切到系统桌面后重新打开应用，系统报告 HOT 启动，进程号保持相同，Vulkan 再次创建 2670×1200 交换链。
- **正常退出失败。** 三次运行均进入上游关闭路径，在日志系统关闭后，`hwuiTask1` 报告 `pthread_mutex_lock called on a destroyed mutex` 和 `SIGABRT`。首次运行的 Android 历史退出记录同时显示 `EXIT_SELF / status=0`。应用代码在第 120 次呈现调用后请求关闭；设备日志未采集到对应的标准错误计数行，实际显示帧数未独立计量。后续正式 Application 需验证平台回调、线程与 GPU 资源的关闭顺序。

### Android 退出探针修复

原 APK 的退出回归检查再次捕捉到 HWUI 线程的 `SIGABRT`。上游循环在收到 Activity 销毁事件前重复调用 `ANativeActivity_finish`，清理后调用 C 运行库的 `exit(0)`。修复后的探针仅请求一次结束，收到销毁事件后排空并释放图形资源，注销生命周期监听并分离 JNI 线程，随后从 `android_main` 返回，由 NDK 的 NativeActivity glue 完成线程退出。每次启动 Activity 都重新创建首帧实例。

首个修复包测得 120 次提交，在 Java 销毁阶段暴露了监听器注销异常。v3 改为保存注册成功的监听器及其 `PowerManager` 实例，并用同一实例注销。v3 APK 通过 v2 签名校验，SHA-256 为 `225a558054448c4a3259cd56f3bab55e24ff4054c3a49e8ef7ee34420e5fd898`。

2026-11-20，确认真机安装 `versionCode=3` 后，使用 ADB 连续执行三轮启动与退出，以及一轮切到系统桌面、恢复后退出。恢复轮在同一进程重建了 2670×1200 交换链。四轮均依次记录 120 次清屏与呈现调用、Renderer 释放、原生线程返回、Java `onDestroy` 完成。Activity 均退出前台，Android 保留了同一个应用进程以供下一轮启动，日志未记录原生崩溃、Java 未捕获异常或 ANR。次数统计来自探针的呈现调用，未独立统计屏幕合成帧数。

修复和回归脚本保存在本地 `.probe/forge-first-frame/`。Android 构建适配、打包脚本和真机回归入口分别为 `vulkan/CMakeLists.txt`、`vulkan/package.ps1` 和 `vulkan/check-exit.ps1`。真机日志为 `vulkan/device/exit-v3-1.log`、`exit-v3-2.log`、`exit-v3-3.log` 和 `exit-v3-resume.log`。
