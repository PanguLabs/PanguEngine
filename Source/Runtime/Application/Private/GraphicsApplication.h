#pragma once

#include "NativeSurface.h"

#include <RendererTypes.h>
#include <Result.h>

#include <optional>

namespace pg::renderer
{
class RendererSession;
}

namespace pg::app::detail
{
class GraphicsApplication;

struct Drawable
{
    renderer::NativeSurface Surface;
    renderer::PixelSize Size;
};

class GraphicsApplicationPlatform
{
public:
    GraphicsApplicationPlatform(const GraphicsApplicationPlatform&) = delete;
    GraphicsApplicationPlatform& operator=(const GraphicsApplicationPlatform&) = delete;
    GraphicsApplicationPlatform(GraphicsApplicationPlatform&&) = delete;
    GraphicsApplicationPlatform& operator=(GraphicsApplicationPlatform&&) = delete;
    virtual ~GraphicsApplicationPlatform() = default;
    [[nodiscard]] virtual Status Run(renderer::RendererSession& session) noexcept = 0;

protected:
    GraphicsApplicationPlatform() noexcept = default;
};

class DesktopWindow : public GraphicsApplicationPlatform
{
public:
    DesktopWindow(const DesktopWindow&) = delete;
    DesktopWindow& operator=(const DesktopWindow&) = delete;
    DesktopWindow(DesktopWindow&&) = delete;
    DesktopWindow& operator=(DesktopWindow&&) = delete;
    ~DesktopWindow() override = default;

    // 无值表示请求关闭。最小化时报告零像素，窗口本身保活。
    [[nodiscard]] virtual Result<std::optional<Drawable>> Poll() noexcept = 0;
    [[nodiscard]] virtual Status WaitForEvent() noexcept = 0;
    [[nodiscard]] Status Run(renderer::RendererSession& session) noexcept override;

protected:
    DesktopWindow() noexcept = default;
    [[nodiscard]] Status RetireDrawable() noexcept;

private:
    friend Status RunGraphicsSession(renderer::RendererSession& session, DesktopWindow& window,
                                     renderer::RendererOpenDesc desc) noexcept;
    GraphicsApplication* ActiveApplication{};
};

[[nodiscard]] Status RunGraphicsApplication(GraphicsApplicationPlatform& platform) noexcept;
[[nodiscard]] Status RunGraphicsSession(renderer::RendererSession& session, DesktopWindow& window,
                                        renderer::RendererOpenDesc desc = {}) noexcept;
int GraphicsExitCode(Status outcome) noexcept;
} // namespace pg::app::detail
