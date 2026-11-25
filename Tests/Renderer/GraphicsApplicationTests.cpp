#include "FakeFrameBackend.h"
#include "GraphicsApplication.h"
#include "NativeSurface.h"

#include <Error.h>
#include <PanguApplication.h>
#include <RendererTypes.h>
#include <Result.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <expected>
#include <limits>
#include <optional>

namespace pg::app
{
namespace
{
// OnFrame 的公开签名没有测试上下文参数，仅在本用例的同步调用中借用。
const renderer::detail::FakeFrameBackend*
    gTriangleBackend{}; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
constexpr LinearColor kBackground{};
constexpr auto kVertices = std::to_array<TriangleVertex>({
    {.X = -0.75F, .Y = -0.25F, .Color = {.R = 1.0F}},
    {.X = 0.25F, .Y = -0.25F, .Color = {.G = 1.0F}},
    {.X = -0.25F, .Y = 0.75F, .Color = {.B = 1.0F}},
});
} // namespace

Result<LoopControl> OnFrame(GraphicsContext& graphics) noexcept
{
    if (gTriangleBackend != nullptr)
    {
        auto invalidPosition = kVertices;
        invalidPosition.front().X = std::numeric_limits<float>::quiet_NaN();
        auto invalidColor = kVertices;
        invalidColor.back().Color.G = std::numeric_limits<float>::infinity();

        for (const auto& invalid : {invalidPosition, invalidColor})
        {
            const auto result = graphics.PresentTriangle(kBackground, invalid);
            EXPECT_FALSE(result);
            if (!result)
            {
                EXPECT_EQ(result.error().Domain(), renderer::kRendererErrorDomain);
                EXPECT_EQ(renderer::GetRendererStage(result.error()), renderer::RendererStage::kSubmit);
                EXPECT_EQ(renderer::GetRendererCause(result.error()), renderer::RendererError::kInvalidVertex);
            }
            const auto& events = gTriangleBackend->Observations().Events;
            using renderer::detail::BackendCall;
            for (const auto call :
                 {BackendCall::kPrepareTriangle, BackendCall::kAcquire, BackendCall::kSubmit, BackendCall::kPresent})
            {
                EXPECT_EQ(std::ranges::count(events, call, &renderer::detail::BackendEvent::Call), 0);
            }
        }

        const auto frame = graphics.PresentTriangle(kBackground, kVertices);
        EXPECT_EQ(frame, FrameResult::kPresentRequested);
        return frame ? Result<LoopControl>{LoopControl::kStop} : std::unexpected(frame.error());
    }
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

TEST(Application, InvalidTriangleCanBeCorrectedInSameFrame)
{
    FakeFrameBackend* backend{};
    auto session = RendererSessionTestAccess::Make(backend);
    TestWindow window;
    app::gTriangleBackend = backend;
    const auto completed = app::detail::RunGraphicsSession(session, window);
    app::gTriangleBackend = nullptr;

    ASSERT_TRUE(completed);
    EXPECT_EQ(backend->Observations().SuccessfulSubmissions, 1U);
    const auto& submitted = backend->Observations().LastTriangle;
    if (!submitted)
    {
        FAIL() << "No triangle reached the backend";
    }
    for (std::size_t index = 0; index < app::kVertices.size(); ++index)
    {
        const auto& actual = submitted->at(index);
        const auto& expected = app::kVertices.at(index);
        EXPECT_EQ(actual.X, expected.X);
        EXPECT_EQ(actual.Y, expected.Y);
        EXPECT_EQ(actual.Color.R, expected.Color.R);
        EXPECT_EQ(actual.Color.G, expected.Color.G);
        EXPECT_EQ(actual.Color.B, expected.Color.B);
        EXPECT_EQ(actual.Color.A, expected.Color.A);
    }
}
} // namespace
} // namespace pg::renderer::detail
