#pragma once

#include "NativeSurface.h"

#include <PanguRendererExport.h>
#include <RendererTypes.h>
#include <Result.h>

#include <array>
#include <cstdint>

namespace pg::renderer
{
class RendererSession;

namespace detail
{
enum class AcquireResult : std::uint8_t
{
    kReady,
    kSkipped,
    kReattach,
};

class FrameBackend
{
public:
    virtual ~FrameBackend() = default;
    FrameBackend(const FrameBackend&) = delete;
    FrameBackend& operator=(const FrameBackend&) = delete;
    FrameBackend(FrameBackend&&) = delete;
    FrameBackend& operator=(FrameBackend&&) = delete;

    [[nodiscard]] virtual Status Open(RendererOpenDesc desc) noexcept = 0;
    [[nodiscard]] virtual Status Attach(NativeSurface surface, PixelSize size) noexcept = 0;
    // 绘制资源在获得图像及其信号量之前完成准备，失败按提交阶段报告。
    [[nodiscard]] virtual Status PrepareTriangle() noexcept = 0;
    // 取图结果返回会话后，由会话检查撤销并决定是否提交。
    [[nodiscard]] virtual Result<AcquireResult> Acquire() noexcept = 0;
    // 会话与撤销在同一个同步点仲裁。取得授权即成为在途提交，撤销发布不等待驱动调用。
    // 平台适配器在退休确认之前须保持 NativeSurface 有效。
    // 顶点借用仅在调用期间有效，空指针表示只清屏。
    [[nodiscard]] virtual Status Submit(LinearColor color, const std::array<TriangleVertex, 3>* triangle) noexcept = 0;
    virtual void PresentImage() noexcept = 0;
    // 通过 Forge 的同步接口清理。也用于失败回滚。
    virtual void RetireSurface() noexcept = 0;
    virtual void Close() noexcept = 0;

protected:
    FrameBackend() noexcept = default;
    [[nodiscard]] PG_RENDERER_API bool IsDetachRequested() const noexcept;
    [[nodiscard]] PG_RENDERER_API SurfaceId CurrentSurfaceId() const noexcept;

private:
    friend class pg::renderer::RendererSession;
    RendererSession* Session{};
};
} // namespace detail
} // namespace pg::renderer
