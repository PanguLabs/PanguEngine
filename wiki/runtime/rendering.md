# 渲染接入原则

图形与渲染功能以仓库固定的 The Forge v1.63 的公开接口和实现为准。PanguEngine 自定要求超出该版本的公开能力时，直接取消。接入验收依据公开接口可观察的结果和适用的平台契约。[Renderer 接入规格](../../docs/superpowers/specs/2026-11-20-renderer-forge-igraphics-design.md)

## 首个三角形

公开的 `GraphicsContext::PresentTriangle` 在同一帧清除颜色附件、使用 Project 提供的三个顶点绘制并请求呈现。顶点使用 X 向右、Y 向上的裁剪空间和线性 RGBA。借用持续到调用返回。位置或颜色包含 NaN 或无穷大时在取图前返回可恢复错误。`Present(LinearColor)` 继续提供清屏入口。图形接口尚未冻结。[设计规格](../../docs/superpowers/specs/2026-11-25-renderer-first-triangle-design.md)

使用三角形的 Project 通过 `pangu_add_application(... TRIANGLE_SHADERS ...)` 显式启用 Forge FSL 资源。桌面独立部署从可执行文件旁的 `CompiledShaders/<平台>/` 加载。Android 真机测试 APK 将 `CompiledShaders/ANDROID_VULKAN/` 按 Gradle 变体和链接模式打包，Forge 通过 `PathStatement.txt` 挂载资源。原生链接与字节码生成可独立验收。[测试工具链说明](../../Tests/Toolchain/README.md)

Windows D3D12 和 Linux Wayland Vulkan 使用独立 Project 验证同一交换链代次内的顶点位置及插值变化、窗口调整后的继续绘制和正常退出。Android Vulkan 验证两个链接模式的 APK、窗口撤销及重建、恢复绘制、SurfaceFlinger 实际呈现和 Activity 销毁。macOS Metal 与 iOS Metal 已完成真实绘制链路的原生编译及最终链接。Apple 宿主当前缺少 `metal` 编译器，macOS 还缺少 Metal 设备及取色权限，实际 Metal 字节码和设备呈现待验。[首轮平台验收](../../docs/superpowers/plans/2026-11-25-renderer-first-triangle.md#首轮平台验收)
