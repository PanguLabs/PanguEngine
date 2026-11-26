#include "GraphicsApplication.h"

#include <EngineHost.h>
#include <Error.h>
#include <HostOperation.h>
#include <HostStartupConfig.h>
#include <LogTypes.h>
#include <ModuleDescriptor.h>
#include <PanguApplication.h>
#include <RendererAccess.h>
#include <RendererModule.h>
#include <RendererSession.h>
#include <RendererTypes.h>
#include <Result.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <expected>
#include <optional>
#include <print>

namespace pg::app
{
namespace detail
{
class GraphicsApplication final
{
public:
    explicit GraphicsApplication(renderer::RendererSession& session, renderer::RendererOpenDesc desc) noexcept
        : Session(session)
        , OpenDesc(desc)
    {
    }

    [[nodiscard]] Status Run(DesktopWindow& window) noexcept;
    [[nodiscard]] Result<FrameResult> Present(auto draw) noexcept;
    [[nodiscard]] Status Reopen() noexcept;
    [[nodiscard]] Status RetireDrawable() noexcept;

private:
    [[nodiscard]] Status RunFrames(DesktopWindow& window) noexcept;
    [[nodiscard]] Status SynchronizeDrawable(const Drawable& drawable) noexcept;

    // 外层 ScopeBorrow 在整个 GraphicsApplication 生命周期内保持有效。
    renderer::RendererSession& Session; // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
    renderer::RendererOpenDesc OpenDesc;
    std::optional<Drawable> CurrentDrawable;
    std::optional<Error> FrameFailure;
    renderer::SurfaceId Attached;
    renderer::PixelSize AttachedSize{};
    std::uint64_t Generation{};
};

Status GraphicsApplication::Reopen() noexcept
{
    const auto closed = Session.Close();
    Attached = {};
    if (!closed && !FrameFailure)
    {
        FrameFailure = closed.error();
        return closed;
    }
    const auto opened = Session.Open(OpenDesc);
    if (opened)
    {
        FrameFailure.reset();
    }
    else if (!FrameFailure)
    {
        FrameFailure = opened.error();
    }
    return opened;
}

Status GraphicsApplication::SynchronizeDrawable(const Drawable& drawable) noexcept
{
    const auto size = drawable.Size;
    if (Attached.IsValid())
    {
        if (AttachedSize.Width == size.Width && AttachedSize.Height == size.Height)
        {
            return {};
        }
        if (const auto retired = Session.Detach(Attached); !retired)
        {
            return retired;
        }
    }
    Attached = renderer::SurfaceId{++Generation};
    AttachedSize = size;
    return Session.Attach(Attached, drawable.Surface, size);
}

Status GraphicsApplication::RetireDrawable() noexcept
{
    CurrentDrawable.reset();
    if (!Attached.IsValid())
    {
        return {};
    }
    if (const auto retired = Session.Detach(Attached); !retired)
    {
        return retired;
    }
    Attached = {};
    AttachedSize = {};
    return {};
}

Result<FrameResult> GraphicsApplication::Present(auto draw) noexcept
{
    if (FrameFailure)
    {
        return std::unexpected(*FrameFailure);
    }
    if (!CurrentDrawable)
    {
        return FrameResult::kSkipped;
    }
    if (const auto ready = SynchronizeDrawable(*CurrentDrawable); !ready)
    {
        FrameFailure = ready.error();
        return std::unexpected(*FrameFailure);
    }
    const auto frame = draw(Session);
    if (!frame)
    {
        const auto error = frame.error();
        if (error !=
            renderer::MakeRendererError(renderer::RendererStage::kSubmit, renderer::RendererError::kInvalidVertex))
        {
            FrameFailure = error;
        }
    }
    else if (*frame == FrameResult::kReattach)
    {
        Attached = {};
    }
    return frame;
}

Status GraphicsApplication::RunFrames(DesktopWindow& window) noexcept
{
    GraphicsContext context(*this);
    for (;;)
    {
        const auto drawable = window.Poll();
        if (!drawable)
        {
            return std::unexpected(drawable.error());
        }
        if (!*drawable)
        {
            return {};
        }
        CurrentDrawable = **drawable;
        const auto size = (**drawable).Size;
        if (size.Width == 0 || size.Height == 0)
        {
            if (const auto waited = window.WaitForEvent(); !waited)
            {
                return waited;
            }
            continue;
        }

        const auto frame = OnFrame(context);
        if (!frame)
        {
            return std::unexpected(frame.error());
        }
        if (FrameFailure)
        {
            return std::unexpected(*FrameFailure);
        }
        if (*frame == LoopControl::kStop)
        {
            return {};
        }
    }
}

Status GraphicsApplication::Run(DesktopWindow& window) noexcept
{
    const auto opened = Session.Open(OpenDesc);
    const auto frames = opened ? RunFrames(window) : opened;
    const auto closed = Session.Close();
    return frames ? closed : frames;
}

Status RunGraphicsSession(renderer::RendererSession& session, DesktopWindow& window,
                          renderer::RendererOpenDesc desc) noexcept
{
#ifdef PG_APPLICATION_INDEXED_MESH
    desc.Quad = &kIndexedQuad;
#endif
    GraphicsApplication app(session, desc);
    window.ActiveApplication = &app;
    const auto result = app.Run(window);
    window.ActiveApplication = nullptr;
    return result;
}

Status DesktopWindow::Run(renderer::RendererSession& session) noexcept { return RunGraphicsSession(session, *this); }

Status DesktopWindow::RetireDrawable() noexcept
{
    return ActiveApplication != nullptr ? ActiveApplication->RetireDrawable() : Status{};
}

Status RunGraphicsApplication(GraphicsApplicationPlatform& platform) noexcept
{
    const std::array<module::QueryModuleFn, 1> queries{renderer::QueryRendererModule};
    const engine::HostStartupConfig config{
        .AdditionalResidentQueries = queries,
        .ResidentBindings = {},
        .Modules = {},
        .ModuleBindings = {},
        .Logging = {.Directory = {}, .Enabled = log::OutputMask::kNone},
    };
    engine::EngineHost host;
    engine::HostOperationReport report;
    const auto started = host.Start(config, report);
    const auto frames = [&] -> Status
    {
        if (!started)
        {
            return started;
        }
        const auto renderer = renderer::AcquireRenderer(host);
        if (!renderer)
        {
            return std::unexpected(renderer.error());
        }
        const auto borrow = renderer->TryBorrow();
        if (!borrow)
        {
            return std::unexpected(borrow.error());
        }
        return platform.Run(**borrow);
    }();
    const auto begun = host.BeginShutdown(report);
    const auto closed = host.TryFinishShutdown(report);
    if (!frames)
    {
        return frames;
    }
    return begun ? closed : begun;
}

int GraphicsExitCode(Status outcome) noexcept
{
    if (outcome)
    {
        return EXIT_SUCCESS;
    }
    const auto error = outcome.error();
    std::print(stderr, "Pangu Application: error domain={:x} code={}", error.Domain().Value(), error.Code());
    if (const auto native = error.Native())
    {
        std::print(stderr, " native-source={:x} native-code={}", native->Source.Value, native->Value);
    }
    std::fputc('\n', stderr);
    if (error.Domain() == renderer::kRendererErrorDomain &&
        renderer::GetRendererCause(error) == renderer::RendererError::kPresentationUnavailable)
    {
        std::fputs("Pangu Application: Windowed presentation is unavailable on this renderer\n", stderr);
    }
    return EXIT_FAILURE;
}
} // namespace detail

GraphicsContext::GraphicsContext(detail::GraphicsApplication& app) noexcept
    : App(app)
{
}

Result<FrameResult> GraphicsContext::Present(LinearColor color) noexcept
{
    return App.Present([color](auto& session) { return session.Present(color); });
}
Result<FrameResult> GraphicsContext::PresentIndexedMesh(LinearColor clearColor) noexcept
{
    return App.Present([clearColor](auto& session) { return session.PresentIndexedMesh(clearColor); });
}
Result<FrameResult> GraphicsContext::PresentTriangle(LinearColor clearColor,
                                                     const std::array<TriangleVertex, 3>& vertices) noexcept
{
    return App.Present([&](auto& session) { return session.PresentTriangle(clearColor, vertices); });
}
Status GraphicsContext::ReopenGraphics() noexcept { return App.Reopen(); }
} // namespace pg::app
