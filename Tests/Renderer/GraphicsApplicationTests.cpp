#include "FakeFrameBackend.h"
#include "GraphicsApplication.h"
#include "NativeSurface.h"

#include <Error.h>
#include <PanguApplication.h>
#include <RendererTypes.h>
#include <Result.h>
#include <gtest/gtest.h>

#include <expected>
#include <optional>

namespace pg::app
{
Result<LoopControl> OnFrame(GraphicsContext& graphics) noexcept
{
    const auto frame = graphics.Present({});
    return frame ? Result<LoopControl>{LoopControl::kStop} : std::unexpected(frame.error());
}
} // namespace pg::app

namespace pg::renderer::detail
{
namespace
{
class TestWindow final : public app::detail::DesktopWindow
{
public:
    [[nodiscard]] Result<std::optional<app::detail::Drawable>> Poll() noexcept override
    {
        return app::detail::Drawable{
            .Surface = NativeSurface{WaylandSurface{}},
            .Size = {.Width = 800, .Height = 600},
        };
    }
    [[nodiscard]] Status WaitForEvent() noexcept override { return {}; }
};

TEST(Application, FrameFailureRetiresSurfaceBeforeReturning)
{
    FakeFrameBackend* backend{};
    auto session = RendererSessionTestAccess::Make(backend);
    constexpr NativeErrorCode kSubmitFailure{.Source = NativeErrorSourceId{7}, .Value = -61};
    backend->FailNext(RendererStage::kSubmit, RendererError::kNativeFailure, kSubmitFailure);
    TestWindow window;

    const auto completed = app::detail::RunGraphicsSession(session, window);
    ASSERT_FALSE(completed);
    const auto error = completed.error();
    EXPECT_EQ(error.Domain(), kRendererErrorDomain);
    EXPECT_EQ(GetRendererStage(error), RendererStage::kSubmit);
    EXPECT_EQ(GetRendererCause(error), RendererError::kNativeFailure);
    EXPECT_EQ(error.Native(), kSubmitFailure);
    const auto& events = backend->Observations().Events;
    ASSERT_EQ(events.size(), 6U);
    EXPECT_EQ(events.at(4).Call, BackendCall::kRetireSurface);
    EXPECT_EQ(events.at(5).Call, BackendCall::kClose);
    EXPECT_TRUE(session.Close());
}
} // namespace
} // namespace pg::renderer::detail
