# Renderer 首版接入 The Forge IGraphics

> 状态：接入方向已确认。实施与实测情况见[迁移计划](../plans/2026-11-20-renderer-forge-igraphics.md)。本文替代[原首帧规格](2026-11-20-renderer-first-frame-design.md)中自有后端、功能补丁及原生呈现验收的安排。原规格保留早期探针与验证记录。

## 源码与所有权

Pangu Application 源码位于 `Source/Runtime/Application/`。图形和无图形入口由 `pangu_add_application()` 按消费目标编译，平台入口管理原生窗口、事件和 EngineHost。Application 目录承担应用组装职责，只有 Renderer 是真实 Runtime Module。

Project 使用 `PanguApplication.h` 和既有 Engine 接线。图形 Application 在 EngineHost 的 Host Scope 取得 Renderer 借用。Renderer 在有效借用期内将原生窗口句柄传给 Forge，停止提交并按 Forge 公开接口完成清理后结束窗口借用。每进程同时只有一个图形会话，会话的原生资源操作由同一线程串行执行。无图形目标独立构建、部署和启动 EngineHost。

## Forge 边界

The Forge 固定在 v1.63 提交 `7443530ef7e888771f307de74b7872becfd64450`。`Pangu::Renderer` 私有链接 `ThirdParty::TheForge`。Forge 的公开 `IGraphics` 接口负责设备、队列、交换链、取图、附件清屏、命令提交和呈现。Pangu 接入层负责所有权、句柄及参数转接、公开 API 调用顺序，以及从公开接口可观察的结果。

超出该版本公开能力的 PanguEngine 自定规格和验收要求直接取消。[渲染接入原则](../../../wiki/runtime/rendering.md)

图形实现采用共享的 Forge 接线。平台差异限于原生显示目标、平台启动参数、平台资源目录和部署。首帧通过颜色附件的 Load Action 清屏。着色器构建与资源上传在需要它们的绘制阶段另行接入。

项目配置优先使用 CMake 与 Forge 的外部配置入口。上游补丁只接受为项目工具链所需的编译或链接兼容调整，构建目录的补丁副本校验输入与输出哈希。三个现有功能补丁与它们的私有调用桥随后端切换一同移除。Forge 工作树归固定上游提交管理。

## 验收口径

- `GraphicsContext::Present(LinearColor)` 通过 `pg::Result` 表达接入层可观察的错误。帧结果 `kPresentRequested` 表示 Forge 的呈现调用已经返回，业务及日志按“呈现调用”计数。暂停及可观察的不可用结果单列。
- `RendererSession` 在会话线程同步调用 Forge 的清理接口，`Close()` 返回 Pangu 接入层此前记录的首个可观察错误。
- 独立 Project 运行至少 120 次呈现调用，核对变化前后两种目标颜色及正常退出。两种链接模式复用 Project 源码和接线。Headless 在单独部署后创建并关闭 Context。
- macOS 的窗口颜色验收需要可用的 Metal 设备、macOS 14 或更高版本及取色进程的屏幕录制权限。观察器按应用 PID 捕获窗口，先确认中心像素为 sRGB `#3F99C8` 再触发换色，达到 120 次呈现调用且采到 `#FF0000` 后请求停止。无物理 Metal 设备时单独验证启动拒绝。
- Linux 和 macOS 的 Modular 应用按链接依赖将本地共享库部署到可执行文件旁。验收搬移应用目录，临时隔离独立 Project 的构建树，并通过系统加载器的日志核对 Pangu 运行库来源。
- 设备或交换链初始化失败按 Forge 公开接口可观察的结果报告。
- Windows D3D12 在 Forge 支持的硬件适配器上验收。仅枚举到软件适配器时，Forge 在交出 Context 前报告错误。开启 Forge 断言的构建会触发断言，关闭断言的构建由 Pangu 报告 `kOpen/kNoDevice`。
- Linux 和 Android 的图形应用使用原版 Forge Vulkan 的取图、呈现及交换链清理。按实际环境验收可观察的呈现、窗口生命周期和应用关闭。Android 的原生窗口销毁回调返回前必须结束访问窗口。
- macOS 或移动端只有链接、签名或无设备运行的证据时，按实际验证范围记录。旧实现的物理呈现及故障注入是历史证据。

相关资料见 [Forge 图形接口](https://github.com/ConfettiFX/The-Forge/blob/v1.63/Common_3/Graphics/Interfaces/IGraphics.h)、[Khronos 交换链同步说明](https://docs.vulkan.org/guide/latest/swapchain_semaphore_reuse.html)和 [Android 窗口销毁契约](https://developer.android.com/ndk/reference/struct/a-native-activity-callbacks#onnativewindowdestroyed)。
