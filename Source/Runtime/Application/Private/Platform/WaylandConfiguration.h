#pragma once

#include <RendererTypes.h>

#include <cstdint>
#include <optional>

namespace pg::app::detail
{
// 窗口配置提交后才发布可绘制尺寸。挂起时保留内容尺寸。
class WaylandConfiguration final
{
public:
    void Configure(std::int32_t width, std::int32_t height, std::optional<bool> suspended) noexcept
    {
        if (width > 0)
        {
            PendingSize.Width = static_cast<std::uint32_t>(width);
        }
        if (height > 0)
        {
            PendingSize.Height = static_cast<std::uint32_t>(height);
        }
        PendingSuspended = suspended.value_or(PendingSuspended);
    }

    void Acknowledge() noexcept { Size = PendingSuspended ? renderer::PixelSize{} : PendingSize; }

    [[nodiscard]] renderer::PixelSize ContentSize() const noexcept { return PendingSize; }
    [[nodiscard]] renderer::PixelSize DrawableSize() const noexcept { return Size; }

private:
    renderer::PixelSize PendingSize{.Width = 720, .Height = 480};
    renderer::PixelSize Size{};
    bool PendingSuspended{};
};
} // namespace pg::app::detail
